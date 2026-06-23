/*
 * dex-scheduler.c
 *
 * Copyright 2022 Christian Hergert <christian@sourceandstack.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>
#include <gobject/gvaluecollector.h>

#include "dex-aio-backend-private.h"
#include "dex-closure.h"
#include "dex-coroutine-private.h"
#include "dex-error.h"
#include "dex-fiber-private.h"
#include "dex-scheduler-private.h"
#include "dex-thread-storage-private.h"

/**
 * DexScheduler:
 *
 * `DexScheduler` is the base class used by schedulers.
 *
 * Schedulers are responsible for ensuring asynchronous IO requests and
 * completions are processed. They also schedule closures to be run as part
 * of future result propagation. Additionally, they manage [class@Dex.Fiber]
 * execution and suspension.
 *
 * Specialized schedulers such as [class@Dex.ThreadPoolScheduler] will do this
 * for a number of threads and dispatch new work between them.
 */

static DexScheduler *default_scheduler;

DEX_DEFINE_ABSTRACT_TYPE (DexScheduler, dex_scheduler, DEX_TYPE_OBJECT)

#undef DEX_TYPE_SCHEDULER
#define DEX_TYPE_SCHEDULER dex_scheduler_type

static void
dex_scheduler_class_init (DexSchedulerClass *scheduler_class)
{
}

static void
dex_scheduler_init (DexScheduler *scheduler)
{
}

/**
 * dex_scheduler_get_default:
 *
 * Gets the default scheduler for the process.
 *
 * The default scheduler executes tasks within the default
 * [struct@GLib.MainContext].  Typically that is the main thread of the
 * application.
 *
 * Returns: (transfer none) (not nullable): a [class@Dex.Scheduler]
 */
DexScheduler *
dex_scheduler_get_default (void)
{
  return default_scheduler;
}

void
dex_scheduler_set_default (DexScheduler *scheduler)
{
  g_return_if_fail (default_scheduler == NULL);
  g_return_if_fail (scheduler != NULL);

  default_scheduler = scheduler;
}

/**
 * dex_scheduler_get_thread_default:
 *
 * Gets the default scheduler for the thread.
 *
 * Returns: (transfer none) (nullable): a [class@Dex.Scheduler] or %NULL
 */
DexScheduler *
dex_scheduler_get_thread_default (void)
{
  return dex_thread_storage_get ()->scheduler;
}

void
dex_scheduler_set_thread_default (DexScheduler *scheduler)
{
  dex_thread_storage_get ()->scheduler = scheduler;
}

/**
 * dex_scheduler_ref_thread_default:
 *
 * Gets the thread default scheduler with the reference count incremented.
 *
 * Returns: (transfer full) (nullable): a [class@Dex.Scheduler] or %NULL
 */
DexScheduler *
dex_scheduler_ref_thread_default (void)
{
  DexScheduler *scheduler = dex_scheduler_get_thread_default ();

  if (scheduler != NULL)
    return dex_ref (scheduler);

  return NULL;
}

/**
 * dex_scheduler_push:
 * @scheduler: a [class@Dex.Scheduler]
 * @func: (scope async): the function callback
 * @func_data: the closure data for @func
 *
 * Queues @func to run on @scheduler.
 */
void
dex_scheduler_push (DexScheduler     *scheduler,
                    DexSchedulerFunc  func,
                    gpointer          func_data)
{
  g_return_if_fail (DEX_IS_SCHEDULER (scheduler));
  g_return_if_fail (func != NULL);

  DEX_SCHEDULER_GET_CLASS (scheduler)->push (scheduler, (DexWorkItem) {func, func_data});
}

/**
 * dex_scheduler_get_main_context:
 * @scheduler: a [class@Dex.Scheduler]
 *
 * Gets the default main context for a scheduler.
 *
 * This may be a different value depending on the calling thread.
 *
 * For example, calling this on the [class@Dex.ThreadPoolScheduler] from
 * outside a worker thread may result in getting a shared
 * [struct@GLib.MainContext] for the process.
 *
 * However, calling from a worker thread may give you a [struct@GLib.MainContext]
 * specifically for that thread.
 *
 * Returns: (transfer none): a [struct@GLib.MainContext]
 */
GMainContext *
dex_scheduler_get_main_context (DexScheduler *scheduler)
{
  return DEX_SCHEDULER_GET_CLASS (scheduler)->get_main_context (scheduler);
}

