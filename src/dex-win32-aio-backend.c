/* dex-win32-aio-backend.c
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

#include "dex-win32-aio-backend-private.h"
#include "dex-win32-aio-future-private.h"

struct _DexWin32AioBackend
{
  DexAioBackend parent_instance;
};

struct _DexWin32AioBackendClass
{
  DexAioBackendClass parent_class;
};

typedef struct _DexWin32AioContext
{
  DexAioContext parent;
  GMutex        mutex;
  GQueue        completed;
} DexWin32AioContext;

DEX_DEFINE_FINAL_TYPE (DexWin32AioBackend, dex_win32_aio_backend, DEX_TYPE_AIO_BACKEND)

static inline void
steal_queue (GQueue *from_queue,
             GQueue *to_queue)
{
  *to_queue = *from_queue;
  *from_queue = (GQueue) {NULL, NULL, 0};
}

static gboolean
dex_win32_aio_context_dispatch (GSource     *source,
                                GSourceFunc  callback,
                                gpointer     user_data)
{
  DexWin32AioContext *aio_context = (DexWin32AioContext *)source;
  GQueue completed;

  g_mutex_lock (&aio_context->mutex);
  steal_queue (&aio_context->completed, &completed);
  g_mutex_unlock (&aio_context->mutex);

  while (completed.length > 0)
    {
      DexWin32AioFuture *win32_aio_future = g_queue_pop_head (&completed);
      dex_win32_aio_future_complete (win32_aio_future);
      dex_unref (win32_aio_future);
    }

  return G_SOURCE_CONTINUE;
}

static gboolean
dex_win32_aio_context_check (GSource *source)
{
  DexWin32AioContext *aio_context = (DexWin32AioContext *)source;
  gboolean ret;

  g_assert (aio_context != NULL);
  g_assert (DEX_IS_WIN32_AIO_BACKEND (aio_context->parent.aio_backend));

  g_mutex_lock (&aio_context->mutex);
  ret = aio_context->completed.length > 0;
  g_mutex_unlock (&aio_context->mutex);

  return ret;
}

static gboolean
dex_win32_aio_context_prepare (GSource *source,
                               int     *timeout)
{
  *timeout = -1;
  return dex_win32_aio_context_check (source);
}

static void
dex_win32_aio_context_finalize (GSource *source)
{
  DexWin32AioContext *aio_context = (DexWin32AioContext *)source;

  g_assert (aio_context != NULL);
  g_assert (DEX_IS_WIN32_AIO_BACKEND (aio_context->parent.aio_backend));
  g_assert (aio_context->completed.length == 0);

  g_mutex_clear (&aio_context->mutex);
}

static GSourceFuncs dex_win32_aio_context_source_funcs = {
  .check = dex_win32_aio_context_check,
  .prepare = dex_win32_aio_context_prepare,
  .dispatch = dex_win32_aio_context_dispatch,
  .finalize = dex_win32_aio_context_finalize,
};

static DexAioContext *
dex_win32_aio_backend_create_context (DexAioBackend *aio_backend)
{
  DexWin32AioBackend *win32_aio_backend = DEX_WIN32_AIO_BACKEND (aio_backend);
  DexWin32AioContext *aio_context;

  g_assert (DEX_IS_WIN32_AIO_BACKEND (win32_aio_backend));

  aio_context = (DexWin32AioContext *)
    g_source_new (&dex_win32_aio_context_source_funcs,
                  sizeof *aio_context);
  _g_source_set_static_name ((GSource *)aio_context, "[dex-win32-aio-backend]");
  g_source_set_can_recurse ((GSource *)aio_context, TRUE);
  aio_context->parent.aio_backend = dex_ref (aio_backend);
  g_mutex_init (&aio_context->mutex);

  return (DexAioContext *)aio_context;
}

static DexFuture *
dex_win32_aio_backend_read (DexAioBackend *aio_backend,
                            DexAioContext *aio_context,
                            int            fd,
                            gpointer       buffer,
                            gsize          count,
                            goffset        offset)
{
  return dex_future_new_reject (G_IO_ERROR,
                                G_IO_ERROR_NOT_SUPPORTED,
                                "Operation not supported");
}

static DexFuture *
dex_win32_aio_backend_write (DexAioBackend *aio_backend,
                             DexAioContext *aio_context,
                             int            fd,
                             gconstpointer  buffer,
                             gsize          count,
                             goffset        offset)
{
  return dex_future_new_reject (G_IO_ERROR,
                                G_IO_ERROR_NOT_SUPPORTED,
                                "Operation not supported");
}

static void
dex_win32_aio_backend_class_init (DexWin32AioBackendClass *win32_aio_backend_class)
{
  DexAioBackendClass *aio_backend_class = DEX_AIO_BACKEND_CLASS (win32_aio_backend_class);

  aio_backend_class->create_context = dex_win32_aio_backend_create_context;
  aio_backend_class->read = dex_win32_aio_backend_read;
  aio_backend_class->write = dex_win32_aio_backend_write;

  g_type_ensure (DEX_TYPE_WIN32_AIO_FUTURE);
}

static void
dex_win32_aio_backend_init (DexWin32AioBackend *win32_aio_backend)
{
}

DexAioBackend *
dex_win32_aio_backend_new (void)
{
  return (DexAioBackend *)g_type_create_instance (DEX_TYPE_WIN32_AIO_BACKEND);
}
