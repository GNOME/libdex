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

#include "dex-work-queue-private.h"

struct _DexWorkQueue
{
  GMutex  mutex;
  GQueue  queue;
  GPollFD pollfd;
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

  work_queue = g_new0 (DexWorkQueue, 1);
  g_mutex_init (&work_queue->mutex);

  return work_queue;
}

void
dex_work_queue_free (DexWorkQueue *work_queue)
{
  g_mutex_clear (&work_queue->mutex);
  g_free (work_queue);
}

void
dex_work_queue_get_pollfd (DexWorkQueue *work_queue,
                           GPollFD      *pollfd)
{
  g_return_if_fail (work_queue != NULL);
  g_return_if_fail (pollfd != NULL);

  /* TODO: actually figure out how we want to use eventfd and/or gwakeup for this */

  *pollfd = work_queue->pollfd;
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
  g_queue_push_head_link (&work_queue->queue, &work_queue_item->link);
  g_mutex_unlock (&work_queue->mutex);

  /* TODO: Signal via gwakeup/eventfd */
}

gboolean
dex_work_queue_pop (DexWorkQueue *work_queue,
                    DexWorkItem  *out_work_item)
{
  DexWorkQueueItem *item;
  gboolean ret;

  g_return_val_if_fail (work_queue != NULL, FALSE);
  g_return_val_if_fail (out_work_item != NULL, FALSE);

  g_mutex_lock (&work_queue->mutex);
  if ((item = g_queue_peek_tail (&work_queue->queue)))
    {
      *out_work_item = item->work_item;
      g_queue_unlink (&work_queue->queue, &item->link);
    }
  g_mutex_unlock (&work_queue->mutex);

  if ((ret = item != NULL))
    g_free (item);

  return ret;
}
