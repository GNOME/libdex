/*
 * dex-thread-pool-scheduler.c
 *
 * Copyright 2022 Christian Hergert <chergert@gnome.org>
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

#include "dex-scheduler-private.h"
#include "dex-thread-pool-scheduler.h"
#include "dex-thread-pool-worker-private.h"
#include "dex-thread-storage-private.h"
#include "dex-work-queue-private.h"

struct _DexThreadPoolScheduler
{
  DexScheduler            parent_instance;
  DexWorkQueue           *global_work_queue;
  DexThreadPoolWorkerSet *set;
  GPtrArray              *workers;
};

typedef struct _DexThreadPoolSchedulerClass
{
  DexSchedulerClass parent_class;
} DexThreadPoolSchedulerClass;

DEX_DEFINE_FINAL_TYPE (DexThreadPoolScheduler, dex_thread_pool_scheduler, DEX_TYPE_SCHEDULER)

static void
dex_thread_pool_scheduler_push (DexScheduler *scheduler,
                                DexWorkItem   work_item)
{
  DexThreadPoolScheduler *thread_pool_scheduler = DEX_THREAD_POOL_SCHEDULER (scheduler);
  DexThreadPoolWorker *worker = DEX_THREAD_POOL_WORKER_CURRENT;

  if (worker != NULL)
    DEX_SCHEDULER_GET_CLASS (worker)->push (DEX_SCHEDULER (worker), work_item);
  else
    dex_work_queue_push (thread_pool_scheduler->global_work_queue, work_item);
}

static GMainContext *
dex_thread_pool_scheduler_get_main_context (DexScheduler *scheduler)
{
  DexThreadPoolWorker *worker = DEX_THREAD_POOL_WORKER_CURRENT;

  /* Give the worker's main context if we're on a pooled thread */
  if (worker != NULL)
    return dex_scheduler_get_main_context (DEX_SCHEDULER (worker));

  /* Otherwise give the application default (main thread) context */
  return dex_scheduler_get_main_context (dex_scheduler_get_default ());
}

static DexAioContext *
dex_thread_pool_scheduler_get_aio_context (DexScheduler *scheduler)
{
  DexThreadPoolWorker *worker = DEX_THREAD_POOL_WORKER_CURRENT;

  /* Give the worker's aio context if we're on a pooled thread */
  if (worker != NULL)
    return dex_scheduler_get_aio_context (DEX_SCHEDULER (worker));

  /* Otherwise give the application default (main thread) aio context */
  return dex_scheduler_get_aio_context (dex_scheduler_get_default ());
}

static void
dex_thread_pool_scheduler_finalize (DexObject *object)
{
  DexThreadPoolScheduler *thread_pool_scheduler = (DexThreadPoolScheduler *)object;

  dex_clear (&thread_pool_scheduler->global_work_queue);

  g_clear_pointer (&thread_pool_scheduler->set, dex_thread_pool_worker_set_unref);
  g_clear_pointer (&thread_pool_scheduler->workers, g_ptr_array_unref);

  DEX_OBJECT_CLASS (dex_thread_pool_scheduler_parent_class)->finalize (object);
}

static void
dex_thread_pool_scheduler_class_init (DexThreadPoolSchedulerClass *thread_pool_scheduler_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (thread_pool_scheduler_class);
  DexSchedulerClass *scheduler_class = DEX_SCHEDULER_CLASS (thread_pool_scheduler_class);

  object_class->finalize = dex_thread_pool_scheduler_finalize;

  scheduler_class->get_main_context = dex_thread_pool_scheduler_get_main_context;
  scheduler_class->get_aio_context = dex_thread_pool_scheduler_get_aio_context;
  scheduler_class->push = dex_thread_pool_scheduler_push;
}

static void
dex_thread_pool_scheduler_init (DexThreadPoolScheduler *thread_pool_scheduler)
{
  thread_pool_scheduler->global_work_queue = dex_work_queue_new ();
  thread_pool_scheduler->set = dex_thread_pool_worker_set_new ();
  thread_pool_scheduler->workers = g_ptr_array_new_with_free_func (dex_unref);
}

/**
 * dex_thread_pool_scheduler_new:
 *
 * Creates a new #DexScheduler that executes work items on a thread pool.
 *
 * Returns: (transfer full): a #DexThreadPoolScheduler
 */
DexThreadPoolScheduler *
dex_thread_pool_scheduler_new (void)
{
  DexThreadPoolScheduler *thread_pool_scheduler;
  guint n_procs;

  thread_pool_scheduler = (DexThreadPoolScheduler *)g_type_create_instance (DEX_TYPE_THREAD_POOL_SCHEDULER);

  /* TODO: let this be dynamic and tunable, as well as thread pinning */
  n_procs = g_get_num_processors ();

  for (guint i = 0; i < n_procs; i++)
    {
      DexThreadPoolWorker *thread_pool_worker;

      thread_pool_worker = dex_thread_pool_worker_new (thread_pool_scheduler->global_work_queue,
                                                       thread_pool_scheduler->set);
      g_ptr_array_add (thread_pool_scheduler->workers, g_steal_pointer (&thread_pool_worker));
    }

  return thread_pool_scheduler;
}
