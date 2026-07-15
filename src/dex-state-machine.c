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
#include "dex-future-private.h"
#include "dex-limiter.h"
#include "dex-object-private.h"
#include "dex-promise.h"
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
 * the [class@Dex.StateMachine]. Use
 * [method@Dex.StateTransitionContext.continue_to] to follow another declared
 * edge before queued transition requests are processed.
 *
 * Since: 1.2
 */

struct _DexStateMachine
{
  DexObject                  parent_instance;
  GType                      state_enum_type;
  DexLimiter                *limiter;
  DexScheduler              *scheduler;
  DexStateTransition        *transitions;
  DexStateTransitionContext *active_context;
  GQueue                     waiters;
  guint                      n_transitions;
  gsize                      stack_size;
  guint                      state;
  guint                      requested_state;
  gpointer                   user_data;
  GDestroyNotify             user_data_destroy;
};

struct _DexStateTransitionContext
{
  DexStateMachine           *state_machine;
  DexStateTransitionContext *previous_context;
  DexPromise                *interrupt;
  guint                      from;
  guint                      to;
  guint                      state_set : 1;
  guint                      interrupted : 1;
};

typedef struct _DexStateMachineClass
{
  DexObjectClass parent_class;
} DexStateMachineClass;

typedef struct _DexStateMachineWaiter
{
  DexFuture  parent_instance;
  GList      link;
  DexWeakRef state_machine_wr;
  guint      state;
  guint      queued : 1;
} DexStateMachineWaiter;

typedef struct _DexStateMachineWaiterClass
{
  DexFutureClass parent_class;
} DexStateMachineWaiterClass;

GType dex_state_machine_waiter_get_type (void);

DEX_DEFINE_FINAL_TYPE (DexStateMachine, dex_state_machine, DEX_TYPE_OBJECT)
DEX_DEFINE_FINAL_TYPE (DexStateMachineWaiter, dex_state_machine_waiter, DEX_TYPE_FUTURE)
DEX_DEFINE_CLOSURE_TYPE (DexStateMachineRun, dex_state_machine_run,
                         DEX_DEFINE_CLOSURE_POINTER (DexStateMachine *, state_machine, dex_unref),
                         DEX_DEFINE_CLOSURE_VALUE (guint, target))

#undef DEX_TYPE_STATE_MACHINE
#define DEX_TYPE_STATE_MACHINE dex_state_machine_type

static void
dex_state_machine_waiter_complete (DexStateMachineWaiter *waiter,
                                   GType                  state_enum_type,
                                   guint                  state)
{
  GValue value = G_VALUE_INIT;

  g_assert (waiter != NULL);
  g_assert (!waiter->queued);
  g_assert (G_TYPE_IS_ENUM (state_enum_type));

  g_value_init (&value, state_enum_type);
  g_value_set_enum (&value, state);
  dex_future_complete (DEX_FUTURE (waiter), &value, NULL);
  g_value_unset (&value);
}

static void
dex_state_machine_waiter_discard (DexFuture *future)
{
  DexStateMachineWaiter *waiter = (DexStateMachineWaiter *)future;
  DexStateMachine *state_machine;
  gboolean unref = FALSE;

  g_assert (waiter != NULL);

  if (!(state_machine = dex_weak_ref_get (&waiter->state_machine_wr)))
    return;

  dex_object_lock (state_machine);
  if (waiter->queued)
    {
      g_queue_unlink (&state_machine->waiters, &waiter->link);
      waiter->queued = FALSE;
      unref = TRUE;
    }
  dex_object_unlock (state_machine);

  if (unref)
    dex_unref (waiter);

  dex_unref (state_machine);
}

static void
dex_state_machine_waiter_finalize (DexObject *object)
{
  DexStateMachineWaiter *waiter = (DexStateMachineWaiter *)object;

  g_assert (!waiter->queued);

  dex_weak_ref_clear (&waiter->state_machine_wr);

  DEX_OBJECT_CLASS (dex_state_machine_waiter_parent_class)->finalize (object);
}

static void
dex_state_machine_waiter_class_init (DexStateMachineWaiterClass *waiter_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (waiter_class);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (waiter_class);

  object_class->finalize = dex_state_machine_waiter_finalize;

  future_class->discard = dex_state_machine_waiter_discard;
}

static void
dex_state_machine_waiter_init (DexStateMachineWaiter *waiter)
{
  waiter->link.data = waiter;
}

