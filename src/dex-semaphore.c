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
#include <unistd.h>

#include "dex-semaphore-private.h"

/* TODO: This is just getting the mechanics in place, particularly for Linux.
 *       It will still have thundering herd because we're using both poll()
 *       and not Edge-Triggered interrupts yet.
 *
 *       To make ET work with GMainContext, we'll probably have to create an
 *       epollfd in ET mode and wrap that into the g_poll() usage.
 *
 *       But I want to get this stuff committed so that I can iterate upon it
 *       before moving forward onto other bits, as it's fairly critical to
 *       get right from the start.
 */

struct _DexSemaphore
{
  gatomicrefcount ref_count;
  int eventfd;
  int epollfd;
};

DexSemaphore *
dex_semaphore_new (void)
{
  DexSemaphore *semaphore;
  struct epoll_event event;
  int evfd;
  int epfd;

  /* Create our eventfd in semaphore mode */
  evfd = eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
  if (evfd == -1)
    return NULL;

  /* Create epollfd for edge-triggered changes */
  epfd = epoll_create (EPOLL_CLOEXEC);
  if (epfd == -1)
    return NULL;

  event.events = EPOLLIN | EPOLLET | EPOLLEXCLUSIVE;
  event.data.fd = evfd;
  epoll_ctl (epfd, EPOLL_CTL_ADD, evfd, &event);

  semaphore = g_new0 (DexSemaphore, 1);
  g_atomic_ref_count_init (&semaphore->ref_count);
  semaphore->eventfd = evfd;
  semaphore->epollfd = epfd;

  return semaphore;
}

static void
dex_semaphore_finalize (DexSemaphore *semaphore)
{
  if (semaphore->eventfd != -1)
    {
      close (semaphore->eventfd);
      semaphore->eventfd = -1;
    }

  g_free (semaphore);
}

void
dex_semaphore_unref (DexSemaphore *semaphore)
{
	if (g_atomic_ref_count_dec (&semaphore->ref_count))
		dex_semaphore_finalize (semaphore);
}

DexSemaphore *
dex_semaphore_ref (DexSemaphore *semaphore)
{
	g_atomic_ref_count_inc (&semaphore->ref_count);
	return semaphore;
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
  GSource       source;
  DexSemaphore *semaphore;
  gpointer      fdtag;
} DexSemaphoreSource;

static gboolean
dex_semaphore_source_dispatch (GSource     *source,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
  DexSemaphoreSource *semaphore_source = (DexSemaphoreSource *)source;
  gint64 counter;
  gssize n_read;

  g_assert (semaphore_source != NULL);
  g_assert (semaphore_source->semaphore != NULL);
  g_assert (semaphore_source->semaphore->eventfd > -1);
 
  n_read = read (semaphore_source->semaphore->eventfd, &counter, sizeof counter);

  g_print ("%p: n_read %d counter %d\n", g_thread_self(), (int)n_read, (int)counter);

  if (n_read == sizeof counter && counter > 0)
    {
      if (callback != NULL)
        return callback (user_data);
    }

  return G_SOURCE_CONTINUE;
}

static void
dex_semaphore_source_finalize (GSource *source)
{
  DexSemaphoreSource *semaphore_source = (DexSemaphoreSource *)source;

  g_clear_pointer (&semaphore_source->semaphore, dex_semaphore_unref);
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
  GSource *source;

  g_return_val_if_fail (semaphore != NULL, NULL);
  g_return_val_if_fail (semaphore->eventfd > -1, NULL);
  g_return_val_if_fail (callback != NULL || callback_data == NULL, NULL);
  g_return_val_if_fail (callback != NULL || callback_data_destroy == NULL, NULL);

  source = g_source_new (&dex_semaphore_source_funcs, sizeof (DexSemaphoreSource));
  if (callback != NULL)
    g_source_set_callback (source, callback, callback_data, callback_data_destroy);
  g_source_set_priority (source, priority);
  g_source_set_static_name (source, "[dex-semaphore-source]");

  semaphore_source = (DexSemaphoreSource *)source;
  semaphore_source->semaphore = dex_semaphore_ref (semaphore);
  semaphore_source->fdtag = g_source_add_unix_fd (source, semaphore->epollfd, G_IO_IN);

  return source;
}
