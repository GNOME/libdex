/*
 * dex-state-machine.c
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

#include <gio/gio.h>

#include "dex-closure.h"
#include "dex-error.h"
#include "dex-future.h"
#include "dex-limiter.h"
#include "dex-object-private.h"
#include "dex-state-machine.h"

/**
 * DexStateMachine:
 *
 * `DexStateMachine` provides a serialized asynchronous state machine.
 *
 * Transitions are declared up front with a static table of
 * [struct@Dex.StateTransition] entries. Requests made with
 * [method@Dex.StateMachine.transition] are serialized through an internal
 * [class@Dex.Limiter] with a max concurrency of one. Transition callbacks run
 * from a fiber, so they may use `dex_await()` and related APIs while still
 * appearing as synchronous functions.
 */

/**
 * DexStateTransitionContext:
 *
 * `DexStateTransitionContext` is an opaque per-callback structure with
 * information and state access for a [struct@Dex.StateTransition] callback.
 *
 * It is only valid for the duration of the callback and must not be stored.
 *
 * [method@Dex.StateTransitionContext.get_from] and
 * [method@Dex.StateTransitionContext.get_to] return the declared edge that
 * caused the callback to run. [method@Dex.StateTransitionContext.get_state]
 * and [method@Dex.StateTransitionContext.set_state] access the real state in
 * the [class@Dex.StateMachine].
 *
 * Since: 1.2
 */

struct _DexStateMachine
{
  DexObject           parent_instance;
  GType               state_enum_type;
  DexLimiter         *limiter;
  DexScheduler       *scheduler;
  DexStateTransition *transitions;
  guint               n_transitions;
  gsize               stack_size;
  guint               state;
  gpointer            user_data;
  GDestroyNotify      user_data_destroy;
};

struct _DexStateTransitionContext
{
  DexStateMachine *state_machine;
  guint            from;
  guint            to;
  guint            state_set : 1;
};

typedef struct _DexStateMachineClass
{
  DexObjectClass parent_class;
} DexStateMachineClass;

DEX_DEFINE_FINAL_TYPE (DexStateMachine, dex_state_machine, DEX_TYPE_OBJECT)
DEX_DEFINE_CLOSURE_TYPE (DexStateMachineRun, dex_state_machine_run,
                         DEX_DEFINE_CLOSURE_POINTER (DexStateMachine *, state_machine, dex_unref),
                         DEX_DEFINE_CLOSURE_VALUE (guint, target))

#undef DEX_TYPE_STATE_MACHINE
#define DEX_TYPE_STATE_MACHINE dex_state_machine_type

static gboolean
dex_state_machine_is_valid_state (DexStateMachine *state_machine,
                                  guint            state)
{
  GEnumClass *enum_class;

  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  enum_class = g_type_class_get (state_machine->state_enum_type);

  return g_enum_get_value (enum_class, state) != NULL;
}

static char *
dex_state_machine_dup_state_name (DexStateMachine *state_machine,
                                  guint            state)
{
  GEnumClass *enum_class;
  GEnumValue *value;
  char *ret;

  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  enum_class = g_type_class_get (state_machine->state_enum_type);
  value = g_enum_get_value (enum_class, state);
  ret = g_strdup (value != NULL ? value->value_nick : "<invalid>");

  return ret;
}

static gboolean
dex_state_machine_validate_transitions (GType                     state_enum_type,
                                        guint                     initial_state,
                                        const DexStateTransition *transitions,
                                        guint                     n_transitions)
{
  GEnumClass *enum_class;
  gboolean ret = TRUE;

  g_assert (g_type_is_a (state_enum_type, G_TYPE_ENUM));
  g_assert (transitions != NULL || n_transitions == 0);

  enum_class = g_type_class_get (state_enum_type);

  if (g_enum_get_value (enum_class, initial_state) == NULL)
    ret = FALSE;

  for (guint i = 0; ret && i < n_transitions; i++)
    {
      if (transitions[i].func == NULL ||
          g_enum_get_value (enum_class, transitions[i].from) == NULL ||
          g_enum_get_value (enum_class, transitions[i].to) == NULL)
        {
          ret = FALSE;
          break;
        }

      for (guint j = i + 1; j < n_transitions; j++)
        {
          if (transitions[i].from == transitions[j].from &&
              transitions[i].to == transitions[j].to)
            {
              ret = FALSE;
              break;
            }
        }
    }

  return ret;
}

static const DexStateTransition *
dex_state_machine_lookup (DexStateMachine *state_machine,
                          guint            from,
                          guint            to)
{
  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  for (guint i = 0; i < state_machine->n_transitions; i++)
    {
      if (state_machine->transitions[i].from == from &&
          state_machine->transitions[i].to == to)
        return &state_machine->transitions[i];
    }

  return NULL;
}

