/*
 * dex-future.c
 *
 * Copyright 2022 Christian Hergert <chergert@gnome.org>
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

#include "dex-block-private.h"
#include "dex-error.h"
#include "dex-future-private.h"
#include "dex-future-set-private.h"
#include "dex-promise.h"
#include "dex-scheduler.h"

static void dex_future_propagate (DexFuture *future,
                                  DexFuture *completed);

DEX_DEFINE_ABSTRACT_TYPE (DexFuture, dex_future, DEX_TYPE_OBJECT)

typedef struct _DexChainedFuture
{
  GList      link;
  DexWeakRef wr;
  guint      awaiting : 1;
} DexChainedFuture;

static DexChainedFuture *
dex_chained_future_new (gpointer object)
{
  DexChainedFuture *cf;

  cf = g_new0 (DexChainedFuture, 1);
  cf->link.data = cf;
  cf->awaiting = TRUE;
  dex_weak_ref_init (&cf->wr, object);

  return cf;
}

static void
dex_chained_future_free (DexChainedFuture *cf)
{
  g_assert (cf != NULL);
  g_assert (cf->link.prev == NULL);
  g_assert (cf->link.next == NULL);
  g_assert (cf->link.data == cf);

  dex_weak_ref_clear (&cf->wr);
  cf->link.data = NULL;
  g_free (cf);
}

void
dex_future_complete_from (DexFuture *future,
                          DexFuture *completed)
{
  GError *error = NULL;
  const GValue *value = dex_future_get_value (completed, &error);
  dex_future_complete (future, value, error);
}

void
dex_future_complete (DexFuture    *future,
                     const GValue *resolved,
                     GError       *rejected)
{
  GList *list;

  g_return_if_fail (DEX_IS_FUTURE (future));
  g_return_if_fail (resolved != NULL || rejected != NULL);
  g_return_if_fail (resolved == NULL || G_IS_VALUE (resolved));

  dex_object_lock (DEX_OBJECT (future));
  if (future->status == DEX_FUTURE_STATUS_PENDING)
    {
      if (resolved != NULL)
        {
          g_value_init (&future->resolved, G_VALUE_TYPE (resolved));
          g_value_copy (resolved, &future->resolved);
          future->status = DEX_FUTURE_STATUS_RESOLVED;
        }
      else
        {
          future->rejected = g_steal_pointer (&rejected);
          future->status = DEX_FUTURE_STATUS_REJECTED;
        }

      list = g_steal_pointer (&future->chained);
    }
  else
    {
      g_clear_error (&rejected);
      list = NULL;
    }
  dex_object_unlock (DEX_OBJECT (future));

  /* Iterate in reverse order to give some predictable ordering based on
   * when chained futures were attached. We've released the lock at this
   * point to avoid any requests back on future from deadlocking.
   */
  list = g_list_last (list);
  while (list != NULL)
    {
      DexChainedFuture *cf = list->data;
      DexFuture *chained;

      list = list->prev;

      if (list != NULL)
        {
          list->next = NULL;
          cf->link.prev = NULL;
        }

      g_assert (cf != NULL);
      g_assert (cf->link.data == cf);

      chained = dex_weak_ref_get (&cf->wr);
      dex_weak_ref_set (&cf->wr, NULL);

      g_assert (!chained || DEX_IS_FUTURE (chained));

      /* Always notify even if the future isn't awaiting as
       * it can provide a bit more information to futures that
       * are bringing in results until their callbacks are
       * scheduled for execution.
       */
      if (chained != NULL)
        {
          dex_future_propagate (chained, future);
          dex_unref (chained);
        }

      dex_chained_future_free (cf);
    }
}

