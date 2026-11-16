/* dex-generator.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "dex-generator.h"
#include "dex-thunk-private.h"
#include "dex-block-private.h"
#include "dex-coroutine-private.h"
#include "dex-error.h"
#include "dex-fd-private.h"
#include "dex-fiber-private.h"
#include "dex-future-private.h"
#include "dex-promise.h"
#include "dex-thread-storage-private.h"

typedef struct
{
  gatomicrefcount  refs;
  guint            is_fiber : 1;
  guint            active : 1;
  guint            started : 1;
  guint            closed : 1;
  guint            exhausted : 1;
  GMutex           mutex;
  DexThunk        *thunk;
  DexScheduler    *scheduler;
  gsize            stack_size;
  DexFuture       *drain;
  DexPromise      *gate;
  DexFuture       *worker;
  DexWeakRef       pull;
  gpointer         pull_identity;
  GError          *error;
} GeneratorState;

struct _DexGenerator
{
  DexObject       parent_instance;
  GeneratorState *state;
};

typedef struct
{
  DexObjectClass parent_class;
} DexGeneratorClass;

typedef struct
{
  DexFuture       parent_instance;
  GeneratorState *state;
} DexGeneratorPull;

typedef struct
{
  DexFutureClass parent_class;
} DexGeneratorPullClass;

/**
 * DexGenerator:
 *
 * A pull-driven sequence of futures. Ordinary generators transfer each future
 * directly to the caller without observing it. Sequential consumers finish each
 * pull before requesting another and check exhaustion after awaiting the pull.
 *
 * Since: 1.3
 */
DEX_DEFINE_FINAL_TYPE (DexGenerator, dex_generator, DEX_TYPE_OBJECT)

static GType dex_generator_pull_get_type (void);

DEX_DEFINE_FINAL_TYPE (DexGeneratorPull, dex_generator_pull, DEX_TYPE_FUTURE)

static void generator_state_close (GeneratorState *state);

static GeneratorState *
generator_state_ref (GeneratorState *state)
{
  g_atomic_ref_count_inc (&state->refs);
  return state;
}

static void
generator_state_unref (gpointer data)
{
  GeneratorState *state = data;

  if (!g_atomic_ref_count_dec (&state->refs))
    return;

  dex_weak_ref_clear (&state->pull);
  dex_clear (&state->scheduler);
  dex_clear (&state->drain);
  dex_clear (&state->gate);
  dex_clear (&state->worker);
  g_clear_error (&state->error);
  g_mutex_clear (&state->mutex);
  g_free (state);
}

static void
generator_state_cleanup (GeneratorState *state)
{
  DexThunk *thunk;

  g_mutex_lock (&state->mutex);
  thunk = g_steal_pointer (&state->thunk);
  g_mutex_unlock (&state->mutex);

  g_clear_pointer (&thunk, dex_thunk_unref);
}

static DexFuture *
generator_detach_pull (GeneratorState *state)
{
  DexFuture *pull = dex_weak_ref_get (&state->pull);

  state->pull_identity = NULL;
  dex_weak_ref_set (&state->pull, NULL);
  return pull;
}

static GError *
generator_cancelled (void)
{
  return g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Generator closed");
}

static void
generator_state_close (GeneratorState *state)
{
  DexFuture *pull;
  DexFuture *worker;
  DexPromise *gate;
  gboolean cleanup;

  g_mutex_lock (&state->mutex);
  if (state->closed)
    {
      g_mutex_unlock (&state->mutex);
      return;
    }
  state->closed = TRUE;
  pull = generator_detach_pull (state);
  gate = g_steal_pointer (&state->gate);
  worker = state->worker ? dex_ref (state->worker) : NULL;
  cleanup = state->scheduler == NULL && !state->active;
  g_mutex_unlock (&state->mutex);

  if (pull != NULL)
    {
      dex_future_complete (pull, NULL, generator_cancelled ());
      dex_unref (pull);
    }

  if (worker != NULL)
    {
      dex_future_abandon (worker);
      dex_unref (worker);
    }

  if (gate != NULL)
    {
      dex_promise_reject (gate, generator_cancelled ());
      dex_unref (gate);
    }

  if (cleanup)
    generator_state_cleanup (state);
}

static void
dex_generator_pull_discard (DexFuture *future)
{
  DexGeneratorPull *self = (DexGeneratorPull *)future;
  gboolean pending;

  g_mutex_lock (&self->state->mutex);
  pending = self->state->pull_identity == self;
  g_mutex_unlock (&self->state->mutex);

  if (pending)
    generator_state_close (self->state);
}

