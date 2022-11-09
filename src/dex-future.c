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

#include "dex-future.h"
#include "dex-promise.h"
#include "dex-private.h"

static void dex_future_propagate (DexFuture *future,
                                  DexFuture *completed);

DEX_DEFINE_ABSTRACT_TYPE (DexFuture, dex_future, DEX_TYPE_OBJECT)

typedef struct _DexChainedFuture
{
  GList      link;
  DexWeakRef wr;
} DexChainedFuture;

static DexChainedFuture *
dex_chained_future_new (gpointer object)
{
  DexChainedFuture *cf;

  cf = g_new0 (DexChainedFuture, 1);
  cf->link.data = cf;
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
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_PENDING,
                   "Future is pending");
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
dex_future_then (DexFuture         *future,
                 DexFutureCallback  callback,
                 gpointer           callback_data,
                 GDestroyNotify     callback_data_destroy)
{
  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  return dex_block_new (future,
                        DEX_BLOCK_KIND_THEN,
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
dex_future_catch (DexFuture         *future,
                  DexFutureCallback  callback,
                  gpointer           callback_data,
                  GDestroyNotify     callback_data_destroy)
{
  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  return dex_block_new (future,
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
dex_future_finally (DexFuture         *future,
                    DexFutureCallback  callback,
                    gpointer           callback_data,
                    GDestroyNotify     callback_data_destroy)
{
  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  return dex_block_new (future,
                        DEX_BLOCK_KIND_FINALLY,
                        callback,
                        callback_data,
                        callback_data_destroy);
}

static GPtrArray *
dex_future_collect_futures (DexFuture *first_future,
                            va_list   *args)
{
  DexFuture *future = first_future;
  GPtrArray *ar;

  g_assert (DEX_IS_FUTURE (first_future));
  g_assert (args != NULL);

  ar = g_ptr_array_new ();

  while (future != NULL)
    {
      g_ptr_array_add (ar, future);
      future = va_arg (*args, DexFuture *);
    }

  return ar;
}

/**
 * dex_future_all:
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
dex_future_all (DexFuture *first_future,
                ...)
{
  GPtrArray *ar;
  va_list args;

  va_start (args, first_future);
  ar = dex_future_collect_futures (first_future, &args);
  va_end (args);

  return DEX_FUTURE (dex_future_set_new ((DexFuture **)ar->pdata,
                                         ar->len,
                                         DEX_FUTURE_SET_FLAGS_NONE));
}

/**
 * dex_future_any:
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
dex_future_any (DexFuture *first_future,
                ...)
{
  GPtrArray *ar;
  va_list args;

  va_start (args, first_future);
  ar = dex_future_collect_futures (first_future, &args);
  va_end (args);

  return DEX_FUTURE (dex_future_set_new ((DexFuture **)ar->pdata,
                                         ar->len,
                                         (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_RESOLVE)));
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
dex_future_all_race (DexFuture *first_future,
                     ...)
{
  GPtrArray *ar;
  va_list args;

  va_start (args, first_future);
  ar = dex_future_collect_futures (first_future, &args);
  va_end (args);

  return DEX_FUTURE (dex_future_set_new ((DexFuture **)ar->pdata,
                                         ar->len,
                                         (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_REJECT)));
}

/**
 * dex_future_any_race:
 * @first_future: (transfer full): a #DexFuture
 * @...: a %NULL terminated list of futures
 *
 * Creates a new #DexFuture that will resolve when any future either
 * resolves or rejects. This is similar to dex_future_any() except
 * that if any future rejects, this will also reject.
 *
 * Returns: (transfer full) (type DexFutureSet): a #DexFuture
 */
DexFuture *
dex_future_any_race (DexFuture *first_future,
                     ...)
{
  GPtrArray *ar;
  va_list args;

  va_start (args, first_future);
  ar = dex_future_collect_futures (first_future, &args);
  va_end (args);

  return DEX_FUTURE (dex_future_set_new ((DexFuture **)ar->pdata,
                                         ar->len,
                                         (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_RESOLVE |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_REJECT)));
}

/**
 * dex_future_any_racev:
 * @futures: (array length=n_futures): an array of futures
 * @n_futures: the number of futures
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
dex_future_any_racev (DexFuture **futures,
                      guint       n_futures)
{
  return DEX_FUTURE (dex_future_set_new (futures, n_futures,
                                         (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_RESOLVE |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_REJECT)));
}

/**
 * dex_future_anyv:
 * @futures: (array length=n_futures): an array of futures
 * @n_futures: the number of futures
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
dex_future_anyv (DexFuture **futures,
                 guint       n_futures)
{
  return DEX_FUTURE (dex_future_set_new (futures, n_futures,
                                         (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_RESOLVE)));
}

/**
 * dex_future_all_racev:
 * @futures: (array length=n_futures): an array of futures
 * @n_futures: the number of futures
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
dex_future_all_racev (DexFuture **futures,
                      guint       n_futures)
{
  return DEX_FUTURE (dex_future_set_new (futures, n_futures,
                                         (DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST |
                                          DEX_FUTURE_SET_FLAGS_PROPAGATE_REJECT)));
}

/**
 * dex_future_allv:
 * @futures: (array length=n_futures): an array of futures
 * @n_futures: the number of futures
 *
 * Returns: (transfer full): a #DexFuture
 */
DexFuture *
dex_future_allv (DexFuture **futures,
                 guint       n_futures)
{
  return DEX_FUTURE (dex_future_set_new (futures, n_futures, DEX_FUTURE_SET_FLAGS_NONE));
}