const GValue *
dex_future_get_value (DexFuture  *future,
                      GError    **error)
{
  const GValue *ret;

  g_return_val_if_fail (DEX_IS_FUTURE (future), FALSE);

  dex_object_lock (DEX_OBJECT (future));

  switch (future->status)
    {
    case DEX_FUTURE_STATUS_PENDING:
      g_set_error_literal (error,
                           DEX_ERROR,
                           DEX_ERROR_PENDING,
                           "Future is still pending");
      ret = NULL;
      break;

    case DEX_FUTURE_STATUS_RESOLVED:
      ret = &future->resolved;
      break;

    case DEX_FUTURE_STATUS_REJECTED:
      ret = NULL;
      if (error != NULL)
        *error = g_error_copy (future->rejected);
      break;

    default:
      g_assert_not_reached ();
    }

  dex_object_unlock (DEX_OBJECT (future));

  return ret;
}

DexFutureStatus
dex_future_get_status (DexFuture *future)
{
  DexFutureStatus status;

  g_return_val_if_fail (DEX_IS_FUTURE (future), 0);

  dex_object_lock (DEX_OBJECT (future));
  status = future->status;
  dex_object_unlock (DEX_OBJECT (future));

  return status;
}

static void
dex_future_finalize (DexObject *object)
{
  DexFuture *future = (DexFuture *)object;

  g_assert (future->chained == NULL);

  if (G_IS_VALUE (&future->resolved))
    g_value_unset (&future->resolved);
  g_clear_error (&future->rejected);

  DEX_OBJECT_CLASS (dex_future_parent_class)->finalize (object);
}

static void
dex_future_class_init (DexFutureClass *klass)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (klass);

  object_class->finalize = dex_future_finalize;
}

static void
dex_future_init (DexFuture *future)
{
}

static void
dex_future_propagate (DexFuture *future,
                      DexFuture *completed)
{
  gboolean handled;

  g_return_if_fail (DEX_IS_FUTURE (future));
  g_return_if_fail (DEX_IS_FUTURE (completed));

  dex_ref (completed);

  if (DEX_FUTURE_GET_CLASS (future)->propagate)
    handled = DEX_FUTURE_GET_CLASS (future)->propagate (future, completed);
  else
    handled = FALSE;

  if (!handled)
    {
      const GValue *resolved = NULL;
      const GError *rejected = NULL;

      dex_object_lock (future);
      if (completed->status == DEX_FUTURE_STATUS_RESOLVED)
        resolved = &completed->resolved;
      else
        rejected = completed->rejected;
      dex_object_unlock (future);

      dex_future_complete (future,
                           resolved,
                           rejected ? g_error_copy (rejected) : NULL);
    }

  dex_unref (completed);
}

void
dex_future_chain (DexFuture *future,
                  DexFuture *chained)
{
  gboolean did_chain = FALSE;

  g_return_if_fail (DEX_IS_FUTURE (future));
  g_return_if_fail (DEX_IS_FUTURE (chained));

  dex_object_lock (future);
  if (future->status == DEX_FUTURE_STATUS_PENDING)
    {
      DexChainedFuture *cf = dex_chained_future_new (chained);
      future->chained = g_list_insert_before_link (future->chained,
                                                   future->chained,
                                                   &cf->link);
      did_chain = TRUE;
    }
  dex_object_unlock (future);

  if (!did_chain)
    dex_future_propagate (chained, future);
}

void
dex_future_discard (DexFuture *future,
                    DexFuture *chained)
{
  gboolean has_awaiting = FALSE;
  gboolean matched = FALSE;

  g_return_if_fail (DEX_IS_FUTURE (future));
  g_return_if_fail (DEX_IS_FUTURE (chained));

  dex_object_lock (future);

  /* Mark the chained future as no longer necessary to dispatch to.
   * If so, we can possibly request that @future discard any ongoing
   * operations so that we propagate cancellation.
   *
   * However, if the status has already completed, just short-circuit.
   */
  if (future->status != DEX_FUTURE_STATUS_PENDING)
    {
      dex_object_unlock (future);
      return;
    }

  for (const GList *iter = future->chained; iter; iter = iter->next)
    {
      DexChainedFuture *cf = iter->data;
      DexFuture *obj;

      g_assert (cf != NULL);

      if ((obj = dex_weak_ref_get (&cf->wr)))
        {
          if (obj == chained)
            {
              if (cf->awaiting == TRUE)
                {
                  matched = TRUE;
                  cf->awaiting = FALSE;
                }
            }

          has_awaiting |= cf->awaiting;
          dex_unref (obj);
        }
    }

  dex_object_unlock (future);

  /* If we discarded the chained future and there are no more futures
   * awaiting our response, then request the class discard the future,
   * possibly cancelling anything in flight.
   */
  if (matched && !has_awaiting)
    {
      if (DEX_FUTURE_GET_CLASS (future)->discard)
        {
          dex_ref (future);
          DEX_FUTURE_GET_CLASS (future)->discard (future);
          dex_unref (future);
        }
    }
}

