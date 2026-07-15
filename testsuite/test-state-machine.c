/* test-state-machine.c
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
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "test-util.h"

typedef enum _TestState
{
  TEST_STATE_INITIAL,
  TEST_STATE_PREPARE,
  TEST_STATE_READY,
  TEST_STATE_FAILED,
} TestState;

typedef struct _TransitionData
{
  GArray *visited;
} TransitionData;

typedef struct _SchedulerData
{
  DexScheduler *scheduler;
  gboolean      used_scheduler;
} SchedulerData;

GType test_state_get_type (void);

G_DEFINE_ENUM_TYPE (TestState, test_state,
                    G_DEFINE_ENUM_VALUE (TEST_STATE_INITIAL, "initial"),
                    G_DEFINE_ENUM_VALUE (TEST_STATE_PREPARE, "prepare"),
                    G_DEFINE_ENUM_VALUE (TEST_STATE_READY, "ready"),
                    G_DEFINE_ENUM_VALUE (TEST_STATE_FAILED, "failed"))

#define TEST_TYPE_STATE (test_state_get_type())

static void
transition_data_free (TransitionData *data)
{
  g_array_unref (data->visited);
  g_free (data);
}

static TransitionData *
transition_data_new (void)
{
  TransitionData *data;

  data = g_new0 (TransitionData, 1);
  data->visited = g_array_new (FALSE, FALSE, sizeof (guint));

  return data;
}

static gboolean
transition_basic (DexStateTransitionContext  *context,
                  gpointer                    user_data,
                  GError                    **error)
{
  TransitionData *data = user_data;
  guint from;
  guint to;

  g_assert_nonnull (context);

  from = dex_state_transition_context_get_from (context);
  to = dex_state_transition_context_get_to (context);

  g_assert_cmpuint (dex_state_transition_context_get_state (context), ==, from);

  g_array_append_val (data->visited, from);
  g_array_append_val (data->visited, to);

  return TRUE;
}

static gboolean
transition_prepare (DexStateTransitionContext  *context,
                    gpointer                    user_data,
                    GError                    **error)
{
  TransitionData *data = user_data;
  guint from;
  guint to;

  from = dex_state_transition_context_get_from (context);
  to = dex_state_transition_context_get_to (context);

  g_assert_cmpuint (from, ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (to, ==, TEST_STATE_PREPARE);
  g_assert_cmpuint (dex_state_transition_context_get_state (context), ==, TEST_STATE_INITIAL);

  g_array_append_val (data->visited, from);
  g_array_append_val (data->visited, to);

  dex_state_transition_context_set_state (context, TEST_STATE_PREPARE);

  g_assert_cmpuint (dex_state_transition_context_get_state (context), ==, TEST_STATE_PREPARE);

  dex_await (dex_timeout_new_msec (1), NULL);

  dex_state_transition_context_set_state (context, TEST_STATE_READY);

  return TRUE;
}

static gboolean
transition_prepare_back (DexStateTransitionContext  *context,
                         gpointer                    user_data,
                         GError                    **error)
{
  TransitionData *data = user_data;
  guint from;
  guint to;

  from = dex_state_transition_context_get_from (context);
  to = dex_state_transition_context_get_to (context);

  g_assert_cmpuint (from, ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (to, ==, TEST_STATE_PREPARE);
  g_assert_cmpuint (dex_state_transition_context_get_state (context), ==, TEST_STATE_INITIAL);

  g_array_append_val (data->visited, from);
  g_array_append_val (data->visited, to);

  dex_state_transition_context_set_state (context, TEST_STATE_PREPARE);

  g_assert_cmpuint (dex_state_transition_context_get_state (context), ==, TEST_STATE_PREPARE);

  dex_state_transition_context_set_state (context, TEST_STATE_INITIAL);

  return TRUE;
}

static gboolean
transition_continue_to_ready (DexStateTransitionContext  *context,
                              gpointer                    user_data,
                              GError                    **error)
{
  TransitionData *data = user_data;
  guint from;
  guint to;

  from = dex_state_transition_context_get_from (context);
  to = dex_state_transition_context_get_to (context);

  g_assert_cmpuint (from, ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (to, ==, TEST_STATE_PREPARE);
  g_assert_cmpuint (dex_state_transition_context_get_state (context), ==, TEST_STATE_INITIAL);

  g_array_append_val (data->visited, from);
  g_array_append_val (data->visited, to);

  return dex_state_transition_context_continue_to (context, TEST_STATE_READY, error);
}

static gboolean
transition_continue_to_failed (DexStateTransitionContext  *context,
                               gpointer                    user_data,
                               GError                    **error)
{
  TransitionData *data = user_data;
  guint from;
  guint to;

  from = dex_state_transition_context_get_from (context);
  to = dex_state_transition_context_get_to (context);

  g_assert_cmpuint (from, ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (to, ==, TEST_STATE_PREPARE);
  g_assert_cmpuint (dex_state_transition_context_get_state (context), ==, TEST_STATE_INITIAL);

  g_array_append_val (data->visited, from);
  g_array_append_val (data->visited, to);

  return dex_state_transition_context_continue_to (context, TEST_STATE_FAILED, error);
}

static gboolean
transition_fail (DexStateTransitionContext  *context,
                 gpointer                    user_data,
                 GError                    **error)
{
  g_set_error (error,
               G_IO_ERROR,
               G_IO_ERROR_FAILED,
               "Failed transition");

  return FALSE;
}

static gboolean
transition_scheduler (DexStateTransitionContext  *context,
                      gpointer                    user_data,
                      GError                    **error)
{
  SchedulerData *data = user_data;
  DexScheduler *thread_default;

  thread_default = dex_scheduler_get_thread_default ();

  g_assert_nonnull (thread_default);
  g_assert_true (dex_scheduler_get_main_context (thread_default) ==
                 dex_scheduler_get_main_context (data->scheduler));

  data->used_scheduler = TRUE;

  return TRUE;
}

static void
test_state_machine_basic (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_READY, transition_basic },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  g_assert_true (DEX_IS_STATE_MACHINE (state_machine));
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_INITIAL);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_READY), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_READY);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_READY);
  g_assert_cmpuint (data->visited->len, ==, 2);
  g_assert_cmpuint (g_array_index (data->visited, guint, 0), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (g_array_index (data->visited, guint, 1), ==, TEST_STATE_READY);
}

static void
test_state_machine_landing (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_PREPARE, transition_prepare },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_PREPARE), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_READY);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_READY);
  g_assert_cmpuint (data->visited->len, ==, 2);
  g_assert_cmpuint (g_array_index (data->visited, guint, 0), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (g_array_index (data->visited, guint, 1), ==, TEST_STATE_PREPARE);
}

static void
test_state_machine_reverse (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_READY, transition_basic },
    { TEST_STATE_READY, TEST_STATE_INITIAL, transition_basic },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_READY), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_READY);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_READY);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_INITIAL), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (data->visited->len, ==, 4);
  g_assert_cmpuint (g_array_index (data->visited, guint, 0), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (g_array_index (data->visited, guint, 1), ==, TEST_STATE_READY);
  g_assert_cmpuint (g_array_index (data->visited, guint, 2), ==, TEST_STATE_READY);
  g_assert_cmpuint (g_array_index (data->visited, guint, 3), ==, TEST_STATE_INITIAL);
}

static void
test_state_machine_landing_reverse (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_PREPARE, transition_prepare_back },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_PREPARE), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (data->visited->len, ==, 2);
  g_assert_cmpuint (g_array_index (data->visited, guint, 0), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (g_array_index (data->visited, guint, 1), ==, TEST_STATE_PREPARE);
}

static void
test_state_machine_invalid (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_READY, transition_basic },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_FAILED), &error);

  g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
  g_assert_cmpuint (state, ==, 0);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_INITIAL);
  g_clear_error (&error);
}

static void
test_state_machine_failure (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_FAILED, transition_fail },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_FAILED), &error);

  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_cmpuint (state, ==, 0);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_INITIAL);
  g_clear_error (&error);
}

static void
test_state_machine_requested_state (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_FAILED, transition_fail },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(DexFuture) transition = NULL;
  g_autoptr(GError) error = NULL;
  guint state;

  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         NULL,
                                         NULL);

  g_assert_cmpuint (dex_state_machine_get_requested_state (state_machine),
                    ==,
                    TEST_STATE_INITIAL);

  transition = dex_state_machine_transition (state_machine, TEST_STATE_PREPARE);

  g_assert_cmpuint (dex_state_machine_get_requested_state (state_machine),
                    ==,
                    TEST_STATE_PREPARE);

  state = dex_await_enum (dex_ref (transition), &error);

  g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
  g_assert_cmpuint (state, ==, 0);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (dex_state_machine_get_requested_state (state_machine),
                    ==,
                    TEST_STATE_PREPARE);
  dex_clear (&transition);
  g_clear_error (&error);

  transition = dex_state_machine_transition (state_machine, TEST_STATE_FAILED);

  g_assert_cmpuint (dex_state_machine_get_requested_state (state_machine),
                    ==,
                    TEST_STATE_FAILED);

  state = dex_await_enum (dex_ref (transition), &error);

  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_cmpuint (state, ==, 0);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (dex_state_machine_get_requested_state (state_machine),
                    ==,
                    TEST_STATE_FAILED);
  dex_clear (&transition);
  g_clear_error (&error);

  transition = dex_state_machine_transition (state_machine, G_MAXUINT);

  g_assert_cmpuint (dex_state_machine_get_requested_state (state_machine),
                    ==,
                    TEST_STATE_FAILED);

  state = dex_await_enum (dex_ref (transition), &error);

  g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
  g_assert_cmpuint (state, ==, 0);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (dex_state_machine_get_requested_state (state_machine),
                    ==,
                    TEST_STATE_FAILED);
  g_clear_error (&error);
}

static void
test_state_machine_wait_for_state_immediate (void)
{
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  guint state;

  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         NULL,
                                         0,
                                         NULL,
                                         0,
                                         NULL,
                                         NULL);

  state = dex_await_enum (dex_state_machine_wait_for_state (state_machine,
                                                            TEST_STATE_INITIAL),
                          &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_INITIAL);
}

static void
test_state_machine_wait_for_state_transition (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_READY, transition_basic },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(DexFuture) wait = NULL;
  g_autoptr(DexFuture) transition = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  wait = dex_state_machine_wait_for_state (state_machine, TEST_STATE_READY);

  g_assert_true (dex_future_is_pending (wait));

  transition = dex_state_machine_transition (state_machine, TEST_STATE_READY);
  state = dex_await_enum (dex_ref (transition), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_READY);

  state = dex_await_enum (dex_ref (wait), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_READY);
}

static void
test_state_machine_wait_for_state_intermediate (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_PREPARE, transition_prepare },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(DexFuture) wait = NULL;
  g_autoptr(DexFuture) transition = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  wait = dex_state_machine_wait_for_state (state_machine, TEST_STATE_PREPARE);
  transition = dex_state_machine_transition (state_machine, TEST_STATE_PREPARE);

  state = dex_await_enum (dex_ref (wait), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_PREPARE);

  state = dex_await_enum (dex_ref (transition), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_READY);
}

static void
test_state_machine_wait_for_state_invalid (void)
{
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  guint state;

  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         NULL,
                                         0,
                                         NULL,
                                         0,
                                         NULL,
                                         NULL);

  state = dex_await_enum (dex_state_machine_wait_for_state (state_machine, G_MAXUINT), &error);

  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVAL);
  g_assert_cmpuint (state, ==, 0);
  g_clear_error (&error);
}

static void
test_state_machine_wait_for_state_finalized (void)
{
  DexStateMachine *state_machine;
  g_autoptr(DexFuture) wait = NULL;
  g_autoptr(GError) error = NULL;
  guint state;

  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         NULL,
                                         0,
                                         NULL,
                                         0,
                                         NULL,
                                         NULL);

  wait = dex_state_machine_wait_for_state (state_machine, TEST_STATE_READY);

  g_assert_true (dex_future_is_pending (wait));

  dex_unref (state_machine);

  state = dex_await_enum (dex_ref (wait), &error);

  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_cmpuint (state, ==, 0);
  g_clear_error (&error);
}

static void
test_state_machine_continue_to (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_PREPARE, transition_continue_to_ready },
    { TEST_STATE_PREPARE, TEST_STATE_READY, transition_basic },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_PREPARE), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_READY);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_READY);
  g_assert_cmpuint (data->visited->len, ==, 4);
  g_assert_cmpuint (g_array_index (data->visited, guint, 0), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (g_array_index (data->visited, guint, 1), ==, TEST_STATE_PREPARE);
  g_assert_cmpuint (g_array_index (data->visited, guint, 2), ==, TEST_STATE_PREPARE);
  g_assert_cmpuint (g_array_index (data->visited, guint, 3), ==, TEST_STATE_READY);
}

static void
test_state_machine_continue_to_before_queued (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_PREPARE, transition_continue_to_ready },
    { TEST_STATE_PREPARE, TEST_STATE_READY, transition_basic },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(DexFuture) first = NULL;
  g_autoptr(DexFuture) second = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  first = dex_state_machine_transition (state_machine, TEST_STATE_PREPARE);
  second = dex_state_machine_transition (state_machine, TEST_STATE_READY);

  state = dex_await_enum (dex_ref (first), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_READY);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_READY);

  state = dex_await_enum (dex_ref (second), &error);

  g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
  g_assert_cmpuint (state, ==, 0);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_READY);
  g_assert_cmpuint (data->visited->len, ==, 4);
  g_clear_error (&error);
}

static void
test_state_machine_continue_to_invalid (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_PREPARE, transition_continue_to_failed },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_PREPARE), &error);

  g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
  g_assert_cmpuint (state, ==, 0);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (data->visited->len, ==, 2);
  g_assert_cmpuint (g_array_index (data->visited, guint, 0), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (g_array_index (data->visited, guint, 1), ==, TEST_STATE_PREPARE);
  g_clear_error (&error);
}

static void
test_state_machine_continue_to_failure (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_PREPARE, transition_continue_to_failed },
    { TEST_STATE_PREPARE, TEST_STATE_FAILED, transition_fail },
  };
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  TransitionData *data;
  guint state;

  data = transition_data_new ();
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         NULL,
                                         0,
                                         data,
                                         (GDestroyNotify)transition_data_free);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_PREPARE), &error);

  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_cmpuint (state, ==, 0);
  g_assert_cmpuint (dex_state_machine_get_state (state_machine), ==, TEST_STATE_PREPARE);
  g_assert_cmpuint (data->visited->len, ==, 2);
  g_assert_cmpuint (g_array_index (data->visited, guint, 0), ==, TEST_STATE_INITIAL);
  g_assert_cmpuint (g_array_index (data->visited, guint, 1), ==, TEST_STATE_PREPARE);
  g_clear_error (&error);
}

static void
test_state_machine_scheduler (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_READY, transition_scheduler },
  };
  g_autoptr(DexScheduler) scheduler = NULL;
  g_autoptr(DexStateMachine) state_machine = NULL;
  g_autoptr(GError) error = NULL;
  SchedulerData data = {0};
  guint state;

  scheduler = dex_thread_pool_scheduler_new ();
  data.scheduler = scheduler;
  state_machine = dex_state_machine_new (TEST_TYPE_STATE,
                                         TEST_STATE_INITIAL,
                                         transitions,
                                         G_N_ELEMENTS (transitions),
                                         scheduler,
                                         dex_get_min_stack_size (),
                                         &data,
                                         NULL);

  state = dex_await_enum (dex_state_machine_transition (state_machine, TEST_STATE_READY), &error);

  g_assert_no_error (error);
  g_assert_cmpuint (state, ==, TEST_STATE_READY);
  g_assert_true (data.used_scheduler);
}

static void
test_state_machine_duplicate (void)
{
  static const DexStateTransition transitions[] = {
    { TEST_STATE_INITIAL, TEST_STATE_READY, transition_basic },
    { TEST_STATE_INITIAL, TEST_STATE_READY, transition_basic },
  };

  g_test_expect_message ("Dex",
                         G_LOG_LEVEL_CRITICAL,
                         "*dex_state_machine_validate_transitions*");
  g_assert_null (dex_state_machine_new (TEST_TYPE_STATE,
                                        TEST_STATE_INITIAL,
                                        transitions,
                                        G_N_ELEMENTS (transitions),
                                        NULL,
                                        0,
                                        NULL,
                                        NULL));
  g_test_assert_expected_messages ();
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);

  _g_test_add_func ("/Dex/StateMachine/basic", test_state_machine_basic);
  _g_test_add_func ("/Dex/StateMachine/landing", test_state_machine_landing);
  _g_test_add_func ("/Dex/StateMachine/reverse", test_state_machine_reverse);
  _g_test_add_func ("/Dex/StateMachine/landing-reverse", test_state_machine_landing_reverse);
  _g_test_add_func ("/Dex/StateMachine/invalid", test_state_machine_invalid);
  _g_test_add_func ("/Dex/StateMachine/failure", test_state_machine_failure);
  _g_test_add_func ("/Dex/StateMachine/requested-state", test_state_machine_requested_state);
  _g_test_add_func ("/Dex/StateMachine/wait-for-state-immediate",
                    test_state_machine_wait_for_state_immediate);
  _g_test_add_func ("/Dex/StateMachine/wait-for-state-transition",
                    test_state_machine_wait_for_state_transition);
  _g_test_add_func ("/Dex/StateMachine/wait-for-state-intermediate",
                    test_state_machine_wait_for_state_intermediate);
  _g_test_add_func ("/Dex/StateMachine/wait-for-state-invalid",
                    test_state_machine_wait_for_state_invalid);
  _g_test_add_func ("/Dex/StateMachine/wait-for-state-finalized",
                    test_state_machine_wait_for_state_finalized);
  _g_test_add_func ("/Dex/StateMachine/continue-to", test_state_machine_continue_to);
  _g_test_add_func ("/Dex/StateMachine/continue-to-before-queued",
                    test_state_machine_continue_to_before_queued);
  _g_test_add_func ("/Dex/StateMachine/continue-to-invalid",
                    test_state_machine_continue_to_invalid);
  _g_test_add_func ("/Dex/StateMachine/continue-to-failure",
                    test_state_machine_continue_to_failure);
  _g_test_add_func ("/Dex/StateMachine/scheduler", test_state_machine_scheduler);
  _g_test_add_func ("/Dex/StateMachine/duplicate", test_state_machine_duplicate);

  return g_test_run ();
}