static void
dex_generator_pull_finalize (DexObject *object)
{
  DexGeneratorPull *self = (DexGeneratorPull *)object;

  dex_generator_pull_discard (DEX_FUTURE (self));
  generator_state_unref (self->state);
  DEX_OBJECT_CLASS (dex_generator_pull_parent_class)->finalize (object);
}

static void
dex_generator_pull_class_init (DexGeneratorPullClass *klass)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (klass);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (klass);

  object_class->finalize = dex_generator_pull_finalize;
  future_class->discard = dex_generator_pull_discard;
}

static void
dex_generator_pull_init (DexGeneratorPull *self)
{
}

static void
dex_generator_finalize (DexObject *object)
{
  DexGenerator *self = DEX_GENERATOR (object);

  dex_generator_close (self);

  generator_state_unref (g_steal_pointer (&self->state));

  DEX_OBJECT_CLASS (dex_generator_parent_class)->finalize (object);
}

static void
dex_generator_class_init (DexGeneratorClass *klass)
{
  DEX_OBJECT_CLASS (klass)->finalize = dex_generator_finalize;
}

static void
dex_generator_init (DexGenerator *self)
{
  self->state = g_new0 (GeneratorState, 1);
  g_atomic_ref_count_init (&self->state->refs);
  g_mutex_init (&self->state->mutex);
  dex_weak_ref_init (&self->state->pull, NULL);
}

/**
 * dex_generator_new:
 * @producer: (scope notified) (closure user_data) (destroy destroy): item factory
 * @user_data: (nullable): factory data
 * @destroy: (nullable): destroy notify for @user_data
 *
 * Creates a generator whose factory runs directly in the calling context.
 * Returned futures must independently retain any state they need. The factory
 * data is retained until close or finalization, including after exhaustion.
 *
 * Returns: (transfer full): a new generator
 * Since: 1.3
 */
DexGenerator *
dex_generator_new (DexGeneratorFunc producer,
                   gpointer         user_data,
                   GDestroyNotify   destroy)
{
  DexGenerator *self;

  g_return_val_if_fail (producer != NULL, NULL);

  self = (DexGenerator *)dex_object_create_instance (DEX_TYPE_GENERATOR);
  self->state->thunk = dex_thunk_new (G_CALLBACK (producer), user_data, destroy, NULL);
  self->state->drain = dex_thunk_dup_completion (self->state->thunk);

  return self;
}

static void generator_start (gpointer data);

/**
 * dex_generator_next:
 * @self: a generator
 *
 * Pulls an item. Concurrent or recursive factory execution is rejected.
 * Adapter generators allow only one outstanding pull. Their terminal pull
 * resolves after setting exhaustion; check exhaustion before reading its value.
 * Explicitly closed generators reject with %G_IO_ERROR_CLOSED.
 *
 * Returns: (transfer full) (nullable): the item future, or %NULL when exhausted
 * Since: 1.3
 */
DexFuture *
dex_generator_next (DexGenerator *self)
{
  GeneratorState *state;
  DexFuture *result = NULL;
  GError *rejection = NULL;
  DexPromise *gate = NULL;
  gboolean start = FALSE;
  gboolean cleanup;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), NULL);

  state = generator_state_ref (self->state);

  g_mutex_lock (&state->mutex);

  if (state->exhausted)
    {
      result = NULL;
    }
  else if (state->closed)
    {
      rejection = g_error_new_literal (G_IO_ERROR,
                                       G_IO_ERROR_CLOSED,
                                       "Generator closed");
    }
  else if (state->error != NULL)
    {
      rejection = g_error_copy (state->error);
    }
  else if (state->active || state->pull_identity != NULL)
    {
      rejection = g_error_new_literal (DEX_ERROR,
                                       DEX_ERROR_INVALID_TRANSITION,
                                       "A generator pull is already active");
    }
  else if (state->scheduler != NULL)
    {
      DexGeneratorPull *pull;

      pull = (DexGeneratorPull *)dex_object_create_instance (dex_generator_pull_get_type ());
      pull->state = generator_state_ref (state);
      state->pull_identity = pull;
      dex_weak_ref_set (&state->pull, pull);
      gate = g_steal_pointer (&state->gate);

      start = !state->started;
      state->started = TRUE;

      result = DEX_FUTURE (pull);
    }
  else
    {
      state->active = TRUE;

      g_mutex_unlock (&state->mutex);
      result = dex_thunk_invoke (state->thunk);
      g_mutex_lock (&state->mutex);

      state->active = FALSE;

      if (result == NULL && !state->closed)
        state->exhausted = TRUE;
    }

  cleanup = state->closed && state->scheduler == NULL && !state->active;

  g_mutex_unlock (&state->mutex);

  if (rejection != NULL)
    result = dex_future_new_for_error (rejection);

  if (start)
    dex_scheduler_push (state->scheduler, generator_start, generator_state_ref (state));

  if (gate != NULL)
    {
      dex_promise_resolve_boolean (gate, TRUE);

      dex_unref (gate);
    }

  if (cleanup)
    generator_state_cleanup (state);

  generator_state_unref (state);

  return result;
}