/**
 * dex_future_then:
 * @future: (transfer full): a #DexFuture
 * @callback: (scope async): a callback to execute
 * @callback_data: closure data for @callback
 * @callback_data_destroy: destroy notify for @callback_data
 *
 * Calls @callback when @future resolves.
 *
 * If @future rejects, then @callback will not be called.
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
(dex_future_then) (DexFuture         *future,
                   DexFutureCallback  callback,
                   gpointer           callback_data,
                   GDestroyNotify     callback_data_destroy)
{
  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  return dex_block_new (future,
                        NULL,
                        DEX_BLOCK_KIND_THEN,
                        callback,
                        callback_data,
                        callback_data_destroy);
}

/**
 * dex_future_then_loop:
 * @future: (transfer full): a #DexFuture
 * @callback: (scope async): a callback to execute
 * @callback_data: closure data for @callback
 * @callback_data_destroy: destroy notify for @callback_data
 *
 * Asynchronously calls @callback when @future resolves.
 *
 * This is similar to dex_future_then() except that it will call
 * @callback multiple times as each returned #DexFuture resolves or
 * rejects, allowing for infinite loops.
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
(dex_future_then_loop) (DexFuture         *future,
                        DexFutureCallback  callback,
                        gpointer           callback_data,
                        GDestroyNotify     callback_data_destroy)
{
  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  return dex_block_new (future,
                        NULL,
                        DEX_BLOCK_KIND_THEN | DEX_BLOCK_KIND_LOOP,
                        callback,
                        callback_data,
                        callback_data_destroy);
}

/**
 * dex_future_catch_loop:
 * @future: (transfer full): a #DexFuture
 * @callback: (scope async): a callback to execute
 * @callback_data: closure data for @callback
 * @callback_data_destroy: destroy notify for @callback_data
 *
 * Asynchronously calls @callback when @future rejects.
 *
 * This is similar to dex_future_catch() except that it will call
 * @callback multiple times as each returned #DexFuture rejects,
 * allowing for infinite loops.
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
(dex_future_catch_loop) (DexFuture         *future,
                         DexFutureCallback  callback,
                         gpointer           callback_data,
                         GDestroyNotify     callback_data_destroy)
{
  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  return dex_block_new (future,
                        NULL,
                        DEX_BLOCK_KIND_CATCH | DEX_BLOCK_KIND_LOOP,
                        callback,
                        callback_data,
                        callback_data_destroy);
}

/**
 * dex_future_finally_loop:
 * @future: (transfer full): a #DexFuture
 * @callback: (scope async): a callback to execute
 * @callback_data: closure data for @callback
 * @callback_data_destroy: destroy notify for @callback_data
 *
 * Asynchronously calls @callback when @future rejects or resolves.
 *
 * This is similar to dex_future_finally() except that it will call
 * @callback multiple times as each returned #DexFuture rejects or resolves,
 * allowing for infinite loops.
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
(dex_future_finally_loop) (DexFuture         *future,
                           DexFutureCallback  callback,
                           gpointer           callback_data,
                           GDestroyNotify     callback_data_destroy)
{
  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  return dex_block_new (future,
                        NULL,
                        DEX_BLOCK_KIND_THEN | DEX_BLOCK_KIND_CATCH | DEX_BLOCK_KIND_LOOP,
                        callback,
                        callback_data,
                        callback_data_destroy);
}

/**
 * dex_future_catch:
 * @future: (transfer full): a #DexFuture
 * @callback: (scope async): a callback to execute
 * @callback_data: closure data for @callback
 * @callback_data_destroy: destroy notify for @callback_data
 *
 * Calls @callback when @future rejects.
 *
 * If @future resolves, then @callback will not be called.
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
(dex_future_catch) (DexFuture         *future,
                    DexFutureCallback  callback,
                    gpointer           callback_data,
                    GDestroyNotify     callback_data_destroy)
{
  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  return dex_block_new (future,
                        NULL,
                        DEX_BLOCK_KIND_CATCH,
                        callback,
                        callback_data,
                        callback_data_destroy);
}

/**
 * dex_future_finally:
 * @future: (transfer full): a #DexFuture
 * @callback: (scope async): a callback to execute
 * @callback_data: closure data for @callback
 * @callback_data_destroy: destroy notify for @callback_data
 *
 * Calls @callback when @future resolves or rejects.
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
(dex_future_finally) (DexFuture         *future,
                      DexFutureCallback  callback,
                      gpointer           callback_data,
                      GDestroyNotify     callback_data_destroy)
{
  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  return dex_block_new (future,
                        NULL,
                        DEX_BLOCK_KIND_FINALLY,
                        callback,
                        callback_data,
                        callback_data_destroy);
}

/**
 * dex_future_all: (skip)
 * @first_future: (transfer full): a #DexFuture
 * @...: a %NULL terminated list of futures
 *
 * Creates a new #DexFuture that will resolve or reject when all futures
 * either resolve or reject.
 *
 * This method will return a #DexFutureSet which provides API to get
 * the exact values of the dependent futures. The value of the future
 * if resolved will be a %G_TYPE_BOOLEAN of %TRUE.
 *
 * Returns: (transfer full) (type DexFutureSet): a #DexFuture
 */