static DexFuture *
dex_state_machine_invalid_transition (DexStateMachine *state_machine,
                                      guint            from,
                                      guint            to)
{
  g_autofree char *from_name = NULL;
  g_autofree char *to_name = NULL;

  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  from_name = dex_state_machine_dup_state_name (state_machine, from);
  to_name = dex_state_machine_dup_state_name (state_machine, to);

  return dex_future_new_reject (DEX_ERROR,
                                DEX_ERROR_INVALID_TRANSITION,
                                "Invalid transition from `%s` to `%s`",
                                from_name, to_name);
}

static gboolean
dex_state_machine_set_state (DexStateMachine *state_machine,
                             guint            state)
{
  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  g_return_val_if_fail (dex_state_machine_is_valid_state (state_machine, state), FALSE);

  dex_object_lock (state_machine);
  state_machine->state = state;
  dex_object_unlock (state_machine);

  return TRUE;
}

static DexFuture *
dex_state_machine_transition_fiber (gpointer user_data)
{
  DexStateMachineRun *run = user_data;
  DexStateMachine *state_machine = run->state_machine;
  DexStateTransitionContext context = {0};
  GError *error = NULL;
  guint current;
  guint final;
  guint target;
  const DexStateTransition *transition;

  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  dex_object_lock (state_machine);
  current = state_machine->state;
  dex_object_unlock (state_machine);

  target = run->target;

  if (!dex_state_machine_is_valid_state (state_machine, target))
    return dex_state_machine_invalid_transition (state_machine, current, target);

  if (!(transition = dex_state_machine_lookup (state_machine, current, target)))
    return dex_state_machine_invalid_transition (state_machine, current, target);

  context.state_machine = state_machine;
  context.from = current;
  context.to = target;

  if (!transition->func (&context, state_machine->user_data, &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  if (!context.state_set)
    dex_state_machine_set_state (state_machine, target);

  final = dex_state_machine_get_state (state_machine);

  return dex_future_new_enum (state_machine->state_enum_type, final);
}

static void
dex_state_machine_finalize (DexObject *object)
{
  DexStateMachine *state_machine = DEX_STATE_MACHINE (object);

  dex_clear (&state_machine->limiter);
  dex_clear (&state_machine->scheduler);
  g_clear_pointer (&state_machine->transitions, g_free);

  if (state_machine->user_data_destroy != NULL)
    g_clear_pointer (&state_machine->user_data, state_machine->user_data_destroy);

  DEX_OBJECT_CLASS (dex_state_machine_parent_class)->finalize (object);
}

static void
dex_state_machine_class_init (DexStateMachineClass *state_machine_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (state_machine_class);

  object_class->finalize = dex_state_machine_finalize;
}

static void
dex_state_machine_init (DexStateMachine *state_machine)
{
  state_machine->limiter = dex_limiter_new (1);
}

/**
 * dex_state_transition_context_get_from:
 * @context: a [struct@Dex.StateTransitionContext]
 *
 * Gets the source state for the transition edge being executed.
 *
 * This value is fixed for the lifetime of @context and does not change if
 * [method@Dex.StateTransitionContext.set_state] is called.
 *
 * Returns: the source state for the transition edge
 *
 * Since: 1.2
 */
guint
dex_state_transition_context_get_from (DexStateTransitionContext *context)
{
  g_return_val_if_fail (context != NULL, 0);

  return context->from;
}

/**
 * dex_state_transition_context_get_to:
 * @context: a [struct@Dex.StateTransitionContext]
 *
 * Gets the target state for the transition edge being executed.
 *
 * This value is fixed for the lifetime of @context and does not change if
 * [method@Dex.StateTransitionContext.set_state] is called.
 *
 * Returns: the target state for the transition edge
 *
 * Since: 1.2
 */
guint
dex_state_transition_context_get_to (DexStateTransitionContext *context)
{
  g_return_val_if_fail (context != NULL, 0);

  return context->to;
}

/**
 * dex_state_transition_context_get_state:
 * @context: a [struct@Dex.StateTransitionContext]
 *
 * Gets the real current state from the [class@Dex.StateMachine].
 *
 * Returns: the current state
 *
 * Since: 1.2
 */
guint
dex_state_transition_context_get_state (DexStateTransitionContext *context)
{
  g_return_val_if_fail (context != NULL, 0);

  return dex_state_machine_get_state (context->state_machine);
}

/**
 * dex_state_transition_context_set_state:
 * @context: a [struct@Dex.StateTransitionContext]
 * @state: the new state
 *
 * Sets the real current state in the [class@Dex.StateMachine].
 *
 * This may be used by transition callbacks to expose intermediate states while
 * doing asynchronous work. @state must be a valid value in the state machine's
 * enum type. This method may only be used while the transition callback is
 * active.
 *
 * Since: 1.2
 */
void
dex_state_transition_context_set_state (DexStateTransitionContext  *context,
                                        guint                       state)
{
  g_return_if_fail (context != NULL);

  if (dex_state_machine_set_state (context->state_machine, state))
    context->state_set = TRUE;
}

/**
 * dex_state_machine_new: (skip)
 * @state_enum_type: a `GType` for a `GEnum`
 * @initial_state: the initial state value
 * @transitions: (array length=n_transitions): transition entries
 * @n_transitions: the number of transition entries
 * @scheduler: (nullable): scheduler to spawn transition fibers on, or %NULL
 *   for the thread default
 * @stack_size: stack size for transition fibers, or zero to use the default
 * @user_data: (nullable): user data for transition callbacks
 * @user_data_destroy: (nullable): destroy notify for @user_data
 *
 * Creates a new [class@Dex.StateMachine].
 *
 * The transition table is copied and validated before the state machine is
 * returned. Each transition must have a valid @from state, valid @to state,
 * and non-%NULL callback. Duplicate edges are rejected.
 *
 * Returns: (transfer full): a new [class@Dex.StateMachine]
 */
DexStateMachine *
dex_state_machine_new (GType                     state_enum_type,
                       guint                     initial_state,
                       const DexStateTransition *transitions,
                       guint                     n_transitions,
                       DexScheduler             *scheduler,
                       gsize                     stack_size,
                       gpointer                  user_data,
                       GDestroyNotify            user_data_destroy)
{
  DexStateMachine *state_machine;

  g_return_val_if_fail (dex_state_machine_type != G_TYPE_INVALID, NULL);
  g_return_val_if_fail (g_type_is_a (state_enum_type, G_TYPE_ENUM), NULL);
  g_return_val_if_fail (transitions != NULL || n_transitions == 0, NULL);
  g_return_val_if_fail (scheduler == NULL || DEX_IS_SCHEDULER (scheduler), NULL);
  g_return_val_if_fail (dex_state_machine_validate_transitions (state_enum_type,
                                                                initial_state,
                                                                transitions,
                                                                n_transitions), NULL);

  state_machine = (DexStateMachine *)dex_object_create_instance (dex_state_machine_type);
  state_machine->state_enum_type = state_enum_type;
  state_machine->state = initial_state;
  state_machine->n_transitions = n_transitions;
  state_machine->scheduler = scheduler ? dex_ref (scheduler) : NULL;
  state_machine->stack_size = stack_size;
  state_machine->user_data = user_data;
  state_machine->user_data_destroy = user_data_destroy;

  if (n_transitions > 0)
    state_machine->transitions = g_memdup2 (transitions, sizeof *transitions * n_transitions);

  return state_machine;
}

/**
 * dex_state_machine_transition:
 * @state_machine: a [class@Dex.StateMachine]
 * @target: the target state
 *
 * Requests a transition to @target.
 *
 * Transition requests are serialized. The matching callback is run from a
 * fiber and may use `dex_await()` to wait for asynchronous work. If the
 * callback succeeds without calling
 * [method@Dex.StateTransitionContext.set_state], the state machine commits
 * @target. If the callback updates the state directly, the returned future
 * resolves to the current state after the callback returns.
 *
 * Returns: (transfer full): a future resolving to the final enum state
 */
DexFuture *
dex_state_machine_transition (DexStateMachine *state_machine,
                              guint            target)
{
  DexStateMachineRun *run;

  dex_return_error_if_fail (DEX_IS_STATE_MACHINE (state_machine));

  run = dex_state_machine_run_new ();
  run->state_machine = dex_ref (state_machine);
  run->target = target;

  return dex_limiter_run (state_machine->limiter,
                          state_machine->scheduler,
                          state_machine->stack_size,
                          dex_state_machine_transition_fiber,
                          run,
                          (GDestroyNotify)dex_state_machine_run_free);
}

/**
 * dex_state_machine_get_state:
 * @state_machine: a [class@Dex.StateMachine]
 *
 * Gets the current state of @state_machine.
 *
 * Returns: the current state
 */
guint
dex_state_machine_get_state (DexStateMachine *state_machine)
{
  guint ret;

  g_return_val_if_fail (DEX_IS_STATE_MACHINE (state_machine), 0);

  dex_object_lock (state_machine);
  ret = state_machine->state;
  dex_object_unlock (state_machine);

  return ret;
}
