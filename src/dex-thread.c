/* dex-thread.c
 *
 * Copyright 2025 Christian Hergert <christian@sourceandstack.com>
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

#include "config.h"

#include <gobject/gvaluecollector.h>

#include "dex-block-private.h"
#include "dex-thunk-private.h"
#include "dex-error.h"
#include "dex-future-private.h"
#include "dex-promise.h"
#include "dex-thread.h"
#include "dex-thread-storage-private.h"
#include "dex-waiter-private.h"

typedef struct _ThreadSpawn
{
  DexScheduler *scheduler;
  DexPromise   *promise;
  DexThunk     *thunk;
} ThreadSpawn;

static DexFuture *
propagate_future (DexFuture *completed,
                  gpointer   user_data)
{
  ThreadSpawn *state = user_data;
  DexPromise *promise = state->promise;
  const GValue *value;
  GError *error = NULL;

  if ((value = dex_future_get_value (completed, &error)))
    dex_promise_resolve (promise, value);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  return dex_future_new_true ();
}

static void
thread_spawn_free (gpointer data)
{
  ThreadSpawn *state = data;

  g_clear_pointer (&state->thunk, dex_thunk_unref);
  dex_clear (&state->scheduler);
  dex_clear (&state->promise);
  g_free (state);
}

static gpointer
dex_trampoline_thread (gpointer data)
{
  ThreadSpawn *state = data;
  DexScheduler *scheduler;
  DexFuture *future;

  g_assert (state != NULL);
  g_assert (state->thunk != NULL);
  g_assert (DEX_IS_SCHEDULER (state->scheduler));
  g_assert (DEX_IS_PROMISE (state->promise));

  future = dex_thunk_invoke (state->thunk);
  if (future == NULL)
    future = dex_future_new_reject (DEX_ERROR, DEX_ERROR_NO_RESULT,
                                    "Thread callback returned NULL");

  scheduler = dex_ref (state->scheduler);
  dex_future_disown_full (dex_block_new (g_steal_pointer (&future),
                                         scheduler,
                                         DEX_BLOCK_KIND_FINALLY,
                                         propagate_future,
                                         state,
                                         thread_spawn_free),
                          scheduler);
  dex_unref (scheduler);

  return NULL;
}

/**
 * dex_thread_spawn:
 * @thread_name: (nullable): the name for the thread
 * @thread_func: the function to call on a thread
 * @user_data: closure data for @thread_func
 * @user_data_destroy: callback to free @user_data which will be called
 *   on the same thread calling this function.
 *
 * Spawns a new thread named @thread_name running @thread_func with
 * @user_data passed to it.
 *
 * @thread_func must return a [class@Dex.Future]. Callback data remains alive
 * until that future completes, even if the returned thread future is dropped.
 * Destruction is queued on the captured scheduler and may run after completion.
 *
 * If this function is called from a thread that is not running a
 * [class@Dex.Scheduler] then the default scheduler will be used
 * to call @user_data_destroy.
 *
 * If the resulting [class@Dex.Future] has not resolved or rejected,
 * then the same scheduler used to call @user_data_destroy will be
 * used to propagate the result to the caller.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves or rejects
 *   the value or error returned from @thread_func as a [class@Dex.Future].
 *
 * Since: 1.0
 */
DexFuture *
dex_thread_spawn (const char     *thread_name,
                  DexThreadFunc   thread_func,
                  gpointer        user_data,
                  GDestroyNotify  user_data_destroy)
{
  DexThreadStorage *storage;
  DexScheduler *scheduler;
  ThreadSpawn *state;
  DexFuture *ret;

  dex_return_error_if_fail (thread_func != NULL);

  if (thread_name == NULL)
    thread_name = "[dex-thread]";

  storage = dex_thread_storage_peek ();

  if (storage == NULL || storage->scheduler == NULL)
    scheduler = dex_scheduler_get_default ();
  else
    scheduler = storage->scheduler;

  g_assert (DEX_IS_SCHEDULER (scheduler));

  state = g_new0 (ThreadSpawn, 1);
  state->scheduler = dex_ref (scheduler);
  state->thunk = dex_thunk_new (G_CALLBACK (thread_func), user_data, user_data_destroy, scheduler);
  state->promise = dex_promise_new ();

  dex_future_set_static_name (DEX_FUTURE (state->promise),
                              g_intern_string (thread_name));

  ret = dex_ref (state->promise);

  g_thread_unref (g_thread_new (thread_name, dex_trampoline_thread, state));

  return DEX_FUTURE (ret);
}

/**
 * dex_thread_wait_for:
 * @future: (transfer full): a [class@Dex.Future]
 * @error: a location for a [struct@GLib.Error] or %NULL
 *
 * Use this when running on a thread spawned with `dex_thread_spawn()` and
 * you need to block the thread until @future has resolved or rejected.
 *
 * Returns: %TRUE if @future resolved, otherwise %FALSE and @error is
 *   set to the rejection.
 *
 * Since: 1.0
 */
gboolean
dex_thread_wait_for (DexFuture  *future,
                     GError    **error)
{
  DexThreadStorage *storage;
  gboolean ret;

  g_return_val_if_fail (DEX_IS_FUTURE (future), FALSE);

  if ((storage = dex_thread_storage_peek ()) && storage->scheduler != NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "Attempt to wait for future on scheduler controlled thread. This is not allowed.");
      return FALSE;
    }

  /* Short-circuit when @future is already completed */
  if (!dex_future_is_pending (future))
    {
      ret = !!dex_future_get_value (future, error);
      dex_unref (future);
    }
  else
    {
      DexWaiter *waiter;

      /* Keep future alive during waiter lifetime */
      waiter = dex_waiter_new (future);
      dex_waiter_wait (waiter);
      ret = !!dex_future_get_value (DEX_FUTURE (waiter), error);
      dex_unref (waiter);
      dex_unref (future);
    }

  return ret;
}