DexFuture *
(dex_future_all) (DexFuture *first_future,
                  ...)
{
  DexFutureSet *ret;
  va_list args;

  va_start (args, first_future);
  ret = dex_future_set_new_va (first_future, &args, DEX_FUTURE_SET_FLAGS_NONE);
  va_end (args);

  return DEX_FUTURE (ret);
}

/**
 * dex_future_any: (skip)
 * @first_future: (transfer full): a #DexFuture
 * @...: a %NULL terminated list of futures
 *
 * Creates a new #DexFuture that will resolve when any dependent future
 * resolves, providing the same result as the resolved future.
 *
 * If no futures resolve, then the future will reject.
 *
 * Returns: (transfer full) (type DexFutureSet): a #DexFuture
 */
DexFuture *
(dex_future_any) (DexFuture *first_future,
                  ...)
{
  DexFutureSet *ret;
  va_list args;

  va_start (args, first_future);
  ret = dex_future_set_new_va (first_future, &args,
                               (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                DEX_FUTURE_SET_FLAGS_PROPAGATE_RESOLVE));
  va_end (args);

  return DEX_FUTURE (ret);
}

/**
 * dex_future_all_race:
 * @first_future: (transfer full): a #DexFuture
 * @...: a %NULL terminated list of futures
 *
 * Creates a new #DexFuture that will resolve when all futures resolve
 * or reject as soon as the first future rejects.
 *
 * This method will return a #DexFutureSet which provides API to get
 * the exact values of the dependent futures. The value of the future
 * will be propagated from the resolved or rejected future.
 *
 * Since the futures race to complete, some futures retrieved with the
 * dex_future_set_get_future() API will still be %DEX_FUTURE_STATUS_PENDING.
 *
 * Returns: (transfer full) (type DexFutureSet): a #DexFuture
 */
