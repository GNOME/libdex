/*
 * dex-state-machine.h
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

#pragma once

#if !defined (DEX_INSIDE) && !defined (DEX_COMPILATION)
# error "Only <libdex.h> can be included directly."
#endif

#include "dex-future.h"
#include "dex-scheduler.h"
#include "dex-version-macros.h"

G_BEGIN_DECLS

#define DEX_TYPE_STATE_MACHINE       (dex_state_machine_get_type())
#define DEX_STATE_MACHINE(object)    (G_TYPE_CHECK_INSTANCE_CAST(object, DEX_TYPE_STATE_MACHINE, DexStateMachine))
#define DEX_IS_STATE_MACHINE(object) (G_TYPE_CHECK_INSTANCE_TYPE(object, DEX_TYPE_STATE_MACHINE))

typedef struct _DexStateMachine DexStateMachine;

/**
 * DexStateTransitionFunc:
 * @from: the current state
 * @to: (inout): the target state for the current edge
 * @user_data: (nullable): user data provided to [ctor@Dex.StateMachine.new]
 * @error: a location for a `GError`, or %NULL
 *
 * Callback used to execute a transition edge.
 *
 * On entry, @to contains the declared target for the edge being executed. If
 * the callback succeeds and leaves @to unchanged, the transition completes at
 * that state. If the callback changes @to, [class@Dex.StateMachine] commits
 * the changed state instead. The changed state must be a valid value in the
 * state enum.
 *
 * Returns: %TRUE if the transition succeeded; otherwise %FALSE and @error is set
 */
typedef gboolean (*DexStateTransitionFunc) (guint      from,
                                            guint     *to,
                                            gpointer   user_data,
                                            GError   **error);

/**
 * DexStateTransition:
 * @from: the state this edge may start from
 * @to: the state this edge transitions to
 * @func: callback used to execute the edge
 *
 * Describes one supported transition edge in a [class@Dex.StateMachine].
 */
typedef struct _DexStateTransition
{
  guint                  from;
  guint                  to;
  DexStateTransitionFunc func;
} DexStateTransition;

DEX_AVAILABLE_IN_1_2
GType            dex_state_machine_get_type   (void) G_GNUC_CONST;
DEX_AVAILABLE_IN_1_2
DexStateMachine *dex_state_machine_new        (GType                     state_enum_type,
                                               guint                     initial_state,
                                               const DexStateTransition *transitions,
                                               guint                     n_transitions,
                                               DexScheduler             *scheduler,
                                               gsize                     stack_size,
                                               gpointer                  user_data,
                                               GDestroyNotify            user_data_destroy);
DEX_AVAILABLE_IN_1_2
DexFuture       *dex_state_machine_transition (DexStateMachine          *state_machine,
                                               guint                     target) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
guint            dex_state_machine_get_state  (DexStateMachine          *state_machine);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexStateMachine, dex_unref)

G_END_DECLS