/**
 * dex_generator_is_exhausted:
 * @self: a generator
 *
 * Tests natural exhaustion. Close and producer failure do not imply exhaustion.
 * This flag classifies sequential pulls, not previously prefetched items.
 *
 * Returns: whether production ended normally
 * Since: 1.3
 */
gboolean
dex_generator_is_exhausted (DexGenerator *self)
{
  gboolean exhausted;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);

  g_mutex_lock (&self->state->mutex);
  exhausted = self->state->exhausted;
  g_mutex_unlock (&self->state->mutex);

  return exhausted;
}

/**
 * dex_generator_close:
 * @self: a generator
 *
 * Requests shutdown without blocking. Ordinary item futures remain independent.
 * Adapter cancellation is cooperative; producers must honor failed yields and
 * awaits. Cleanup runs on the adapter scheduler, or on the thread closing an
 * ordinary generator (the callback thread if a callback is still active).
 *
 * Since: 1.3
 */
void
dex_generator_close (DexGenerator *self)
{
  GeneratorState *state;
  gboolean start;

  g_return_if_fail (DEX_IS_GENERATOR (self));

  state = generator_state_ref (self->state);
  generator_state_close (state);

  g_mutex_lock (&state->mutex);
  start = state->scheduler != NULL && !state->started;
  state->started |= start;
  g_mutex_unlock (&state->mutex);

  if (start)
    dex_scheduler_push (state->scheduler, generator_start, generator_state_ref (state));

  generator_state_unref (state);
}

/**
 * dex_generator_close_after_drain:
 * @self: a generator
 *
 * Closes and waits for callback data destruction. Repeated calls share a result.
 * Production errors are reported by pulls, not drain. Drain can remain pending
 * while a producer or coroutine-owned external operation is still running.
 *
 * Returns: (transfer full): a future resolving to %TRUE after cleanup
 * Since: 1.3
 */
DexFuture *
dex_generator_close_after_drain (DexGenerator *self)
{
  DexFuture *drain;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), NULL);

  drain = dex_ref (self->state->drain);
  dex_generator_close (self);

  return drain;
}

static DexFuture *
generator_await_item (DexGenerator  *self,
                      GError       **error)
{
  DexFuture *item;

  dex_ref (self);

  if ((item = dex_generator_next (self)))
    {
      if (dex_await_borrowed (item, error) == NULL || dex_generator_is_exhausted (self))
        dex_clear (&item);
    }

  dex_unref (self);

  return item;
}

/**
 * dex_generator_await_next:
 * @self: a generator
 * @value: (out caller-allocates): an initially uninitialized value
 * @error: return location for an error
 *
 * Consumes one sequential pull. Rejection is checked before exhaustion. On
 * success, unset @value after use. On exhaustion or error it stays uninitialized.
 * Completed pulls can be consumed outside fibers; pending work requires a fiber.
 *
 * Returns: %TRUE for an item, including false, zero, and null payloads
 * Since: 1.3
 */
gboolean
dex_generator_await_next (DexGenerator  *self,
                          GValue        *value,
                          GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL && G_VALUE_TYPE (value) == 0, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);
  g_value_init (value, G_VALUE_TYPE (resolved));
  g_value_copy (resolved, value);

  dex_unref (item);

  return TRUE;
}

