/*
 * dex-async-pair.c
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

#include "dex-async-pair.h"
#include "dex-private.h"

typedef struct _DexAsyncPair
{
  DexFuture parent_instance;
  gpointer instance;
  DexAsyncPairInfo info;
} DexAsyncPair;

typedef struct _DexAsyncPairClass
{
  DexFutureClass parent_class;
} DexAsyncPairClass;

DEX_DEFINE_FINAL_TYPE (DexAsyncPair, dex_async_pair, DEX_TYPE_FUTURE)

static void
dex_async_pair_finalize (DexObject *object)
{
  DexAsyncPair *async_pair = DEX_ASYNC_PAIR (object);

  g_clear_object (&async_pair->instance);

  DEX_OBJECT_CLASS (dex_async_pair_parent_class)->finalize (object);
}

static void
dex_async_pair_class_init (DexAsyncPairClass *async_pair_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (async_pair_class);

  object_class->finalize = dex_async_pair_finalize;
}

static void
dex_async_pair_init (DexAsyncPair *async_pair)
{
}

#define FINISH_AS(ap, TYPE) \
  (((TYPE (*) (gpointer, GAsyncResult*, GError**))ap->info.finish) (ap->instance, result, &error))

static void
dex_async_pair_ready_callback (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GValue value = G_VALUE_INIT;
  GError *error = NULL;

  g_assert (G_IS_OBJECT (object));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_ASYNC_PAIR (async_pair));

  switch (async_pair->info.return_type)
    {
    case G_TYPE_BOOLEAN:
      g_value_init (&value, G_TYPE_BOOLEAN);
      g_value_set_boolean (&value, FINISH_AS (async_pair, gboolean));
      break;

    case G_TYPE_ENUM:
      g_value_init (&value, G_TYPE_ENUM);
      g_value_set_enum (&value, FINISH_AS (async_pair, guint));
      break;

    case G_TYPE_FLAGS:
      g_value_init (&value, G_TYPE_FLAGS);
      g_value_set_flags (&value, FINISH_AS (async_pair, guint));
      break;

    case G_TYPE_INT:
      g_value_init (&value, G_TYPE_POINTER);
      g_value_set_int (&value, FINISH_AS (async_pair, int));
      break;

    case G_TYPE_UINT:
      g_value_init (&value, G_TYPE_POINTER);
      g_value_set_uint (&value, FINISH_AS (async_pair, guint));
      break;

    case G_TYPE_STRING:
      g_value_init (&value, G_TYPE_STRING);
      g_value_take_string (&value, FINISH_AS (async_pair, char *));
      break;

    case G_TYPE_POINTER:
      g_value_init (&value, G_TYPE_POINTER);
      g_value_set_pointer (&value, FINISH_AS (async_pair, gpointer));
      break;

    case G_TYPE_OBJECT:
      g_value_init (&value, G_TYPE_OBJECT);
      g_value_take_object (&value, FINISH_AS (async_pair, gpointer));
      break;

    default:
      error = g_error_new (G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Type '%s' is not currently supported by DexAsyncPair!",
                           g_type_name (async_pair->info.return_type));
      break;
    }

  if (error != NULL)
    dex_future_complete (DEX_FUTURE (async_pair), NULL, g_steal_pointer (&error));
  else
    dex_future_complete (DEX_FUTURE (async_pair), &value, NULL);

  g_value_unset (&value);
  dex_clear (&async_pair);
}

DexFuture *
dex_async_pair_new (gpointer                instance,
                    const DexAsyncPairInfo *info)
{
  void (*async_func) (gpointer, GCancellable*, GAsyncReadyCallback, gpointer);
  DexAsyncPair *async_pair;

  g_return_val_if_fail (instance != NULL, NULL);
  g_return_val_if_fail (info != NULL, NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);
  async_pair->instance = g_object_ref (instance);
  async_pair->info = *info;

  /* TODO: How can we propagate cancellable state back to the
   * the async_func? We can probably teach the muxer future to
   * notify other futures they are no longer needed, and that
   * could propagate to a cancellable?
   */

  async_func = info->async;
  async_func (instance, NULL, dex_async_pair_ready_callback, async_pair);

  return DEX_FUTURE (async_pair);
}