/**
 * dex_scheduler_get_aio_context: (skip)
 * @scheduler: a [class@Dex.Scheduler]
 *
 * Gets a `DexAioContext` for the scheduler.
 *
 * This context can be used to execute asyncronous operations within the
 * context of the scheduler. Generally this is done using asynchronous
 * operations and submission/completions managed by the threads scheduler.
 *
 * Stability: Private
 */
DexAioContext *
dex_scheduler_get_aio_context (DexScheduler *scheduler)
{
  return DEX_SCHEDULER_GET_CLASS (scheduler)->get_aio_context (scheduler);
}

/**
 * dex_scheduler_spawn:
 * @scheduler: (nullable): a [class@Dex.Scheduler]
 * @stack_size: stack size in bytes or 0
 * @func: (scope notified) (closure func_data) (destroy func_data_destroy): a [callback@Dex.FiberFunc]
 * @func_data: closure data for @func
 * @func_data_destroy: closure notify for @func_data
 *
 * Request @scheduler to spawn a [class@Dex.Fiber].
 *
 * The fiber will have its own stack and cooperatively schedules among other
 * fibers sharing the scheduler.
 *
 * This can be called from any thread. The resulting fiber runs on the thread
 * associated with the @scheduler.
 *
 * If @stack_size is 0, it will set to a sensible default. Otherwise, it is
 * rounded up to the nearest page size.
 *
 * ```c
 * static DexFuture *
 * fiber_func (gpointer data)
 * {
 *   GInputStream *stream = data;
 *   GError *error = NULL;
 *   GBytes *bytes = NULL;
 *
 *   if (!(bytes = dex_await_boxed (dex_input_stream_read_bytes (stream, 4096, 0), &error)))
 *     return dex_future_new_for_error (error);
 *
 *   ...
 *
 *   return dex_future_new_true ();
 * }
 *
 * DexFuture *
 * spawn_fiber (GInputStream *stream)
 * {
 *   return dex_scheduler_spawn (NULL, 0, fiber_func,
 *                               g_object_ref (stream),
 *                               g_object_unref);
 * }
 * ```
 *
 * Returns: (transfer full): a [class@Dex.Future] that will resolve or reject when
 *   @func completes (or its resulting `DexFuture` completes).
 */
DexFuture *
(dex_scheduler_spawn) (DexScheduler   *scheduler,
                       gsize           stack_size,
                       DexFiberFunc    func,
                       gpointer        func_data,
                       GDestroyNotify  func_data_destroy)
{
  DexFiber *fiber;

  dex_return_error_if_fail (!scheduler || DEX_IS_SCHEDULER (scheduler));
  dex_return_error_if_fail (func != NULL);

  if (scheduler == NULL)
    scheduler = dex_scheduler_get_default ();

  dex_return_error_if_fail (scheduler != NULL);
  dex_return_error_if_fail (DEX_SCHEDULER_GET_CLASS (scheduler)->spawn != NULL);

  fiber = dex_fiber_new (func, func_data, func_data_destroy, stack_size);
  DEX_SCHEDULER_GET_CLASS (scheduler)->spawn (scheduler, fiber);
  return DEX_FUTURE (fiber);
}

/**
 * dex_scheduler_spawn_coroutine:
 * @scheduler: (nullable): a [class@Dex.Scheduler]
 * @func: (scope notified): coroutine entrypoint
 * @user_data: (transfer none): user data passed to the coroutine entrypoint
 * @user_data_destroy: destroy notify for @user_data
 *
 * Request @scheduler to spawn a [class@Dex.Coroutine] and execute
 * @func with user data.
 *
 * If the function returns %NULL while suspended, it should have set an awaited
 * future in its context using one of the [macro@DEX_COROUTINE_SUSPEND_*] helpers.
 *
 * Returns: (transfer full): a [class@Dex.Future] that will resolve or reject
 *   when @func finishes or returns an error.
 *
 * Since: 1.2
 */
DexFuture *
(dex_scheduler_spawn_coroutine) (DexScheduler     *scheduler,
                                 DexCoroutineFunc  func,
                                 gpointer          user_data,
                                 GDestroyNotify    user_data_destroy)
{
  DexCoroutine *coroutine;

  dex_return_error_if_fail (!scheduler || DEX_IS_SCHEDULER (scheduler));
  dex_return_error_if_fail (func != NULL);
  if (scheduler == NULL)
    scheduler = dex_scheduler_get_default ();

  dex_return_error_if_fail (scheduler != NULL);
  dex_return_error_if_fail (DEX_SCHEDULER_GET_CLASS (scheduler)->spawn_coroutine != NULL);

  coroutine = dex_coroutine_new (func, user_data, user_data_destroy);
  DEX_SCHEDULER_GET_CLASS (scheduler)->spawn_coroutine (scheduler, coroutine);

  return DEX_FUTURE (coroutine);
}

