/*
 * dex-promise.c
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

#include "dex-future-private.h"
#include "dex-promise.h"

typedef struct _DexPromise
{
  DexFuture parent_instance;
} DexPromise;

typedef struct _DexPromiseClass
{
  DexFutureClass parent_class;
} DexPromiseClass;

DEX_DEFINE_FINAL_TYPE (DexPromise, dex_promise, DEX_TYPE_FUTURE)

static void
dex_promise_class_init (DexPromiseClass *promise_class)
{
}

static void
dex_promise_init (DexPromise *promise)
{
}

DexPromise *
dex_promise_new (void)
{
  return (DexPromise *)g_type_create_instance (dex_promise_type);
}

DexPromise *
dex_promise_new_for_value (const GValue *value)
{
  DexPromise *promise;

  g_return_val_if_fail (!value || G_IS_VALUE (value), NULL);

  promise = dex_promise_new ();

  if (value != NULL)
    dex_promise_resolve (promise, value);

  return promise;
}

DexPromise *
dex_promise_new_for_boolean (gboolean v_bool)
{
  GValue value = G_VALUE_INIT;
  DexPromise *promise;

  g_value_init (&value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&value, v_bool);
  promise = dex_promise_new_for_value (&value);
  g_value_unset (&value);

  return promise;
}

DexPromise *
dex_promise_new_for_int (int v_int)
{
  GValue value = G_VALUE_INIT;
  DexPromise *promise;

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, v_int);
  promise = dex_promise_new_for_value (&value);
  g_value_unset (&value);

  return promise;
}

DexPromise *
dex_promise_new_for_string (const char *string)
{
  GValue value = G_VALUE_INIT;
  DexPromise *promise;

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_static_string (&value, string);
  promise = dex_promise_new_for_value (&value);
  g_value_unset (&value);

  return promise;
}

/**
 * dex_promise_new_for_error:
 * @error: (transfer full): a #GError
 *
 * Creates a new #DexPromise that is rejeced.
 *
 * @error should contain the rejection error that will be
 * propagated to waiters of the promise.
 *
 * Returns: (transfer full): a new #DexPromise
 */
DexPromise *
dex_promise_new_for_error (GError *error)
{
  DexPromise *promise;

  g_return_val_if_fail (error != NULL, NULL);

  promise = dex_promise_new ();
  dex_future_complete (DEX_FUTURE (promise), NULL, error);
  return promise;
}

/**
 * dex_promise_resolve:
 * @promise: a #DexPromise
 * @value: a #GValue containing the resolved value
 *
 * Sets the result for a #DexPromise.
 */
void
dex_promise_resolve (DexPromise   *promise,
                     const GValue *value)
{
  g_return_if_fail (DEX_IS_PROMISE (promise));
  g_return_if_fail (value != NULL && G_IS_VALUE (value));

  dex_future_complete (DEX_FUTURE (promise), value, NULL);
}

/**
 * dex_promise_reject:
 * @promise: a #DexPromise
 * @error: (transfer full): a #GError
 *
 * Marks the promise as rejected, indicating a failure.
 */
void
dex_promise_reject (DexPromise *promise,
                    GError     *error)
{
  g_return_if_fail (DEX_IS_PROMISE (promise));
  g_return_if_fail (error != NULL);

  dex_future_complete (DEX_FUTURE (promise), NULL, error);
}