static DexFuture *
generator_finished (DexFuture *completed,
                    gpointer   data)
{
  GeneratorState *state = data;
  DexFuture *pull;
  DexFuture *worker;
  GError *error = NULL;

  dex_future_get_value (completed, &error);

  g_mutex_lock (&state->mutex);

  if (!state->closed)
    {
      state->exhausted = error == NULL;
      state->error = error ? g_error_copy (error) : NULL;
    }

  pull = generator_detach_pull (state);
  worker = g_steal_pointer (&state->worker);

  g_mutex_unlock (&state->mutex);

  if (pull != NULL)
    {
      if (error != NULL)
        {
          dex_future_complete (pull, NULL, g_steal_pointer (&error));
        }
      else
        {
          DexFuture *end = dex_future_new_true ();

          dex_future_complete_from (pull, end);
          dex_unref (end);
        }

      dex_unref (pull);
    }

  dex_clear (&worker);
  g_clear_error (&error);

  return dex_future_new_true ();
}

static void
generator_worker_destroy (gpointer data)
{
  GeneratorState *state = data;

  generator_state_cleanup (state);
  generator_state_unref (state);
}

static DexFiber *
generator_current_fiber (void)
{
  DexThreadStorage *storage = dex_thread_storage_peek ();

  return storage && storage->fiber_scheduler ? storage->fiber_scheduler->running : NULL;
}

static DexFuture *
generator_fiber_run (gpointer data)
{
  GeneratorState *state = data;
  DexFiber *fiber = generator_current_fiber ();
  DexFuture *result;

  fiber->generator_state = state;
  result = dex_thunk_invoke (state->thunk);
  fiber->generator_state = NULL;

  return result;
}

static DexFuture *
generator_coroutine_run (DexCoroutineContext *context,
                         gpointer             data)
{
  GeneratorState *state = data;

  dex_coroutine_context_set_generator (context, state);

  return dex_thunk_invoke_coroutine (state->thunk, context);
}

static void
generator_start (gpointer data)
{
  GeneratorState *state = data;
  DexFuture *worker;
  DexFuture *monitor;
  gboolean closed;
  gboolean is_fiber;

  g_mutex_lock (&state->mutex);
  closed = state->closed;
  is_fiber = state->is_fiber;
  g_mutex_unlock (&state->mutex);

  if (closed)
    {
      generator_worker_destroy (state);
      return;
    }

  if (is_fiber)
    worker = dex_scheduler_spawn (state->scheduler, state->stack_size,
                                  generator_fiber_run, generator_state_ref (state),
                                  generator_worker_destroy);
  else
    worker = dex_scheduler_spawn_coroutine (state->scheduler,
                                            generator_coroutine_run, generator_state_ref (state),
                                            generator_worker_destroy);

  g_mutex_lock (&state->mutex);
  state->worker = dex_ref (worker);
  closed = state->closed;
  g_mutex_unlock (&state->mutex);

  monitor = dex_block_new (dex_ref (worker),
                           state->scheduler,
                           DEX_BLOCK_KIND_FINALLY,
                           generator_finished,
                           generator_state_ref (state),
                           generator_state_unref);

  dex_future_disown_full (monitor, state->scheduler);

  if (closed)
    dex_future_abandon (worker);

  dex_unref (worker);
  generator_state_unref (state);
}

static DexGenerator *
generator_new_adapter (DexScheduler   *scheduler,
                       GCallback       callback,
                       gpointer        user_data,
                       GDestroyNotify  destroy)
{
  DexGenerator *self;
  DexThreadStorage *storage;

  self = (DexGenerator *)dex_object_create_instance (DEX_TYPE_GENERATOR);

  if (scheduler == NULL)
    {
      storage = dex_thread_storage_peek ();
      scheduler = storage && storage->scheduler ? storage->scheduler : dex_scheduler_get_default ();
    }

  self->state->scheduler = dex_ref (scheduler);
  self->state->thunk = dex_thunk_new (callback, user_data, destroy, scheduler);
  self->state->drain = dex_thunk_dup_completion (self->state->thunk);

  return self;
}

/**
 * dex_generator_new_fiber:
 * @scheduler: (nullable): scheduler, or %NULL to capture the current scheduler
 * @stack_size: stack size, or zero for the default
 * @func: (scope notified) (closure user_data) (destroy destroy): producer
 * @user_data: (nullable): producer data
 * @destroy: (nullable): producer data destroy notify
 *
 * Creates a lazy fiber producer. The first pull starts the fiber; each yield
 * waits for another pull. A successful final future means exhaustion, and a
 * rejected final future means failure. Returning %NULL retains the normal fiber
 * error. If no thread scheduler exists, the process default is captured.
 *
 * Returns: (transfer full): a generator
 * Since: 1.3
 */
