/* dex-kqueue-future.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <errno.h>
#include <unistd.h>

#include <gio/gio.h>

#include "dex-future-private.h"
#include "dex-kqueue-future-private.h"

typedef enum _DexKqueueType
{
  DEX_KQUEUE_TYPE_READ = 1,
  DEX_KQUEUE_TYPE_WRITE,
} DexKqueueType;

struct _DexKqueueFuture
{
  DexFuture parent_instance;
  DexKqueueType type;
  union {
    struct {
      int fd;
      gpointer buffer;
      gsize count;
      goffset offset;
      gsize bytes_available;
    } read;
    struct {
      int fd;
      gconstpointer buffer;
      gsize count;
      goffset offset;
      gsize bytes_available;
    } write;
  };
};

typedef struct _DexKqueueFutureClass
{
  DexFutureClass parent_class;
} DexKqueueFutureClass;

DEX_DEFINE_FINAL_TYPE (DexKqueueFuture, dex_kqueue_future, DEX_TYPE_FUTURE)

static void
dex_kqueue_future_class_init (DexKqueueFutureClass *kqueue_future_class)
{
}

static void
dex_kqueue_future_init (DexKqueueFuture *kqueue_future)
{
}

static void
dex_kqueue_future_complete_read (DexKqueueFuture     *kqueue_future,
                                 const struct kevent *event)
{
  gsize to_read = kqueue_future->read.count;
  gssize n_read;

  if (kqueue_future->read.bytes_available > 0)
    to_read = MIN (to_read, kqueue_future->read.bytes_available);

  if (kqueue_future->read.offset >= 0)
    n_read = pread (kqueue_future->read.fd,
                    kqueue_future->read.buffer,
                    to_read,
                    kqueue_future->read.offset);
  else
    n_read = read (kqueue_future->read.fd,
                   kqueue_future->read.buffer,
                   to_read);

  if (n_read < 0)
    {
      int errsv = errno;
      dex_future_complete (DEX_FUTURE (kqueue_future),
                           NULL,
                           g_error_new_literal (G_IO_ERROR,
                                                g_io_error_from_errno (errsv),
                                                g_strerror (errsv)));
    }
  else
    {
      GValue value = {G_TYPE_INT64, {{.v_int64 = n_read}}};
      dex_future_complete (DEX_FUTURE (kqueue_future), &value, NULL);
    }
}

static void
dex_kqueue_future_complete_write (DexKqueueFuture     *kqueue_future,
                                  const struct kevent *event)
{
  gsize to_write = kqueue_future->write.count;
  gssize n_write;

  if (kqueue_future->write.bytes_available > 0)
    to_write = MIN (to_write, kqueue_future->write.bytes_available);

  if (kqueue_future->write.offset >= 0)
    n_write = pwrite (kqueue_future->write.fd,
                      kqueue_future->write.buffer,
                      to_write,
                      kqueue_future->write.offset);
  else
    n_write = write (kqueue_future->write.fd,
                     kqueue_future->write.buffer,
                     to_write);

  if (n_write < 0)
    {
      int errsv = errno;
      dex_future_complete (DEX_FUTURE (kqueue_future),
                           NULL,
                           g_error_new_literal (G_IO_ERROR,
                                                g_io_error_from_errno (errsv),
                                                g_strerror (errsv)));
    }
  else
    {
      GValue value = {G_TYPE_INT64, {{.v_int64 = n_write}}};
      dex_future_complete (DEX_FUTURE (kqueue_future), &value, NULL);
    }
}

void
dex_kqueue_future_complete (DexKqueueFuture     *kqueue_future,
                            const struct kevent *event)
{
  switch (kqueue_future->type)
    {
    case DEX_KQUEUE_TYPE_READ:
      dex_kqueue_future_complete_read (kqueue_future, event);
      break;

    case DEX_KQUEUE_TYPE_WRITE:
      dex_kqueue_future_complete_write (kqueue_future, event);
      break;

    default:
      g_assert_not_reached ();
    }
}

gboolean
dex_kqueue_future_submit (DexKqueueFuture *kqueue_future,
                          int              kqueue_fd)
{
  struct kevent change;
  gboolean ret = FALSE;

  switch (kqueue_future->type)
    {
    case DEX_KQUEUE_TYPE_READ:
      kqueue_future->read.bytes_available = 0;

      /* FIXME: We sort of want this to work like pread(),
       * but that requires not modifying the offset. Can that
       * be done with kqueue event filters?
       */
      if (kqueue_future->read.offset >= 0)
        lseek (kqueue_future->read.fd, kqueue_future->read.offset, SEEK_SET);

      EV_SET (&change,
              kqueue_future->read.fd,
              EVFILT_READ,
              0,
              0,
              (intptr_t)&kqueue_future->read.bytes_available,
              dex_ref (kqueue_future));
      break;

    case DEX_KQUEUE_TYPE_WRITE:
      kqueue_future->write.bytes_available = 0;

      /* FIXME: Same as for read */
      if (kqueue_future->write.offset >= 0)
        lseek (kqueue_future->write.fd, kqueue_future->write.offset, SEEK_SET);

      EV_SET (&change,
              kqueue_future->write.fd,
              EVFILT_WRITE,
              0,
              0,
              (intptr_t)&kqueue_future->write.bytes_available,
              dex_ref (kqueue_future));
      break;

    default:
      g_return_val_if_reached (FALSE);
    }

  if (kevent (kqueue_fd, &change, 1, NULL, 0, NULL) == 1)
    ret = TRUE;
  else
    dex_unref (kqueue_future);

  return ret;
}

DexKqueueFuture *
dex_kqueue_future_new_read (int      fd,
                            gpointer buffer,
                            gsize    count,
                            goffset  offset)
{
  DexKqueueFuture *future;

  future = (DexKqueueFuture *)g_type_create_instance (DEX_TYPE_KQUEUE_FUTURE);
  future->type = DEX_KQUEUE_TYPE_READ;
  future->read.fd = fd;
  future->read.buffer = buffer;
  future->read.count = count;
  future->read.offset = offset;

  return future;
}

DexKqueueFuture *
dex_kqueue_future_new_write (int           fd,
                             gconstpointer buffer,
                             gsize         count,
                             goffset       offset)
{
  DexKqueueFuture *future;

  future = (DexKqueueFuture *)g_type_create_instance (DEX_TYPE_KQUEUE_FUTURE);
  future->type = DEX_KQUEUE_TYPE_WRITE;
  future->write.fd = fd;
  future->write.buffer = buffer;
  future->write.count = count;
  future->write.offset = offset;

  return future;
}
