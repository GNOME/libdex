/* dex-aio-context.c
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

#ifdef __linux__
# include <sys/eventfd.h>
#endif

#include "dex-aio.h"
#include "dex-aio-context-private.h"
#include "dex-scheduler-private.h"
#ifdef HAVE_LIBURING
# include "dex-uring-future-private.h"
#endif

#define DEFAULT_URING_SIZE 32

typedef struct _DexAioContext
{
  GSource source;
  union {
#ifdef HAVE_LIBURING
    struct {
      struct io_uring ring;
      int eventfd;
      gpointer eventfdtag;
      GQueue queued;
    } liburing;
#endif
  };
} DexAioContext;

#ifdef HAVE_LIBURING
static gboolean
dex_aio_context_uring_dispatch (GSource     *source,
                                GSourceFunc  callback,
                                gpointer     user_data)
{
  DexAioContext *aio_context = (DexAioContext *)source;
  struct io_uring_cqe *cqe;

  while (io_uring_peek_cqe (&aio_context->liburing.ring, &cqe))
    {
      DexUringFuture *future = io_uring_cqe_get_data (cqe);
      dex_uring_future_complete (future, cqe);
      io_uring_cqe_seen (&aio_context->liburing.ring, cqe);
      dex_unref (future);
    }

  return G_SOURCE_CONTINUE;
}

static gboolean
dex_aio_context_uring_prepare (GSource *source,
                               int     *timeout)
{
  DexAioContext *context = (DexAioContext *)source;

  *timeout = -1;

  if (io_uring_sq_ready (&context->liburing.ring) > 0)
    io_uring_submit (&context->liburing.ring);

  if (context->liburing.queued.length)
    {
      struct io_uring_sqe *sqe;
      gboolean do_submit = FALSE;

      while (context->liburing.queued.length &&
             (sqe = io_uring_get_sqe (&context->liburing.ring)))
        {
          DexUringFuture *future = g_queue_pop_head (&context->liburing.queued);
          dex_uring_future_prepare (future, sqe);
          do_submit = TRUE;
        }

      if (do_submit)
        io_uring_submit (&context->liburing.ring);
    }

  return io_uring_cq_ready (&context->liburing.ring) > 0;
}

static GSourceFuncs dex_aio_context_uring_funcs = {
  .prepare = dex_aio_context_uring_prepare,
  .dispatch = dex_aio_context_uring_dispatch,
};

static gboolean
dex_aio_context_uring_init (DexAioContext *context)
{
  guint uring_flags = 0;

#ifdef IORING_SETUP_COOP_TASKRUN
  uring_flags |= IORING_SETUP_COOP_TASKRUN;
#endif

  context->liburing.eventfd = -1;

  /* Setup uring submission/completion queue */
  if (io_uring_queue_init (DEFAULT_URING_SIZE, &context->liburing.ring, uring_flags) != 0)
    return FALSE;

  /* Register the ring FD so we don't have to on every io_ring_enter() */
  if (io_uring_register_ring_fd (&context->liburing.ring) < 0)
    return FALSE;

  /* Create eventfd() we can poll() on with GMainContext since GMainContext
   * knows nothing of uring and how to drive the loop using that.
   */
  if (-1 == (context->liburing.eventfd = eventfd (0, EFD_CLOEXEC)) ||
      io_uring_register_eventfd (&context->liburing.ring, context->liburing.eventfd) != 0)
    return FALSE;

  /* Add the eventfd() to our set of pollfds and keep the tag around so
   * we can check the condition directly.
   */
  context->liburing.eventfdtag =
    g_source_add_unix_fd ((GSource *)context, context->liburing.eventfd, G_IO_IN);

  return TRUE;
}
#endif

GSource *
_dex_aio_context_new (void)
{
  GSource *source;

#ifdef HAVE_LIBURING
  source = g_source_new (&dex_aio_context_uring_funcs, sizeof (DexAioContext));
#else
# error "No aio backend configured"
#endif
  g_source_set_static_name (source, "[dex-aio-context]");

#ifdef HAVE_LIBURING
  if (!dex_aio_context_uring_init ((DexAioContext *)source))
    g_clear_pointer (&source, g_source_unref);
#endif

  return source;
}

static DexAioContext *
dex_aio_context_get_current (void)
{
  return dex_scheduler_get_aio_context (dex_scheduler_get_thread_default ());
}

#ifdef HAVE_LIBURING
static DexFuture *
dex_aio_context_uring_queue (DexUringFuture *future)
{
  DexAioContext *context = dex_aio_context_get_current ();
  struct io_uring_sqe *sqe;

  g_assert (context != NULL);
  g_assert (DEX_IS_URING_FUTURE (future));

  if (context->liburing.queued.length > 0 ||
      !(sqe = io_uring_get_sqe (&context->liburing.ring)))
    {
      g_queue_push_tail (&context->liburing.queued, dex_ref (future));
    }
  else
    {
      dex_uring_future_prepare (future, sqe);
      io_uring_sqe_set_data (sqe, dex_ref (future));
    }

  return DEX_FUTURE (future);
}
#endif

DexFuture *
dex_aio_read (int      fd,
              gpointer buffer,
              gsize    count,
              goffset  offset)
{
#ifdef HAVE_LIBURING
  return dex_aio_context_uring_queue (dex_uring_future_new_read (fd, buffer, count, offset));
#else
# error "No aio backend configured for dex_aio_read()"
#endif
}