DexGenerator *
dex_generator_new_fiber (DexScheduler   *scheduler,
                         gsize           stack_size,
                         DexFiberFunc    func,
                         gpointer        user_data,
                         GDestroyNotify  destroy)
{
  DexGenerator *self;

  g_return_val_if_fail (scheduler == NULL || DEX_IS_SCHEDULER (scheduler), NULL);
  g_return_val_if_fail (func != NULL, NULL);

  self = generator_new_adapter (scheduler, G_CALLBACK (func), user_data, destroy);
  self->state->is_fiber = TRUE;
  self->state->stack_size = stack_size;

  return self;
}

/**
 * dex_generator_new_coroutine:
 * @scheduler: (nullable): scheduler, or %NULL to capture the current scheduler
 * @func: (scope notified) (closure user_data) (destroy destroy): producer
 * @user_data: (nullable): producer data
 * @destroy: (nullable): producer data destroy notify
 *
 * Creates a lazy coroutine producer with the same demand protocol as a fiber
 * generator. State surviving suspension belongs in @user_data. Cancellation
 * retains data until pending work completes. If no thread scheduler exists,
 * the process default is captured.
 *
 * Returns: (transfer full): a generator
 * Since: 1.3
 */
DexGenerator *
dex_generator_new_coroutine (DexScheduler     *scheduler,
                             DexCoroutineFunc  func,
                             gpointer          user_data,
                             GDestroyNotify    destroy)
{
  g_return_val_if_fail (scheduler == NULL || DEX_IS_SCHEDULER (scheduler), NULL);
  g_return_val_if_fail (func != NULL, NULL);

  return generator_new_adapter (scheduler, G_CALLBACK (func), user_data, destroy);
}

static DexFuture *
generator_yield (GeneratorState *state,
                 const GValue   *value)
{
  DexFuture *pull;
  DexPromise *gate;

  if (state == NULL)
    return dex_future_new_reject (DEX_ERROR, DEX_ERROR_INVALID_TRANSITION,
                                  "Not a generator producer");

  g_mutex_lock (&state->mutex);

  if (state->closed)
    {
      g_mutex_unlock (&state->mutex);
      return dex_future_new_for_error (generator_cancelled ());
    }

  pull = generator_detach_pull (state);

  if (pull == NULL)
    {
      g_mutex_unlock (&state->mutex);
      generator_state_close (state);
      return dex_future_new_for_error (generator_cancelled ());
    }

  gate = dex_promise_new ();
  state->gate = dex_ref (gate);

  g_mutex_unlock (&state->mutex);

  dex_future_complete (pull, value, NULL);
  dex_unref (pull);

  return DEX_FUTURE (gate);
}

/**
 * dex_fiber_yield_value:
 * @value: the item to copy
 * @error: return location for an error
 *
 * Yields a copy of @value and waits for the next pull. Only valid in a
 * generator producer fiber. Return from the producer if yielding fails.
 * Future values are retained without observing them.
 *
 * Returns: %TRUE when another pull resumes the producer
 * Since: 1.3
 */
