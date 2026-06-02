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

#include <fcntl.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <glib/gstdio.h>

#include <libdex.h>

#include "dex-aio-backend-private.h"
#include "dex-posix-aio-backend-private.h"

#ifdef HAVE_LIBURING
# include "dex-uring-aio-backend-private.h"
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
run_aio_open_success (DexAioContext *aio_context)
{
  static const char contents[] = "libdex aio open";
  g_autofree char *path = NULL;
  g_autoptr(GError) error = NULL;
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

  g_assert_cmpint (g_unlink (path), ==, 0);
}

static void
run_aio_open_missing (DexAioContext *aio_context)
{
  g_autofree char *path = NULL;
  g_autoptr(GError) error = NULL;
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

  run_aio_open_success (aio_context);
  run_aio_open_missing (aio_context);

  g_source_destroy ((GSource *)aio_context);
  g_source_unref ((GSource *)aio_context);
  dex_unref (backend);
}

#ifdef HAVE_LIBURING
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

  run_aio_open_success (aio_context);
  run_aio_open_missing (aio_context);

  g_source_destroy ((GSource *)aio_context);
  g_source_unref ((GSource *)aio_context);
  dex_unref (backend);
}
#endif

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/Aio/open-success", test_aio_open_success);
  g_test_add_func ("/Dex/TestSuite/Aio/open-missing", test_aio_open_missing);
  g_test_add_func ("/Dex/TestSuite/Aio/open-posix", test_aio_open_posix);
#ifdef HAVE_LIBURING
  g_test_add_func ("/Dex/TestSuite/Aio/open-uring", test_aio_open_uring);
#endif
  return g_test_run ();
}
