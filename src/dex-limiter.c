/*
 * dex-limiter.c
 *
 * Copyright 2026 Christian Hergert
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libdex.h>

#include "dex-block-private.h"
#include "dex-error.h"
#include "dex-future-private.h"
#include "dex-object-private.h"
#include "dex-semaphore-private.h"

/**
 * DexLimiter:
 *
 * `DexLimiter` limits the number of operations running concurrently.
 *
 * A limiter starts with a fixed number of permits. Use [method@Dex.Limiter.acquire]
 * and [method@Dex.Limiter.release] directly when a permit must cover a custom
 * scope, or use [method@Dex.Limiter.run] or
 * [method@Dex.Limiter.run_on_pool] to acquire a permit and release it
 * automatically when the work completes.
 *
 * Since: 1.2
 */

typedef struct _DexLimiterAcquire DexLimiterAcquire;

struct _DexLimiter
{
  DexObject     parent_instance;
  DexSemaphore *semaphore;
  DexPromise   *drain_promise;
  guint         max_concurrency;
  guint         pending_acquires;
  guint         acquired;
  guint         closed : 1;
};

typedef struct _DexLimiterClass
{
  DexObjectClass parent_class;
} DexLimiterClass;

struct _DexLimiterAcquire
{
  DexFuture parent_instance;

  DexLimiter *limiter;
  guint       discarded : 1;
};

typedef struct _DexLimiterAcquireClass
{
  DexFutureClass parent_class;
} DexLimiterAcquireClass;

typedef struct _DexLimiterRun
{
  DexLimiter     *limiter;
  DexScheduler   *scheduler;
  DexFiberFunc    func;
  gpointer        func_data;
  GDestroyNotify  func_data_destroy;
  gsize           stack_size;
} DexLimiterRun;

typedef struct _DexLimiterRunCoroutine
{
  DexLimiter       *limiter;
  DexScheduler     *scheduler;
  DexCoroutineFunc  func;
  gpointer          user_data;
  GDestroyNotify    user_data_destroy;
} DexLimiterRunCoroutine;

typedef struct _DexLimiterRunOnPool
{
  DexLimiter     *limiter;
  DexThreadPool  *pool;
  DexThreadFunc   thread_func;
  gpointer        user_data;
  GDestroyNotify  user_data_destroy;
} DexLimiterRunOnPool;

#define DEX_TYPE_LIMITER_ACQUIRE    (dex_limiter_acquire_get_type())
#define DEX_IS_LIMITER_ACQUIRE(obj) (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_LIMITER_ACQUIRE))

static GType dex_limiter_acquire_get_type    (void);
static void  dex_limiter_maybe_resolve_drain (DexLimiter *limiter);

DEX_DEFINE_FINAL_TYPE (DexLimiter, dex_limiter, DEX_TYPE_OBJECT)
DEX_DEFINE_FINAL_TYPE (DexLimiterAcquire, dex_limiter_acquire, DEX_TYPE_FUTURE)

#undef DEX_TYPE_LIMITER
#define DEX_TYPE_LIMITER dex_limiter_type

static void
dex_limiter_do_release (DexLimiter *limiter)
{
  gboolean post = FALSE;

  g_assert (DEX_IS_LIMITER (limiter));

  dex_object_lock (limiter);
  if (limiter->acquired > 0)
    {
      limiter->acquired--;
      post = !limiter->closed;
    }
  dex_object_unlock (limiter);

  if (post)
    dex_semaphore_post (limiter->semaphore);

  dex_limiter_maybe_resolve_drain (limiter);
}

static void
dex_limiter_maybe_resolve_drain (DexLimiter *limiter)
{
  DexPromise *drain_promise = NULL;

  g_assert (DEX_IS_LIMITER (limiter));

  dex_object_lock (limiter);
  if (limiter->drain_promise != NULL &&
      limiter->pending_acquires == 0 &&
      limiter->acquired == 0)
    drain_promise = g_steal_pointer (&limiter->drain_promise);
  dex_object_unlock (limiter);

  if (drain_promise != NULL)
    {
      dex_promise_resolve_boolean (drain_promise, TRUE);
      dex_clear (&drain_promise);
    }
}

