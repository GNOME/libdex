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

#undef DEX_TYPE_PROMISE
#define DEX_TYPE_PROMISE dex_promise_type

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
