/*
 * dex-thread-pool-worker.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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

#include "dex-thread-pool-worker-private.h"
#include "dex-work-stealing-queue-private.h"

typedef enum _DexThreadPoolWorkerStatus
{
  DEX_THREAD_POOL_WORKER_INITIAL,
  DEX_THREAD_POOL_WORKER_RUNNING,
  DEX_THREAD_POOL_WORKER_STOPPING,
  DEX_THREAD_POOL_WORKER_FINISHED,
} DexThreadPoolWorkerStatus;

struct _DexThreadPoolWorker
{
  DexObject             parent_instance;
  GThread              *thread;
  GMainContext         *main_context;
  DexWorkStealingQueue  queue;
  guint                 status : 2;
};

typedef struct _DexThreadPoolWorkerClass
{
  DexObjectClass parent_class;
} DexThreadPoolWorkerClass;

DEX_DEFINE_FINAL_TYPE (DexThreadPoolWorker, dex_thread_pool_worker, DEX_TYPE_OBJECT)

static void
dex_thread_pool_worker_finalize_scheduler_func (gpointer data)
{
  DexThreadPoolWorker *thread_pool_worker = DEX_THREAD_POOL_WORKER (data);
  thread_pool_worker->status = DEX_THREAD_POOL_WORKER_STOPPING;
}

static void
dex_thread_pool_worker_finalize (DexObject *object)
{
  DexThreadPoolWorker *thread_pool_worker = DEX_THREAD_POOL_WORKER (object);

  /* TODO: WE CAN'T do this with work stealing queue since it can only
   * be written to by the worker thread itself.
   *
   * To finalize the worker, we need to push a work item to the thread
   * that will cause it to stop processing and then wait for the thread
   * to exit after processing the rest of the queue. We change the finalizing
   * bit on the worker thread so the common case (it's FALSE) doesn't require
   * any sort of synchronization/atomic operations.
   */
#if 0
  dex_thread_pool_worker_push (thread_pool_worker,
                               dex_thread_pool_worker_finalize_scheduler_func,
                               thread_pool_worker);
#endif
  g_thread_join (thread_pool_worker->thread);

  g_assert (thread_pool_worker->status == DEX_THREAD_POOL_WORKER_FINISHED);

  g_clear_pointer (&thread_pool_worker->thread, g_thread_unref);
  g_clear_pointer (&thread_pool_worker->main_context, g_main_context_unref);
  dex_work_stealing_queue_clear (&thread_pool_worker->queue);

  DEX_OBJECT_CLASS (dex_thread_pool_worker_parent_class)->finalize (object);
}

static void
dex_thread_pool_worker_class_init (DexThreadPoolWorkerClass *thread_pool_worker_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (thread_pool_worker_class);

  object_class->finalize = dex_thread_pool_worker_finalize;
}

static void
dex_thread_pool_worker_init (DexThreadPoolWorker *thread_pool_worker)
{
#if GLIB_SIZEOF_VOID_P == 8
  dex_work_stealing_queue_init (&thread_pool_worker->queue, 255);
#elif GLIB_SIZEOF_VOID_P == 4
  dex_work_stealing_queue_init (&thread_pool_worker->queue, 1020);
#else
# error "Unsupported value for GLIB_SIZEOF_VOID_P"
#endif
}

static gpointer
dex_thread_pool_worker_thread_func (gpointer data)
{
  DexThreadPoolWorker *thread_pool_worker = DEX_THREAD_POOL_WORKER (data);

  thread_pool_worker->status = DEX_THREAD_POOL_WORKER_RUNNING;

  g_main_context_push_thread_default (thread_pool_worker->main_context);
  while (thread_pool_worker->status != DEX_THREAD_POOL_WORKER_STOPPING)
    g_main_context_iteration (thread_pool_worker->main_context, TRUE);
  g_main_context_pop_thread_default (thread_pool_worker->main_context);

  thread_pool_worker->status = DEX_THREAD_POOL_WORKER_FINISHED;

  return NULL;
}