static gboolean
dex_limiter_acquire_mark_acquired (DexLimiterAcquire *acquire,
                                   gboolean          *should_release)
{
  DexLimiter *limiter = acquire->limiter;
  gboolean acquired = FALSE;
  gboolean discarded;
  gboolean closed;

  g_assert (DEX_IS_LIMITER_ACQUIRE (acquire));
  g_assert (DEX_IS_LIMITER (limiter));

  dex_object_lock (acquire);
  discarded = acquire->discarded;
  dex_object_unlock (acquire);

  dex_object_lock (limiter);
  closed = limiter->closed;
  if (!discarded && !closed)
    {
      limiter->acquired++;
      acquired = TRUE;
    }
  else if (discarded && !closed)
    *should_release = TRUE;
  dex_object_unlock (limiter);

  return acquired;
}

static DexFuture *
dex_limiter_acquire_wait_cb (DexFuture *completed,
                             gpointer   user_data)
{
  static const GValue limiter_acquired_value = {G_TYPE_BOOLEAN, {{.v_int = TRUE}, {.v_int = 0}}};
  DexLimiterAcquire *acquire = user_data;
  GError *error = NULL;
  gboolean should_release = FALSE;
  gboolean closed;
  const GValue *value;

  g_assert (DEX_IS_LIMITER_ACQUIRE (acquire));

  value = dex_future_get_value (completed, &error);

  if (value != NULL)
    {
      if (dex_limiter_acquire_mark_acquired (acquire, &should_release))
        {
          dex_future_complete (DEX_FUTURE (acquire), &limiter_acquired_value, NULL);
        }
      else
        {
          dex_object_lock (acquire->limiter);
          closed = acquire->limiter->closed;
          dex_object_unlock (acquire->limiter);

          if (closed)
            dex_future_complete (DEX_FUTURE (acquire),
                                 NULL,
                                 g_error_new (DEX_ERROR,
                                              DEX_ERROR_SEMAPHORE_CLOSED,
                                              "Limiter is closed"));
          else
            dex_future_complete (DEX_FUTURE (acquire),
                                 NULL,
                                 g_error_new_literal (G_IO_ERROR,
                                                      G_IO_ERROR_CANCELLED,
                                                      "Limiter acquisition was cancelled"));
        }


      if (should_release)
        dex_semaphore_post (acquire->limiter->semaphore);
    }
  else if (error != NULL)
    {
      dex_object_lock (acquire->limiter);
      closed = acquire->limiter->closed;
      dex_object_unlock (acquire->limiter);

      if (closed)
        dex_future_complete (DEX_FUTURE (acquire),
                             NULL,
                             g_error_new (DEX_ERROR,
                                          DEX_ERROR_SEMAPHORE_CLOSED,
                                          "Limiter is closed"));
      else
        dex_future_complete (DEX_FUTURE (acquire), NULL, g_steal_pointer (&error));
    }

  g_clear_error (&error);
  dex_object_lock (acquire->limiter);
  if (acquire->limiter->pending_acquires > 0)
    acquire->limiter->pending_acquires--;
  dex_object_unlock (acquire->limiter);

  dex_limiter_maybe_resolve_drain (acquire->limiter);

  return NULL;
}

static void
dex_limiter_acquire_discard (DexFuture *future)
{
  DexLimiterAcquire *acquire = (DexLimiterAcquire *)future;

  g_assert (DEX_IS_LIMITER_ACQUIRE (acquire));

  dex_object_lock (acquire);
  acquire->discarded = TRUE;
  dex_object_unlock (acquire);
}

static void
dex_limiter_acquire_finalize (DexObject *object)
{
  DexLimiterAcquire *acquire = (DexLimiterAcquire *)object;

  dex_clear (&acquire->limiter);

  DEX_OBJECT_CLASS (dex_limiter_acquire_parent_class)->finalize (object);
}

