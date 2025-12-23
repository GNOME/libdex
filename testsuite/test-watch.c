/* test-watch.c
 *
 * Copyright 2025 Christian Hergert
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

#include <libdex.h>
#include <gio/gio.h>
#include <unistd.h>

#include "dex-watch-private.h"

#define ASSERT_STATUS(f,status) g_assert_cmpint(status, ==, dex_future_get_status(DEX_FUTURE(f)))

static DexFuture *
quit_cb (DexFuture *future,
         gpointer   user_data)
{
  g_main_loop_quit (user_data);
  return NULL;
}

static void
test_watch_read_ready (void)
{
  int pipefd[2];
  DexFuture *watch;
  GMainLoop *main_loop;
  GError *error = NULL;
  const GValue *value;
  int revents;

  g_assert_cmpint (pipe (pipefd), ==, 0);

  /* Watch for read readiness */
  watch = dex_watch_new (pipefd[0], G_IO_IN);
  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_PENDING);

  main_loop = g_main_loop_new (NULL, FALSE);
  watch = dex_future_then (watch, quit_cb, main_loop, NULL);

  /* Write data to make read side ready */
  g_assert_cmpint (write (pipefd[1], "test", 4), ==, 4);

  g_main_loop_run (main_loop);

  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_RESOLVED);
  value = dex_future_get_value (watch, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_INT (value));
  revents = g_value_get_int (value);
  g_assert_cmpint (revents & G_IO_IN, !=, 0);

  close (pipefd[0]);
  close (pipefd[1]);
  g_main_loop_unref (main_loop);
  dex_unref (watch);
}

static void
test_watch_write_ready (void)
{
  int pipefd[2];
  DexFuture *watch;
  GMainLoop *main_loop;
  GError *error = NULL;
  const GValue *value;
  int revents;

  g_assert_cmpint (pipe (pipefd), ==, 0);

  /* Watch for write readiness (pipe should be writable immediately) */
  watch = dex_watch_new (pipefd[1], G_IO_OUT);
  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_PENDING);

  main_loop = g_main_loop_new (NULL, FALSE);
  watch = dex_future_then (watch, quit_cb, main_loop, NULL);

  g_main_loop_run (main_loop);

  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_RESOLVED);
  value = dex_future_get_value (watch, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_INT (value));
  revents = g_value_get_int (value);
  g_assert_cmpint (revents & G_IO_OUT, !=, 0);

  close (pipefd[0]);
  close (pipefd[1]);
  g_main_loop_unref (main_loop);
  dex_unref (watch);
}

static void
test_watch_write_side_closed (void)
{
  int pipefd[2];
  DexFuture *watch;
  GMainLoop *main_loop;
  GError *error = NULL;
  const GValue *value;
  int revents;

  g_assert_cmpint (pipe (pipefd), ==, 0);

  /* Watch for read readiness and hangup */
  watch = dex_watch_new (pipefd[0], G_IO_IN);
  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_PENDING);

  /* Close write side, which should trigger HUP on read side */
  close (pipefd[1]);

  main_loop = g_main_loop_new (NULL, FALSE);
  watch = dex_future_then (watch, quit_cb, main_loop, NULL);

  g_main_loop_run (main_loop);

  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_RESOLVED);
  value = dex_future_get_value (watch, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_INT (value));
  revents = g_value_get_int (value);
  g_assert_cmpint (revents & G_IO_HUP, !=, 0);

  close (pipefd[0]);
  g_main_loop_unref (main_loop);
  dex_unref (watch);
}

static void
test_watch_read_waiting_write_closed (void)
{
  int pipefd[2];
  DexFuture *watch;
  GMainLoop *main_loop;
  GError *error = NULL;
  const GValue *value;
  int revents;

  g_assert_cmpint (pipe (pipefd), ==, 0);

  /* Watch for read readiness only (not explicitly HUP) */
  watch = dex_watch_new (pipefd[0], G_IO_IN);
  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_PENDING);

  main_loop = g_main_loop_new (NULL, FALSE);
  watch = dex_future_then (watch, quit_cb, main_loop, NULL);

  /* Close write side, which should trigger HUP on read side */
  close (pipefd[1]);

  g_main_loop_run (main_loop);

  /* Future should complete even though we only asked for G_IO_IN */
  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_RESOLVED);
  value = dex_future_get_value (watch, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_INT (value));
  revents = g_value_get_int (value);
  /* When write side closes, read side gets HUP */
  g_assert_cmpint (revents & G_IO_HUP, !=, 0);

  close (pipefd[0]);
  g_main_loop_unref (main_loop);
  dex_unref (watch);
}

static void
test_watch_read_side_closed (void)
{
  int pipefd[2];
  DexFuture *watch;
  GMainLoop *main_loop;
  GError *error = NULL;
  const GValue *value;
  int revents;

  g_assert_cmpint (pipe (pipefd), ==, 0);

  /* Watch for write readiness and hangup */
  watch = dex_watch_new (pipefd[1], G_IO_OUT | G_IO_HUP);
  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_PENDING);

  main_loop = g_main_loop_new (NULL, FALSE);
  watch = dex_future_then (watch, quit_cb, main_loop, NULL);

  /* Close read side, which should trigger HUP on write side */
  close (pipefd[0]);

  g_main_loop_run (main_loop);

  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_RESOLVED);
  value = dex_future_get_value (watch, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_INT (value));
  revents = g_value_get_int (value);
  /* When read side is closed, write side gets HUP or ERR */
  g_assert_true ((revents & G_IO_HUP) != 0 || (revents & G_IO_ERR) != 0);

  close (pipefd[1]);
  g_main_loop_unref (main_loop);
  dex_unref (watch);
}

static void
test_watch_multiple_events (void)
{
  int pipefd[2];
  DexFuture *watch;
  GMainLoop *main_loop;
  GError *error = NULL;
  const GValue *value;
  int revents;

  g_assert_cmpint (pipe (pipefd), ==, 0);

  /* Watch for both read and write readiness */
  watch = dex_watch_new (pipefd[1], G_IO_OUT | G_IO_IN);
  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_PENDING);

  main_loop = g_main_loop_new (NULL, FALSE);
  watch = dex_future_then (watch, quit_cb, main_loop, NULL);

  /* Write side should be immediately writable */
  g_main_loop_run (main_loop);

  ASSERT_STATUS (watch, DEX_FUTURE_STATUS_RESOLVED);
  value = dex_future_get_value (watch, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_INT (value));
  revents = g_value_get_int (value);
  g_assert_cmpint (revents & G_IO_OUT, !=, 0);

  close (pipefd[0]);
  close (pipefd[1]);
  g_main_loop_unref (main_loop);
  dex_unref (watch);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/Watch/read_ready", test_watch_read_ready);
  g_test_add_func ("/Dex/TestSuite/Watch/write_ready", test_watch_write_ready);
  g_test_add_func ("/Dex/TestSuite/Watch/write_side_closed", test_watch_write_side_closed);
  g_test_add_func ("/Dex/TestSuite/Watch/read_waiting_write_closed", test_watch_read_waiting_write_closed);
  g_test_add_func ("/Dex/TestSuite/Watch/read_side_closed", test_watch_read_side_closed);
  g_test_add_func ("/Dex/TestSuite/Watch/multiple_events", test_watch_multiple_events);
  return g_test_run ();
}
