/* dex-uring-future.c
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

#include <liburing.h>

#include <gio/gio.h>

#include "dex-future-private.h"
#include "dex-uring-future-private.h"

typedef enum _DexUringType
{
  DEX_URING_TYPE_READ = 1,
  DEX_URING_TYPE_WRITE,
} DexUringType;

struct _DexUringFuture
{
  DexFuture parent_instance;
  DexUringType type;
  union {
    struct {
      int fd;
      gpointer buffer;
      gsize count;
      goffset offset;
    } read;
    struct {
      int fd;
      gconstpointer buffer;
      gsize count;
      goffset offset;
    } write;
  };
};

typedef struct _DexUringFutureClass
{
  DexFutureClass parent_class;
} DexUringFutureClass;

DEX_DEFINE_FINAL_TYPE (DexUringFuture, dex_uring_future, DEX_TYPE_FUTURE)

static void
dex_uring_future_class_init (DexUringFutureClass *uring_future_class)
{
}

static void
dex_uring_future_init (DexUringFuture *uring_future)
{
}

void
dex_uring_future_complete (DexUringFuture      *uring_future,
                           struct io_uring_cqe *cqe)
{
  if (cqe->res < 0)
    {
      dex_future_complete (DEX_FUTURE (uring_future),
                           NULL,
                           g_error_new_literal (G_IO_ERROR,
                                                g_io_error_from_errno (-cqe->res),
                                                g_strerror (-cqe->res)));
      return;
    }

  switch (uring_future->type)
    {
    case DEX_URING_TYPE_READ:
    case DEX_URING_TYPE_WRITE:
      GValue value = {G_TYPE_INT64, {{.v_int64 = cqe->res}}};
      dex_future_complete (DEX_FUTURE (uring_future), &value, NULL);
      break;

    default:
      g_assert_not_reached ();
    }
}

void
dex_uring_future_prepare (DexUringFuture      *uring_future,
                          struct io_uring_sqe *sqe)
{
  switch (uring_future->type)
    {
    case DEX_URING_TYPE_READ:
      io_uring_prep_read (sqe,
                          uring_future->read.fd,
                          uring_future->read.buffer,
                          uring_future->read.count,
                          uring_future->read.offset);
      break;

    case DEX_URING_TYPE_WRITE:
      io_uring_prep_write (sqe,
                           uring_future->write.fd,
                           uring_future->write.buffer,
                           uring_future->write.count,
                           uring_future->write.offset);
      break;

    default:
      g_assert_not_reached ();
    }
}

DexUringFuture *
dex_uring_future_new_read (int      fd,
                           gpointer buffer,
                           gsize    count,
                           goffset  offset)
{
  DexUringFuture *future;

  future = (DexUringFuture *)g_type_create_instance (DEX_TYPE_URING_FUTURE);
  future->type = DEX_URING_TYPE_READ;
  future->read.fd = fd;
  future->read.buffer = buffer;
  future->read.count = count;
  future->read.offset = offset;

  return future;
}

DexUringFuture *
dex_uring_future_new_write (int           fd,
                            gconstpointer buffer,
                            gsize         count,
                            goffset       offset)
{
  DexUringFuture *future;

  future = (DexUringFuture *)g_type_create_instance (DEX_TYPE_URING_FUTURE);
  future->type = DEX_URING_TYPE_WRITE;
  future->write.fd = fd;
  future->write.buffer = buffer;
  future->write.count = count;
  future->write.offset = offset;

  return future;
}