static void
dex_limiter_acquire_class_init (DexLimiterAcquireClass *acquire_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (acquire_class);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (acquire_class);

  object_class->finalize = dex_limiter_acquire_finalize;

  future_class->discard = dex_limiter_acquire_discard;
}

static void
dex_limiter_acquire_init (DexLimiterAcquire *acquire)
{
}

static void
dex_limiter_run_free (DexLimiterRun *state)
{
  if (state == NULL)
    return;

  dex_clear (&state->limiter);
  dex_clear (&state->scheduler);

  if (state->func_data_destroy != NULL)
    state->func_data_destroy (state->func_data);

  g_free (state);
}

static void
dex_limiter_run_coroutine_free (DexLimiterRunCoroutine *state)
{
  if (state == NULL)
    return;

  dex_clear (&state->limiter);
  dex_clear (&state->scheduler);
  if (state->user_data_destroy != NULL)
    state->user_data_destroy (g_steal_pointer (&state->user_data));

  g_free (state);
}

static void
dex_limiter_run_on_pool_free (DexLimiterRunOnPool *state)
{
  if (state == NULL)
    return;

  dex_clear (&state->limiter);
  dex_clear (&state->pool);

  if (state->user_data_destroy != NULL)
    state->user_data_destroy (state->user_data);

  g_free (state);
}

static DexFuture *
dex_limiter_run_release_cb (DexFuture *completed,
                            gpointer   user_data)
{
  dex_limiter_do_release (user_data);

  return NULL;
}

static DexFuture *
dex_limiter_run_acquired_cb (DexFuture *completed,
                             gpointer   user_data)
{
  DexLimiterRun *state = user_data;
  gpointer func_data;
  GDestroyNotify func_data_destroy;
  DexFuture *fiber;

  g_assert (state != NULL);
  g_assert (DEX_IS_LIMITER (state->limiter));
  g_assert (state->scheduler == NULL || DEX_IS_SCHEDULER (state->scheduler));
  g_assert (state->func != NULL);

  func_data = g_steal_pointer (&state->func_data);
  func_data_destroy = g_steal_pointer (&state->func_data_destroy);

  fiber = dex_scheduler_spawn (state->scheduler,
                               state->stack_size,
                               state->func,
                               func_data,
                               func_data_destroy);

  dex_future_disown (dex_future_finally (dex_ref (fiber),
                                         dex_limiter_run_release_cb,
                                         dex_ref (state->limiter),
                                         dex_unref));

  return fiber;
}

static DexFuture *
dex_limiter_run_coroutine_acquired_cb (DexFuture *completed,
                                       gpointer   user_data)
{
  DexLimiterRunCoroutine *state = user_data;
  DexFuture *coroutine;
  GDestroyNotify user_data_destroy;
  gpointer data;

  g_assert (state != NULL);
  g_assert (DEX_IS_LIMITER (state->limiter));
  g_assert (state->scheduler == NULL || DEX_IS_SCHEDULER (state->scheduler));
  g_assert (state->func != NULL);

  data = g_steal_pointer (&state->user_data);
  user_data_destroy = state->user_data_destroy;
  state->user_data_destroy = NULL;

  coroutine = dex_scheduler_spawn_coroutine (state->scheduler,
                                             state->func,
                                             data,
                                             user_data_destroy);

  dex_future_disown (dex_future_finally (dex_ref (coroutine),
                                         dex_limiter_run_release_cb,
                                         dex_ref (state->limiter),
                                         dex_unref));

  return coroutine;
}

static DexFuture *
dex_limiter_run_on_pool_acquired_cb (DexFuture *completed,
                                     gpointer   user_data)
{
  DexLimiterRunOnPool *state = user_data;
  gpointer thread_data;
  GDestroyNotify thread_data_destroy;
  DexFuture *thread;

  g_assert (state != NULL);
  g_assert (DEX_IS_LIMITER (state->limiter));
  g_assert (DEX_IS_THREAD_POOL (state->pool));
  g_assert (state->thread_func != NULL);

  thread_data = g_steal_pointer (&state->user_data);
  thread_data_destroy = g_steal_pointer (&state->user_data_destroy);

  thread = dex_thread_pool_submit (state->pool,
                                   "[dex-limiter-run-on-pool]",
                                   state->thread_func,
                                   thread_data,
                                   thread_data_destroy);

  dex_future_disown (dex_future_finally (dex_ref (thread),
                                         dex_limiter_run_release_cb,
                                         dex_ref (state->limiter),
                                         dex_unref));

  return thread;
}

