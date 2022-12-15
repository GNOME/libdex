/* dex-win32-aio-future.c
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

#include <io.h>
#include <windows.h>

#include <gio/gio.h>

#include "dex-future-private.h"
#include "dex-win32-aio-backend-private.h"
#include "dex-win32-aio-future-private.h"

typedef enum _DexWin32AioFutureKind
{
  DEX_WIN32_AIO_FUTURE_READ = 1,
  DEX_WIN32_AIO_FUTURE_WRITE,
} DexWin32AioFutureKind;

struct _DexWin32AioFuture
{
  DexFuture              parent_instance;
  GMainContext          *main_context;
  DexWin32AioContext    *aio_context;
  DexWin32AioFutureKind  kind;
  int                    errsv;
  union {
    struct {
      int                fd;
      gpointer           buffer;
      gsize              count;
      goffset            offset;
      gssize             res;
    } read;
    struct {
      int                fd;
      gconstpointer      buffer;
      gsize              count;
      goffset            offset;
      gssize             res;
    } write;
  };
};

typedef struct _DexWin32AioFutureClass
{
  DexFutureClass parent_class;
} DexWin32AioFutureClass;

DEX_DEFINE_FINAL_TYPE (DexWin32AioFuture, dex_win32_aio_future, DEX_TYPE_FUTURE)

#undef DEX_TYPE_WIN32_AIO_FUTURE
#define DEX_TYPE_WIN32_AIO_FUTURE dex_win32_aio_future_type

static void
dex_win32_aio_future_finalize (DexObject *object)
{
  DexWin32AioFuture *win32_aio_future = DEX_WIN32_AIO_FUTURE (object);

  g_clear_pointer ((GSource **)&win32_aio_future->aio_context, g_source_unref);
  g_clear_pointer (&win32_aio_future->main_context, g_main_context_unref);

  DEX_OBJECT_CLASS (dex_win32_aio_future_parent_class)->finalize (object);
}

static void
dex_win32_aio_future_class_init (DexWin32AioFutureClass *win32_aio_future_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (win32_aio_future_class);

  object_class->finalize = dex_win32_aio_future_finalize;
}

static void
dex_win32_aio_future_init (DexWin32AioFuture *win32_aio_future)
{
}

static DexWin32AioFuture *
dex_win32_aio_future_new (DexWin32AioFutureKind  kind,
                          DexWin32AioContext    *aio_context)
{
  DexWin32AioFuture *win32_aio_future;
  GMainContext *main_context;

  if ((main_context = g_source_get_context ((GSource *)aio_context)))
    g_main_context_ref (main_context);

  win32_aio_future = (DexWin32AioFuture *)g_type_create_instance (DEX_TYPE_WIN32_AIO_FUTURE);
  win32_aio_future->kind = kind;
  win32_aio_future->aio_context = (DexWin32AioContext *)g_source_ref ((GSource *)aio_context);
  win32_aio_future->main_context = main_context;

  return win32_aio_future;
}

DexWin32AioFuture *
dex_win32_aio_future_new_read (DexWin32AioContext *win32_aio_context,
                               int                 fd,
                               gpointer            buffer,
                               gsize               count,
                               goffset             offset)
{
  DexWin32AioFuture *win32_aio_future;

  win32_aio_future = dex_win32_aio_future_new (DEX_WIN32_AIO_FUTURE_READ, win32_aio_context);
  win32_aio_future->read.fd = fd;
  win32_aio_future->read.buffer = buffer;
  win32_aio_future->read.count = count;
  win32_aio_future->read.offset = offset;
  win32_aio_future->read.res = -1;

  return win32_aio_future;
}

DexWin32AioFuture *
dex_win32_aio_future_new_write (DexWin32AioContext *win32_aio_context,
                                int                 fd,
                                gconstpointer       buffer,
                                gsize               count,
                                goffset             offset)
{
  DexWin32AioFuture *win32_aio_future;

  win32_aio_future = dex_win32_aio_future_new (DEX_WIN32_AIO_FUTURE_WRITE, win32_aio_context);
  win32_aio_future->write.fd = fd;
  win32_aio_future->write.buffer = buffer;
  win32_aio_future->write.count = count;
  win32_aio_future->write.offset = offset;
  win32_aio_future->write.res = -1;

  return win32_aio_future;
}

void
dex_win32_aio_future_complete (DexWin32AioFuture *win32_aio_future)
{
  g_return_if_fail (DEX_IS_WIN32_AIO_FUTURE (win32_aio_future));

  g_error ("%s: TODO\n", G_STRFUNC);
}

DexWin32AioContext *
dex_win32_aio_future_get_aio_context (DexWin32AioFuture *win32_aio_future)
{
  g_return_val_if_fail (DEX_IS_WIN32_AIO_FUTURE (win32_aio_future), NULL);

  return win32_aio_future->aio_context;
}

GMainContext *
dex_win32_aio_future_get_main_context (DexWin32AioFuture *win32_aio_future)
{
  g_return_val_if_fail (DEX_IS_WIN32_AIO_FUTURE (win32_aio_future), NULL);

  return win32_aio_future->main_context;
}
