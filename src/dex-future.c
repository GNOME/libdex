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
#include "dex-static-future-private.h"

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

/**
 * dex_future_new_for_value:
 * @value: the resolved #GValue
 *
 * Creates a read-only #DexFuture that has resolved.
 *
 * Returns: (transfer full) (type DexStaticFuture): a #DexFuture
 */
DexFuture *
(dex_future_new_for_value) (const GValue *value)
{
  g_return_val_if_fail (G_IS_VALUE (value), NULL);

  return dex_static_future_new_resolved (value);
}

/**
 * dex_future_new_for_error:
 * @error: (transfer full): a #GError
 *
 * Creates a read-only #DexFuture that has rejected.
 *
 * Returns: (transfer full) (type DexStaticFuture): a #DexFuture
 */
DexFuture *
(dex_future_new_for_error) (GError *error)
{
  g_return_val_if_fail (error != NULL, NULL);

  return dex_static_future_new_rejected (error);
}

/**
 * dex_future_new_for_boolean: (constructor)
 * @v_bool: the resolved value for the future
 *
 * Creates a new #DexFuture and resolves it with @v_bool.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_for_boolean) (gboolean v_bool)
{
  GValue value = G_VALUE_INIT;
  DexFuture *future;

  g_value_init (&value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&value, v_bool);
  future = (dex_future_new_for_value) (&value);
  g_value_unset (&value);

  return future;
}

/**
 * dex_future_new_for_int: (constructor)
 * @v_int: the resolved value for the future
 *
 * Creates a new #DexFuture and resolves it with @v_int.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_for_int) (int v_int)
{
  GValue value = G_VALUE_INIT;
  DexFuture *future;

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, v_int);
  future = (dex_future_new_for_value) (&value);
  g_value_unset (&value);

  return future;
}

/**
 * dex_future_new_for_int64: (constructor)
 * @v_int64: the resolved value for the future
 *
 * Creates a new #DexFuture and resolves it with @v_int64.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_for_int64) (gint64 v_int64)
{
  GValue value = G_VALUE_INIT;
  DexFuture *future;

  g_value_init (&value, G_TYPE_INT64);
  g_value_set_int64 (&value, v_int64);
  future = (dex_future_new_for_value) (&value);
  g_value_unset (&value);

  return future;
}

/**
 * dex_future_new_for_uint64: (constructor)
 * @v_uint64: the resolved value for the future
 *
 * Creates a new #DexFuture and resolves it with @v_uint64.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_for_uint64) (guint64 v_uint64)
{
  GValue value = G_VALUE_INIT;
  DexFuture *future;

  g_value_init (&value, G_TYPE_UINT64);
  g_value_set_uint64 (&value, v_uint64);
  future = (dex_future_new_for_value) (&value);
  g_value_unset (&value);

  return future;
}

/**
 * dex_future_new_for_float: (constructor)
 * @v_float: the resolved value for the future
 *
 * Creates a new #DexFuture and resolves it with @v_float.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_for_float) (gfloat v_float)
{
  GValue value = G_VALUE_INIT;
  DexFuture *future;

  g_value_init (&value, G_TYPE_FLOAT);
  g_value_set_float (&value, v_float);
  future = (dex_future_new_for_value) (&value);
  g_value_unset (&value);

  return future;
}

/**
 * dex_future_new_for_double: (constructor)
 * @v_double: the resolved value for the future
 *
 * Creates a new #DexFuture and resolves it with @v_double.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_for_double) (gdouble v_double)
{
  GValue value = G_VALUE_INIT;
  DexFuture *future;

  g_value_init (&value, G_TYPE_DOUBLE);
  g_value_set_double (&value, v_double);
  future = (dex_future_new_for_value) (&value);
  g_value_unset (&value);

  return future;
}

/**
 * dex_future_new_for_uint: (constructor)
 * @v_uint: the resolved value for the future
 *
 * Creates a new #DexFuture and resolves it with @v_uint.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_for_uint) (guint v_uint)
{
  GValue value = G_VALUE_INIT;
  DexFuture *future;

  g_value_init (&value, G_TYPE_UINT);
  g_value_set_uint (&value, v_uint);
  future = (dex_future_new_for_value) (&value);
  g_value_unset (&value);

  return future;
}

/**
 * dex_future_new_for_string: (constructor)
 * @string: the resolved value for the future
 *
 * Creates a new #DexFuture and resolves it with @string.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_for_string) (const char *string)
{
  GValue value = G_VALUE_INIT;
  DexFuture *future;

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_static_string (&value, string);
  future = (dex_future_new_for_value) (&value);
  g_value_unset (&value);

  return future;
}

/**
 * dex_future_new_take_string: (constructor)
 * @string: (transfer full): the resolved value for the future
 *
 * Creates a new #DexFuture and resolves it with @string.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_take_string) (char *string)
{
  GValue value = G_VALUE_INIT;
  DexFuture *future;

  g_value_init (&value, G_TYPE_STRING);
  g_value_take_string (&value, string);
  future = (dex_future_new_for_value) (&value);
  g_value_unset (&value);

  return future;
}

/**
 * dex_future_new_take_boxed: (constructor) (skip)
 * @boxed_type: the GBoxed-based type
 * @value: (transfer full): the value for the boxed type
 *
 * Creates a new #DexFuture that is resolved with @value.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_take_boxed) (GType    boxed_type,
                             gpointer value)
{
  GValue gvalue = G_VALUE_INIT;
  DexFuture *ret;

  g_return_val_if_fail (G_TYPE_FUNDAMENTAL (boxed_type) == G_TYPE_BOXED, NULL);

  g_value_init (&gvalue, boxed_type);
  g_value_take_boxed (&gvalue, value);
  ret = dex_future_new_for_value (&gvalue);
  g_value_unset (&gvalue);

  return ret;
}

/**
 * dex_future_new_for_pointer: (constructor)
 * @pointer: the resolved future value as a pointer
 *
 * Creates a new #DexFuture that is resolved with @pointer as a %G_TYPE_POINTER.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_for_pointer) (gpointer pointer)
{
  GValue gvalue = G_VALUE_INIT;
  DexFuture *ret;

  g_value_init (&gvalue, G_TYPE_POINTER);
  g_value_set_pointer (&gvalue, pointer);
  ret = dex_future_new_for_value (&gvalue);
  g_value_unset (&gvalue);

  return ret;
}

/**
 * dex_future_new_for_object: (constructor)
 * @value: (type GObject): the value
 *
 * Creates a new #DexFuture that is resolved with @value.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_for_object) (gpointer value)
{
  GValue gvalue = G_VALUE_INIT;
  DexFuture *ret;

  g_return_val_if_fail (G_IS_OBJECT (value), NULL);

  g_value_init (&gvalue, G_OBJECT_TYPE (value));
  g_value_set_object (&gvalue, value);
  ret = dex_future_new_for_value (&gvalue);
  g_value_unset (&gvalue);

  return ret;
}

/**
 * dex_future_new_take_object: (constructor)
 * @value: (transfer full) (type GObject): the value
 *
 * Creates a new #DexFuture that is resolved with @value.
 *
 * Returns: (transfer full) (type DexStaticFuture): a resolved #DexFuture
 */