static void
dex_limiter_finalize (DexObject *object)
{
  DexLimiter *limiter = (DexLimiter *)object;

  if (limiter->semaphore != NULL)
    dex_semaphore_close (limiter->semaphore);

  dex_clear (&limiter->drain_promise);

  dex_clear (&limiter->semaphore);

  DEX_OBJECT_CLASS (dex_limiter_parent_class)->finalize (object);
}

static void
dex_limiter_class_init (DexLimiterClass *limiter_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (limiter_class);

  object_class->finalize = dex_limiter_finalize;

  g_type_ensure (DEX_TYPE_LIMITER_ACQUIRE);
}

static void
dex_limiter_init (DexLimiter *limiter)
{
  limiter->semaphore = dex_semaphore_new ();
}

/**
 * dex_limiter_new:
 * @max_concurrency: the maximum number of concurrent operations
 *
 * Creates a new `DexLimiter` with @max_concurrency permits.
 *
 * @max_concurrency must be greater than zero. Each successful acquisition
 * consumes one permit until [method@Dex.Limiter.release] is called.
 *
 * Returns: (transfer full): a new `DexLimiter`
 *
 * Since: 1.2
 */
DexLimiter *
dex_limiter_new (guint max_concurrency)
{
  DexLimiter *limiter;

  g_return_val_if_fail (max_concurrency > 0, NULL);

  limiter = (DexLimiter *)dex_object_create_instance (DEX_TYPE_LIMITER);
  limiter->max_concurrency = max_concurrency;
  dex_semaphore_post_many (limiter->semaphore, max_concurrency);

  return limiter;
}

/**
 * dex_limiter_get_max_concurrency:
 * @limiter: a `DexLimiter`
 *
 * Gets the maximum number of permits available from @limiter.
 *
 * Returns: the maximum number of concurrent operations
 * Since: 1.2
 */
guint
dex_limiter_get_max_concurrency (DexLimiter *limiter)
{
  g_return_val_if_fail (DEX_IS_LIMITER (limiter), 0);

  return limiter->max_concurrency;
}

/**
 * dex_limiter_acquire:
 * @limiter: a `DexLimiter`
 *
 * Acquires one permit from @limiter.
 *
 * The returned future resolves to %TRUE when a permit has been acquired. Call
 * [method@Dex.Limiter.release] exactly once for each resolved acquisition.
 *
 * If the returned future is discarded before the permit is acquired, the permit
 * is returned to the limiter when it becomes available. If @limiter is closed
 * before acquisition completes, the returned future rejects with
 * %DEX_ERROR_SEMAPHORE_CLOSED.
 *
 * Returns: (transfer full): a future that resolves when a permit is acquired
 * Since: 1.2
 */
DexFuture *
dex_limiter_acquire (DexLimiter *limiter)
{
  DexLimiterAcquire *acquire;
  DexFuture *wait;

  dex_return_error_if_fail (DEX_IS_LIMITER (limiter));

  dex_object_lock (limiter);
  if (limiter->closed)
    {
      dex_object_unlock (limiter);
      return dex_future_new_reject (DEX_ERROR,
                                    DEX_ERROR_SEMAPHORE_CLOSED,
                                    "Limiter is closed");
    }

  limiter->pending_acquires++;
  dex_object_unlock (limiter);

  acquire = (DexLimiterAcquire *)dex_object_create_instance (dex_limiter_acquire_get_type ());
  acquire->limiter = dex_ref (limiter);

  wait = dex_future_finally (dex_semaphore_wait (limiter->semaphore),
                             dex_limiter_acquire_wait_cb,
                             dex_ref (acquire),
                             dex_unref);
  dex_future_disown (wait);

  return DEX_FUTURE (acquire);
}

