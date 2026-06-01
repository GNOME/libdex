/* test-thread-pool.c
 *
 * Copyright 2026 Christian Hergert
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <libdex.h>

typedef struct
{
  GMainLoop *main_loop;
  DexThreadPool *pool;
  DexFuture *close1;
  DexFuture *close2;
} CloseState;

static DexFuture *
sleep_thread_func (gpointer data)
{
  g_usleep (100000);
  return dex_future_new_true ();
}

static gboolean
quit_main_loop (gpointer data)
{
  GMainLoop *main_loop = data;

  g_main_loop_quit (main_loop);
  return G_SOURCE_REMOVE;
}

static DexFuture *
close_future_cb (DexFuture *future,
                 gpointer   user_data)
{
  CloseState *state = user_data;
  GError *error = NULL;
  const GValue *value;

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_BOOLEAN (value));
  g_assert_true (g_value_get_boolean (value));

  g_main_context_invoke (NULL, quit_main_loop, state->main_loop);

  return NULL;
}

static void
test_thread_pool_close_is_single_flight (void)
{
  CloseState state = {0};

  state.main_loop = g_main_loop_new (NULL, FALSE);
  state.pool = dex_thread_pool_new (2);

  dex_future_disown (dex_thread_pool_submit (state.pool,
                                             "[test-thread-pool]",
                                             sleep_thread_func,
                                             NULL,
                                             NULL));

  state.close1 = dex_thread_pool_close (state.pool,
                                        DEX_THREAD_POOL_SHUTDOWN_DRAIN);
  state.close2 = dex_thread_pool_close (state.pool,
                                        DEX_THREAD_POOL_SHUTDOWN_DRAIN);

  g_assert_true (state.close1 == state.close2);

  dex_future_disown (dex_future_finally (dex_ref (state.close1),
                                         close_future_cb,
                                         &state,
                                         NULL));

  g_main_loop_run (state.main_loop);

  g_assert_true (dex_future_is_resolved (state.close1));
  g_assert_true (dex_future_is_resolved (state.close2));

  dex_unref (state.close1);
  dex_unref (state.close2);
  dex_unref (state.pool);
  g_main_loop_unref (state.main_loop);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/ThreadPool/close_is_single_flight",
                   test_thread_pool_close_is_single_flight);
  return g_test_run ();
}
