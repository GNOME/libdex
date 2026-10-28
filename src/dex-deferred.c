/* dex-deferred.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "dex-block-private.h"
#include "dex-deferred.h"
#include "dex-error.h"
#include "dex-future-private.h"
#include "dex-thread.h"
#include "dex-thread-storage-private.h"

typedef enum
{
  DEX_DEFERRED_CALLBACK,
  DEX_DEFERRED_FIBER,
  DEX_DEFERRED_COROUTINE,
  DEX_DEFERRED_THREAD,
  DEX_DEFERRED_THREAD_POOL,
} DexDeferredKind;

typedef struct
{
  DexDeferred     *deferred;
  DexScheduler    *scheduler;
  DexThreadPool   *pool;
  char            *name;
  gpointer         user_data;
  GDestroyNotify   destroy;
  gsize            stack_size;
  DexDeferredKind  kind;
  union {
    DexDeferredFunc  callback;
    DexFiberFunc     fiber;
    DexCoroutineFunc coroutine;
    DexThreadFunc    thread;
  }                func;
} DexDeferredState;

struct _DexDeferred
{
  DexFuture         parent_instance;
  DexDeferredState *state;
  DexFuture        *awaiting;
};

typedef struct _DexDeferredClass
{
  DexFutureClass parent_class;
} DexDeferredClass;

/**
 * DexDeferred:
 *
 * A future whose work starts when another future observes it, such as through
 * [ctor@Dex.Future.then] or [method@Dex.Future.await]. Its work starts once.
 * Querying its status or value does not start the work.
 * Dropping it without observing it releases its callback data without
 * invoking the callback.
 * If its last observer is discarded before the work starts, the deferred
 * rejects with %G_IO_ERROR_CANCELLED. Work that has already started may
 * still run while cancellation propagates.
 *
 * Since: 1.3
 */
DEX_DEFINE_FINAL_TYPE (DexDeferred, dex_deferred, DEX_TYPE_FUTURE)

#undef DEX_TYPE_DEFERRED
#define DEX_TYPE_DEFERRED dex_deferred_type

static void
dex_deferred_state_free (DexDeferredState *state)
{
  if (state == NULL)
    return;

  if (state->destroy != NULL)
    state->destroy (state->user_data);

  dex_clear (&state->scheduler);
  dex_clear (&state->pool);
  g_free (state->name);
  g_free (state);
}

static void
dex_deferred_cancel_unstarted (DexDeferred      *self,
                               DexDeferredState *state)
{
  dex_deferred_state_free (state);
  dex_future_complete (DEX_FUTURE (self), NULL,
                       g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                            "Deferred was discarded before starting"));
}

static gboolean
dex_deferred_propagate (DexFuture *future,
                        DexFuture *completed)
{
  DexDeferred *self = DEX_DEFERRED (future);
  DexFuture *awaiting;

  dex_object_lock (self);
  awaiting = g_steal_pointer (&self->awaiting);
  dex_object_unlock (self);

  dex_clear (&awaiting);

  return FALSE;
}

static void
dex_deferred_discard (DexFuture *future)
{
  DexDeferred *self = DEX_DEFERRED (future);
  DexDeferredState *state;
  DexFuture *awaiting;

  dex_object_lock (self);
  state = g_steal_pointer (&self->state);
  awaiting = g_steal_pointer (&self->awaiting);
  dex_object_unlock (self);

  if (state != NULL)
    dex_deferred_cancel_unstarted (self, state);

  if (awaiting != NULL)
    {
      dex_future_discard (awaiting, future);
      dex_clear (&awaiting);
    }
}

static void
dex_deferred_finalize (DexObject *object)
{
  DexDeferred *self = DEX_DEFERRED (object);

  dex_deferred_state_free (g_steal_pointer (&self->state));
  dex_clear (&self->awaiting);

  DEX_OBJECT_CLASS (dex_deferred_parent_class)->finalize (object);
}

static void dex_deferred_observe (DexFuture *future);

static void
dex_deferred_class_init (DexDeferredClass *klass)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (klass);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (klass);

  object_class->finalize = dex_deferred_finalize;

  future_class->observe = dex_deferred_observe;
  future_class->propagate = dex_deferred_propagate;
  future_class->discard = dex_deferred_discard;
}