/**
 * dex_limiter_release:
 * @limiter: a `DexLimiter`
 *
 * Releases one permit previously acquired from @limiter.
 *
 * This must be called exactly once for each successful
 * [method@Dex.Limiter.acquire] unless the permit is managed by
 * [method@Dex.Limiter.run].
 *
 * Since: 1.2
 */
void
dex_limiter_release (DexLimiter *limiter)
{
  g_return_if_fail (DEX_IS_LIMITER (limiter));

  dex_limiter_do_release (limiter);
}

/**
 * dex_limiter_close_after_drain:
 * @limiter: a `DexLimiter`
 *
 * Closes @limiter and waits for all queued and running work to complete.
 *
 * After this function is called, new acquire attempts are rejected with
 * %DEX_ERROR_SEMAPHORE_CLOSED.
 *
 * The returned future resolves to `%TRUE` once all outstanding pending acquire
 * futures and held permits are complete. Existing permit holders must still
 * eventually release.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to `true`
 *
 * Since: 1.2
 */
DexFuture *
dex_limiter_close_after_drain (DexLimiter *limiter)
{
  DexPromise *drain_promise;
  gboolean     should_close = FALSE;

  g_return_val_if_fail (DEX_IS_LIMITER (limiter), NULL);

  dex_object_lock (limiter);
  if (limiter->drain_promise == NULL)
    limiter->drain_promise = dex_promise_new ();

  drain_promise = dex_ref (limiter->drain_promise);
  if (!limiter->closed)
    should_close = TRUE;
  dex_object_unlock (limiter);

  if (should_close)
    dex_limiter_close (limiter);

  dex_limiter_maybe_resolve_drain (limiter);

  return DEX_FUTURE (drain_promise);
}

/**
 * dex_limiter_run:
 * @limiter: a `DexLimiter`
 * @scheduler: (nullable): scheduler to spawn @func on, or %NULL for the thread default
 * @stack_size: stack size for the spawned fiber, or zero to use the default
 * @func: (scope async): fiber function to run after a permit is acquired
 * @func_data: closure data for @func
 * @func_data_destroy: destroy notify for @func_data
 *
 * Runs @func while holding one permit from @limiter.
 *
 * The returned future resolves or rejects with the result of the spawned fiber.
 * The permit is released automatically after the fiber resolves or rejects. If
 * the returned future is discarded after the fiber starts, the fiber is allowed
 * to complete so that the permit can be released.
 *
 * Returns: (transfer full): a future representing the spawned fiber
 *
 * Since: 1.2
 */
DexFuture *
dex_limiter_run (DexLimiter     *limiter,
                 DexScheduler   *scheduler,
                 gsize           stack_size,
                 DexFiberFunc    func,
                 gpointer        func_data,
                 GDestroyNotify  func_data_destroy)
{
  DexLimiterRun *state;

  dex_return_error_if_fail (DEX_IS_LIMITER (limiter));
  dex_return_error_if_fail (scheduler == NULL || DEX_IS_SCHEDULER (scheduler));
  dex_return_error_if_fail (func != NULL);

  state = g_new0 (DexLimiterRun, 1);
  state->limiter = dex_ref (limiter);
  state->scheduler = scheduler ? dex_ref (scheduler) : NULL;
  state->stack_size = stack_size;
  state->func = func;
  state->func_data = func_data;
  state->func_data_destroy = func_data_destroy;

  return dex_future_then (dex_limiter_acquire (limiter),
                          dex_limiter_run_acquired_cb,
                          state,
                          (GDestroyNotify)dex_limiter_run_free);
}

/**
 * dex_limiter_run_coroutine:
 * @limiter: a `DexLimiter`
 * @scheduler: (nullable): scheduler to spawn @func on, or %NULL for the thread default
 * @func: (scope async): coroutine function to run after a permit is acquired
 * @user_data: closure data for @func
 * @user_data_destroy: destroy notify for @user_data
 *
 * Runs @func while holding one permit from @limiter.
 *
 * The returned future resolves or rejects with the result of the spawned
 * coroutine. The permit is released automatically after the coroutine resolves
 * or rejects. If the returned future is discarded after the coroutine starts,
 * the coroutine is allowed to complete so that the permit can be released.
 *
 * Returns: (transfer full): a future representing the spawned coroutine
 *
 * Since: 1.2
 */