static DexStateMachineWaiter *
dex_state_machine_waiter_new (DexStateMachine *state_machine,
                              guint            state)
{
  DexStateMachineWaiter *waiter;

  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  waiter = (DexStateMachineWaiter *)dex_object_create_instance (dex_state_machine_waiter_type);
  waiter->state = state;
  dex_weak_ref_init (&waiter->state_machine_wr, state_machine);

  return waiter;
}

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

static gboolean
dex_state_machine_set_invalid_transition_error (DexStateMachine  *state_machine,
                                                guint             from,
                                                guint             to,
                                                GError          **error)
{
  g_autofree char *from_name = NULL;
  g_autofree char *to_name = NULL;

  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  from_name = dex_state_machine_dup_state_name (state_machine, from);
  to_name = dex_state_machine_dup_state_name (state_machine, to);

  g_set_error (error,
               DEX_ERROR,
               DEX_ERROR_INVALID_TRANSITION,
               "Invalid transition from `%s` to `%s`",
               from_name,
               to_name);

  return FALSE;
}

static gboolean
dex_state_machine_set_state (DexStateMachine *state_machine,
                             guint            state)
{
  GQueue waiters = G_QUEUE_INIT;

  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  g_return_val_if_fail (dex_state_machine_is_valid_state (state_machine, state), FALSE);

  dex_object_lock (state_machine);
  state_machine->state = state;
  for (const GList *iter = state_machine->waiters.head; iter; )
    {
      DexStateMachineWaiter *waiter = iter->data;

      iter = iter->next;

      if (waiter->state == state)
        {
          g_queue_unlink (&state_machine->waiters, &waiter->link);
          g_queue_push_tail_link (&waiters, &waiter->link);
          waiter->queued = FALSE;
        }
    }
  dex_object_unlock (state_machine);

  while (waiters.head != NULL)
    {
      DexStateMachineWaiter *waiter = g_queue_pop_head_link (&waiters)->data;

      dex_state_machine_waiter_complete (waiter, state_machine->state_enum_type, state);
      dex_unref (waiter);
    }

  return TRUE;
}

static gboolean
dex_state_machine_set_requested_state (DexStateMachine *state_machine,
                                       guint            state)
{
  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  if (!dex_state_machine_is_valid_state (state_machine, state))
    return FALSE;

  dex_object_lock (state_machine);
  state_machine->requested_state = state;
  dex_object_unlock (state_machine);

  return TRUE;
}

/* continue_to() synchronously enters another transition while the outer
 * callback is still on the stack. Keep active_context stacked so interrupt()
 * targets the innermost/current callback, then restores the outer interrupt
 * scope when the continuation returns.
 */
static void
dex_state_machine_push_context (DexStateMachine           *state_machine,
                                DexStateTransitionContext *context)
{
  g_assert (DEX_IS_STATE_MACHINE (state_machine));
  g_assert (context != NULL);
  g_assert (context->state_machine == state_machine);

  dex_object_lock (state_machine);
  context->previous_context = state_machine->active_context;
  state_machine->active_context = context;
  dex_object_unlock (state_machine);
}

static DexPromise *
dex_state_machine_pop_context (DexStateMachine           *state_machine,
                               DexStateTransitionContext *context)
{
  DexPromise *interrupt;

  g_assert (DEX_IS_STATE_MACHINE (state_machine));
  g_assert (context != NULL);
  g_assert (context->state_machine == state_machine);

  dex_object_lock (state_machine);
  g_assert (state_machine->active_context == context);
  state_machine->active_context = context->previous_context;
  interrupt = g_steal_pointer (&context->interrupt);
  dex_object_unlock (state_machine);

  return interrupt;
}

static gboolean
dex_state_machine_transition_to (DexStateMachine  *state_machine,
                                 guint             from,
                                 guint             target,
                                 GError          **error)
{
  DexStateTransitionContext context = {0};
  g_autoptr(DexPromise) interrupt = NULL;
  const DexStateTransition *transition;
  gboolean succeeded;

  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  if (!dex_state_machine_is_valid_state (state_machine, target))
    return dex_state_machine_set_invalid_transition_error (state_machine, from, target, error);

  if (!(transition = dex_state_machine_lookup (state_machine, from, target)))
    return dex_state_machine_set_invalid_transition_error (state_machine, from, target, error);

  context.state_machine = state_machine;
  context.from = from;
  context.to = target;

  dex_state_machine_push_context (state_machine, &context);

  succeeded = transition->func (&context, state_machine->user_data, error);
  interrupt = dex_state_machine_pop_context (state_machine, &context);

  if (interrupt != NULL)
    dex_promise_reject (interrupt,
                        g_error_new_literal (G_IO_ERROR,
                                             G_IO_ERROR_CANCELLED,
                                             "State transition context completed"));

  if (!succeeded)
    return FALSE;

  if (!context.state_set)
    dex_state_machine_set_state (state_machine, target);

  return TRUE;
}

