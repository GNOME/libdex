/* test-coroutine.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libdex.h>
#include <string.h>

#include "test-util.h"

DEX_DEFINE_CLOSURE_TYPE (TestCoroutineCompleteState, test_coroutine_complete_state,
                         DEX_DEFINE_CLOSURE_POINTER (guint *, ran, g_free))

DEX_DEFINE_CLOSURE_TYPE (TestCoroutineSuspendState, test_coroutine_suspend_state,
                         DEX_DEFINE_CLOSURE_POINTER (DexPromise *, gate, dex_unref),
                         DEX_DEFINE_CLOSURE_POINTER (gboolean *, saw_result, g_free),
                         DEX_DEFINE_CLOSURE_POINTER (guint *, run_count, g_free),
                         DEX_DEFINE_CLOSURE_VALUE (gboolean, gate_value))

DEX_DEFINE_CLOSURE_TYPE (TestCoroutineSuspendVoidState, test_coroutine_suspend_void_state,
                         DEX_DEFINE_CLOSURE_POINTER (DexPromise *, gate, dex_unref),
                         DEX_DEFINE_CLOSURE_POINTER (guint *, run_count, g_free))

DEX_DEFINE_CLOSURE_TYPE (TestCoroutineTaskGroupCancelState,
                         test_coroutine_task_group_cancel_state,
                         DEX_DEFINE_CLOSURE_POINTER (DexPromise *, gate, dex_unref))
DEX_DEFINE_CLOSURE_TYPE (TestCoroutineDestroyState,
                         test_coroutine_destroy_state,
                         DEX_DEFINE_CLOSURE_VALUE (gboolean, value))

static gint test_coroutine_destroy_state_freed = 0;

static DexFuture *
test_coroutine_complete_func (DexCoroutineContext *context,
                              gpointer             user_data)
{
  TestCoroutineCompleteState *state = user_data;

  DEX_COROUTINE_BEGIN (context);

  if (state->ran != NULL)
    *state->ran = 1;

  return dex_future_new_for_int (123);

  DEX_COROUTINE_END;
}

static DexFuture *
test_coroutine_suspend_func (DexCoroutineContext *context,
                             gpointer             user_data)
{
  TestCoroutineSuspendState *state = user_data;
  GError *error = NULL;

  DEX_COROUTINE_BEGIN (context);

  (*state->run_count)++;

  DEX_COROUTINE_SUSPEND_BOOLEAN (&state->gate_value,
                                 &error,
                                 dex_ref (DEX_FUTURE (state->gate)));

  if (error != NULL)
    return dex_future_new_for_error (g_steal_pointer (&error));

  (*state->run_count)++;

  if (state->saw_result != NULL)
    *state->saw_result = state->gate_value;

  return dex_future_new_for_boolean (state->gate_value);

  DEX_COROUTINE_END;
}

static DexFuture *
test_coroutine_suspend_void_func (DexCoroutineContext *context,
                                  gpointer             user_data)
{
  TestCoroutineSuspendVoidState *state = user_data;

  DEX_COROUTINE_BEGIN (context);

  (*state->run_count)++;
  DEX_COROUTINE_SUSPEND (dex_ref (DEX_FUTURE (state->gate)), NULL);
  (*state->run_count)++;

  return dex_future_new_true ();

  DEX_COROUTINE_END;
}

static DexFuture *
test_coroutine_task_group_cancel_func (DexCoroutineContext *context,
                                       gpointer             user_data)
{
  TestCoroutineTaskGroupCancelState *state = user_data;
  GError *error = NULL;
  gboolean value = FALSE;

  DEX_COROUTINE_BEGIN (context);

  DEX_COROUTINE_SUSPEND_BOOLEAN (&value, &error, dex_ref (DEX_FUTURE (state->gate)));
  if (error != NULL)
    return dex_future_new_for_error (g_steal_pointer (&error));

  return dex_future_new_for_boolean (value);

  DEX_COROUTINE_END;
}

static DexFuture *
test_coroutine_return_int_func (DexCoroutineContext *context,
                                gpointer             user_data)
{
  return dex_future_new_for_int (123);
}

static DexFuture *
test_coroutine_return_uint_func (DexCoroutineContext *context,
                                 gpointer             user_data)
{
  return dex_future_new_for_uint (123u);
}

static DexFuture *
test_coroutine_return_boolean_func (DexCoroutineContext *context,
                                    gpointer             user_data)
{
  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
test_coroutine_return_double_func (DexCoroutineContext *context,
                                   gpointer             user_data)
{
  return dex_future_new_for_double (3.25);
}

static DexFuture *
test_coroutine_return_float_func (DexCoroutineContext *context,
                                  gpointer             user_data)
{
  return dex_future_new_for_float (6.5f);
}

static DexFuture *
test_coroutine_return_object_func (DexCoroutineContext *context,
                                   gpointer             user_data)
{
  GObject *object = g_object_new (G_TYPE_OBJECT, NULL);
  DexFuture *future = dex_future_new_take_object (object);

  return future;
}

static DexFuture *
test_coroutine_return_boxed_func (DexCoroutineContext *context,
                                  gpointer             user_data)
{
  GBytes *boxed = g_bytes_new_static ("boxed", 5);
  GValue value = G_VALUE_INIT;
  DexFuture *future;

  g_value_init (&value, G_TYPE_BYTES);
  g_value_take_boxed (&value, boxed);
  future = dex_future_new_for_value (&value);
  g_value_unset (&value);

  return future;
}

static DexFuture *
test_coroutine_return_pointer_func (DexCoroutineContext *context,
                                    gpointer             user_data)
{
  return dex_future_new_for_pointer (GSIZE_TO_POINTER (0xdeadbeef));
}

static DexFuture *
test_coroutine_thread_pool_return_uint_func (DexCoroutineContext *context,
                                             gpointer             user_data)
{
  return dex_future_new_for_uint (7u);
}

static void
test_coroutine_spawn_complete (void)
{
  TestCoroutineCompleteState *state;
  DexFuture *future;
  int value;

  state = test_coroutine_complete_state_new ();
  state->ran = g_new0 (guint, 1);

  future = dex_scheduler_spawn_coroutine (NULL,
                                          test_coroutine_complete_func,
                                          state,
                                          NULL);

  value = dex_await_int (dex_ref (future), NULL);
  g_assert_cmpint (value, ==, 123);
  g_assert_cmpuint (*state->ran, ==, 1);
  test_coroutine_complete_state_free (state);
  dex_clear (&future);
}

static void
test_coroutine_destroy_state_destroy_cb (gpointer user_data)
{
  g_atomic_int_inc (&test_coroutine_destroy_state_freed);
  g_free (user_data);
}

static DexFuture *
test_coroutine_destroy_func (DexCoroutineContext *context,
                             gpointer             user_data)
{
  TestCoroutineDestroyState *state = user_data;

  DEX_COROUTINE_BEGIN (context);

  return dex_future_new_for_boolean (state->value);

  DEX_COROUTINE_END;
}

static void
test_coroutine_destroy_state (void)
{
  TestCoroutineDestroyState *state;
  DexFuture *future;
  GError *error = NULL;
  gboolean value;

  g_atomic_int_set (&test_coroutine_destroy_state_freed, 0);

  state = test_coroutine_destroy_state_new ();
  state->value = TRUE;

  future = dex_scheduler_spawn_coroutine (NULL,
                                         test_coroutine_destroy_func,
                                         state,
                                         (GDestroyNotify) test_coroutine_destroy_state_destroy_cb);

  value = dex_await_boolean (dex_ref (future), &error);
  g_assert_no_error (error);
  g_assert_true (value);
  g_assert_cmpint (g_atomic_int_get (&test_coroutine_destroy_state_freed), ==, 1);

  dex_clear (&future);
}

typedef struct _TestCoroutineCancelRaceState
{
  DexFuture  *future;
  DexPromise *gate;
  gint        entered;
} TestCoroutineCancelRaceState;

static DexFuture *
test_coroutine_cancel_race_func (DexCoroutineContext *context,
                                 gpointer             user_data)
{
  TestCoroutineCancelRaceState *state = user_data;
  GError *error = NULL;
  gboolean value = FALSE;

  DEX_COROUTINE_BEGIN (context);

  g_atomic_int_set (&state->entered, 1);
  DEX_COROUTINE_SUSPEND_BOOLEAN (&value, &error, dex_ref (DEX_FUTURE (state->gate)));
  if (error != NULL)
    return dex_future_new_for_error (g_steal_pointer (&error));

  return dex_future_new_for_boolean (value);

  DEX_COROUTINE_END;
}

static void
test_coroutine_cancel_race (void)
{
  GError *error = NULL;

  for (guint i = 0; i < 128; i++)
    {
      TestCoroutineCancelRaceState state = {0};
      DexFuture *future;
      DexTaskGroup *group;

      state.gate = dex_promise_new ();
      state.future = future = dex_scheduler_spawn_coroutine (NULL,
                                                             test_coroutine_cancel_race_func,
                                                             &state,
                                                             NULL);
      group = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
      g_assert_true (dex_task_group_add (group, dex_ref (future)));

      while (g_atomic_int_get (&state.entered) == 0)
        g_main_context_iteration (NULL, TRUE);

      dex_task_group_cancel (group);
      dex_promise_resolve_boolean (state.gate, TRUE);

      g_assert_false (dex_await_boolean (dex_ref (future), &error));
      g_assert_error (error, DEX_ERROR, DEX_ERROR_FIBER_CANCELLED);
      g_clear_error (&error);

      dex_clear (&group);
      dex_clear (&state.gate);
      dex_clear (&future);
      state.entered = 0;
    }
}

static void
test_coroutine_suspend_resume (void)
{
  TestCoroutineSuspendState *state;
  DexFuture *future;
  GError *error = NULL;
  gboolean value;

  state = test_coroutine_suspend_state_new ();
  state->gate = dex_promise_new ();
  state->saw_result = g_new0 (gboolean, 1);
  state->run_count = g_new0 (guint, 1);

  future = dex_scheduler_spawn_coroutine (NULL,
                                          test_coroutine_suspend_func,
                                          state,
                                          NULL);

  while (*state->run_count == 0)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpuint (*state->run_count, ==, 1);

  dex_promise_resolve_boolean (state->gate, TRUE);
  value = dex_await_boolean (dex_ref (future), &error);
  g_assert_no_error (error);
  g_assert_true (value);
  g_assert_cmpuint (*state->run_count, ==, 2);
  g_assert_true (*state->saw_result);

  test_coroutine_suspend_state_free (state);
  dex_clear (&future);
}

static void
test_coroutine_suspend_void (void)
{
  TestCoroutineSuspendVoidState *state;
  DexFuture *future;
  gboolean value;

  state = test_coroutine_suspend_void_state_new ();
  state->gate = dex_promise_new ();
  state->run_count = g_new0 (guint, 1);

  future = dex_scheduler_spawn_coroutine (NULL,
                                          test_coroutine_suspend_void_func,
                                          state,
                                          NULL);

  while (*state->run_count == 0)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpuint (*state->run_count, ==, 1);

  dex_promise_resolve_boolean (state->gate, TRUE);
  value = dex_await_boolean (dex_ref (future), NULL);
  g_assert_true (value);
  g_assert_cmpuint (*state->run_count, ==, 2);

  test_coroutine_suspend_void_state_free (state);
  dex_clear (&future);
}

static void
test_coroutine_returns_int (void)
{
  GError *error = NULL;
  int value;

  value = dex_await_int (dex_scheduler_spawn_coroutine (NULL,
                                                        test_coroutine_return_int_func,
                                                        NULL,
                                                        NULL),
                         &error);
  g_assert_no_error (error);
  g_assert_cmpint (value, ==, 123);
}

static void
test_coroutine_returns_uint (void)
{
  GError *error = NULL;
  guint value;

  value = dex_await_uint (dex_scheduler_spawn_coroutine (NULL, test_coroutine_return_uint_func, NULL, NULL), &error);
  g_assert_no_error (error);
  g_assert_cmpuint (value, ==, 123u);
}

static void
test_coroutine_returns_boolean (void)
{
  GError *error = NULL;
  gboolean value;

  value = dex_await_boolean (dex_scheduler_spawn_coroutine (NULL, test_coroutine_return_boolean_func, NULL, NULL), &error);
  g_assert_no_error (error);
  g_assert_true (value);
}

static void
test_coroutine_returns_double (void)
{
  GError *error = NULL;
  double value;

  value = dex_await_double (dex_scheduler_spawn_coroutine (NULL, test_coroutine_return_double_func, NULL, NULL), &error);
  g_assert_no_error (error);
  g_assert_cmpfloat_with_epsilon (value, 3.25, 0.0001);
}

static void
test_coroutine_returns_float (void)
{
  GError *error = NULL;
  float value;

  value = dex_await_float (dex_scheduler_spawn_coroutine (NULL, test_coroutine_return_float_func, NULL, NULL), &error);
  g_assert_no_error (error);
  g_assert_cmpfloat_with_epsilon (value, 6.5f, 0.0001);
}

static void
test_coroutine_returns_object (void)
{
  GError *error = NULL;
  GObject *value = NULL;

  value = dex_await_object (dex_scheduler_spawn_coroutine (NULL,
                                                           test_coroutine_return_object_func,
                                                           NULL,
                                                           NULL),
                            &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_IS_OBJECT (value));

  g_clear_object (&value);
}

static void
test_coroutine_returns_boxed (void)
{
  GError *error = NULL;
  GBytes *boxed = NULL;
  const char *boxed_data;
  gsize length = 0;

  boxed = dex_await_boxed (dex_scheduler_spawn_coroutine (NULL, test_coroutine_return_boxed_func, NULL, NULL), &error);
  g_assert_no_error (error);

  boxed_data = g_bytes_get_data (boxed, &length);
  g_assert_cmpuint (length, ==, 5);
  g_assert_true (memcmp (boxed_data, "boxed", 5) == 0);

  g_clear_pointer (&boxed, g_bytes_unref);
}

static void
test_coroutine_returns_pointer (void)
{
  GError *error = NULL;
  gpointer expected = GSIZE_TO_POINTER (0xdeadbeef);
  gpointer value;

  value = dex_await_pointer (dex_scheduler_spawn_coroutine (NULL, test_coroutine_return_pointer_func, NULL, NULL), &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (value == expected);
}

static void
test_coroutine_task_group_cancel (void)
{
  DexTaskGroup *group = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexFuture *coroutines[3] = { NULL, NULL, NULL };
  GError *error = NULL;
  TestCoroutineTaskGroupCancelState *states[3] = { NULL, NULL, NULL };

  for (guint i = 0; i < G_N_ELEMENTS (coroutines); i++)
    {
      TestCoroutineTaskGroupCancelState *state;

      state = test_coroutine_task_group_cancel_state_new ();
      state->gate = dex_promise_new ();
      states[i] = state;

      coroutines[i] = dex_scheduler_spawn_coroutine (NULL, test_coroutine_task_group_cancel_func, state, NULL);

      g_assert_true (dex_task_group_add (group, dex_ref (coroutines[i])));
    }

  dex_task_group_cancel (group);

  for (guint i = 0; i < G_N_ELEMENTS (coroutines); i++)
    {
      g_assert_false (dex_await_boolean (dex_ref (coroutines[i]), &error));
      g_assert_true (g_error_matches (error, DEX_ERROR, DEX_ERROR_FIBER_CANCELLED));
      g_clear_error (&error);
      test_coroutine_task_group_cancel_state_free (states[i]);
    }

  dex_clear (&group);

  for (guint i = 0; i < G_N_ELEMENTS (coroutines); i++)
    dex_clear (&coroutines[i]);
}

static void
test_coroutine_thread_pool_await (void)
{
  DexScheduler *thread_pool = dex_thread_pool_scheduler_get_default ();
  GError *error = NULL;
  guint value;

  g_assert_true (DEX_IS_THREAD_POOL_SCHEDULER (thread_pool));
  value = dex_await_uint (dex_scheduler_spawn_coroutine (thread_pool,
                                                         test_coroutine_thread_pool_return_uint_func,
                                                         NULL,
                                                         NULL),
                          &error);
  g_assert_no_error (error);
  g_assert_cmpuint (value, ==, 7u);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);

  _g_test_add_func ("/Dex/TestSuite/Coroutine/spawn_complete", test_coroutine_spawn_complete);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/cancel_race_awaited", test_coroutine_cancel_race);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/suspend_resume", test_coroutine_suspend_resume);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/suspend_void", test_coroutine_suspend_void);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/returns-int", test_coroutine_returns_int);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/returns-uint", test_coroutine_returns_uint);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/returns-boolean", test_coroutine_returns_boolean);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/returns-double", test_coroutine_returns_double);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/returns-float", test_coroutine_returns_float);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/returns-object", test_coroutine_returns_object);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/returns-boxed", test_coroutine_returns_boxed);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/returns-pointer", test_coroutine_returns_pointer);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/destroy-state", test_coroutine_destroy_state);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/task-group-cancel", test_coroutine_task_group_cancel);
  _g_test_add_func ("/Dex/TestSuite/Coroutine/thread-pool-await", test_coroutine_thread_pool_await);

  return g_test_run ();
}