typedef struct _DexThreadPoolWorkerSource
{
  GSource source;
  DexThreadPoolWorker *thread_pool_worker;
} DexThreadPoolWorkerSource;

static gboolean
dex_thread_pool_worker_source_check (GSource *source)
{
  DexThreadPoolWorkerSource *thread_pool_worker_source = (DexThreadPoolWorkerSource *)source;
  DexThreadPoolWorker *thread_pool_worker = thread_pool_worker_source->thread_pool_worker;

  /* TODO: This will cause us to spin currently, because we need a way to block
   * on the global queue from a pollfd so that we can both wait for timers
   * and wait for incoming work items if we've exhausted ours/neighbors.
   */

  return TRUE;
}

static gboolean
dex_thread_pool_worker_source_dispatch (GSource     *source,
                                        GSourceFunc  callback,
                                        gpointer     callback_data)
{
  DexThreadPoolWorkerSource *thread_pool_worker_source = (DexThreadPoolWorkerSource *)source;
  DexThreadPoolWorker *thread_pool_worker = thread_pool_worker_source->thread_pool_worker;

  for (guint i = 0; i < DEX_THREAD_POOL_WORKER_BATCH_SIZE; i++)
    {
      DexWorkItem work_item;

      if (!dex_work_stealing_queue_pop (&thread_pool_worker->queue, &work_item))
        break;

      work_item.func (work_item.func_data);
    }

  /* TODO: Iterate across neighbors and attempt to steal work items */
  /* TODO: Fallback to global queue if no items where processed */

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs dex_thread_pool_worker_source_funcs = {
  .check = dex_thread_pool_worker_source_check,
  .dispatch = dex_thread_pool_worker_source_dispatch,
};

static GSource *
dex_thread_pool_worker_source_new (DexThreadPoolWorker *thread_pool_worker)
{
  GSource *source;

  source = g_source_new (&dex_thread_pool_worker_source_funcs, sizeof (DexThreadPoolWorkerSource));
  g_source_set_static_name (source, "[dex-thread-pool-worker-source]");
  g_source_set_priority (source, DEX_THREAD_POOL_WORKER_PRIORITY);

  ((DexThreadPoolWorkerSource *)source)->thread_pool_worker = thread_pool_worker;

  return source;
}

DexThreadPoolWorker *
dex_thread_pool_worker_new (void)
{
  DexThreadPoolWorker *thread_pool_worker;
  GSource *source;

  thread_pool_worker = (DexThreadPoolWorker *)g_type_create_instance (DEX_TYPE_THREAD_POOL_WORKER);
  thread_pool_worker->main_context = g_main_context_new ();

  /* Create a GSource which will dispatch work items as they come in.
   * We do this via a GMainContext rather than just some code directly
   * against a Queue so that we can integrate other GSource on the same
   * thread without ping-pong'ing between threads for some types of
   * DexFuture.
   */
  source = dex_thread_pool_worker_source_new (thread_pool_worker);
  g_source_attach (source, thread_pool_worker->main_context);
  g_source_unref (source);

  /* Now spawn our thread to process events via GSource */
  thread_pool_worker->thread = g_thread_new ("dex-thread-pool-worker",
                                             dex_thread_pool_worker_thread_func,
                                             thread_pool_worker);

  return thread_pool_worker;
}

void
dex_thread_pool_worker_push (DexThreadPoolWorker *thread_pool_worker,
                             DexSchedulerFunc     func,
                             gpointer             func_data)
{
  g_assert (DEX_IS_THREAD_POOL_WORKER (thread_pool_worker));
  g_assert (g_thread_self () == thread_pool_worker->thread);

  /* We _MUST_ be running on @thread_pool_worker->thread to use this API
   * which means we don't need to worry about waking up our GMainContext,
   * it will wake up automatically to process additional work items.
   */
  dex_work_stealing_queue_push (&thread_pool_worker->queue,
                                (DexWorkItem) {func, func_data});
}
