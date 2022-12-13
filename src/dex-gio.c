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
  GBytes *bytes;

  bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_boxed (async_pair, G_TYPE_BYTES, bytes);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
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
  gssize len;

  len = g_output_stream_write_bytes_finish (G_OUTPUT_STREAM (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_int64 (async_pair, len);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
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
  GFileInputStream *stream;
  GError *error = NULL;

  if ((stream = g_file_read_finish (G_FILE (object), result, &error)))
    dex_async_pair_return_object (async_pair, stream);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
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

static void
dex_file_replace_cb (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GFileOutputStream *stream;
  GError *error = NULL;

  if ((stream = g_file_replace_finish (G_FILE (object), result, &error)))
    dex_async_pair_return_object (async_pair, stream);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

DexFuture *
dex_file_replace (GFile            *file,
                  const char       *etag,
                  gboolean          make_backup,
                  GFileCreateFlags  flags,
                  int               priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);

  g_file_replace_async (file,
                        etag,
                        make_backup,
                        flags,
                        priority,
                        async_pair->cancellable,
                        dex_file_replace_cb,
                        dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_input_stream_read_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  gssize len;

  len = g_input_stream_read_finish (G_INPUT_STREAM (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_int64 (async_pair, len);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

DexFuture *
dex_input_stream_read (GInputStream *self,
                       gpointer      buffer,
                       gsize         count,
                       int           priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_INPUT_STREAM (self), NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);

  g_input_stream_read_async (self,
                             buffer,
                             count,
                             priority,
                             async_pair->cancellable,
                             dex_input_stream_read_cb,
                             dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_output_stream_write_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  gssize len;

  len = g_output_stream_write_finish (G_OUTPUT_STREAM (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_int64 (async_pair, len);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

DexFuture *
dex_output_stream_write (GOutputStream *self,
                         gconstpointer  buffer,
                         gsize          count,
                         int            priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (self), NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);

  g_output_stream_write_async (self,
                               buffer,
                               count,
                               priority,
                               async_pair->cancellable,
                               dex_output_stream_write_cb,
                               dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_output_stream_close_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;

  g_output_stream_close_finish (G_OUTPUT_STREAM (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_boolean (async_pair, TRUE);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

DexFuture *
dex_output_stream_close (GOutputStream *self,
                         int            priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (self), NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);

  g_output_stream_close_async (self,
                               priority,
                               async_pair->cancellable,
                               dex_output_stream_close_cb,
                               dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_input_stream_close_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;

  g_input_stream_close_finish (G_INPUT_STREAM (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_boolean (async_pair, TRUE);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

DexFuture *
dex_input_stream_close (GInputStream *self,
                        int           priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_INPUT_STREAM (self), NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);

  g_input_stream_close_async (self,
                              priority,
                              async_pair->cancellable,
                              dex_input_stream_close_cb,
                              dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_output_stream_splice_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  gssize len;

  len = g_output_stream_splice_finish (G_OUTPUT_STREAM (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_int64 (async_pair, len);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

DexFuture *
dex_output_stream_splice (GOutputStream            *output,
                          GInputStream             *input,
                          GOutputStreamSpliceFlags  flags,
                          int                       io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (output), NULL);
  g_return_val_if_fail (G_IS_INPUT_STREAM (input), NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);

  g_output_stream_splice_async (output,
                                input,
                                flags,
                                io_priority,
                                async_pair->cancellable,
                                dex_output_stream_splice_cb,
                                dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}
