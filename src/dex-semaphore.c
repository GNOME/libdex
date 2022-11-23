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
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <liburing.h>
#include <unistd.h>

#include "dex-aio.h"
#include "dex-object-private.h"
#include "dex-semaphore-private.h"

struct _DexSemaphore
{
  DexObject parent_instance;
  int eventfd;
};

typedef struct _DexSemaphoreClass
{
  DexObjectClass parent_class;
} DexSemaphoreClass;

DEX_DEFINE_FINAL_TYPE (DexSemaphore, dex_semaphore, DEX_TYPE_OBJECT)

DexSemaphore *
dex_semaphore_new (void)
{
  return (DexSemaphore *)g_type_create_instance (DEX_TYPE_SEMAPHORE);
}

static void
dex_semaphore_finalize (DexObject *object)
{
  DexSemaphore *semaphore = (DexSemaphore *)object;

  if (semaphore->eventfd != -1)
    {
      close (semaphore->eventfd);
      semaphore->eventfd = -1;
    }

  DEX_OBJECT_CLASS (dex_semaphore_parent_class)->finalize (object);
}

static void
dex_semaphore_class_init (DexSemaphoreClass *semaphore_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (semaphore_class);

  object_class->finalize = dex_semaphore_finalize;
}

static void
dex_semaphore_init (DexSemaphore *semaphore)
{
  semaphore->eventfd = eventfd (0, EFD_SEMAPHORE);
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

typedef struct _DexSemaphoreSource
{
  GSource          source;
  DexSemaphore    *semaphore;
  struct io_uring  ring;
  gint64           counter;
  int              eventfd;
  gpointer         eventfdtag;
} DexSemaphoreSource;

static gboolean
dex_semaphore_source_dispatch (GSource     *source,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
  DexSemaphoreSource *semaphore_source = (DexSemaphoreSource *)source;
  struct io_uring_cqe *cqe;
  struct io_uring_sqe *sqe;
  gint64 counter;
  gssize n_read;

  g_assert (semaphore_source != NULL);
  g_assert (semaphore_source->semaphore != NULL);
  g_assert (semaphore_source->semaphore->eventfd > -1);

  n_read = read (semaphore_source->eventfd, &counter, sizeof counter);

  while (io_uring_peek_cqe (&semaphore_source->ring, &cqe))
    io_uring_cqe_seen (&semaphore_source->ring, cqe);

  if (n_read == sizeof counter)
    callback (user_data);

	sqe = io_uring_get_sqe (&semaphore_source->ring);
	io_uring_prep_read (sqe, semaphore_source->semaphore->eventfd, &semaphore_source->counter, sizeof semaphore_source->counter, -1);
  io_uring_submit (&semaphore_source->ring);

  return G_SOURCE_CONTINUE;
}

static void
dex_semaphore_source_finalize (GSource *source)
{
  DexSemaphoreSource *semaphore_source = (DexSemaphoreSource *)source;

  dex_clear (&semaphore_source->semaphore);

  if (semaphore_source->eventfd != -1)
    close (semaphore_source->eventfd);

  io_uring_queue_exit (&semaphore_source->ring);
}

static GSourceFuncs dex_semaphore_source_funcs = {
  .dispatch = dex_semaphore_source_dispatch,
  .finalize = dex_semaphore_source_finalize,
};

GSource *
dex_semaphore_source_new (int             priority,
                          DexSemaphore   *semaphore,
                          GSourceFunc     callback,
                          gpointer        callback_data,
                          GDestroyNotify  callback_data_destroy)
{
  DexSemaphoreSource *semaphore_source;
  struct io_uring_sqe *sqe;
  GSource *source;

  g_return_val_if_fail (semaphore != NULL, NULL);
  g_return_val_if_fail (semaphore->eventfd > -1, NULL);
  g_return_val_if_fail (callback != NULL || callback_data == NULL, NULL);
  g_return_val_if_fail (callback != NULL || callback_data_destroy == NULL, NULL);

  source = g_source_new (&dex_semaphore_source_funcs, sizeof (DexSemaphoreSource));
  semaphore_source = (DexSemaphoreSource *)source;

  if (callback != NULL)
    g_source_set_callback (source, callback, callback_data, callback_data_destroy);
  g_source_set_priority (source, priority);
  g_source_set_static_name (source, "[dex-semaphore-source]");

  semaphore_source->semaphore = dex_ref (semaphore);
  semaphore_source->eventfd = eventfd (0, EFD_CLOEXEC);
  semaphore_source->eventfdtag = g_source_add_unix_fd (source, semaphore_source->eventfd, G_IO_IN);

  io_uring_queue_init (8, &semaphore_source->ring, 0);
  io_uring_register_eventfd (&semaphore_source->ring, semaphore_source->eventfd);

	sqe = io_uring_get_sqe (&semaphore_source->ring);
	io_uring_prep_read (sqe, semaphore->eventfd, &semaphore_source->counter, sizeof semaphore_source->counter, -1);
  io_uring_submit (&semaphore_source->ring);

  return source;
}
