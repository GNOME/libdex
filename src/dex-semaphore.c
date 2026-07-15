/*
 * dex-semaphore.c
 *
 * Copyright 2022 Christian Hergert <christian@sourceandstack.com>
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

#include <gio/gio.h>

#include "dex-block-private.h"
#include "dex-error.h"
#include "dex-future-private.h"
#include "dex-object-private.h"
#include "dex-promise.h"
#include "dex-semaphore-private.h"
#include "dex-thread-storage-private.h"

/*
 * NOTES:
 *
 * This is a userspace-only semaphore implementation. When a post occurs,
 * we complete waiting futures directly when possible. If a waiter has not yet
 * been registered we queue it and complete it on the next post.
 *
 * We use a DexFuture waiter that is completed when an item is posted.
 *
 * In the past, we used io_uring/eventfd on Linux but this turns out to be
 * faster simply because we do not enter the kernel. In synthetic benchmarks
 * we spent about 2/3 the time in kernel workers that was not necessary.
 * Expecially since we need this implementation on non-Linux anyway.
 */

#define DEX_TYPE_SEMAPHORE_WAITER dex_semaphore_waiter_type

typedef struct _DexSemaphoreWaiter
{
  DexFuture parent_instance;
  GList link;
} DexSemaphoreWaiter;

typedef struct _DexSemaphoreWaiterClass
{
  DexFutureClass parent_class;
} DexSemaphoreWaiterClass;

static GType dex_semaphore_waiter_get_type (void);

DEX_DEFINE_FINAL_TYPE (DexSemaphoreWaiter, dex_semaphore_waiter, DEX_TYPE_FUTURE)

static GError semaphore_closed_error;
static GValue semaphore_waiter_value;

static void
dex_semaphore_waiter_class_init (DexSemaphoreWaiterClass *semaphore_waiter_class)
{
  g_value_init (&semaphore_waiter_value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&semaphore_waiter_value, TRUE);
  semaphore_closed_error = (GError) {
    .domain = DEX_ERROR,
    .code = DEX_ERROR_SEMAPHORE_CLOSED,
    .message = (char *)"Semaphore is closed",
  };
}

static void
dex_semaphore_waiter_init (DexSemaphoreWaiter *semaphore_waiter)
{
  semaphore_waiter->link.data = semaphore_waiter;
}

struct _DexSemaphore
{
  DexObject parent_instance;

  gint64 counter;
  GQueue waiters;
};

typedef struct _DexSemaphoreClass
{
  DexObjectClass parent_class;
} DexSemaphoreClass;

DEX_DEFINE_FINAL_TYPE (DexSemaphore, dex_semaphore, DEX_TYPE_OBJECT)

#undef DEX_TYPE_SEMAPHORE
#define DEX_TYPE_SEMAPHORE dex_semaphore_type

DexSemaphore *
dex_semaphore_new (void)
{
  return (DexSemaphore *)dex_object_create_instance (DEX_TYPE_SEMAPHORE);
}

static void
dex_semaphore_finalize (DexObject *object)
{
  DexSemaphore *semaphore = (DexSemaphore *)object;

  while (semaphore->waiters.length > 0)
    {
      DexSemaphoreWaiter *waiter = g_queue_pop_head_link (&semaphore->waiters)->data;
      dex_future_complete (DEX_FUTURE (waiter),
                           NULL,
                           g_error_copy (&semaphore_closed_error));
      dex_unref (waiter);
    }
  DEX_OBJECT_CLASS (dex_semaphore_parent_class)->finalize (object);
}

static void
dex_semaphore_class_init (DexSemaphoreClass *semaphore_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (semaphore_class);

  object_class->finalize = dex_semaphore_finalize;

  g_type_ensure (dex_semaphore_waiter_get_type ());
}

static void
dex_semaphore_init (DexSemaphore *semaphore)
{
}

void
dex_semaphore_post (DexSemaphore *semaphore)
{
  dex_semaphore_post_many (semaphore, 1);
}

void
dex_semaphore_post_many (DexSemaphore *semaphore,
                         guint         count)
{
  GQueue queue;

  g_return_if_fail (DEX_IS_SEMAPHORE (semaphore));

  if (count == 0)
    return;

  queue = (GQueue) G_QUEUE_INIT;

  /* Post count and steal as many workers as we can complete
   * immediately which are waiting on a result.
   */
  dex_object_lock (semaphore);
  semaphore->counter += count;
  while (semaphore->counter > 0 && semaphore->waiters.length > 0)
    {
      g_queue_push_tail_link (&queue, g_queue_pop_head_link (&semaphore->waiters));
      semaphore->counter--;
    }
  dex_object_unlock (semaphore);

  /* Now complete the waiters outside our object lock */
  while (queue.length > 0)
    {
      DexSemaphoreWaiter *waiter = g_queue_pop_head_link (&queue)->data;
      dex_future_complete (DEX_FUTURE (waiter), &semaphore_waiter_value, NULL);
      dex_unref (waiter);
    }
}

static DexFuture *
dex_semaphore_wait_on_scheduler (DexFuture *future,
                                 gpointer   user_data)
{
  return dex_ref (future);
}

DexFuture *
dex_semaphore_wait (DexSemaphore *semaphore)
{
  DexFuture *ret = NULL;
  DexSemaphoreWaiter *waiter;
  DexScheduler *scheduler;
  DexFuture *block;

  g_return_val_if_fail (DEX_IS_SEMAPHORE (semaphore), NULL);

  waiter = (DexSemaphoreWaiter *)
    dex_object_create_instance (DEX_TYPE_SEMAPHORE_WAITER);

  dex_object_lock (semaphore);
  if (semaphore->counter > 0)
    {
      semaphore->counter--;
      dex_future_complete (DEX_FUTURE (waiter), &semaphore_waiter_value, NULL);
      ret = DEX_FUTURE (g_steal_pointer (&waiter));
    }
  else
    {
      scheduler = dex_scheduler_ref_thread_default ();

      g_assert (scheduler != NULL);
      g_assert (DEX_IS_SCHEDULER (scheduler));

      block = dex_block_new (dex_ref (waiter),
                             scheduler,
                             DEX_BLOCK_KIND_FINALLY,
                             dex_semaphore_wait_on_scheduler, NULL, NULL);
      g_queue_push_tail_link (&semaphore->waiters, &waiter->link);
      ret = DEX_FUTURE (g_steal_pointer (&block));

      dex_unref (scheduler);
    }
  dex_object_unlock (semaphore);

  g_assert (ret != NULL);
  g_assert (DEX_IS_FUTURE (ret));

  return ret;
}

void
dex_semaphore_close (DexSemaphore *semaphore)
{
  g_return_if_fail (DEX_IS_SEMAPHORE (semaphore));

  dex_object_lock (semaphore);

  if (semaphore->waiters.length > 0)
    {
      GQueue queue = semaphore->waiters;
      semaphore->waiters = (GQueue) {NULL, NULL, 0};

      while (queue.length)
        {
          DexSemaphoreWaiter *waiter = g_queue_pop_head_link (&queue)->data;
          dex_future_complete (DEX_FUTURE (waiter),
                               NULL,
                               g_error_copy (&semaphore_closed_error));
          dex_unref (waiter);
        }
    }

  dex_object_unlock (semaphore);
}