static void
dex_deferred_init (DexDeferred *self)
{
}

static DexDeferred *
dex_deferred_new_with_state (DexDeferredState *state)
{
  DexDeferred *self;

  self = (DexDeferred *)dex_object_create_instance (dex_deferred_get_type ());
  self->state = state;

  return self;
}

static DexScheduler *
dex_deferred_ref_scheduler (DexScheduler *scheduler)
{
  DexThreadStorage *storage;

  if (scheduler != NULL)
    return dex_ref (scheduler);

  if ((storage = dex_thread_storage_peek ()) && storage->scheduler != NULL)
    return dex_ref (storage->scheduler);

  return dex_ref (dex_scheduler_get_default ());
}

/**
 * dex_deferred_new:
 * @scheduler: (nullable): scheduler for @func, or %NULL to capture the thread's scheduler
 * @func: (scope notified) (closure user_data) (destroy destroy): callback to run
 * @user_data: closure data for @func
 * @destroy: destroy notify for @user_data
 *
 * Creates deferred work that calls @func on @scheduler. The returned future
 * completes with the result of the future returned by @func.
 *
 * If @scheduler is %NULL and the current thread has no scheduler, the process
 * default scheduler is captured instead.
 *
 * Returns: (transfer full): a new deferred
 *
 * Since: 1.3
 */
DexDeferred *
dex_deferred_new (DexScheduler    *scheduler,
                  DexDeferredFunc  func,
                  gpointer         user_data,
                  GDestroyNotify   destroy)
{
  DexDeferredState *state;

  g_return_val_if_fail (scheduler == NULL || DEX_IS_SCHEDULER (scheduler), NULL);
  g_return_val_if_fail (func != NULL, NULL);

  state = g_new0 (DexDeferredState, 1);
  state->scheduler = dex_deferred_ref_scheduler (scheduler);
  state->func.callback = func;
  state->user_data = user_data;
  state->destroy = destroy;
  state->kind = DEX_DEFERRED_CALLBACK;

  if (state->scheduler == NULL)
    {
      dex_deferred_state_free (state);
      return NULL;
    }

  return dex_deferred_new_with_state (state);
}

/**
 * dex_deferred_new_fiber:
 * @scheduler: (nullable): scheduler for the fiber, or %NULL to capture the thread's scheduler
 * @stack_size: fiber stack size in bytes, or 0 for the default
 * @func: (scope notified) (closure user_data) (destroy destroy): fiber entry point
 * @user_data: closure data for @func
 * @destroy: destroy notify for @user_data
 *
 * Creates a deferred fiber. The fiber starts when the deferred is observed.
 *
 * If @scheduler is %NULL and the current thread has no scheduler, the process
 * default scheduler is captured instead.
 *
 * Returns: (transfer full): a new deferred
 *
 * Since: 1.3
 */
DexDeferred *
dex_deferred_new_fiber (DexScheduler   *scheduler,
                        gsize           stack_size,
                        DexFiberFunc    func,
                        gpointer        user_data,
                        GDestroyNotify  destroy)
{
  DexDeferredState *state;

  g_return_val_if_fail (scheduler == NULL || DEX_IS_SCHEDULER (scheduler), NULL);
  g_return_val_if_fail (func != NULL, NULL);

  state = g_new0 (DexDeferredState, 1);
  state->scheduler = dex_deferred_ref_scheduler (scheduler);
  state->func.fiber = func;
  state->user_data = user_data;
  state->destroy = destroy;
  state->stack_size = stack_size;
  state->kind = DEX_DEFERRED_FIBER;

  if (state->scheduler == NULL)
    {
      dex_deferred_state_free (state);
      return NULL;
    }

  return dex_deferred_new_with_state (state);
}

/**
 * dex_deferred_new_coroutine:
 * @scheduler: (nullable): scheduler for the coroutine, or %NULL to capture the thread's scheduler
 * @func: (scope notified) (closure user_data) (destroy destroy): coroutine entry point
 * @user_data: closure data for @func
 * @destroy: destroy notify for @user_data
 *
 * Creates a deferred coroutine. The coroutine starts when the deferred is observed.
 * If @scheduler is %NULL and the current thread has no scheduler, the process
 * default scheduler is captured instead.
 *
 * Returns: (transfer full): a new deferred
 *
 * Since: 1.3
 */
