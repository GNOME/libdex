/* dex-thread-pool.c
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

#include "dex-error.h"
#include "dex-object-private.h"
#include "dex-promise.h"
#include "dex-thread-pool.h"
#include "dex-waiter-private.h"

/**
 * DexThreadPool:
 *
 * `DexThreadPool` is a thread pool for managing native OS threads similar
 * to [struct@GLib.ThreadPool].
 *
 * The threads managed by `DexThreadPool` do not contain a
 * [class@Dex.Scheduler] which means that you cannot await
 * futures or schedule [class@Dex.Block] from a worker thread.
 *
 * Threads are created up-front from [ctor@Dex.ThreadPool.new].
 *
 * `DexThreadPool` primarily exists for situations where you are
 * using blocking external libraries and want to avoid calling
 * [func@Dex.thread_spawn] without any sort of queuing or bounding
 * on the permitted concurrency.
 *
 * Since: 1.2
 */

typedef enum
{
  DEX_THREAD_POOL_STATE_RUNNING,
  DEX_THREAD_POOL_STATE_DRAINING,
  DEX_THREAD_POOL_STATE_CANCEL_QUEUED,
  DEX_THREAD_POOL_STATE_CLOSED,
} DexThreadPoolState;

typedef struct
{
  char           *thread_name;
  DexThreadFunc   thread_func;
  gpointer        user_data;
  GDestroyNotify  user_data_destroy;
  DexPromise     *promise;
  guint           close_request : 1;
} DexThreadPoolItem;

struct _DexThreadPool
{
  DexObject            parent_instance;
  guint                n_threads;
  GThread            **threads;
  GAsyncQueue         *queue;
  DexPromise          *close_promise;
  GMutex               mutex;
  DexThreadPoolState   state;
  guint                joined : 1;
};

typedef struct _DexThreadPoolClass
{
  DexObjectClass parent_class;
} DexThreadPoolClass;

DEX_DEFINE_FINAL_TYPE (DexThreadPool, dex_thread_pool, DEX_TYPE_OBJECT)

#undef DEX_TYPE_THREAD_POOL
#define DEX_TYPE_THREAD_POOL dex_thread_pool_type

static void
dex_thread_pool_item_free (DexThreadPoolItem *item)
{
  if (item == NULL)
    return;

  if (item->user_data_destroy != NULL)
    item->user_data_destroy (item->user_data);

  dex_clear (&item->promise);
  g_clear_pointer (&item->thread_name, g_free);
  g_free (item);
}

static gpointer
dex_thread_pool_worker (gpointer data)
{
  GAsyncQueue *queue = data;

  for (;;)
    {
      DexThreadPoolItem *item;
      DexFuture *future;

      item = g_async_queue_pop (queue);
      if (item == NULL)
        break;

      if (item->close_request)
        {
          dex_thread_pool_item_free (item);
          break;
        }

      future = item->thread_func (item->user_data);

      if (future != NULL)
        {
          DexWaiter *waiter = dex_waiter_new (future);
          GError *error = NULL;
          const GValue *value;

          dex_waiter_wait (waiter);
          value = dex_future_get_value (DEX_FUTURE (waiter), &error);

          if (value != NULL)
            dex_promise_resolve (item->promise, value);
          else
            dex_promise_reject (item->promise, g_steal_pointer (&error));

          dex_unref (waiter);
          dex_unref (future);
        }
      else
        {
          dex_promise_resolve_boolean (item->promise, TRUE);
        }

      dex_thread_pool_item_free (item);
    }

  g_async_queue_unref (queue);

  return NULL;
}

static void
dex_thread_pool_finalize (DexObject *object)
{
  DexThreadPool *self = (DexThreadPool *)object;
  guint n_threads = self->threads != NULL ? self->n_threads : 0;

  g_mutex_lock (&self->mutex);
  if (self->state == DEX_THREAD_POOL_STATE_RUNNING)
    self->state = DEX_THREAD_POOL_STATE_DRAINING;
  g_mutex_unlock (&self->mutex);

  if (!self->joined && self->threads != NULL)
    {
      DexThreadPoolItem *item;

      for (guint i = 0; i < n_threads; i++)
        {
          if (self->threads[i] == NULL)
            continue;

          item = g_new0 (DexThreadPoolItem, 1);
          item->close_request = TRUE;
          g_async_queue_push (self->queue, item);
        }

      for (guint i = 0; i < n_threads; i++)
        {
          GThread *thread = self->threads[i];

          if (thread == NULL)
            continue;

          g_thread_join (thread);
          g_thread_unref (thread);
        }
    }

  g_clear_pointer (&self->close_promise, dex_unref);
  g_clear_pointer (&self->queue, g_async_queue_unref);
  g_clear_pointer (&self->threads, g_free);
  g_mutex_clear (&self->mutex);

  DEX_OBJECT_CLASS (dex_thread_pool_parent_class)->finalize (object);
}

