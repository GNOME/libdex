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

  dex_scheduler_push (scheduler,
                      test_main_scheduler_simple_cb,
                      &count);

  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);

  g_assert_cmpint (count, ==, 123);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  main_loop = g_main_loop_new (NULL, FALSE);
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/MainScheduler/simple", test_main_scheduler_simple);
  return g_test_run ();
}