DexFuture *
(dex_future_new_take_object) (gpointer value)
{
  GValue gvalue = G_VALUE_INIT;
  DexFuture *ret;

  g_return_val_if_fail (G_IS_OBJECT (value), NULL);

  g_value_init (&gvalue, G_OBJECT_TYPE (value));
  g_value_take_object (&gvalue, value);
  ret = dex_future_new_for_value (&gvalue);
  g_value_unset (&gvalue);

  return ret;
}

/**
 * dex_future_new_reject: (constructor)
 * @domain: the error domain
 * @code: the error code
 * @format: a printf-style format string
 *
 * Creates a new #DexFuture that is rejeced.
 *
 * Returns: (transfer full) (type DexStaticFuture): a new #DexFuture
 */
DexFuture *
(dex_future_new_reject) (GQuark      domain,
                         int         code,
                         const char *format,
                         ...)
{
  GError *error;
  va_list args;

  va_start (args, format);
  error = g_error_new_valist (domain, code, format, args);
  va_end (args);

  g_return_val_if_fail (error != NULL, NULL);

  return dex_future_new_for_error (error);
}

static const GValue *
dex_future_await_check (DexFuture  *future,
                        GType       type,
                        GError    **error)
{
  const GValue *value;

  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);

  if ((value = dex_future_await (future, error)))
    {
      if (!G_VALUE_HOLDS (value, type))
        {
          g_set_error (error,
                       DEX_ERROR,
                       DEX_ERROR_TYPE_MISMATCH,
                       "Got type %s, expected %s",
                       G_VALUE_TYPE_NAME (value),
                       g_type_name (type));
          return NULL;
        }
    }

  return value;
}

/**
 * dex_future_await_pointer: (skip)
 * @future: a #DexFuture
 * @error: a location for a #GError
 *
 * Calls dex_future_await() and returns the value of g_value_get_pointer(),
 * otherwise @error is set if the future rejected.
 *
 * Returns: (nullable): a pointer or %NULL
 */
gpointer
dex_future_await_pointer (DexFuture  *future,
                          GError    **error)
{
  const GValue *value;

  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);

  if ((value = dex_future_await_check (future, G_TYPE_POINTER, error)))
    return g_value_get_pointer (value);

  return NULL;
}

/**
 * dex_future_await_int64:
 * @future: a #DexFuture
 * @error: a location for a #GError
 *
 * Awaits on @future and returns the result as an int64.
 *
 * The resolved value must be of type %G_TYPE_INT64 or @error is set.
 *
 * Returns: an int64, or 0 in case of failure and @error is set.
 */
gint64
dex_future_await_int64 (DexFuture  *future,
                        GError    **error)
{
  const GValue *value;

  g_return_val_if_fail (DEX_IS_FUTURE (future), 0);

  if ((value = dex_future_await_check (future, G_TYPE_INT64, error)))
    return g_value_get_int64 (value);

  return 0;
}

/**
 * dex_future_await_boxed: (skip)
 * @future: a #DexFuture
 * @error: a location for a #GError
 *
 * Awaits on @future and returns the %G_TYPE_BOXED based result.
 *
 * Returns: (transfer full): the boxed result, or %NULL and @error is set.
 */
gpointer
dex_future_await_boxed (DexFuture *future,
                        GError **error)
{
  const GValue *value;

  g_return_val_if_fail (DEX_IS_FUTURE (future), 0);

  if ((value = dex_future_await_check (future, G_TYPE_BOXED, error)))
    return g_value_dup_boxed (value);

  return NULL;
}