static void
dex_thread_pool_class_init (DexThreadPoolClass *klass)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (klass);

  object_class->finalize = dex_thread_pool_finalize;
}

static void
dex_thread_pool_init (DexThreadPool *self)
{
  g_mutex_init (&self->mutex);
  self->queue = g_async_queue_new ();
}

/**
 * dex_thread_pool_new:
 * @n_threads: the number of threads to create
 *
 * Creates a fixed-size pool of reusable operating system threads.
 *
 * Returns: (transfer full): a new `DexThreadPool`
 *
 * Since: 1.2
 */
DexThreadPool *
dex_thread_pool_new (guint n_threads)
{
  DexThreadPool *self;
  guint n_started = 0;

  g_return_val_if_fail (n_threads > 0, NULL);

  self = (DexThreadPool *)dex_object_create_instance (dex_thread_pool_type);
  self->threads = g_new0 (GThread *, n_threads);
  self->n_threads = n_threads;
  self->state = DEX_THREAD_POOL_STATE_RUNNING;

  for (guint i = 0; i < n_threads; i++)
    {
      self->threads[i] = g_thread_try_new ("dex-thread-pool",
                                           dex_thread_pool_worker,
                                           g_async_queue_ref (self->queue),
                                           NULL);

      if (self->threads[i] == NULL)
        {
          self->n_threads = n_started;
          dex_unref (self);
          return NULL;
        }

      n_started++;
    }

  return self;
}

/**
 * dex_thread_pool_get_n_threads:
 * @pool: a `DexThreadPool`
 *
 * Gets the fixed number of threads owned by the pool.
 *
 * Returns: the number of threads in the pool
 *
 * Since: 1.2
 */
guint
dex_thread_pool_get_n_threads (DexThreadPool *pool)
{
  g_return_val_if_fail (DEX_IS_THREAD_POOL (pool), 0);

  return pool->n_threads;
}

/**
 * dex_thread_pool_submit:
 * @pool: a `DexThreadPool`
 * @thread_name: (nullable): the name to use for debugging the returned future
 * @thread_func: the function to run on a pooled thread
 * @user_data: closure data for @thread_func
 * @user_data_destroy: destroy notify for @user_data
 *
 * Queues blocking work to run on one of the pool's reusable threads.
 *
 * The provided @thread_name is applied to the returned future using
 * `dex_future_set_static_name()` so that tracing and debugging tools can
 * identify the work item. It does not rename the underlying OS worker thread.
 *
 * Returns: (transfer full): a future that resolves when the work completes
 *
 * Since: 1.2
 */
DexFuture *
dex_thread_pool_submit (DexThreadPool  *pool,
                        const char     *thread_name,
                        DexThreadFunc   thread_func,
                        gpointer        user_data,
                        GDestroyNotify  user_data_destroy)
{
  DexThreadPoolItem *item;
  DexPromise *promise;

  dex_return_error_if_fail (DEX_IS_THREAD_POOL (pool));
  dex_return_error_if_fail (thread_func != NULL);

  if (thread_name != NULL)
    thread_name = g_intern_string (thread_name);
  else
    thread_name = "[dex-thread-pool]";

  promise = dex_promise_new ();
  dex_future_set_static_name (DEX_FUTURE (promise), thread_name);

  item = g_new0 (DexThreadPoolItem, 1);
  item->thread_name = g_strdup (thread_name);
  item->thread_func = thread_func;
  item->user_data = user_data;
  item->user_data_destroy = user_data_destroy;
  item->promise = dex_ref (promise);

  g_mutex_lock (&pool->mutex);
  if (pool->state != DEX_THREAD_POOL_STATE_RUNNING)
    {
      g_mutex_unlock (&pool->mutex);
      dex_promise_reject (promise,
                          g_error_new_literal (DEX_ERROR,
                                               DEX_ERROR_SEMAPHORE_CLOSED,
                                               "Thread pool is closed"));
      dex_thread_pool_item_free (item);
      return DEX_FUTURE (promise);
    }

  g_async_queue_push (pool->queue, item);
  g_mutex_unlock (&pool->mutex);

  return DEX_FUTURE (promise);
}

