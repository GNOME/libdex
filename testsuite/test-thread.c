/* test-thread.c
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

typedef struct
{
  char *str1;
  char *str2;
} State;

static void
state_free (State *state)
{
  g_free (state->str1);
  g_free (state->str2);
  g_free (state);
}

static DexFuture *
thread_func (gpointer data)
{
  State *state = data;

  g_assert_cmpstr (state->str1, ==, "string1");
  g_assert_cmpstr (state->str2, ==, "string2");

  return dex_future_new_take_string (g_strdup ("string3"));
}

static DexFuture *
after_thread_func (DexFuture *completed,
                   gpointer   user_data)
{
  GMainLoop *main_loop = user_data;
  GError *error = NULL;
  const GValue *value;

  value = dex_future_get_value (completed, &error);

  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_STRING (value));
  g_assert_cmpstr ("string3", ==, g_value_get_string (value));

  g_main_loop_quit (main_loop);

  return dex_future_new_true ();
}

static void
test_thread_spawn (void)
{
  GMainLoop *main_loop = g_main_loop_new (NULL, FALSE);
  State *state;

  state = g_new0 (State, 1);
  state->str1 = g_strdup ("string1");
  state->str2 = g_strdup ("string2");

  dex_future_disown (dex_future_finally (dex_thread_spawn ("[test-thread]",
                                                           thread_func,
                                                           state,
                                                           (GDestroyNotify) state_free),
                                         after_thread_func,
                                         g_main_loop_ref (main_loop),
                                         (GDestroyNotify) g_main_loop_unref));

  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);
}

static DexFuture *
test_thread_wait_for_thread (gpointer data)
{
  DexPromise *promise = data;
  GError *error = NULL;

  g_assert_true (dex_thread_wait_for (dex_ref (promise), &error));
  g_assert_no_error (error);

  return dex_future_new_for_int (123);
}

static DexFuture *
quit_main (DexFuture *future,
           gpointer   user_data)
{
  GMainLoop *main_loop = user_data;
  g_main_loop_quit (main_loop);
  return dex_ref (future);
}

static gboolean
do_resolve (gpointer data)
{
  DexPromise *promise = data;
  dex_promise_resolve_boolean (promise, TRUE);
  return G_SOURCE_REMOVE;
}

static void
test_thread_wait_for (void)
{
  GMainLoop *main_loop = g_main_loop_new (NULL, FALSE);
  DexPromise *promise = dex_promise_new ();
  DexFuture *thread;
  const GValue *value;

  thread = dex_thread_spawn ("[test-wait-for]",
                             test_thread_wait_for_thread,
                             dex_ref (promise),
                             dex_unref);

  dex_future_disown (dex_future_finally (dex_ref (thread),
                                         quit_main,
                                         g_main_loop_ref (main_loop),
                                         (GDestroyNotify) g_main_loop_unref));

  g_idle_add_full (G_PRIORITY_DEFAULT,
                   do_resolve,
                   dex_ref (promise),
                   dex_unref);

  g_main_loop_run (main_loop);

  g_assert_true (dex_future_is_resolved (thread));
  value = dex_future_get_value (thread, NULL);
  g_assert_true (G_VALUE_HOLDS_INT (value));
  g_assert_cmpint (123, ==, g_value_get_int (value));

  dex_unref (promise);
  dex_unref (thread);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  g_main_loop_unref (main_loop);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/Thread/spawn", test_thread_spawn);
  g_test_add_func ("/Dex/TestSuite/Thread/wait_for", test_thread_wait_for);
  return g_test_run ();
}
