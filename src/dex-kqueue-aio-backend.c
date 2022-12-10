/* dex-kqueue-aio-backend.c
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
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "dex-kqueue-aio-backend-private.h"
#include "dex-kqueue-future-private.h"

struct _DexKqueueAioBackend
{
  DexAioBackend parent_instance;
};

struct _DexKqueueAioBackendClass
{
  DexAioBackendClass parent_class;
};

typedef struct _DexKqueueAioContext
{
  DexAioContext    parent;
  int              kqueue_fd;
  gpointer         kqueue_fd_tag;
  GMutex           mutex;
} DexKqueueAioContext;

DEX_DEFINE_FINAL_TYPE (DexKqueueAioBackend, dex_kqueue_aio_backend, DEX_TYPE_AIO_BACKEND)

static gboolean
dex_kqueue_aio_context_dispatch (GSource     *source,
                                 GSourceFunc  callback,
                                 gpointer     user_data)
{
  DexKqueueAioContext *aio_context = (DexKqueueAioContext *)source;
  const struct timespec timeout = {0};
  struct kevent events[32];
  int n_events;

  n_events = kevent (aio_context->kqueue_fd,
                     NULL,
                     0,
                     &events[0],
                     G_N_ELEMENTS (events),
                     &timeout);

  if (n_events > 0)
    {
      for (guint i = 0; i < n_events; i++)
        {
          DexKqueueFuture *future = events[i].udata;
          g_assert (DEX_IS_KQUEUE_FUTURE (future));
          dex_kqueue_future_complete (future, &events[i]);
          dex_clear (&future);
        }
    }

  return G_SOURCE_CONTINUE;
}

static gboolean
dex_kqueue_aio_context_prepare (GSource *source,
                                int     *timeout)
{
  DexKqueueAioContext *aio_context = (DexKqueueAioContext *)source;

  g_assert (aio_context != NULL);
  g_assert (DEX_IS_KQUEUE_AIO_BACKEND (aio_context->parent.aio_backend));

  *timeout = -1;

  return g_source_query_unix_fd (source, aio_context->kqueue_fd_tag) != 0;
}

static gboolean
dex_kqueue_aio_context_check (GSource *source)
{
  DexKqueueAioContext *aio_context = (DexKqueueAioContext *)source;

  g_assert (aio_context != NULL);
  g_assert (DEX_IS_KQUEUE_AIO_BACKEND (aio_context->parent.aio_backend));

  return g_source_query_unix_fd (source, aio_context->kqueue_fd_tag) != 0;
}

static void
dex_kqueue_aio_context_finalize (GSource *source)
{
  DexKqueueAioContext *aio_context = (DexKqueueAioContext *)source;

  g_assert (aio_context != NULL);
  g_assert (DEX_IS_KQUEUE_AIO_BACKEND (aio_context->parent.aio_backend));

  dex_clear (&aio_context->parent.aio_backend);
  g_mutex_clear (&aio_context->mutex);

  aio_context->kqueue_fd_tag = NULL;

  if (aio_context->kqueue_fd != -1)
    {
      close (aio_context->kqueue_fd);
      aio_context->kqueue_fd = -1;
    }
}

static GSourceFuncs dex_kqueue_aio_context_source_funcs = {
  .check = dex_kqueue_aio_context_check,
  .prepare = dex_kqueue_aio_context_prepare,
  .dispatch = dex_kqueue_aio_context_dispatch,
  .finalize = dex_kqueue_aio_context_finalize,
};

static DexFuture *
dex_kqueue_aio_context_queue (DexKqueueAioContext *aio_context,
                              DexKqueueFuture     *future)
{
  g_assert (aio_context != NULL);
  g_assert (DEX_IS_KQUEUE_AIO_BACKEND (aio_context->parent.aio_backend));
  g_assert (DEX_IS_KQUEUE_FUTURE (future));

  g_mutex_lock (&aio_context->mutex);
  dex_kqueue_future_submit (future, aio_context->kqueue_fd);
  g_mutex_unlock (&aio_context->mutex);

  return DEX_FUTURE (future);
}

static DexAioContext *
dex_kqueue_aio_backend_create_context (DexAioBackend *aio_backend)
{
  DexKqueueAioBackend *kqueue_aio_backend = DEX_KQUEUE_AIO_BACKEND (aio_backend);
  DexKqueueAioContext *aio_context;

  g_assert (DEX_IS_KQUEUE_AIO_BACKEND (kqueue_aio_backend));

  aio_context = (DexKqueueAioContext *)
    g_source_new (&dex_kqueue_aio_context_source_funcs,
                  sizeof *aio_context);
  aio_context->parent.aio_backend = dex_ref (aio_backend);
  aio_context->kqueue_fd = -1;
  g_mutex_init (&aio_context->mutex);

  /* Setup our kqueue to deliver/receive events */
  if ((aio_context->kqueue_fd = kqueue ()) == -1)
    {
      int errsv = errno;
      g_critical ("Failed to setup kqueue(): %s", g_strerror (errsv));
      goto failure;
    }

  /* Add the kqueue() to our set of pollfds and keep the tag around so
   * we can check the condition directly.
   */
  aio_context->kqueue_fd_tag = g_source_add_unix_fd ((GSource *)aio_context,
                                                     aio_context->kqueue_fd,
                                                     G_IO_IN);

  return (DexAioContext *)aio_context;

failure:
  g_source_unref ((GSource *)aio_context);

  return NULL;
}

static DexFuture *
dex_kqueue_aio_backend_read (DexAioBackend *aio_backend,
                             DexAioContext *aio_context,
                             int            fd,
                             gpointer       buffer,
                             gsize          count,
                             goffset        offset)
{
  return dex_kqueue_aio_context_queue ((DexKqueueAioContext *)aio_context,
                                      dex_kqueue_future_new_read (fd, buffer, count, offset));
}

static DexFuture *
dex_kqueue_aio_backend_write (DexAioBackend *aio_backend,
                              DexAioContext *aio_context,
                              int            fd,
                              gconstpointer  buffer,
                              gsize          count,
                              goffset        offset)
{
  return dex_kqueue_aio_context_queue ((DexKqueueAioContext *)aio_context,
                                       dex_kqueue_future_new_write (fd, buffer, count, offset));
}

static void
dex_kqueue_aio_backend_class_init (DexKqueueAioBackendClass *kqueue_aio_backend_class)
{
  DexAioBackendClass *aio_backend_class = DEX_AIO_BACKEND_CLASS (kqueue_aio_backend_class);

  aio_backend_class->create_context = dex_kqueue_aio_backend_create_context;
  aio_backend_class->read = dex_kqueue_aio_backend_read;
  aio_backend_class->write = dex_kqueue_aio_backend_write;
}

static void
dex_kqueue_aio_backend_init (DexKqueueAioBackend *kqueue_aio_backend)
{
}

DexAioBackend *
dex_kqueue_aio_backend_new (void)
{
  return (DexAioBackend *)g_type_create_instance (DEX_TYPE_KQUEUE_AIO_BACKEND);
}