gboolean
dex_fiber_yield_value (const GValue  *value,
                       GError       **error)
{
  DexFiber *fiber = generator_current_fiber ();

  g_return_val_if_fail (G_IS_VALUE (value), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (fiber == NULL)
    {
      g_set_error_literal (error,
                           DEX_ERROR,
                           DEX_ERROR_NO_FIBER,
                           "Not in a fiber");
      return FALSE;
    }

  return dex_await_boolean (generator_yield (fiber->generator_state, value), error);
}

/**
 * dex_coroutine_context_yield_value: (skip)
 * @context: the producer coroutine context
 * @value: the item to copy
 *
 * Publishes @value before returning. Suspend on the returned demand gate using
 * DEX_COROUTINE_YIELD_VALUE(). Yield expressions are evaluated once per yield.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_value (DexCoroutineContext *context,
                                   const GValue        *value)
{
  g_return_val_if_fail (context != NULL, NULL);
  g_return_val_if_fail (G_IS_VALUE (value), NULL);

  return generator_yield (dex_coroutine_context_get_generator (context), value);
}

/**
 * dex_generator_await_next_boolean:
 * @self: a generator
 * @value: (out): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for boolean values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_boolean (DexGenerator  *self,
                                  gboolean      *value,
                                  GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = FALSE;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_BOOLEAN))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_BOOLEAN),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_get_boolean (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_boolean:
 * @value: the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for boolean values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_boolean (gboolean   value,
                         GError   **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_BOOLEAN);
  g_value_set_boolean (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_boolean: (skip)
 * @context: producer coroutine context
 * @value: the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for boolean values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_boolean (DexCoroutineContext *context,
                                     gboolean             value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_BOOLEAN);
  g_value_set_boolean (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_int:
 * @self: a generator
 * @value: (out): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for int values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_int (DexGenerator  *self,
                              int           *value,
                              GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = 0;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_INT))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_INT),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_get_int (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_int:
 * @value: the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for int values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_int (int      value,
                     GError **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_INT);
  g_value_set_int (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_int: (skip)
 * @context: producer coroutine context
 * @value: the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for int values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_int (DexCoroutineContext *context,
                                 int                  value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_INT);
  g_value_set_int (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_uint:
 * @self: a generator
 * @value: (out): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for uint values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_uint (DexGenerator  *self,
                               guint         *value,
                               GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = 0;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_UINT))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_UINT),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_get_uint (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_uint:
 * @value: the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for uint values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_uint (guint    value,
                      GError **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_UINT);
  g_value_set_uint (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_uint: (skip)
 * @context: producer coroutine context
 * @value: the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for uint values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_uint (DexCoroutineContext *context,
                                  guint                value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_UINT);
  g_value_set_uint (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_int64:
 * @self: a generator
 * @value: (out): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for int64 values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_int64 (DexGenerator  *self,
                                gint64        *value,
                                GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = 0;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_INT64))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_INT64),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_get_int64 (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_int64:
 * @value: the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for int64 values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_int64 (gint64   value,
                       GError **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_INT64);
  g_value_set_int64 (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_int64: (skip)
 * @context: producer coroutine context
 * @value: the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for int64 values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_int64 (DexCoroutineContext *context,
                                   gint64               value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_INT64);
  g_value_set_int64 (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_uint64:
 * @self: a generator
 * @value: (out): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for uint64 values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_uint64 (DexGenerator  *self,
                                 guint64       *value,
                                 GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = 0;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_UINT64))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_UINT64),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_get_uint64 (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_uint64:
 * @value: the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for uint64 values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_uint64 (guint64   value,
                        GError  **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_UINT64);
  g_value_set_uint64 (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_uint64: (skip)
 * @context: producer coroutine context
 * @value: the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for uint64 values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_uint64 (DexCoroutineContext *context,
                                    guint64              value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_UINT64);
  g_value_set_uint64 (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_float:
 * @self: a generator
 * @value: (out): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for float values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_float (DexGenerator  *self,
                                float         *value,
                                GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = 0;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_FLOAT))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_FLOAT),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_get_float (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_float:
 * @value: the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for float values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_float (float    value,
                       GError **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_FLOAT);
  g_value_set_float (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_float: (skip)
 * @context: producer coroutine context
 * @value: the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for float values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_float (DexCoroutineContext *context,
                                   float                value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_FLOAT);
  g_value_set_float (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_double:
 * @self: a generator
 * @value: (out): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for double values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_double (DexGenerator  *self,
                                 double        *value,
                                 GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = 0;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_DOUBLE))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_DOUBLE),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_get_double (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_double:
 * @value: the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for double values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_double (double   value,
                        GError **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_DOUBLE);
  g_value_set_double (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_double: (skip)
 * @context: producer coroutine context
 * @value: the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for double values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_double (DexCoroutineContext *context,
                                    double               value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_DOUBLE);
  g_value_set_double (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_enum:
 * @self: a generator
 * @expected_type: expected item type
 * @value: (out): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for enum values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_enum (DexGenerator  *self,
                               GType          expected_type,
                               int           *value,
                               GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (g_type_is_a (expected_type, G_TYPE_ENUM), FALSE);

  *value = 0;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, expected_type))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (expected_type),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_get_enum (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_enum:
 * @expected_type: expected item type
 * @value: the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for enum values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_enum (GType    expected_type,
                      int      value,
                      GError **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_return_val_if_fail (g_type_is_a (expected_type, G_TYPE_ENUM), FALSE);

  g_value_init (&item, expected_type);
  g_value_set_enum (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_enum: (skip)
 * @context: producer coroutine context
 * @expected_type: expected item type
 * @value: the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for enum values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_enum (DexCoroutineContext *context,
                                  GType                expected_type,
                                  int                  value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_return_val_if_fail (g_type_is_a (expected_type, G_TYPE_ENUM), NULL);

  g_value_init (&item, expected_type);
  g_value_set_enum (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_flags:
 * @self: a generator
 * @expected_type: expected item type
 * @value: (out): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for flags values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_flags (DexGenerator  *self,
                                GType          expected_type,
                                guint         *value,
                                GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (g_type_is_a (expected_type, G_TYPE_FLAGS), FALSE);

  *value = 0;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, expected_type))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (expected_type),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_get_flags (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_flags:
 * @expected_type: expected item type
 * @value: the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for flags values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_flags (GType    expected_type,
                       guint    value,
                       GError **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_return_val_if_fail (g_type_is_a (expected_type, G_TYPE_FLAGS), FALSE);

  g_value_init (&item, expected_type);
  g_value_set_flags (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_flags: (skip)
 * @context: producer coroutine context
 * @expected_type: expected item type
 * @value: the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for flags values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_flags (DexCoroutineContext *context,
                                   GType                expected_type,
                                   guint                value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_return_val_if_fail (g_type_is_a (expected_type, G_TYPE_FLAGS), NULL);

  g_value_init (&item, expected_type);
  g_value_set_flags (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_string:
 * @self: a generator
 * @value: (out) (transfer full) (nullable): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for string values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_string (DexGenerator  *self,
                                 char         **value,
                                 GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = NULL;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_STRING))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_STRING),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_dup_string (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_string:
 * @value: (nullable): the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for string values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_string (const char  *value,
                        GError     **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_STRING);
  g_value_set_string (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_string: (skip)
 * @context: producer coroutine context
 * @value: (nullable): the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for string values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_string (DexCoroutineContext *context,
                                    const char          *value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_STRING);
  g_value_set_string (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_variant:
 * @self: a generator
 * @value: (out) (transfer full) (nullable): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for variant values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_variant (DexGenerator  *self,
                                  GVariant     **value,
                                  GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = NULL;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_VARIANT))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_VARIANT),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_dup_variant (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_variant:
 * @value: (nullable): the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for variant values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_variant (GVariant  *value,
                         GError   **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_VARIANT);
  g_value_set_variant (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_variant: (skip)
 * @context: producer coroutine context
 * @value: (nullable): the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for variant values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_variant (DexCoroutineContext *context,
                                     GVariant            *value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_VARIANT);
  g_value_set_variant (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_boxed:
 * @self: a generator
 * @expected_type: expected item type
 * @value: (out) (transfer full) (nullable): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for boxed values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_boxed (DexGenerator  *self,
                                GType          expected_type,
                                gpointer      *value,
                                GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (g_type_is_a (expected_type, G_TYPE_BOXED), FALSE);

  *value = NULL;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, expected_type))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (expected_type),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_dup_boxed (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_boxed:
 * @expected_type: expected item type
 * @value: (nullable): the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for boxed values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_boxed (GType      expected_type,
                       gpointer   value,
                       GError   **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_return_val_if_fail (g_type_is_a (expected_type, G_TYPE_BOXED), FALSE);

  g_value_init (&item, expected_type);
  g_value_set_boxed (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_boxed: (skip)
 * @context: producer coroutine context
 * @expected_type: expected item type
 * @value: (nullable): the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for boxed values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_boxed (DexCoroutineContext *context,
                                   GType                expected_type,
                                   gpointer             value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_return_val_if_fail (g_type_is_a (expected_type, G_TYPE_BOXED), NULL);

  g_value_init (&item, expected_type);
  g_value_set_boxed (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_pointer:
 * @self: a generator
 * @value: (out) (transfer none) (nullable): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for pointer values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_pointer (DexGenerator  *self,
                                  gpointer      *value,
                                  GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = NULL;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_POINTER))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_POINTER),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_get_pointer (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_pointer:
 * @value: (nullable): the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for pointer values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_pointer (gpointer   value,
                         GError   **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_POINTER);
  g_value_set_pointer (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_pointer: (skip)
 * @context: producer coroutine context
 * @value: (nullable): the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for pointer values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_pointer (DexCoroutineContext *context,
                                     gpointer             value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_POINTER);
  g_value_set_pointer (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_object:
 * @self: a generator
 * @value: (out) (transfer full) (nullable): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for object values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_object (DexGenerator  *self,
                                 GObject      **value,
                                 GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = NULL;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, G_TYPE_OBJECT))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (G_TYPE_OBJECT),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = g_value_dup_object (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_object:
 * @value: (nullable): the item to publish
 * @error: return location for an error
 *
 * Like dex_fiber_yield_value(), for object values.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_object (GObject  *value,
                        GError  **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, G_TYPE_OBJECT);
  g_value_set_object (&item, value);
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_object: (skip)
 * @context: producer coroutine context
 * @value: (nullable): the item to publish
 *
 * Like dex_coroutine_context_yield_value(), for object values.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_object (DexCoroutineContext *context,
                                    GObject             *value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, G_TYPE_OBJECT);
  g_value_set_object (&item, value);
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_future:
 * @self: a generator
 * @value: (out) (transfer full) (nullable): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Like dex_generator_await_next(), for future values.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_future (DexGenerator  *self,
                                 DexFuture    **value,
                                 GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = NULL;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, DEX_TYPE_FUTURE))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (DEX_TYPE_FUTURE),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = (DexFuture *)dex_value_dup_object (resolved);
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_future:
 * @value: (nullable): the item to publish
 * @error: return location for an error
 *
 * Publishes a future as an item without observing it. The future is retained.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_future (DexFuture  *value,
                        GError    **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_return_val_if_fail (value == NULL || DEX_IS_FUTURE (value), FALSE);

  g_value_init (&item, DEX_TYPE_FUTURE);
  dex_value_set_object (&item, DEX_OBJECT (value));
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_future: (skip)
 * @context: producer coroutine context
 * @value: (nullable): the item to publish
 *
 * Publishes a future as an item without observing it. The future is retained.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_future (DexCoroutineContext *context,
                                    DexFuture           *value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_return_val_if_fail (value == NULL || DEX_IS_FUTURE (value), NULL);

  g_value_init (&item, DEX_TYPE_FUTURE);
  dex_value_set_object (&item, DEX_OBJECT (value));
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}

/**
 * dex_generator_await_next_fd:
 * @self: a generator
 * @value: (out): item output, reset on exhaustion or failure
 * @error: return location for an error
 *
 * Consumes a sequential fd item. See dex_generator_await_next().
 * The caller owns the returned file descriptor.
 *
 * Returns: whether an item was received
 * Since: 1.3
 */
gboolean
dex_generator_await_next_fd (DexGenerator  *self,
                             int           *value,
                             GError       **error)
{
  DexFuture *item;
  const GValue *resolved;

  g_return_val_if_fail (DEX_IS_GENERATOR (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *value = -1;

  if (!(item = generator_await_item (self, error)))
    return FALSE;

  resolved = dex_future_get_value (item, NULL);

  if (!G_VALUE_HOLDS (resolved, DEX_TYPE_FD))
    {
      g_set_error (error,
                   DEX_ERROR,
                   DEX_ERROR_TYPE_MISMATCH,
                   "Expected %s, got %s",
                   g_type_name (DEX_TYPE_FD),
                   G_VALUE_TYPE_NAME (resolved));
      dex_unref (item);
      return FALSE;
    }

  *value = dex_fd_steal (g_value_get_boxed (resolved));
  dex_unref (item);

  return TRUE;
}

/**
 * dex_fiber_yield_fd:
 * @value: the file descriptor to take ownership of
 * @error: return location for an error
 *
 * Publishes a file descriptor, taking ownership of @value.
 *
 * Returns: %TRUE when resumed for another pull
 * Since: 1.3
 */
gboolean
dex_fiber_yield_fd (int      value,
                    GError **error)
{
  GValue item = G_VALUE_INIT;
  gboolean result;

  g_value_init (&item, DEX_TYPE_FD);
  g_value_take_boxed (&item, g_memdup2 (&value, sizeof value));
  result = dex_fiber_yield_value (&item, error);
  g_value_unset (&item);

  return result;
}

/**
 * dex_coroutine_context_yield_fd: (skip)
 * @context: producer coroutine context
 * @value: the file descriptor to take ownership of
 *
 * Publishes a file descriptor, taking ownership of @value.
 *
 * Returns: (transfer full): the next-demand gate
 * Since: 1.3
 */
DexFuture *
dex_coroutine_context_yield_fd (DexCoroutineContext *context,
                                int                  value)
{
  GValue item = G_VALUE_INIT;
  DexFuture *result;

  g_value_init (&item, DEX_TYPE_FD);
  g_value_take_boxed (&item, g_memdup2 (&value, sizeof value));
  result = dex_coroutine_context_yield_value (context, &item);
  g_value_unset (&item);

  return result;
}
