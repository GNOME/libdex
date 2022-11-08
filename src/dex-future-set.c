/*
 * dex-future-set.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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

#include "dex-future-set.h"
#include "dex-private.h"

typedef struct _DexFutureSet
{
  DexFuture parent_instance;

  /* Future's we're waiting on */
  DexFuture **futures;
  guint n_futures;

  /* Number of futures required to succeed */
  guint n_success;

  /* Number of futures resolved or rejected */
  guint n_resolved;
  guint n_rejected;

  /* If we can race, meaning complete as soon as we meet the
   * minimum bar for n_success rather than wait for all to
   * complete.
   */
  guint can_race : 1;

  /* If the first resolve/reject should be propagated as this
   * futures resolve/reject (such as dex_future_any()).
   */
  guint propagate_first : 1;
} DexFutureSet;

typedef struct _DexFutureSetClass
{
  DexFutureClass parent_class;
} DexFutureSetClass;

DEX_DEFINE_FINAL_TYPE (DexFutureSet, dex_future_set, DEX_TYPE_FUTURE)

static GValue success_value = G_VALUE_INIT;

static gboolean
dex_future_set_propagate (DexFuture *future,
                          DexFuture *completed)
{
  DexFutureSet *future_set = DEX_FUTURE_SET (future);
  const GValue *resolved = NULL;
  GError *rejected = NULL;
  guint n_active = 0;

  g_assert (DEX_IS_FUTURE_SET (future_set));
  g_assert (DEX_IS_FUTURE (completed));
  g_assert (future != completed);

  dex_object_lock (future_set);

  switch (dex_future_get_status (completed))
    {
    case DEX_FUTURE_STATUS_RESOLVED:
      future_set->n_resolved++;
      break;

    case DEX_FUTURE_STATUS_REJECTED:
      future_set->n_rejected++;
      break;

    case DEX_FUTURE_STATUS_PENDING:
    default:
      g_assert_not_reached ();
    }

  /* Only process results if our result is still pending */
  if (future->status == DEX_FUTURE_STATUS_PENDING)
    {
      n_active = future_set->n_futures - (future_set->n_resolved + future_set->n_rejected);

      if (future_set->propagate_first)
        resolved = dex_future_get_value (completed, &rejected);
      else if (future_set->n_futures - future_set->n_rejected < future_set->n_success)
        rejected = g_error_new_literal (G_IO_ERROR,
                                        G_IO_ERROR_FAILED,
                                        "Too many failures, cannot complete");
      else if (future_set->n_resolved >= future_set->n_success)
        resolved = &success_value;
    }

  dex_object_unlock (future_set);

  if (n_active == 0 || future_set->can_race)
    {
      if (resolved != NULL || rejected != NULL)
        dex_future_complete (future, resolved, g_steal_pointer (&rejected));
    }

  g_clear_error (&rejected);

  return TRUE;
}

static void
dex_future_set_finalize (DexObject *object)
{
  DexFutureSet *future_set = DEX_FUTURE_SET (object);

  for (guint i = 0; i < future_set->n_futures; i++)
    dex_unref (future_set->futures[i]);
  g_free (future_set->futures);

  future_set->futures = NULL;
  future_set->n_futures = 0;
  future_set->n_success = 0;
  future_set->can_race = 0;

  DEX_OBJECT_CLASS (dex_future_set_parent_class)->finalize (object);
}

static void
dex_future_set_class_init (DexFutureSetClass *future_set_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (future_set_class);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (future_set_class);

  object_class->finalize = dex_future_set_finalize;

  future_class->propagate = dex_future_set_propagate;

  g_value_init (&success_value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&success_value, TRUE);
}

static void
dex_future_set_init (DexFutureSet *future_set)
{
}

DexFutureSet *
dex_future_set_new (DexFuture **futures,
                    guint       n_futures,
                    guint       n_success,
                    gboolean    can_race,
                    gboolean    propagate_first)
{
  DexFutureSet *future_set;

  g_return_val_if_fail (futures != NULL, NULL);
  g_return_val_if_fail (n_futures > 0, NULL);
  g_return_val_if_fail (n_success > 0, NULL);
  g_return_val_if_fail (n_success <= n_futures, NULL);

  future_set = (DexFutureSet *)g_type_create_instance (DEX_TYPE_FUTURE_SET);
  future_set->n_futures = n_futures;
  future_set->n_success = n_success;
  future_set->can_race = !!can_race;
  future_set->propagate_first = !!propagate_first;
  future_set->futures = g_memdup2 (futures, sizeof (DexFuture *) * n_futures);

  for (guint i = 0; i < n_futures; i++)
    {
      g_assert (DEX_IS_FUTURE (futures[i]));
      dex_ref (future_set->futures[i]);
    }

  for (guint i = 0; i < n_futures; i++)
    dex_future_chain (future_set->futures[i], DEX_FUTURE (future_set));

  return future_set;
}

guint
dex_future_set_get_size (DexFutureSet *future_set)
{
  g_return_val_if_fail (DEX_IS_FUTURE_SET (future_set), 0);

  return future_set->n_futures;
}

/**
 * dex_future_set_get_future:
 * @future_set: a #DexFutureSet
 *
 * Gets a #DexFuture that was used to produce the result of @future_set.
 *
 * Use dex_future_set_get_size() to determine the number of #DexFuture that
 * are contained within the #DexFutureSet.
 *
 * Returns: (transfer none): a #DexFuture
 */
DexFuture *
dex_future_set_get_future (DexFutureSet *future_set,
                           guint         position)
{
  g_return_val_if_fail (DEX_IS_FUTURE_SET (future_set), NULL);
  g_return_val_if_fail (position < future_set->n_futures, NULL);

  return future_set->futures[position];
}