DexDeferred *
dex_deferred_new_coroutine (DexScheduler     *scheduler,
                            DexCoroutineFunc  func,
                            gpointer          user_data,
                            GDestroyNotify    destroy)
{
  DexDeferredState *state;

  g_return_val_if_fail (scheduler == NULL || DEX_IS_SCHEDULER (scheduler), NULL);
  g_return_val_if_fail (func != NULL, NULL);

  state = g_new0 (DexDeferredState, 1);
  state->scheduler = dex_deferred_ref_scheduler (scheduler);
  state->func.coroutine = func;
  state->user_data = user_data;
  state->destroy = destroy;
  state->kind = DEX_DEFERRED_COROUTINE;

  if (state->scheduler == NULL)
    {
      dex_deferred_state_free (state);
      return NULL;
    }

  return dex_deferred_new_with_state (state);
}

/**
 * dex_deferred_new_thread:
 * @thread_name: (nullable): name of the new thread
 * @func: (scope notified) (closure user_data) (destroy destroy): thread entry point
 * @user_data: closure data for @func
 * @destroy: destroy notify for @user_data
 *
 * Creates deferred work on a dedicated thread.
 *
 * Once the thread starts, discarding the deferred does not stop the work.
 * Its callback data remains valid until the work completes.
 *
 * Returns: (transfer full): a new deferred
 *
 * Since: 1.3
 */
DexDeferred *
dex_deferred_new_thread (const char     *thread_name,
                         DexThreadFunc   func,
                         gpointer        user_data,
                         GDestroyNotify  destroy)
{
  DexDeferredState *state;

  g_return_val_if_fail (func != NULL, NULL);

  state = g_new0 (DexDeferredState, 1);
  state->name = g_strdup (thread_name);
  state->func.thread = func;
  state->user_data = user_data;
  state->destroy = destroy;
  state->kind = DEX_DEFERRED_THREAD;

  return dex_deferred_new_with_state (state);
}

/**
 * dex_deferred_new_on_thread_pool:
 * @pool: a thread pool
 * @name: (nullable): name for the returned future
 * @func: (scope notified) (closure user_data) (destroy destroy): thread entry point
 * @user_data: closure data for @func
 * @destroy: destroy notify for @user_data
 *
 * Creates deferred work on @pool.
 *
 * The deferred retains @pool until the submitted work completes, even if
 * the deferred is discarded after submission.
 *
 * Returns: (transfer full): a new deferred
 *
 * Since: 1.3
 */
DexDeferred *
dex_deferred_new_on_thread_pool (DexThreadPool  *pool,
                                 const char     *name,
                                 DexThreadFunc   func,
                                 gpointer        user_data,
                                 GDestroyNotify  destroy)
{
  DexDeferredState *state;

  g_return_val_if_fail (DEX_IS_THREAD_POOL (pool), NULL);
  g_return_val_if_fail (func != NULL, NULL);

  state = g_new0 (DexDeferredState, 1);
  state->pool = dex_ref (pool);
  state->name = g_strdup (name);
  state->func.thread = func;
  state->user_data = user_data;
  state->destroy = destroy;
  state->kind = DEX_DEFERRED_THREAD_POOL;

  return dex_deferred_new_with_state (state);
}

static void
dex_deferred_attach (DexDeferred *self,
                     DexFuture   *future)
{
  gboolean discarded;

  g_assert (DEX_IS_DEFERRED (self));
  g_assert (DEX_IS_FUTURE (future));

  dex_object_lock (self);
  if (!DEX_FUTURE (self)->abandoned)
    self->awaiting = dex_ref (future);
  dex_object_unlock (self);

  /* Keep the caller's reference through chaining. Discard may release
   * self->awaiting before this call, or may arrive before the chain exists.
   */
  dex_future_chain (future, DEX_FUTURE (self));

  dex_object_lock (self);
  discarded = DEX_FUTURE (self)->abandoned;
  dex_object_unlock (self);

  if (discarded)
    dex_future_discard (future, DEX_FUTURE (self));

  dex_unref (future);
}

