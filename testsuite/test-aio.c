/* test-aio.c
 *
 * Copyright 2026 Christian Hergert
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <glib/gstdio.h>

#include <libdex.h>

#include "dex-aio-backend-private.h"
#include "dex-future-private.h"
#include "dex-posix-aio-backend-private.h"

#ifdef HAVE_LIBURING
# include "dex-uring-aio-backend-private.h"
# include "dex-uring-future-private.h"
#endif

static DexFuture *
quit_cb (DexFuture *future,
         gpointer   user_data)
{
  g_main_loop_quit (user_data);
  return NULL;
}

static DexFuture *
await_future (DexFuture *future)
{
  GMainLoop *main_loop = g_main_loop_new (NULL, FALSE);

  future = dex_future_finally (future, quit_cb, main_loop, NULL);
  g_main_loop_run (main_loop);

  g_main_loop_unref (main_loop);

  return future;
}

static void
run_aio_close_success (DexAioContext *aio_context)
{
  char *path = NULL;
  GError * error = NULL;
  DexFuture *future;
  gboolean ret;
  int fd;

  fd = g_file_open_tmp ("libdex-aio-close-XXXXXX", &path, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, >=, 0);

  future = await_future (dex_aio_close (aio_context, fd));
  ret = dex_await_boolean (future, &error);

  g_assert_no_error (error);
  g_assert_true (ret);
  g_clear_error (&error);

  errno = 0;
  g_assert_cmpint (fcntl (fd, F_GETFD), ==, -1);
  g_assert_cmpint (errno, ==, EBADF);

  g_assert_cmpint (g_unlink (path), ==, 0);
  g_clear_pointer (&path, g_free);
}

static void
run_aio_close_bad_fd (DexAioContext *aio_context)
{
  GError * error = NULL;
  DexFuture *future;
  gboolean ret;

  future = await_future (dex_aio_close (aio_context, G_MAXINT));
  ret = dex_await_boolean (future, &error);

  g_assert_false (ret);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error (&error);
}

static void
run_aio_open_success (DexAioContext *aio_context)
{
  static const char contents[] = "libdex aio open";
  char *path = NULL;
  GError * error = NULL;
  DexFuture *future;
  char buffer[sizeof contents] = {0};
  int source_fd;
  int fd;

  source_fd = g_file_open_tmp ("libdex-aio-open-XXXXXX", &path, &error);
  g_assert_no_error (error);
  g_assert_cmpint (source_fd, >=, 0);
  g_assert_cmpint (write (source_fd, contents, strlen (contents)), ==, strlen (contents));
  g_assert_cmpint (close (source_fd), ==, 0);

  future = await_future (dex_aio_open (aio_context, path, O_RDONLY | O_CLOEXEC, 0));
  fd = dex_await_fd (future, &error);

  g_assert_no_error (error);
  g_assert_cmpint (fd, >=, 0);
  g_assert_cmpint (read (fd, buffer, sizeof buffer), ==, strlen (contents));
  g_assert_cmpmem (buffer, strlen (contents), contents, strlen (contents));
  g_assert_cmpint (close (fd), ==, 0);
  g_clear_error (&error);

  g_assert_cmpint (g_unlink (path), ==, 0);
  g_clear_pointer (&path, g_free);
}

static void
run_aio_open_missing (DexAioContext *aio_context)
{
  char *path = NULL;
  GError * error = NULL;
  DexFuture *future;
  int fd;

  fd = g_file_open_tmp ("libdex-aio-open-missing-XXXXXX", &path, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, >=, 0);
  g_assert_cmpint (close (fd), ==, 0);
  g_assert_cmpint (g_unlink (path), ==, 0);

  future = await_future (dex_aio_open (aio_context, path, O_RDONLY | O_CLOEXEC, 0));
  fd = dex_await_fd (future, &error);

  g_assert_cmpint (fd, ==, -1);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);

  g_clear_pointer (&path, g_free);
}

static void
run_aio_read_write_bad_fd (DexAioContext *aio_context)
{
  char buffer[1] = {0};
  GError *error = NULL;
  DexFuture *future;

  future = await_future (dex_aio_read (aio_context, G_MAXINT, buffer, sizeof buffer, 0));
  dex_await_int64 (future, &error);
  g_assert_error (error, G_IO_ERROR, g_io_error_from_errno (EBADF));
  g_clear_error (&error);

  future = await_future (dex_aio_write (aio_context, G_MAXINT, buffer, sizeof buffer, 0));
  dex_await_int64 (future, &error);
  g_assert_error (error, G_IO_ERROR, g_io_error_from_errno (EBADF));
  g_clear_error (&error);
}

static void
test_aio_close_success (void)
{
  run_aio_close_success (NULL);
}

static void
test_aio_close_bad_fd (void)
{
  run_aio_close_bad_fd (NULL);
}

static void
test_aio_open_success (void)
{
  run_aio_open_success (NULL);
}

static void
test_aio_open_missing (void)
{
  run_aio_open_missing (NULL);
}

static void
test_aio_open_posix (void)
{
  DexAioBackend *backend;
  DexAioContext *aio_context;

  backend = dex_posix_aio_backend_new ();
  aio_context = dex_aio_backend_create_context (backend);
  g_source_attach ((GSource *)aio_context, NULL);

  run_aio_close_success (aio_context);
  run_aio_close_bad_fd (aio_context);
  run_aio_open_success (aio_context);
  run_aio_open_missing (aio_context);
  run_aio_read_write_bad_fd (aio_context);

  g_source_destroy ((GSource *)aio_context);
  g_source_unref ((GSource *)aio_context);
  dex_unref (backend);
}

#ifdef HAVE_LIBURING
static void
test_aio_uring_read_write_error_mapping (void)
{
  struct io_uring_cqe cqe = {.res = -EBADF};
  DexUringFuture *future;
  GError *error = NULL;
  char buffer[1];

  future = dex_uring_future_new_read (G_MAXINT, buffer, sizeof buffer, 0);
  dex_uring_future_cqe (future, &cqe);
  dex_uring_future_complete (future);
  g_assert_null (dex_future_get_value (DEX_FUTURE (future), &error));
  g_assert_error (error, G_IO_ERROR, g_io_error_from_errno (EBADF));
  g_clear_error (&error);
  dex_unref (future);

  future = dex_uring_future_new_write (G_MAXINT, buffer, sizeof buffer, 0);
  dex_uring_future_cqe (future, &cqe);
  dex_uring_future_complete (future);
  g_assert_null (dex_future_get_value (DEX_FUTURE (future), &error));
  g_assert_error (error, G_IO_ERROR, g_io_error_from_errno (EBADF));
  g_clear_error (&error);
  dex_unref (future);
}

static void
test_aio_open_uring (void)
{
  DexAioBackend *backend;
  DexAioContext *aio_context;

  if (!(backend = dex_uring_aio_backend_new ()))
    {
      g_test_skip ("io_uring backend is not available");
      return;
    }

  aio_context = dex_aio_backend_create_context (backend);
  g_source_attach ((GSource *)aio_context, NULL);

  run_aio_close_success (aio_context);
  run_aio_close_bad_fd (aio_context);
  run_aio_open_success (aio_context);
  run_aio_open_missing (aio_context);
  run_aio_read_write_bad_fd (aio_context);

  g_source_destroy ((GSource *)aio_context);
  g_source_unref ((GSource *)aio_context);
  dex_unref (backend);
}
#endif

#ifdef G_OS_UNIX
typedef struct
{
  DexAioContext *aio_context;
  DexFuture     *read;
  GThread       *owner;
  int            fd;
  char           buffer[1];
  gboolean       destroyed;
} TestAioCoroutine;

static DexFuture *
test_aio_coroutine_read (DexCoroutineContext *context,
                         gpointer             user_data)
{
  TestAioCoroutine *state = user_data;

  DEX_COROUTINE_BEGIN (context);

  state->read = dex_aio_read (state->aio_context,
                              state->fd,
                              state->buffer,
                              sizeof state->buffer,
                              -1);
  DEX_COROUTINE_SUSPEND (dex_ref (state->read), NULL);
  g_assert_not_reached ();

  DEX_COROUTINE_END;
}

static void
test_aio_coroutine_destroy (gpointer user_data)
{
  TestAioCoroutine *state = user_data;

  g_assert_true (g_thread_self () == state->owner);
  g_assert_true (dex_future_is_resolved (state->read));
  g_assert_cmpint (state->buffer[0], ==, 'x');
  state->destroyed = TRUE;
}

static void
run_aio_coroutine_cancel (DexAioBackend *backend)
{
  TestAioCoroutine state = { 0 };
  DexFuture *coroutine;
  DexWeakRef weak;
  int fds[2];

  g_assert_cmpint (pipe (fds), ==, 0);
  state.aio_context = dex_aio_backend_create_context (backend);
  state.owner = g_thread_self ();
  state.fd = fds[0];
  g_source_attach ((GSource *)state.aio_context, NULL);

  coroutine = dex_scheduler_spawn_coroutine (NULL, test_aio_coroutine_read,
                                              &state, test_aio_coroutine_destroy);
  dex_weak_ref_init (&weak, coroutine);
  while (state.read == NULL)
    g_main_context_iteration (NULL, TRUE);

  dex_future_abandon (coroutine);
  dex_clear (&coroutine);
  g_assert_false (state.destroyed);
  g_assert_true (dex_future_is_pending (state.read));

  g_assert_cmpint (write (fds[1], "x", 1), ==, 1);
  while (!state.destroyed)
    g_main_context_iteration (NULL, TRUE);

  g_assert_null (dex_weak_ref_get (&weak));
  dex_weak_ref_clear (&weak);
  dex_clear (&state.read);
  dex_unref (await_future (dex_aio_close (state.aio_context, fds[0])));
  dex_unref (await_future (dex_aio_close (state.aio_context, fds[1])));
  g_source_destroy ((GSource *)state.aio_context);
  g_source_unref ((GSource *)state.aio_context);
}

static void
test_aio_coroutine_cancel (void)
{
  DexAioBackend *backend = dex_posix_aio_backend_new ();

  run_aio_coroutine_cancel (backend);
  dex_unref (backend);
}

#ifdef HAVE_LIBURING
static void
test_aio_coroutine_cancel_uring (void)
{
  DexAioBackend *backend = dex_uring_aio_backend_new ();

  if (backend == NULL)
    {
      g_test_skip ("io_uring backend is not available");
      return;
    }

  run_aio_coroutine_cancel (backend);
  dex_unref (backend);
}
#endif

#endif

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/Aio/close-success", test_aio_close_success);
  g_test_add_func ("/Dex/TestSuite/Aio/close-bad-fd", test_aio_close_bad_fd);
  g_test_add_func ("/Dex/TestSuite/Aio/open-success", test_aio_open_success);
  g_test_add_func ("/Dex/TestSuite/Aio/open-missing", test_aio_open_missing);
  g_test_add_func ("/Dex/TestSuite/Aio/open-posix", test_aio_open_posix);
#ifdef HAVE_LIBURING
  g_test_add_func ("/Dex/TestSuite/Aio/open-uring", test_aio_open_uring);
  g_test_add_func ("/Dex/TestSuite/Aio/uring-read-write-error-mapping", test_aio_uring_read_write_error_mapping);
#endif
#ifdef G_OS_UNIX
  g_test_add_func ("/Dex/TestSuite/Aio/coroutine-cancel", test_aio_coroutine_cancel);
#ifdef HAVE_LIBURING
  g_test_add_func ("/Dex/TestSuite/Aio/coroutine-cancel-uring", test_aio_coroutine_cancel_uring);
#endif
#endif
  return g_test_run ();
}
