/* test-limiter.c
 *
 * Copyright 2026 Christian Hergert
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <libdex.h>

typedef DexFuture *(*TestFiberFunc) (gpointer user_data);

typedef struct
{
  GMainLoop *main_loop;
  DexFuture *future;
} TestRun;

typedef struct
{
  DexLimiter *limiter;
  int active;
  int max_active;
  int completed;
} RunState;

typedef struct
{
  RunState *state;
  guint value;
} ItemState;

static DexFuture *
test_quit_cb (DexFuture *future,
              gpointer   user_data)
{
  TestRun *run = user_data;

  g_main_loop_quit (run->main_loop);

  return NULL;
}

static void
run_test_fiber (TestFiberFunc func,
                gpointer      user_data)
{
  TestRun run = {0};

  run.main_loop = g_main_loop_new (NULL, FALSE);
  run.future = dex_scheduler_spawn (NULL, 0, func, user_data, NULL);
  run.future = dex_future_finally (run.future, test_quit_cb, &run, NULL);

  g_main_loop_run (run.main_loop);

  while (dex_future_is_pending (run.future))
    g_main_context_iteration (NULL, TRUE);

  g_assert_true (dex_future_is_resolved (run.future));

  dex_clear (&run.future);
  g_main_loop_unref (run.main_loop);
}

static DexFuture *
test_limiter_basic_fiber (gpointer user_data)
{
  g_autoptr(DexLimiter) limiter = dex_limiter_new (1);
  g_autoptr(GError) error = NULL;
  DexFuture *second;

  g_assert_true (DEX_IS_LIMITER (limiter));
  g_assert_cmpuint (dex_limiter_get_max_concurrency (limiter), ==, 1);

  g_assert_true (dex_await (dex_limiter_acquire (limiter), &error));
  g_assert_no_error (error);

  second = dex_limiter_acquire (limiter);
  g_assert_true (dex_future_is_pending (second));

  dex_limiter_release (limiter);
  g_assert_true (dex_await (second, &error));
  g_assert_no_error (error);

  dex_limiter_release (limiter);

  return dex_future_new_true ();
}

static void
test_limiter_basic (void)
{
  run_test_fiber (test_limiter_basic_fiber, NULL);
}

static DexFuture *
test_limiter_run_item (gpointer user_data)
{
  ItemState *item = user_data;
  RunState *state = item->state;
  int active;
  int max_active;

  active = g_atomic_int_add (&state->active, 1) + 1;

  while (active > (max_active = g_atomic_int_get (&state->max_active)))
    {
      if (g_atomic_int_compare_and_exchange (&state->max_active, max_active, active))
        break;
    }

  g_assert_cmpuint (active, <=, dex_limiter_get_max_concurrency (state->limiter));

  dex_await (dex_timeout_new_msec (5), NULL);

  g_atomic_int_dec_and_test (&state->active);
  g_atomic_int_inc (&state->completed);

  return dex_future_new_for_uint (item->value);
}

static DexFuture *
test_limiter_run_fiber (gpointer user_data)
{
  g_autoptr(DexLimiter) limiter = dex_limiter_new (3);
  g_autoptr(GPtrArray) futures = NULL;
  RunState state = {0};

  state.limiter = limiter;
  futures = g_ptr_array_new_with_free_func (dex_unref);

  for (guint i = 0; i < 24; i++)
    {
      ItemState *item = g_new0 (ItemState, 1);

      item->state = &state;
      item->value = i;
      g_ptr_array_add (futures,
                       dex_limiter_run (limiter,
                                        NULL,
                                        0,
                                        test_limiter_run_item,
                                        item,
                                        g_free));
    }

  dex_await (dex_future_allv ((DexFuture **)futures->pdata, futures->len), NULL);

  g_assert_cmpuint (g_atomic_int_get (&state.completed), ==, futures->len);
  g_assert_cmpuint (g_atomic_int_get (&state.max_active), ==, 3);
  g_assert_cmpuint (g_atomic_int_get (&state.active), ==, 0);

  return dex_future_new_true ();
}

static void
test_limiter_run (void)
{
  run_test_fiber (test_limiter_run_fiber, NULL);
}

static DexFuture *
test_limiter_error_item (gpointer user_data)
{
  return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_FAILED, "Failed");
}

static DexFuture *
test_limiter_run_error_fiber (gpointer user_data)
{
  g_autoptr(DexLimiter) limiter = dex_limiter_new (1);
  g_autoptr(GError) error = NULL;
  DexFuture *future;

  future = dex_limiter_run (limiter, NULL, 0, test_limiter_error_item, NULL, NULL);

  g_assert_false (dex_await (future, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error (&error);

  g_assert_true (dex_await (dex_limiter_acquire (limiter), &error));
  g_assert_no_error (error);
  dex_limiter_release (limiter);

  return dex_future_new_true ();
}

static void
test_limiter_run_error (void)
{
  run_test_fiber (test_limiter_run_error_fiber, NULL);
}

static DexFuture *
test_limiter_not_reached (DexFuture *future,
                          gpointer   user_data)
{
  g_assert_not_reached ();
  return dex_future_new_true ();
}

static DexFuture *
test_limiter_discard_waiting_fiber (gpointer user_data)
{
  g_autoptr(DexLimiter) limiter = dex_limiter_new (1);
  g_autoptr(GError) error = NULL;
  DexFuture *block;
  DexFuture *second;
  DexFuture *third;

  g_assert_true (dex_await (dex_limiter_acquire (limiter), &error));
  g_assert_no_error (error);

  second = dex_limiter_acquire (limiter);
  block = dex_future_then (second, test_limiter_not_reached, NULL, NULL);
  dex_clear (&block);

  dex_limiter_release (limiter);
  dex_await (dex_timeout_new_msec (10), NULL);

  third = dex_limiter_acquire (limiter);
  g_assert_true (dex_await (third, &error));
  g_assert_no_error (error);
  dex_limiter_release (limiter);

  return dex_future_new_true ();
}

static void
test_limiter_discard_waiting (void)
{
  run_test_fiber (test_limiter_discard_waiting_fiber, NULL);
}

static DexFuture *
test_limiter_discard_run_item (gpointer user_data)
{
  int *completed = user_data;

  dex_await (dex_timeout_new_msec (10), NULL);
  g_atomic_int_inc (completed);

  return dex_future_new_true ();
}

static DexFuture *
test_limiter_discard_run_fiber (gpointer user_data)
{
  g_autoptr(DexLimiter) limiter = dex_limiter_new (1);
  g_autoptr(GError) error = NULL;
  DexFuture *block;
  DexFuture *future;
  int completed = 0;

  future = dex_limiter_run (limiter,
                            NULL,
                            0,
                            test_limiter_discard_run_item,
                            &completed,
                            NULL);
  block = dex_future_then (future, test_limiter_not_reached, NULL, NULL);

  dex_await (dex_timeout_new_msec (1), NULL);

  dex_clear (&block);

  dex_await (dex_timeout_new_msec (20), NULL);

  g_assert_cmpint (g_atomic_int_get (&completed), ==, 1);
  g_assert_true (dex_await (dex_limiter_acquire (limiter), &error));
  g_assert_no_error (error);
  dex_limiter_release (limiter);

  return dex_future_new_true ();
}

static void
test_limiter_discard_run (void)
{
  run_test_fiber (test_limiter_discard_run_fiber, NULL);
}

static DexFuture *
test_limiter_close_fiber (gpointer user_data)
{
  g_autoptr(DexLimiter) limiter = dex_limiter_new (1);
  g_autoptr(GError) error = NULL;
  DexFuture *future;

  g_assert_true (dex_await (dex_limiter_acquire (limiter), &error));
  g_assert_no_error (error);

  future = dex_limiter_acquire (limiter);
  dex_limiter_close (limiter);

  g_assert_false (dex_await (future, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_SEMAPHORE_CLOSED);
  g_clear_error (&error);

  g_assert_false (dex_await (dex_limiter_acquire (limiter), &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_SEMAPHORE_CLOSED);

  dex_limiter_release (limiter);

  return dex_future_new_true ();
}

static void
test_limiter_close (void)
{
  run_test_fiber (test_limiter_close_fiber, NULL);
}

int
main (int argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/Dex/TestSuite/Limiter/basic", test_limiter_basic);
  g_test_add_func ("/Dex/TestSuite/Limiter/run", test_limiter_run);
  g_test_add_func ("/Dex/TestSuite/Limiter/run_error", test_limiter_run_error);
  g_test_add_func ("/Dex/TestSuite/Limiter/discard_waiting", test_limiter_discard_waiting);
  g_test_add_func ("/Dex/TestSuite/Limiter/discard_run", test_limiter_discard_run);
  g_test_add_func ("/Dex/TestSuite/Limiter/close", test_limiter_close);

  return g_test_run ();
}
