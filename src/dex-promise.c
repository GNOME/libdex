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
(dex_promise_new) (void)
{
  return (DexPromise *)g_type_create_instance (dex_promise_type);
}

/**
 * dex_promise_new_for_value: (constructor)
 * @value: a #GValue containing the resolved value
 *
 * Creates a new #DexPromise which is already resolved.
 *
 * Returns: (transfer full): a resolved #DexPromise
 */
DexPromise *
(dex_promise_new_for_value) (const GValue *value)
{
  DexPromise *promise;

  g_return_val_if_fail (!value || G_IS_VALUE (value), NULL);

  promise = dex_promise_new ();

  if (value != NULL)
    dex_promise_resolve (promise, value);

  return promise;
}

/**
 * dex_promise_new_for_boolean: (constructor)
 * @v_bool: the resolved value for the promise
 *
 * Creates a new #DexPromise and resolves it with @v_bool.
 *
 * Returns: (transfer full): a resolved #DexPromise
 */
DexPromise *
(dex_promise_new_for_boolean) (gboolean v_bool)
{
  GValue value = G_VALUE_INIT;
  DexPromise *promise;

  g_value_init (&value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&value, v_bool);
  promise = (dex_promise_new_for_value) (&value);
  g_value_unset (&value);

  return promise;
}

/**
 * dex_promise_new_for_int: (constructor)
 * @v_int: the resolved value for the promise
 *
 * Creates a new #DexPromise and resolves it with @v_int.
 *
 * Returns: (transfer full): a resolved #DexPromise
 */
DexPromise *
(dex_promise_new_for_int) (int v_int)
{
  GValue value = G_VALUE_INIT;
  DexPromise *promise;

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, v_int);
  promise = (dex_promise_new_for_value) (&value);
  g_value_unset (&value);

  return promise;
}

/**
 * dex_promise_new_for_uint: (constructor)
 * @v_uint: the resolved value for the promise
 *
 * Creates a new #DexPromise and resolves it with @v_uint.
 *
 * Returns: (transfer full): a resolved #DexPromise
 */
DexPromise *
(dex_promise_new_for_uint) (guint v_uint)
{
  GValue value = G_VALUE_INIT;
  DexPromise *promise;

  g_value_init (&value, G_TYPE_UINT);
  g_value_set_uint (&value, v_uint);
  promise = (dex_promise_new_for_value) (&value);
  g_value_unset (&value);

  return promise;
}

/**
 * dex_promise_new_for_string: (constructor)
 * @string: the resolved value for the promise
 *
 * Creates a new #DexPromise and resolves it with @string.
 *
 * Returns: (transfer full): a resolved #DexPromise
 */
DexPromise *
(dex_promise_new_for_string) (const char *string)
{
  GValue value = G_VALUE_INIT;
  DexPromise *promise;

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_static_string (&value, string);
  promise = (dex_promise_new_for_value) (&value);
  g_value_unset (&value);

  return promise;
}

/**
 * dex_promise_new_for_error: (constructor)
 * @error: (transfer full): a #GError containing the rejection
 *
 * Creates a new #DexPromise that is rejeced.
 *
 * @error should contain the rejection error that will be
 * propagated to waiters of the promise.
 *
 * Returns: (transfer full): a new #DexPromise
 */
DexPromise *
(dex_promise_new_for_error) (GError *error)
{
  DexPromise *promise;

  g_return_val_if_fail (error != NULL, NULL);

  promise = dex_promise_new ();
  dex_future_complete (DEX_FUTURE (promise), NULL, error);
  return promise;
}

/**
 * dex_promise_new_reject: (constructor)
 * @domain: the error domain
 * @code: the error code
 * @format: a printf-style format string
 *
 * Creates a new #DexPromise that is rejeced.
 *
 * Returns: (transfer full): a new #DexPromise
 */
DexPromise *
(dex_promise_new_reject) (GQuark      domain,
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

  return dex_promise_new_for_error (error);
}

/**
 * dex_promise_new_take_boxed: (constructor) (skip)
 * @boxed_type: the GBoxed-based type
 * @value: (transfer full): the value for the boxed type
 *
 * Creates a new #DexPromise that is resolved with @value.
 *
 * Returns: (transfer full): a new #DexPromise that is resolved
 */
DexPromise *
dex_promise_new_take_boxed (GType    boxed_type,
                            gpointer value)
{
  GValue gvalue = G_VALUE_INIT;
  DexPromise *ret;

  g_return_val_if_fail (G_TYPE_FUNDAMENTAL (boxed_type) == G_TYPE_BOXED, NULL);

  g_value_init (&gvalue, boxed_type);
  g_value_take_boxed (&gvalue, value);
  ret = dex_promise_new_for_value (&gvalue);
  g_value_unset (&gvalue);

  return ret;
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

void
dex_promise_resolve_int (DexPromise *promise,
                         int         value)
{
  GValue gvalue = {G_TYPE_INT, {{.v_int = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
}

void
dex_promise_resolve_uint (DexPromise *promise,
                          guint       value)
{
  GValue gvalue = {G_TYPE_UINT, {{.v_uint = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
}

void
dex_promise_resolve_int64 (DexPromise *promise,
                           gint64      value)
{
  GValue gvalue = {G_TYPE_INT64, {{.v_int64 = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
}

void
dex_promise_resolve_uint64 (DexPromise *promise,
                            guint64     value)
{
  GValue gvalue = {G_TYPE_UINT64, {{.v_int64 = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
}

void
dex_promise_resolve_long (DexPromise *promise,
                          glong       value)
{
  GValue gvalue = {G_TYPE_LONG, {{.v_long = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
}

void
dex_promise_resolve_ulong (DexPromise *promise,
                           glong       value)
{
  GValue gvalue = {G_TYPE_ULONG, {{.v_ulong = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
}

void
dex_promise_resolve_float (DexPromise *promise,
                           float       value)
{
  GValue gvalue = {G_TYPE_FLOAT, {{.v_float = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
}

void
dex_promise_resolve_double (DexPromise *promise,
                            double      value)
{
  GValue gvalue = {G_TYPE_DOUBLE, {{.v_double = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
}

void
dex_promise_resolve_boolean (DexPromise *promise,
                             gboolean    value)
{
  GValue gvalue = {G_TYPE_BOOLEAN, {{.v_int = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
}

/**
 * dex_promise_resolve_string:
 * @self: a #DexPromise
 * @value: (transfer full): a string to use to resolve the promise
 *
 */
void
dex_promise_resolve_string (DexPromise *promise,
                            char       *value)
{
  GValue gvalue = {G_TYPE_STRING, {{.v_pointer = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
  g_free (value);
}

/**
 * dex_promise_resolve_object:
 * @self: a #DexPromise
 * @value: (transfer full): a #GObject
 *
 */
void
dex_promise_resolve_object (DexPromise *promise,
                            gpointer    value)
{
  GValue gvalue = {G_OBJECT_TYPE (value), {{.v_pointer = value}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
  g_object_unref (value);
}
