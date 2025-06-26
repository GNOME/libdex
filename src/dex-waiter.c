/*
 * dex-waiter.c
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include "dex-waiter-private.h"

typedef struct _DexWaiter
{
  DexFuture parent_instance;
  GMutex mutex;
  GCond cond;
} DexWaiter;

typedef struct _DexWaiterClass
{
  DexFutureClass parent_class;
} DexWaiterClass;

DEX_DEFINE_FINAL_TYPE (DexWaiter, dex_waiter, DEX_TYPE_FUTURE)

#undef DEX_TYPE_WAITER
#define DEX_TYPE_WAITER dex_waiter_type

static void
dex_waiter_discard (DexFuture *future)
{
}

static gboolean
dex_waiter_propagate (DexFuture *future,
                      DexFuture *completed)
{
  DexWaiter *waiter = (DexWaiter *)future;
  GError *error = NULL;
  const GValue *value;

  g_assert (DEX_IS_WAITER (waiter));

  g_mutex_lock (&waiter->mutex);
  value = dex_future_get_value (completed, &error);
  dex_future_complete (future, value, error);
  g_cond_signal (&waiter->cond);
  g_mutex_unlock (&waiter->mutex);

  return TRUE;
}

static void
dex_waiter_finalize (DexObject *object)
{
  DexWaiter *waiter = (DexWaiter *)object;

  g_cond_clear (&waiter->cond);
  g_mutex_clear (&waiter->mutex);

  DEX_OBJECT_CLASS (dex_waiter_parent_class)->finalize (object);
}

static void
dex_waiter_class_init (DexWaiterClass *waiter_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (waiter_class);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (waiter_class);

  object_class->finalize = dex_waiter_finalize;

  future_class->propagate = dex_waiter_propagate;
  future_class->discard = dex_waiter_discard;
}

static void
dex_waiter_init (DexWaiter *waiter)
{
}

DexWaiter *
dex_waiter_new (DexFuture *future)
{
  DexWaiter *waiter;

  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);

  waiter = (DexWaiter *)dex_object_create_instance (dex_waiter_type);
  g_mutex_init (&waiter->mutex);
  g_cond_init (&waiter->cond);

  dex_future_chain (future, DEX_FUTURE (waiter));

  dex_unref (future);

  return waiter;
}

void
dex_waiter_wait (DexWaiter *self)
{
  g_return_if_fail (DEX_IS_WAITER (self));

  g_mutex_lock (&self->mutex);
  if (dex_future_is_pending (DEX_FUTURE (self)))
    g_cond_wait (&self->cond, &self->mutex);
  g_mutex_unlock (&self->mutex);
}
