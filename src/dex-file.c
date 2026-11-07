/* dex-file.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "dex-gio.h"

#ifdef HAVE_LIBURING
# include <fcntl.h>
# include <linux/stat.h>
# include <sys/stat.h>

# include "dex-aio.h"
# include "dex-coroutine.h"
# include "dex-scheduler-private.h"
# include "dex-uring-aio-backend-private.h"

#define INITIAL_BUFFER_SIZE 4096

typedef struct
{
  DexAioContext *aio_context;
  char          *filename;
  char          *buffer;
  GError        *error;
  struct statx   stat;
  gsize          capacity;
  gsize          length;
  int            fd;
} DexFileContents;

static void
dex_file_contents_free (gpointer data)
{
  DexFileContents *state = data;

  if (state->fd != -1)
    dex_unref (dex_aio_close (state->aio_context, g_steal_fd (&state->fd)));

  g_clear_pointer ((GSource **)&state->aio_context, g_source_unref);
  g_clear_pointer (&state->filename, g_free);
  g_clear_pointer (&state->buffer, g_free);
  g_clear_error (&state->error);
  g_free (state);
}

static gboolean
dex_file_contents_resize (DexFileContents *state,
                          gsize            capacity)
{
  char *buffer;

  g_assert (state != NULL);
  g_assert (capacity > state->length);

  if (!(buffer = g_try_realloc (state->buffer, capacity)))
    {
      g_set_error_literal (&state->error,
                           G_IO_ERROR,
                           G_IO_ERROR_NO_SPACE,
                           "Not enough memory to read file contents");
      return FALSE;
    }

  state->buffer = buffer;
  state->capacity = capacity;

  return TRUE;
}

static DexFuture *
dex_file_contents_read (DexCoroutineContext *context,
                        gpointer             user_data)
{
  DexFileContents *state = user_data;
  gint64 n_read;
  gboolean have_stat;

  DEX_COROUTINE_BEGIN (context);

  DEX_COROUTINE_SUSPEND_FD (&state->fd, &state->error,
                            dex_aio_open (state->aio_context, state->filename,
                                          (O_RDONLY | O_CLOEXEC), 0));
  if (state->error != NULL)
    return dex_future_new_for_error (g_steal_pointer (&state->error));

  DEX_COROUTINE_SUSPEND_BOOLEAN (&have_stat, NULL,
                                 _dex_uring_aio_stat (state->aio_context, state->fd, &state->stat));

  if (have_stat &&
      (state->stat.stx_mask & (STATX_SIZE | STATX_TYPE)) == (STATX_SIZE | STATX_TYPE) &&
      S_ISREG (state->stat.stx_mode) &&
      state->stat.stx_size > 0)
    {
      if (state->stat.stx_size >= G_MAXSIZE)
        g_set_error_literal (&state->error,
                             G_IO_ERROR,
                             G_IO_ERROR_NO_SPACE,
                             "File contents are too large to fit in memory");
      else
        state->capacity = state->stat.stx_size + 1;
    }
  else
    {
      state->capacity = INITIAL_BUFFER_SIZE;
    }

  if (state->error == NULL && dex_file_contents_resize (state, state->capacity))
    {
      for (;;)
        {
          DEX_COROUTINE_SUSPEND_INT64 (&n_read, &state->error,
                                       dex_aio_read (state->aio_context, state->fd,
                                                     state->buffer + state->length,
                                                     MIN (state->capacity - state->length, G_MAXINT),
                                                     -1));
          if (state->error != NULL || n_read == 0)
            break;

          state->length += n_read;

          if (state->length == state->capacity)
            {
              if (state->capacity > G_MAXSIZE / 2)
                {
                  g_set_error_literal (&state->error,
                                       G_IO_ERROR,
                                       G_IO_ERROR_NO_SPACE,
                                       "File contents are too large to fit in memory");
                  break;
                }

              if (!dex_file_contents_resize (state, state->capacity * 2))
                break;
            }
        }
    }

  DEX_COROUTINE_SUSPEND (dex_aio_close (state->aio_context, g_steal_fd (&state->fd)), NULL);

  if (state->error != NULL)
    return dex_future_new_for_error (g_steal_pointer (&state->error));

  state->buffer[state->length] = '\0';

  return dex_future_new_take_boxed (G_TYPE_BYTES,
                                    g_bytes_new_take (g_steal_pointer (&state->buffer),
                                                      state->length));

  DEX_COROUTINE_END;
}
#endif

/**
 * dex_file_get_contents_bytes:
 * @filename: (type filename): the path of the file to read
 *
 * Reads a file asynchronously into a [struct@GLib.Bytes].
 *
 * Uses the calling thread's io_uring context when available, falling back to
 * GIO's asynchronous file operations. The calling thread must continue iterating
 * its main context until the operation completes. This is always the case with
 * scheduled threads (main, threadpool scheduler, etc).
 *
 * The file is read until EOF. The contents are zero-terminated, but the trailing
 * zero is not included in the size of the bytes. The result is not a snapshot
 * if another process changes the file during reading.
 *
 * No synchronous filesystem I/O is performed on the calling thread. Cancellation
 * may be reported before outstanding I/O and asynchronous cleanup finish.
 *
 * Returns: (transfer full): a future that resolves to [struct@GLib.Bytes],
 *   or rejects with an error
 *
 * Since: 1.3
 */
DexFuture *
dex_file_get_contents_bytes (const char *filename)
{
  DexFuture *future;
  GFile *file;

  dex_return_error_if_fail (filename != NULL);

#ifdef HAVE_LIBURING
  {
    DexScheduler *scheduler = dex_scheduler_get_thread_default ();
    DexAioContext *aio_context = scheduler ? dex_scheduler_get_aio_context (scheduler) : NULL;

    if (aio_context != NULL && DEX_IS_URING_AIO_BACKEND (aio_context->aio_backend))
      {
        DexFileContents *state = g_new0 (DexFileContents, 1);

        state->aio_context = (DexAioContext *)g_source_ref ((GSource *)aio_context);
        state->filename = g_strdup (filename);
        state->fd = -1;

        return dex_scheduler_spawn_coroutine (scheduler, dex_file_contents_read,
                                              state, dex_file_contents_free);
      }
  }
#endif

  file = g_file_new_for_path (filename);
  future = dex_file_load_contents_bytes (file);
  g_object_unref (file);

  return future;
}