DexFuture *
(dex_future_all_race) (DexFuture *first_future,
                       ...)
{
  DexFutureSet *ret;
  va_list args;

  va_start (args, first_future);
  ret = dex_future_set_new_va (first_future, &args,
                               (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                DEX_FUTURE_SET_FLAGS_PROPAGATE_REJECT));
  va_end (args);

  return DEX_FUTURE (ret);
}

/**
 * dex_future_first: (skip)
 * @first_future: (transfer full): a #DexFuture
 * @...: a %NULL terminated list of futures
 *
 * Creates a new #DexFuture that resolves or rejects as soon as the
 * first dependent future resolves or rejects, sharing the same result.
 *
 * Returns: (transfer full) (type DexFutureSet): a #DexFuture
 */
DexFuture *
(dex_future_first) (DexFuture *first_future,
                    ...)
{
  DexFutureSet *ret;
  va_list args;

  va_start (args, first_future);
  ret = dex_future_set_new_va (first_future, &args,
                               (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                DEX_FUTURE_SET_FLAGS_PROPAGATE_RESOLVE |
                                DEX_FUTURE_SET_FLAGS_PROPAGATE_REJECT));
  va_end (args);

  return DEX_FUTURE (ret);
}

/**
 * dex_future_firstv: (rename-to dex_future_first)
 * @futures: (array length=n_futures) (transfer none): an array of futures
 * @n_futures: the number of futures
 *
 * Creates a new #DexFuture that resolves or rejects as soon as the
 * first dependent future resolves or rejects, sharing the same result.
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
(dex_future_firstv) (DexFuture * const *futures,
                     guint              n_futures)
{
  return DEX_FUTURE (dex_future_set_new (futures, n_futures,
                                         (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_RESOLVE |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_REJECT)));
}

/**
 * dex_future_anyv: (rename-to dex_future_any)
 * @futures: (array length=n_futures) (transfer none): an array of futures
 * @n_futures: the number of futures
 *
 * Creates a new #DexFuture that resolves when the first future resolves.
 *
 * If all futures reject, then the #DexFuture returned will also reject.
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
(dex_future_anyv) (DexFuture * const *futures,
                   guint              n_futures)
{
  return DEX_FUTURE (dex_future_set_new (futures, n_futures,
                                         (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_RESOLVE)));
}

/**
 * dex_future_all_racev: (rename-to dex_future_all_race)
 * @futures: (array length=n_futures) (transfer none): an array of futures
 * @n_futures: the number of futures
 *
 * Creates a new #DexFuture that resolves when all futures resolve.
 *
 * If any future rejects, the resulting #DexFuture also rejects immediately.
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
(dex_future_all_racev) (DexFuture * const *futures,
                        guint              n_futures)
{
  return DEX_FUTURE (dex_future_set_new (futures, n_futures,
                                         (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_REJECT)));
}

/**
 * dex_future_allv: (rename-to dex_future_all)
 * @futures: (array length=n_futures) (transfer none): an array of futures
 * @n_futures: the number of futures
 *
 * Creates a new #DexFuture that resolves when all futures resolve.
 *
 * The resulting #DexFuture will not resolve or reject until all futures
 * have either resolved or rejected.
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
(dex_future_allv) (DexFuture * const *futures,
                   guint              n_futures)
{
  return DEX_FUTURE (dex_future_set_new (futures, n_futures, DEX_FUTURE_SET_FLAGS_NONE));
}

/**
 * dex_future_set_static_name: (skip)
 * @future: a #DexFuture
 * @name: the name of the future
 *
 * Sets the name of the future with a static/internal string.
 *
 * @name will not be copied, so it must be static/internal which can be done
 * either by using string literals or by using g_string_intern().
 */
void
dex_future_set_static_name (DexFuture  *future,
                            const char *name)
{
  g_return_if_fail (DEX_IS_FUTURE (future));

  dex_object_lock (future);
  future->name = name;
  dex_object_unlock (future);
}

const char *
dex_future_get_name (DexFuture *future)
{
  const char *name;

  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);

  dex_object_lock (future);
  name = future->name;
  dex_object_unlock (future);

  return name;
}
