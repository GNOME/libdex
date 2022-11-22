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
#include "dex-thread-pool-scheduler-private.h"
#include "dex-thread-pool-worker-private.h"
#include "dex-thread-storage-private.h"

struct _DexThreadPoolScheduler
{
  DexScheduler parent_instance;
  GQueue global_queue;
  GMutex mutex;
};

typedef struct _DexThreadPoolSchedulerClass
{
  DexSchedulerClass parent_class;
} DexThreadPoolSchedulerClass;

typedef struct _QueuedWorkItem
{
  GList link;
  DexWorkItem work_item;
} QueuedWorkItem;

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
    dex_thread_pool_scheduler_push_global (thread_pool_scheduler, work_item);
}

void
dex_thread_pool_scheduler_push_global (DexThreadPoolScheduler *thread_pool_scheduler,
                                       DexWorkItem             work_item)
{
  QueuedWorkItem *qwi;

  g_return_if_fail (DEX_IS_THREAD_POOL_SCHEDULER (thread_pool_scheduler));

  qwi = g_new0 (QueuedWorkItem, 1);
  qwi->work_item = work_item;

  g_mutex_lock (&thread_pool_scheduler->mutex);
  g_queue_push_tail_link (&thread_pool_scheduler->global_queue, &qwi->link);
  g_mutex_unlock (&thread_pool_scheduler->mutex);
}

/**
 * dex_thread_pool_scheduler_pop_global: (skip)
 * @thread_pool_scheduler: a #DexThreadPoolScheduler
 * @work_item: (out): a location for a #DexWorkItem
 *
 * Pops an item off the global work queue.
 *
 * This can be used to take an item from the work queue. This is generally
 * only useful to situations where you'd want to cooperate in processing
 * work, such as from #DexThreadPoolWorker.
 *
 * Returns:  %TRUE if an item was removed and placed in @work_item; otherwise
 *   %FALSE is returned.
 */
gboolean
dex_thread_pool_scheduler_pop_global (DexThreadPoolScheduler *thread_pool_scheduler,
                                      DexWorkItem            *work_item)
{
  GList *link;

  g_return_val_if_fail (DEX_IS_THREAD_POOL_SCHEDULER (thread_pool_scheduler), FALSE);
  g_return_val_if_fail (work_item != NULL, FALSE);

  g_mutex_lock (&thread_pool_scheduler->mutex);
  link = g_queue_pop_head_link (&thread_pool_scheduler->global_queue);
  g_mutex_unlock (&thread_pool_scheduler->mutex);

  if (link != NULL)
    {
      QueuedWorkItem *qwi = link->data;
      *work_item = qwi->work_item;
      g_free (qwi);
      return TRUE;
    }

  return FALSE;
}

static GMainContext *
dex_thread_pool_scheduler_get_main_context (DexScheduler *scheduler)
{
  DexThreadPoolScheduler *thread_pool_scheduler = DEX_THREAD_POOL_SCHEDULER (scheduler);
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
  DexThreadPoolScheduler *thread_pool_scheduler = DEX_THREAD_POOL_SCHEDULER (scheduler);
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
  g_mutex_init (&thread_pool_scheduler->mutex);
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

  thread_pool_scheduler = (DexThreadPoolScheduler *)g_type_create_instance (DEX_TYPE_THREAD_POOL_SCHEDULER);

  return thread_pool_scheduler;
}