DEX_DEFINE_CLOSURE_TYPE (DexSchedulerSpawnTrampoline, dex_scheduler_spawn_trampoline,
                         DEX_DEFINE_CLOSURE_VALUE (GCallback, callback),
                         DEX_DEFINE_CLOSURE_POINTER (GArray *, values, g_array_unref))

static inline DexFuture *
dex_scheduler_spawn_trampoline_fiber (gpointer data)
{
  DexSchedulerSpawnTrampoline *state = data;
  GClosure *closure = NULL;
  GValue return_value = G_VALUE_INIT;
  gpointer res;

  g_assert (state != NULL);
  g_assert (state->callback != NULL);
  g_assert (state->values != NULL);

  g_value_init (&return_value, G_TYPE_POINTER);
  closure = g_cclosure_new (state->callback, NULL, NULL);
  g_closure_set_marshal (closure, g_cclosure_marshal_generic);
  g_closure_invoke (closure,
                    &return_value,
                    state->values->len,
                    (const GValue *)(gpointer)state->values->data,
                    NULL);
  res = g_value_get_pointer (&return_value);

  g_closure_unref (closure);
  g_value_unset (&return_value);

  return res;
}

/**
 * dex_scheduler_spawnv: (skip)
 * @scheduler: (nullable): a [class@Dex.Scheduler]
 * @stack_size: stack size in bytes or 0
 * @callback: the fiber to spawn
 * @n_params: number of arguments of the fiber
 * @...: arguments, pairs of #GType followed by the value
 *
 * Same as dex_scheduler_spawn() but trampolines into a fiber without having to
 * create special structures on the way there.
 *
 * ```c
 * static DexFuture *
 * fiber_func (GInputStream *stream,
 *             int           num)
 * {
 *   ...
 *
 *   return dex_future_new_true ();
 * }
 *
 * DexFuture *
 * spawn_fiber (GInputStream *stream)
 * {
 *   return dex_scheduler_spawnv (NULL, 0,
 *                                G_CALLBACK (fiber_func),
 *                                2,
 *                                G_TYPE_POINTER, stream,
 *                                G_TYPE_INT, 42);
 * }
 * ```
 *
 * Returns: (transfer full): a [class@Dex.Future] that will resolve or reject when
 *   @callback completes (or its resulting `DexFuture` completes).
 */
DexFuture *
dex_scheduler_spawnv (DexScheduler *scheduler,
                      gsize         stack_size,
                      GCallback     callback,
                      guint         n_params,
                      ...)
{
  DexSchedulerSpawnTrampoline *state;
  char *errmsg = NULL;
  GArray *values = NULL;
  DexFuture *future;
  va_list args;

  values = g_array_new (FALSE, TRUE, sizeof (GValue));
  g_array_set_clear_func (values, (GDestroyNotify)g_value_unset);
  g_array_set_size (values, n_params);

  va_start (args, n_params);

  for (guint i = 0; i < n_params; i++)
    {
      GType gtype = va_arg (args, GType);
      GValue *dest = &g_array_index (values, GValue, i);
      GValue value = G_VALUE_INIT;

      G_VALUE_COLLECT_INIT (&value, gtype, args, 0, &errmsg);

      if (errmsg != NULL)
        break;

      g_value_init (dest, gtype);
      g_value_copy (&value, dest);
      g_value_unset (&value);
    }

  va_end (args);

  if (errmsg != NULL)
    {
      future = dex_future_new_reject (DEX_ERROR,
                                      DEX_ERROR_TYPE_MISMATCH,
                                      "Failed to trampoline to fiber: %s",
                                      errmsg);
    }
  else
    {
      state = dex_scheduler_spawn_trampoline_new ();
      state->values = g_steal_pointer (&values);
      state->callback = callback;

      future = dex_scheduler_spawn (scheduler, stack_size,
                                    dex_scheduler_spawn_trampoline_fiber,
                                    g_steal_pointer (&state),
                                    (GDestroyNotify) dex_scheduler_spawn_trampoline_free);
    }

  g_clear_pointer (&values, g_array_unref);
  g_free (errmsg);

  return future;
}
