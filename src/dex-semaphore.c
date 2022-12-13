/*
 * dex-semaphore.c
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

#include <errno.h>
#include <unistd.h>

#ifdef HAVE_EVENTFD
# include <sys/eventfd.h>
#endif

#ifdef HAVE_LIBURING
# include <liburing.h>
#endif

#include <gio/gio.h>

#ifdef G_OS_UNIX
# include <glib-unix.h>
#endif

#include "dex-aio-private.h"
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
 * The DexSemaphore class works in it's ideal state by using an
 * eventfd() to notify other threads of work to be done using the
 * EFD_SEMPAHORE eventfd type.
 *
 * This is preferred to using a DexFuture waiter that is completed
 * when an item is posted because it uses less memory, doesn't require
 * queuing work on a thread, nor then waking up the specific waiter
 * thread using another eventfd from gwakeup.c. Additionally, all this
 * work can be completed in a single io_uring_submit() when running
 * on Linux.
 *
 * For the fallback case, we use a future that is enqueued on the waiter
 * thread, and when a post comes in, we attempt to complete those. If
 * the waiter enqueues and items are already available, they will be
 * completed immediately. Since the waiter thread may be blocked in
 * poll(), or racing to block in poll(), we must wake up the GMainContext
 * so that it will process the completed future. DexBlock does this
 * automatically for us when processing the finally() block. Handling
 * things in this way has the additional benefit of working on Windows
 * should we get around to running there (even if we should use IoRing
 * there too).
 */

#if !defined(HAVE_EVENT_FD)
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

GType dex_semaphore_waiter_get_type (void) G_GNUC_CONST;

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
#endif

struct _DexSemaphore
{
  DexObject parent_instance;
#ifdef HAVE_EVENTFD
  int eventfd;
#elif defined(G_OS_UNIX)
  gint64 counter;
  GQueue waiters;
#else
# error "DexSemaphore is not yet supported on your platform"
#endif
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
  return (DexSemaphore *)g_type_create_instance (DEX_TYPE_SEMAPHORE);
}

static void
dex_semaphore_finalize (DexObject *object)
{
  DexSemaphore *semaphore = (DexSemaphore *)object;

#ifdef HAVE_EVENTFD
  if (semaphore->eventfd != -1)
    {
      close (semaphore->eventfd);
      semaphore->eventfd = -1;
    }
#else
  while (semaphore->waiters.length > 0)
    {
      DexSemaphoreWaiter *waiter = g_queue_pop_head_link (&semaphore->waiters)->data;
      dex_future_complete (DEX_FUTURE (waiter),
                           NULL,
                           g_error_copy (&semaphore_closed_error));
      dex_unref (waiter);
    }
#endif

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
#ifdef HAVE_EVENTFD
  semaphore->eventfd = eventfd (0, EFD_SEMAPHORE);
#endif
}

void
dex_semaphore_post (DexSemaphore *semaphore)
{
  return dex_semaphore_post_many (semaphore, 1);
}

void
dex_semaphore_post_many (DexSemaphore *semaphore,
                         guint         count)
{
  g_return_if_fail (DEX_IS_SEMAPHORE (semaphore));

  if (count == 0)
    return;

#ifdef HAVE_EVENTFD
  {
    guint64 counter = count;

    /* Writes to eventfd are 64-bit integers and always atomic. Anything
     * other than sizeof(counter) indicates failure and we are not prepared
     * to handle that as it shouldn't happen. Just bail.
     */
    if (write (semaphore->eventfd, &counter, sizeof counter) != sizeof counter)
      {
        int errsv = errno;
        g_error ("Failed to post semaphore counter: %s",
                 g_strerror (errsv));
      }
  }
#else
  {
    GQueue queue = G_QUEUE_INIT;

    /* Post count and steal as many workers as we can complete
     * immediately which are waiting on a result.
     */
    dex_object_lock (semaphore);
    semaphore->counter += count;
    while (semaphore->counter > 0 &&
           semaphore->waiters.length > 0)
      g_queue_push_tail_link (&queue, g_queue_pop_tail_link (&semaphore->waiters));
    semaphore->counter -= queue.length;
    dex_object_unlock (semaphore);

    /* Now complete the waiters outside our object lock */
    while (queue.length > 0)
      {
        DexSemaphoreWaiter *waiter = g_queue_pop_head_link (&queue)->data;
        dex_future_complete (DEX_FUTURE (waiter), &semaphore_waiter_value, NULL);
        dex_unref (waiter);
      }
  }
#endif
}

#ifndef HAVE_EVENTFD
static DexFuture *
dex_semaphore_wait_on_scheduler (DexFuture *future,
                                 gpointer   user_data)
{
  return NULL;
}
#endif

DexFuture *
dex_semaphore_wait (DexSemaphore *semaphore)
{
  g_return_val_if_fail (DEX_IS_SEMAPHORE (semaphore), NULL);

#ifdef HAVE_EVENTFD
  {
    static gint64 trash_value;
    if G_UNLIKELY (semaphore->eventfd < 0)
      return DEX_FUTURE (dex_future_new_reject (G_IO_ERROR,
                                                G_IO_ERROR_CLOSED,
                                                "The semaphore has already been closed"));
    else
      return dex_aio_read (NULL,
                           semaphore->eventfd,
                           &trash_value,
                           sizeof trash_value,
                           -1);
  }
#else
  {
    DexSemaphoreWaiter *waiter;
    DexFuture *ret = NULL;

    waiter = (DexSemaphoreWaiter *)
      g_type_create_instance (DEX_TYPE_SEMAPHORE_WAITER);

    dex_object_lock (semaphore);
    if (semaphore->counter > 0)
      {
        semaphore->counter--;
        dex_future_complete (DEX_FUTURE (waiter), &semaphore_waiter_value, NULL);
        ret = DEX_FUTURE (g_steal_pointer (&waiter));
      }
    else
      {
        DexScheduler *scheduler = dex_scheduler_ref_thread_default ();
        DexFuture *block;

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
#endif
}
