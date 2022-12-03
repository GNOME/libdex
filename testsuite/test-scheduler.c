/* test-scheduler.c
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

#include <libdex.h>

static GMainLoop *main_loop;

static void
test_main_scheduler_simple_cb (gpointer data)
{
  gboolean *count = data;
  *count = 123;
  g_main_loop_quit (main_loop);
}

static void
test_main_scheduler_simple (void)
{
  DexScheduler *scheduler = dex_scheduler_get_default ();
  gboolean count = 0;

  g_assert_nonnull (scheduler);
  g_assert_true (DEX_IS_MAIN_SCHEDULER (scheduler));

  main_loop = g_main_loop_new (NULL, FALSE);
  dex_scheduler_push (scheduler,
                      test_main_scheduler_simple_cb,
                      &count);
  g_main_loop_run (main_loop);
  g_clear_pointer (&main_loop, g_main_loop_unref);

  g_assert_cmpint (count, ==, 123);
}

static DexFuture *
test_main_scheduler_routine_func (gpointer user_data)
{
  DexCancellable *cancellable = dex_cancellable_new ();
  dex_cancellable_cancel (cancellable);
  return DEX_FUTURE (cancellable);
}

static DexFuture *
test_main_scheduler_routine_quit (DexFuture *future,
                                  gpointer   user_data)
{
  GError *error = NULL;
  const GValue *value = dex_future_get_value (future, &error);

  g_assert_null (value);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

  g_main_loop_quit (main_loop);

  return NULL;
}

static void
test_main_scheduler_routine (void)
{
  DexFuture *future;

  main_loop = g_main_loop_new (NULL, FALSE);

  future = dex_scheduler_spawn (NULL, test_main_scheduler_routine_func, NULL, NULL);
  future = dex_future_finally (future, test_main_scheduler_routine_quit, NULL, NULL);

  g_main_loop_run (main_loop);
  g_clear_pointer (&main_loop, g_main_loop_unref);

  dex_unref (future);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/MainScheduler/simple", test_main_scheduler_simple);
  g_test_add_func ("/Dex/TestSuite/MainScheduler/routine", test_main_scheduler_routine);
  return g_test_run ();
}