static void
dex_deferred_state_destroy (gpointer data)
{
  dex_deferred_state_free (data);
}

static DexFuture *
dex_deferred_finished (DexFuture *completed,
                       gpointer   user_data)
{
  return dex_ref (completed);
}

static void
dex_deferred_invoke (gpointer user_data)
{
  DexDeferredState *state = user_data;
  DexDeferred *self = state->deferred;
  DexFuture *future;
  gboolean abandoned;

  g_assert (state->kind == DEX_DEFERRED_CALLBACK);

  dex_object_lock (self);
  abandoned = DEX_FUTURE (self)->abandoned;
  dex_object_unlock (self);

  if (abandoned)
    {
      state->deferred = NULL;
      dex_deferred_cancel_unstarted (self, state);
      dex_unref (self);
      return;
    }

  future = state->func.callback (state->user_data);
  if (future == NULL)
    future = dex_future_new_reject (DEX_ERROR,
                                    DEX_ERROR_NO_RESULT,
                                    "Deferred callback returned NULL");

  state->deferred = NULL;
  future = dex_future_finally (future,
                               dex_deferred_finished,
                               state,
                               dex_deferred_state_destroy);
  dex_deferred_attach (self, future);
  dex_unref (self);
}

static void
dex_deferred_observe (DexFuture *future)
{
  DexDeferred *self = DEX_DEFERRED (future);
  DexDeferredState *state;
  DexFuture *started = NULL;
  GDestroyNotify destroy;
  gpointer user_data;

  dex_object_lock (self);
  state = future->abandoned ? NULL : g_steal_pointer (&self->state);
  dex_object_unlock (self);

  if (state == NULL)
    return;

  dex_object_lock (self);
  if (future->abandoned)
    {
      dex_object_unlock (self);
      dex_deferred_cancel_unstarted (self, state);
      return;
    }

  if (state->kind == DEX_DEFERRED_CALLBACK)
    {
      state->deferred = dex_ref (self);
      dex_object_unlock (self);
      dex_scheduler_push (state->scheduler, dex_deferred_invoke, state);
      return;
    }

  user_data = g_steal_pointer (&state->user_data);
  destroy = state->destroy;
  state->destroy = NULL;

  switch (state->kind)
    {
    case DEX_DEFERRED_FIBER:
      started = (dex_scheduler_spawn) (state->scheduler, state->stack_size, state->func.fiber, user_data, destroy);
      break;

    case DEX_DEFERRED_COROUTINE:
      started = (dex_scheduler_spawn_coroutine) (state->scheduler, state->func.coroutine, user_data, destroy);
      break;

    case DEX_DEFERRED_THREAD:
      started = dex_thread_spawn (state->name, state->func.thread, user_data, destroy);
      break;

    case DEX_DEFERRED_THREAD_POOL:
      started = dex_thread_pool_submit (state->pool, state->name, state->func.thread, user_data, destroy);
      break;

    case DEX_DEFERRED_CALLBACK:
    default:
      g_assert_not_reached ();
    }

  /* Discard must not mark the deferred abandoned between the check above
   * and the launch. Once launched, dex_deferred_attach() handles discard.
   */
  dex_object_unlock (self);

  if (state->kind == DEX_DEFERRED_THREAD || state->kind == DEX_DEFERRED_THREAD_POOL)
    {
      DexScheduler *scheduler = dex_deferred_ref_scheduler (NULL);

      /* Native work continues after discard. Keep its result and pool alive
       * until completion, releasing them on the scheduler so pool finalization
       * cannot join the worker from within that worker.
       */
      dex_future_disown_full (dex_block_new (dex_ref (started),
                                             scheduler,
                                             DEX_BLOCK_KIND_FINALLY,
                                             dex_deferred_finished,
                                             state,
                                             dex_deferred_state_destroy),
                              scheduler);
      dex_unref (scheduler);
    }
  else
    {
      dex_deferred_state_free (state);
    }

  dex_deferred_attach (self, started);
}
