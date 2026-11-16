/* dex-thunk.c
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

#include "dex-thunk-private.h"
#include "dex-promise.h"

struct _DexThunk
{
  gatomicrefcount  refs;
  GMutex           mutex;
  GCallback        callback;
  gpointer         user_data;
  GDestroyNotify   notify;
  DexScheduler    *scheduler;
  DexPromise      *completion;
};

DexThunk *
dex_thunk_new (GCallback       callback,
               gpointer        user_data,
               GDestroyNotify  notify,
               DexScheduler   *scheduler)
{
  DexThunk *thunk;

  g_return_val_if_fail (callback != NULL, NULL);
  g_return_val_if_fail (scheduler == NULL || DEX_IS_SCHEDULER (scheduler), NULL);

  thunk = g_new0 (DexThunk, 1);

  g_atomic_ref_count_init (&thunk->refs);
  g_mutex_init (&thunk->mutex);

  thunk->callback = callback;
  thunk->user_data = user_data;
  thunk->notify = notify;
  thunk->scheduler = scheduler ? dex_ref (scheduler) : NULL;

  return thunk;
}

DexThunk *
dex_thunk_ref (DexThunk *thunk)
{
  g_return_val_if_fail (thunk != NULL, NULL);

  g_atomic_ref_count_inc (&thunk->refs);

  return thunk;
}

static void
dex_thunk_free (gpointer data)
{
  DexThunk *thunk = data;
  GDestroyNotify notify = g_steal_pointer (&thunk->notify);
  gpointer user_data = g_steal_pointer (&thunk->user_data);

  if (notify != NULL)
    notify (user_data);

  if (thunk->completion != NULL)
    dex_promise_resolve_boolean (thunk->completion, TRUE);

  dex_clear (&thunk->completion);
  dex_clear (&thunk->scheduler);

  g_mutex_clear (&thunk->mutex);

  g_free (thunk);
}

void
dex_thunk_unref (gpointer data)
{
  DexThunk *thunk = data;

  g_return_if_fail (thunk != NULL);

  if (!g_atomic_ref_count_dec (&thunk->refs))
    return;

  if (thunk->scheduler != NULL)
    dex_scheduler_push (thunk->scheduler, dex_thunk_free, thunk);
  else
    dex_thunk_free (thunk);
}

DexFuture *
dex_thunk_dup_completion (DexThunk *thunk)
{
  DexFuture *completion;

  g_return_val_if_fail (thunk != NULL, NULL);

  g_mutex_lock (&thunk->mutex);
  if (thunk->completion == NULL)
    thunk->completion = dex_promise_new ();
  completion = dex_ref (thunk->completion);
  g_mutex_unlock (&thunk->mutex);

  return completion;
}

DexFuture *
dex_thunk_invoke (DexThunk *thunk)
{
  DexFiberFunc callback;
  DexFuture *future;

  g_return_val_if_fail (thunk != NULL, NULL);

  callback = (DexFiberFunc)thunk->callback;

  dex_thunk_ref (thunk);
  future = callback (thunk->user_data);
  dex_thunk_unref (thunk);

  return future;
}

DexFuture *
dex_thunk_invoke_callback (DexThunk  *thunk,
                           DexFuture *completed)
{
  DexFutureCallback callback;
  DexFuture *future;

  g_return_val_if_fail (thunk != NULL, NULL);
  g_return_val_if_fail (DEX_IS_FUTURE (completed), NULL);

  callback = (DexFutureCallback)thunk->callback;

  dex_thunk_ref (thunk);
  future = callback (completed, thunk->user_data);
  dex_thunk_unref (thunk);

  return future;
}

DexFuture *
dex_thunk_invoke_coroutine (DexThunk            *thunk,
                            DexCoroutineContext *context)
{
  DexCoroutineFunc callback;
  DexFuture *future;

  g_return_val_if_fail (thunk != NULL, NULL);
  g_return_val_if_fail (context != NULL, NULL);

  callback = (DexCoroutineFunc)thunk->callback;

  dex_thunk_ref (thunk);
  future = callback (context, thunk->user_data);
  dex_thunk_unref (thunk);

  return future;
}
