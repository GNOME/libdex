/*
 * dex-gio.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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

#include "dex-async-pair-private.h"
#include "dex-future-private.h"
#include "dex-gio.h"

static void
dex_input_stream_read_bytes_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  GValue value = G_VALUE_INIT;
  const GValue *ret = NULL;
  GBytes *bytes;

  bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (object), result, &error);

  if (error == NULL)
    {
      g_value_init (&value, G_TYPE_BYTES);
      g_value_take_boxed (&value, g_steal_pointer (&bytes));
      ret = &value;
    }

  dex_future_complete (DEX_FUTURE (async_pair), ret, error);
  g_value_unset (&value);
}

DexFuture *
dex_input_stream_read_bytes (GInputStream *stream,
                             gsize         count,
                             int           priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_INPUT_STREAM (stream), NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);

  g_input_stream_read_bytes_async (stream,
                                   count,
                                   priority,
                                   async_pair->cancellable,
                                   dex_input_stream_read_bytes_cb,
                                   dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_output_stream_write_bytes_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  GValue value = G_VALUE_INIT;
  const GValue *ret = NULL;
  gssize len;

  len = g_output_stream_write_bytes_finish (G_OUTPUT_STREAM (object), result, &error);

  if (error == NULL)
    {
      g_value_init (&value, G_TYPE_INT64);
      g_value_set_int64 (&value, len);
      ret = &value;
    }

  dex_future_complete (DEX_FUTURE (async_pair), ret, error);
  g_value_unset (&value);
}

DexFuture *
dex_output_stream_write_bytes (GOutputStream *stream,
                               GBytes        *bytes,
                               int            priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (stream), NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);

  g_output_stream_write_bytes_async (stream,
                                     bytes,
                                     priority,
                                     async_pair->cancellable,
                                     dex_output_stream_write_bytes_cb,
                                     dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_file_read_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  GValue value = G_VALUE_INIT;
  const GValue *ret = NULL;
  GFileInputStream *stream;

  if ((stream = g_file_read_finish (G_FILE (object), result, &error)))
    {
      g_value_init (&value, G_TYPE_FILE_INPUT_STREAM);
      g_value_take_object (&value, g_steal_pointer (&stream));
      ret = &value;
    }

  dex_future_complete (DEX_FUTURE (async_pair), ret, error);
  g_value_unset (&value);
}

DexFuture *
dex_file_read (GFile *file,
               int    priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);

  g_file_read_async (file,
                     priority,
                     async_pair->cancellable,
                     dex_file_read_cb,
                     dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}