static void
dex_thread_pool_cancel_queued (DexThreadPool *pool,
                               GPtrArray     *queued_items)
{
  DexThreadPoolItem *item;

  while ((item = g_async_queue_try_pop (pool->queue)) != NULL)
    {
      g_ptr_array_add (queued_items, item);
    }
}

static void
dex_thread_pool_push_close_requests (DexThreadPool *pool)
{
  for (guint i = 0; i < pool->n_threads; i++)
    {
      DexThreadPoolItem *item = g_new0 (DexThreadPoolItem, 1);

      item->close_request = TRUE;
      g_async_queue_push (pool->queue, item);
    }
}

typedef struct
{
  DexThreadPool *pool;
  DexPromise *promise;
} DexThreadPoolCloseState;

static gpointer
dex_thread_pool_close_thread (gpointer data)
{
  DexThreadPoolCloseState *state = data;

  dex_thread_pool_push_close_requests (state->pool);

  for (guint i = 0; i < state->pool->n_threads; i++)
    {
      GThread *thread = state->pool->threads[i];

      g_thread_join (thread);
      g_thread_unref (thread);
    }

  g_mutex_lock (&state->pool->mutex);
  state->pool->joined = TRUE;
  g_mutex_unlock (&state->pool->mutex);

  dex_promise_resolve_boolean (state->promise, TRUE);
  dex_clear (&state->pool->close_promise);
  dex_clear (&state->pool);
  dex_clear (&state->promise);
  g_free (state);

  return NULL;
}

/**
 * dex_thread_pool_close:
 * @pool: a `DexThreadPool`
 * @mode: shutdown policy for queued work
 *
 * Begins shutting down the pool and prevents new submissions.
 *
 * Returns: (transfer full): a future that resolves when shutdown completes
 */
DexFuture *
dex_thread_pool_close (DexThreadPool             *pool,
                       DexThreadPoolShutdownMode  mode)
{
  gboolean spawn_close_thread = FALSE;
  gboolean cancel_queued = FALSE;
  DexPromise *close_promise = NULL;
  GPtrArray *queued_items = NULL;

  dex_return_error_if_fail (DEX_IS_THREAD_POOL (pool));

  g_mutex_lock (&pool->mutex);
  if (pool->state == DEX_THREAD_POOL_STATE_RUNNING)
    {
      cancel_queued = (mode == DEX_THREAD_POOL_SHUTDOWN_CANCEL_QUEUED);
      pool->state = cancel_queued ?
                    DEX_THREAD_POOL_STATE_CANCEL_QUEUED :
                    DEX_THREAD_POOL_STATE_DRAINING;
      pool->close_promise = dex_promise_new ();
      spawn_close_thread = TRUE;

      if (cancel_queued)
        {
          queued_items = g_ptr_array_new ();
          dex_thread_pool_cancel_queued (pool, queued_items);
        }
    }

  if (pool->close_promise != NULL)
    close_promise = dex_ref (pool->close_promise);
  g_mutex_unlock (&pool->mutex);

  if (queued_items != NULL)
    {
      for (guint i = 0; i < queued_items->len; i++)
        {
          DexThreadPoolItem *item = g_ptr_array_index (queued_items, i);

          if (item->promise != NULL)
            dex_promise_reject (item->promise,
                                g_error_new_literal (DEX_ERROR,
                                                     DEX_ERROR_SEMAPHORE_CLOSED,
                                                     "Thread pool is closed"));
          dex_thread_pool_item_free (item);
        }

      g_ptr_array_free (queued_items, TRUE);
    }

  if (close_promise == NULL)
    {
      DexPromise *promise = dex_promise_new ();

      dex_promise_resolve_boolean (promise, TRUE);
      return DEX_FUTURE (promise);
    }

  if (spawn_close_thread)
    {
      DexThreadPoolCloseState *state = g_new0 (DexThreadPoolCloseState, 1);

      state->pool = dex_ref (pool);
      state->promise = dex_ref (close_promise);
      g_thread_unref (g_thread_new ("dex-thread-pool-close",
                                     dex_thread_pool_close_thread,
                                     state));
    }

  return DEX_FUTURE (close_promise);
}
