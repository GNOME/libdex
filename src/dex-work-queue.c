/*
 * dex-work-queue.c
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

#include "dex-semaphore-private.h"
#include "dex-work-queue-private.h"

struct _DexWorkQueue
{
  DexSemaphore *semaphore;
  GMutex mutex;
  GQueue queue;
};

typedef struct _DexWorkQueueItem
{
  GList link;
  DexWorkItem work_item;
} DexWorkQueueItem;

DexWorkQueue *
dex_work_queue_new (void)
{
  DexWorkQueue *work_queue;

  work_queue = g_atomic_rc_box_new0 (DexWorkQueue);
  work_queue->semaphore = dex_semaphore_new ();
  g_mutex_init (&work_queue->mutex);

  return work_queue;
}

DexWorkQueue *
dex_work_queue_ref (DexWorkQueue *work_queue)
{
  g_atomic_rc_box_acquire (work_queue);
  return work_queue;
}

static void
dex_work_queue_finalize (gpointer data)
{
  DexWorkQueue *work_queue = data;

  if (work_queue->queue.length > 0)
    g_critical ("Work queue %p freed with %u items still in it!",
                work_queue, work_queue->queue.length);

  g_mutex_clear (&work_queue->mutex);
  g_clear_pointer (&work_queue->semaphore, dex_semaphore_unref);
}

void
dex_work_queue_unref (DexWorkQueue *work_queue)
{
  g_atomic_rc_box_release_full (work_queue, dex_work_queue_finalize);
}

void
dex_work_queue_push (DexWorkQueue *work_queue,
                     DexWorkItem   work_item)
{
  DexWorkQueueItem *work_queue_item;

  work_queue_item = g_new0 (DexWorkQueueItem, 1);
  work_queue_item->link.data = work_queue_item;
  work_queue_item->work_item = work_item;

  g_mutex_lock (&work_queue->mutex);
  g_queue_push_tail_link (&work_queue->queue, &work_queue_item->link);
  g_mutex_unlock (&work_queue->mutex);

  dex_semaphore_post (work_queue->semaphore);
}

gboolean
dex_work_queue_pop (DexWorkQueue *work_queue,
                    DexWorkItem  *out_work_item)
{
  GList *link;

  g_return_val_if_fail (work_queue != NULL, FALSE);
  g_return_val_if_fail (out_work_item != NULL, FALSE);

  g_mutex_lock (&work_queue->mutex);
  link = g_queue_pop_head_link (&work_queue->queue);
  g_mutex_unlock (&work_queue->mutex);

  if (link != NULL)
    {
      DexWorkQueueItem *item = link->data;
      *out_work_item = item->work_item;
      g_free (item);
    }

  return link != NULL;
}

static gboolean
dex_work_queue_pop_and_invoke_item_source_func (gpointer data)
{
  DexWorkQueue *work_queue = data;
  DexWorkItem work_item;

  g_assert (work_queue != NULL);

  if (dex_work_queue_pop (work_queue, &work_item))
    dex_work_item_invoke (&work_item);

  return G_SOURCE_CONTINUE;
}

guint
dex_work_queue_attach (DexWorkQueue *work_queue,
                       DexScheduler *scheduler)
{
  GMainContext *main_context;
  GSource *source;
  guint source_id;

  g_return_val_if_fail (work_queue != NULL, 0);
  g_return_val_if_fail (work_queue->semaphore != NULL, 0);
  g_return_val_if_fail (DEX_IS_SCHEDULER (scheduler), 0);

  source = dex_semaphore_source_new (G_PRIORITY_DEFAULT,
                                     work_queue->semaphore,
                                     dex_work_queue_pop_and_invoke_item_source_func,
                                     dex_work_queue_ref (work_queue),
                                     (GDestroyNotify)dex_work_queue_unref);
  main_context = dex_scheduler_get_main_context (scheduler);
  source_id = g_source_attach (source, main_context);
  g_source_unref (source);

  return source_id;
}