static DexFuture *
dex_state_machine_transition_fiber (gpointer user_data)
{
  DexStateMachineRun *run = user_data;
  DexStateMachine *state_machine = run->state_machine;
  GError *error = NULL;
  guint current;
  guint final;
  guint target;

  g_assert (DEX_IS_STATE_MACHINE (state_machine));

  dex_object_lock (state_machine);
  current = state_machine->state;
  dex_object_unlock (state_machine);

  target = run->target;

  if (!dex_state_machine_transition_to (state_machine, current, target, &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  final = dex_state_machine_get_state (state_machine);

  return dex_future_new_enum (state_machine->state_enum_type, final);
}

static void
dex_state_machine_finalize (DexObject *object)
{
  DexStateMachine *state_machine = DEX_STATE_MACHINE (object);

  while (state_machine->waiters.head != NULL)
    {
      DexStateMachineWaiter *waiter = g_queue_pop_head_link (&state_machine->waiters)->data;

      waiter->queued = FALSE;
      dex_future_complete (DEX_FUTURE (waiter),
                           NULL,
                           g_error_new_literal (G_IO_ERROR,
                                                G_IO_ERROR_CANCELLED,
                                                "State machine was finalized"));
      dex_unref (waiter);
    }

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

  g_type_ensure (dex_state_machine_waiter_get_type ());
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
 * dex_state_transition_context_wait_for_interrupt:
 * @context: a [struct@Dex.StateTransitionContext]
 *
 * Creates a future that resolves when the active transition context is
 * interrupted.
 *
 * This is a cooperative mechanism for long-running transition callbacks. The
 * returned future resolves to %TRUE when [method@Dex.StateMachine.interrupt]
 * is called while @context is active. If the context was already interrupted
 * before this function is called, the returned future is already resolved.
 *
 * If @context completes before it is interrupted, the returned future is
 * rejected with %G_IO_ERROR_CANCELLED.
 *
 * Returns: (transfer full): a future resolving to %TRUE when interrupted
 *
 * Since: 1.2
 */
DexFuture *
dex_state_transition_context_wait_for_interrupt (DexStateTransitionContext *context)
{
  DexStateMachine *state_machine;
  DexPromise *interrupt;

  dex_return_error_if_fail (context != NULL);

  state_machine = context->state_machine;

  dex_return_error_if_fail (DEX_IS_STATE_MACHINE (state_machine));

  dex_object_lock (state_machine);
  if (context->interrupted)
    {
      dex_object_unlock (state_machine);
      return dex_future_new_true ();
    }

  if (context->interrupt == NULL)
    context->interrupt = dex_promise_new ();

  interrupt = dex_ref (context->interrupt);
  dex_object_unlock (state_machine);

  return DEX_FUTURE (interrupt);
}

/**
 * dex_state_transition_context_continue_to:
 * @context: a [struct@Dex.StateTransitionContext]
 * @target: the target state for the next edge
 * @error: a location for a `GError`, or %NULL
 *
 * Attempts to continue from the current transition to @target immediately.
 *
 * The continuation runs while the state machine still holds its internal
 * serialization slot, so queued [method@Dex.StateMachine.transition] requests
 * are not processed first. If @context has not explicitly set the state with
 * [method@Dex.StateTransitionContext.set_state], the current edge target is
 * committed before the next edge is executed.
 *
 * The next transition callback is called before this function returns. If that
 * callback uses `dex_await()`, the same transition fiber suspends and resumes,
 * while the state machine still holds the serialization slot. Chained
 * continuations therefore use the normal C call stack and should be reserved
 * for short, bounded chains rather than unbounded graph traversal.
 *
 * The next edge is looked up from the real current state to @target. If no
 * such edge exists, %FALSE is returned and @error is set to
 * [error@Dex.Error.INVALID_TRANSITION]. If the next edge callback fails, its
 * error is propagated.
 *
 * Returns: %TRUE if the continuation succeeded; otherwise %FALSE
 *
 * Since: 1.2
 */
gboolean
dex_state_transition_context_continue_to (DexStateTransitionContext  *context,
                                          guint                       target,
                                          GError                    **error)
{
  DexStateMachine *state_machine;
  guint from;

  g_return_val_if_fail (context != NULL, FALSE);

  state_machine = context->state_machine;

  g_return_val_if_fail (DEX_IS_STATE_MACHINE (state_machine), FALSE);

  from = context->state_set ? dex_state_machine_get_state (state_machine) : context->to;

  if (!dex_state_machine_is_valid_state (state_machine, target))
    return dex_state_machine_set_invalid_transition_error (state_machine, from, target, error);

  if (dex_state_machine_lookup (state_machine, from, target) == NULL)
    return dex_state_machine_set_invalid_transition_error (state_machine, from, target, error);

  if (!context->state_set)
    {
      if (!dex_state_machine_set_state (state_machine, context->to))
        return FALSE;

      context->state_set = TRUE;
    }

  return dex_state_machine_transition_to (state_machine, from, target, error);
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
  state_machine->requested_state = initial_state;
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
 * If @target is a valid value for the state machine's enum type, it becomes
 * the requested state immediately and can be read with
 * [method@Dex.StateMachine.get_requested_state]. The requested state is the
 * most recent valid target passed to this method. It may differ from the
 * current state and does not imply that the transition will succeed.
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

  dex_state_machine_set_requested_state (state_machine, target);

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
 * dex_state_machine_wait_for_state:
 * @state_machine: a [class@Dex.StateMachine]
 * @state: the state to wait for
 *
 * Waits until @state_machine enters @state.
 *
 * If @state_machine is already in @state, the returned future is already
 * resolved. Otherwise, the future resolves the next time the current state is
 * set to @state. This does not request a transition; use
 * [method@Dex.StateMachine.transition] to move the state machine.
 *
 * Returns: (transfer full): a future resolving to @state
 *
 * Since: 1.2
 */
DexFuture *
dex_state_machine_wait_for_state (DexStateMachine *state_machine,
                                  guint            state)
{
  DexStateMachineWaiter *waiter;

  dex_return_error_if_fail (DEX_IS_STATE_MACHINE (state_machine));
  dex_return_error_if_fail (dex_state_machine_is_valid_state (state_machine, state));

  waiter = dex_state_machine_waiter_new (state_machine, state);

  dex_object_lock (state_machine);
  if (state_machine->state == state)
    {
      dex_object_unlock (state_machine);
      dex_unref (waiter);
      return dex_future_new_enum (state_machine->state_enum_type, state);
    }

  dex_ref (waiter);
  waiter->queued = TRUE;
  g_queue_push_tail_link (&state_machine->waiters, &waiter->link);
  dex_object_unlock (state_machine);

  return DEX_FUTURE (waiter);
}

/**
 * dex_state_machine_interrupt:
 * @state_machine: a [class@Dex.StateMachine]
 *
 * Cooperatively interrupts the active transition callback.
 *
 * If a transition callback is currently active, its interrupt future returned
 * by [method@Dex.StateTransitionContext.wait_for_interrupt] is resolved. This
 * does not request a transition or change the current state; the active
 * callback decides how to handle the interrupt.
 *
 * Returns: %TRUE if an active transition context was marked interrupted;
 *   otherwise %FALSE
 *
 * Since: 1.2
 */
gboolean
dex_state_machine_interrupt (DexStateMachine *state_machine)
{
  DexStateTransitionContext *context;
  DexPromise *interrupt = NULL;
  gboolean ret = FALSE;

  g_return_val_if_fail (DEX_IS_STATE_MACHINE (state_machine), FALSE);

  dex_object_lock (state_machine);
  if ((context = state_machine->active_context))
    {
      context->interrupted = TRUE;
      if (context->interrupt != NULL)
        interrupt = dex_ref (context->interrupt);
      ret = TRUE;
    }
  dex_object_unlock (state_machine);

  if (interrupt != NULL)
    {
      dex_promise_resolve_boolean (interrupt, TRUE);
      dex_unref (interrupt);
    }

  return ret;
}

/**
 * dex_state_machine_get_requested_state:
 * @state_machine: a [class@Dex.StateMachine]
 *
 * Gets the most recent valid state requested with
 * [method@Dex.StateMachine.transition].
 *
 * The requested state is initialized to the initial state passed to
 * [ctor@Dex.StateMachine.new]. It is updated immediately when
 * [method@Dex.StateMachine.transition] is called with a valid target state,
 * before that transition is run. It is not a guarantee that the transition
 * will run or succeed, and it may differ from the current state returned by
 * [method@Dex.StateMachine.get_state].
 *
 * Returns: the most recent valid requested state
 *
 * Since: 1.2
 */
guint
dex_state_machine_get_requested_state (DexStateMachine *state_machine)
{
  guint ret;

  g_return_val_if_fail (DEX_IS_STATE_MACHINE (state_machine), 0);

  dex_object_lock (state_machine);
  ret = state_machine->requested_state;
  dex_object_unlock (state_machine);

  return ret;
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
