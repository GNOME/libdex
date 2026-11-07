/* test-file.c
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

#include <glib/gstdio.h>
#include <libdex.h>

#include "dex-future-private.h"
#include "dex-main-scheduler-private.h"
#include "dex-scheduler-private.h"
#include "dex-thread-storage-private.h"
#include "test-util.h"

#ifdef HAVE_LIBURING
# include "dex-uring-aio-backend-private.h"
#endif

#ifdef G_OS_UNIX
# include <fcntl.h>
# include <sys/stat.h>
# include <unistd.h>
#endif

static void
assert_contents (const char *path,
                 const char *expected,
                 gsize       length)
{
  GError *error = NULL;
  GBytes *bytes;
  DexFuture *future;
  gsize size;
  const char *data;

  future = dex_file_get_contents_bytes (path);
#ifdef HAVE_LIBURING
  {
    DexAioContext *aio_context = dex_scheduler_get_aio_context (dex_scheduler_get_thread_default ());

    g_assert_cmpint (DEX_IS_COROUTINE (future), ==,
                     DEX_IS_URING_AIO_BACKEND (aio_context->aio_backend));
  }
#endif
  bytes = dex_await_boxed (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (bytes);
  data = g_bytes_get_data (bytes, &size);
  g_assert_cmpuint (size, ==, length);
  g_assert_nonnull (data);
  g_assert_cmpint (data[size], ==, '\0');
  if (length > 0)
    g_assert_cmpmem (data, size, expected, length);
  g_bytes_unref (bytes);
}

static void
test_file_contents (void)
{
  static const gsize sizes[] = { 0, 1, 4095, 4096, 4097, 65536, 2 * 1024 * 1024 };
  GError *error = NULL;
  char *directory = g_dir_make_tmp ("libdex-contents-XXXXXX", &error);
  char *path;
  char *data;

  g_assert_no_error (error);
  path = g_build_filename (directory, "contents", NULL);
  data = g_malloc (sizes[G_N_ELEMENTS (sizes) - 1]);
  for (gsize i = 0; i < sizes[G_N_ELEMENTS (sizes) - 1]; i++)
    data[i] = i % 251;

  for (guint i = 0; i < G_N_ELEMENTS (sizes); i++)
    {
      g_assert_true (g_file_set_contents (path, data, sizes[i], &error));
      g_assert_no_error (error);
      assert_contents (path, data, sizes[i]);
    }

  g_assert_cmpint (g_unlink (path), ==, 0);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
  g_free (data);
  g_free (path);
  g_free (directory);
}

static void
test_file_contents_errors (void)
{
  GError *error = NULL;
  char *directory = g_dir_make_tmp ("libdex-contents-errors-XXXXXX", &error);
  char *missing;

  g_assert_no_error (error);
  missing = g_build_filename (directory, "missing", NULL);
  g_assert_null (dex_await_boxed (dex_file_get_contents_bytes (missing), &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);

  g_assert_null (dex_await_boxed (dex_file_get_contents_bytes (directory), &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY);
  g_clear_error (&error);

  g_assert_cmpint (g_rmdir (directory), ==, 0);
  g_free (missing);
  g_free (directory);
}

static gpointer
test_file_contents_thread (gpointer user_data)
{
  const char *path = user_data;
  GMainContext *context = g_main_context_new ();
  GError *error = NULL;
  char *expected = NULL;
  gsize length;

  g_assert_true (g_file_get_contents (path, &expected, &length, &error));
  g_assert_no_error (error);

  g_main_context_push_thread_default (context);

  for (guint with_scheduler = 0; with_scheduler < 2; with_scheduler++)
    {
      DexMainScheduler *scheduler = NULL;
      DexFuture *future;
      GBytes *bytes;

      if (with_scheduler)
        scheduler = dex_main_scheduler_new (context);

      future = dex_file_get_contents_bytes (path);
      if (!with_scheduler)
        g_assert_false (DEX_IS_COROUTINE (future));

      while (dex_future_is_pending (future))
        g_main_context_iteration (context, TRUE);

      bytes = dex_await_boxed (future, &error);
      g_assert_no_error (error);
      g_assert_nonnull (bytes);
      g_assert_cmpuint (g_bytes_get_size (bytes), ==, length);
      g_assert_nonnull (g_bytes_get_data (bytes, NULL));
      g_assert_cmpmem (g_bytes_get_data (bytes, NULL), length + 1, expected, length + 1);
      g_bytes_unref (bytes);

      if (scheduler != NULL)
        {
          dex_scheduler_set_thread_default (NULL);
          dex_thread_storage_get ()->aio_context = NULL;
          dex_unref (scheduler);
        }
    }

  g_main_context_pop_thread_default (context);
  g_main_context_unref (context);
  g_free (expected);
  return NULL;
}

static void
test_file_contents_thread_affinity (void)
{
  GError *error = NULL;
  char *directory = g_dir_make_tmp ("libdex-contents-thread-XXXXXX", &error);
  char *path;
  GThread *thread;

  g_assert_no_error (error);
  path = g_build_filename (directory, "contents", NULL);
  for (gsize length = 0; length <= 6; length++)
    {
      g_assert_true (g_file_set_contents (path, "thread", length, &error));
      g_assert_no_error (error);

      thread = g_thread_new ("file-contents", test_file_contents_thread, path);
      g_thread_join (thread);
    }

  g_assert_cmpint (g_unlink (path), ==, 0);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
  g_free (path);
  g_free (directory);
}

#ifdef G_OS_UNIX
typedef struct
{
  const char *path;
  DexPromise *opened;
  GMutex      mutex;
  GCond       cond;
  gboolean    proceed;
  gboolean    cancel;
} TestFileFifo;

static gpointer
test_file_fifo_writer (gpointer user_data)
{
  TestFileFifo *state = user_data;
  int fd;

  fd = g_open (state->path, O_WRONLY, 0);
  g_assert_cmpint (fd, >=, 0);
  dex_promise_resolve_boolean (state->opened, TRUE);

  g_mutex_lock (&state->mutex);
  while (!state->proceed)
    g_cond_wait (&state->cond, &state->mutex);
  g_mutex_unlock (&state->mutex);

  if (!state->cancel)
    {
      char block[257];

      memset (block, 'x', sizeof block);
      for (guint i = 0; i < 128; i++)
        g_assert_cmpint (write (fd, block, sizeof block), ==, sizeof block);
    }

  g_assert_cmpint (close (fd), ==, 0);
  return NULL;
}

static void
test_file_fifo (void)
{
  GError *error = NULL;
  char *directory = g_dir_make_tmp ("libdex-contents-fifo-XXXXXX", &error);
  char *path;

  g_assert_no_error (error);
  path = g_build_filename (directory, "fifo", NULL);
  g_assert_cmpint (mkfifo (path, 0600), ==, 0);

  for (guint cancel = 0; cancel < 2; cancel++)
    {
      TestFileFifo state = { 0 };
      DexFuture *future;
      DexWeakRef weak;
      GThread *writer;
      gboolean is_coroutine;
      gint64 deadline;

      state.path = path;
      state.opened = dex_promise_new ();
      state.cancel = cancel;
      g_mutex_init (&state.mutex);
      g_cond_init (&state.cond);
      writer = g_thread_new ("fifo-writer", test_file_fifo_writer, &state);

      future = dex_file_get_contents_bytes (path);
      is_coroutine = DEX_IS_COROUTINE (future);
      dex_weak_ref_init (&weak, future);
      g_assert_true (dex_await_boolean (dex_ref (DEX_FUTURE (state.opened)), &error));
      g_assert_no_error (error);

      dex_await (dex_timeout_new_msec (10), NULL);
      g_assert_true (dex_future_is_pending (future));
      if (cancel)
        dex_future_abandon (future);

      g_mutex_lock (&state.mutex);
      state.proceed = TRUE;
      g_cond_signal (&state.cond);
      g_mutex_unlock (&state.mutex);

      if (cancel)
        {
          g_assert_null (dex_await_boxed (future, &error));
          if (is_coroutine)
            g_assert_error (error, DEX_ERROR, DEX_ERROR_FIBER_CANCELLED);
          else
            g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
          g_clear_error (&error);
        }
      else
        {
          GBytes *bytes = dex_await_boxed (future, &error);
          const char *data;
          gsize length;

          g_assert_no_error (error);
          g_assert_nonnull (bytes);
          data = g_bytes_get_data (bytes, &length);
          g_assert_cmpuint (length, ==, 257 * 128);
          g_assert_nonnull (data);
          g_assert_cmpint (data[length], ==, '\0');
          for (gsize i = 0; i < length; i++)
            g_assert_cmpint (data[i], ==, 'x');
          g_bytes_unref (bytes);
        }

      g_thread_join (writer);
      deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;
      while ((future = dex_weak_ref_get (&weak)))
        {
          dex_unref (future);
          g_assert_cmpint (g_get_monotonic_time (), <, deadline);
          dex_await (dex_timeout_new_msec (1), NULL);
        }
      dex_weak_ref_clear (&weak);
      dex_unref (state.opened);
      g_cond_clear (&state.cond);
      g_mutex_clear (&state.mutex);
    }

  g_assert_cmpint (g_unlink (path), ==, 0);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
  g_free (path);
  g_free (directory);
}
#endif

#ifdef __linux__
static void
test_file_contents_proc (void)
{
  GError *error = NULL;
  char *contents;
  gsize length;

  g_assert_true (g_file_get_contents ("/proc/self/cmdline", &contents, &length, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (length, >, 0);
  assert_contents ("/proc/self/cmdline", contents, length);
  g_free (contents);
}
#endif

int
main (int argc,
       char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);

  _g_test_add_func ("/Dex/File/contents", test_file_contents);
  _g_test_add_func ("/Dex/File/errors", test_file_contents_errors);
  g_test_add_func ("/Dex/File/thread-affinity", test_file_contents_thread_affinity);
#ifdef G_OS_UNIX
  _g_test_add_func ("/Dex/File/fifo", test_file_fifo);
#endif
#ifdef __linux__
  _g_test_add_func ("/Dex/File/proc", test_file_contents_proc);
#endif

  return g_test_run ();
}