DexFuture *
dex_limiter_run_coroutine (DexLimiter       *limiter,
                           DexScheduler     *scheduler,
                           DexCoroutineFunc  func,
                           gpointer          user_data,
                           GDestroyNotify    user_data_destroy)
{
  DexLimiterRunCoroutine *state;

  dex_return_error_if_fail (DEX_IS_LIMITER (limiter));
  dex_return_error_if_fail (scheduler == NULL || DEX_IS_SCHEDULER (scheduler));
  dex_return_error_if_fail (func != NULL);

  state = g_new0 (DexLimiterRunCoroutine, 1);
  state->limiter = dex_ref (limiter);
  state->scheduler = scheduler ? dex_ref (scheduler) : NULL;
  state->func = func;
  state->user_data = user_data;
  state->user_data_destroy = user_data_destroy;

  return dex_future_then (dex_limiter_acquire (limiter),
                          dex_limiter_run_coroutine_acquired_cb,
                          state,
                          (GDestroyNotify)dex_limiter_run_coroutine_free);
}

/**
 * dex_limiter_run_on_pool:
 * @limiter: a `DexLimiter`
 * @pool: a `DexThreadPool`
 * @thread_func: (scope async) (closure user_data) (destroy user_data_destroy):
 *   function to run on @pool after a permit is acquired
 * @user_data: closure data for @thread_func
 * @user_data_destroy: destroy notify for @user_data
 *
 * Runs @thread_func on @pool while holding one permit from @limiter.
 *
 * The returned future resolves or rejects with the result of the submitted
 * thread-pool work. The permit is released automatically after the work
 * resolves or rejects. If the returned future is discarded after the work is
 * submitted to @pool, the work is allowed to complete so that the permit can be
 * released.
 *
 * Workers in `DexThreadPool` are not scheduler threads, so @thread_func must
 * not use `dex_await()`.
 *
 * Returns: (transfer full): a future representing the submitted work
 *
 * Since: 1.2
 */
DexFuture *
dex_limiter_run_on_pool (DexLimiter     *limiter,
                         DexThreadPool  *pool,
                         DexThreadFunc   thread_func,
                         gpointer        user_data,
                         GDestroyNotify  user_data_destroy)
{
  DexLimiterRunOnPool *state;

  dex_return_error_if_fail (DEX_IS_LIMITER (limiter));
  dex_return_error_if_fail (DEX_IS_THREAD_POOL (pool));
  dex_return_error_if_fail (thread_func != NULL);

  state = g_new0 (DexLimiterRunOnPool, 1);
  state->limiter = dex_ref (limiter);
  state->pool = dex_ref (pool);
  state->thread_func = thread_func;
  state->user_data = user_data;
  state->user_data_destroy = user_data_destroy;

  return dex_future_then (dex_limiter_acquire (limiter),
                          dex_limiter_run_on_pool_acquired_cb,
                          state,
                          (GDestroyNotify)dex_limiter_run_on_pool_free);
}

/**
 * dex_limiter_close:
 * @limiter: a `DexLimiter`
 *
 * Closes @limiter.
 *
 * Pending and future acquisitions reject with %DEX_ERROR_SEMAPHORE_CLOSED.
 * Permits already acquired remain valid, but releasing them after close will
 * not make them available for new work.
 *
 * Since: 1.2
 */
void
dex_limiter_close (DexLimiter *limiter)
{
  gboolean do_close = FALSE;

  g_return_if_fail (DEX_IS_LIMITER (limiter));

  dex_object_lock (limiter);
  if (!limiter->closed)
    {
      limiter->closed = TRUE;
      do_close = TRUE;
    }
  dex_object_unlock (limiter);

  if (do_close)
    dex_semaphore_close (limiter->semaphore);
}
