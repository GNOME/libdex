/*
 * dex-gio.c
 *
 * Copyright 2022-2023 Christian Hergert <chergert@redhat.com>
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

#include <glib/gstdio.h>

#include "dex-async-pair-private.h"
#include "dex-future-private.h"
#include "dex-future-set.h"
#include "dex-gio.h"
#include "dex-promise.h"
#include "dex-thread-pool-scheduler.h"
#include "dex-thread.h"
#include "dex-watch-private.h"

typedef struct _DexFileInfoList DexFileInfoList;

static DexFileInfoList *
dex_file_info_list_copy (DexFileInfoList *list)
{
  return (DexFileInfoList *)g_list_copy_deep ((GList *)list, (GCopyFunc)g_object_ref, NULL);
}

static void
dex_file_info_list_free (DexFileInfoList *list)
{
  GList *real_list = (GList *)list;
  g_list_free_full (real_list, g_object_unref);
}

typedef struct _DexInetAddressList DexInetAddressList;

G_DEFINE_BOXED_TYPE (DexFileInfoList, dex_file_info_list, dex_file_info_list_copy, dex_file_info_list_free)

static DexInetAddressList *
dex_inet_address_list_copy (DexInetAddressList *list)
{
  return (DexInetAddressList *)g_list_copy_deep ((GList *)list, (GCopyFunc)g_object_ref, NULL);
}

static void
dex_inet_address_list_free (DexInetAddressList *list)
{
  GList *real_list = (GList *)list;
  g_list_free_full (real_list, g_object_unref);
}

G_DEFINE_BOXED_TYPE (DexInetAddressList, dex_inet_address_list, dex_inet_address_list_copy, dex_inet_address_list_free)

static inline DexAsyncPair *
create_async_pair (const char *name)
{
  DexAsyncPair *async_pair;

  async_pair = (DexAsyncPair *)dex_object_create_instance (DEX_TYPE_ASYNC_PAIR);
  dex_future_set_static_name (DEX_FUTURE (async_pair), name);

  return async_pair;
}

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

/**
 * dex_input_stream_read_bytes:
 * @stream: a [class@Gio.InputStream]
 * @count: size in bytes to read from the stream
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Reads @count bytes from the stream.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to a [struct@GLib.Bytes].
 */
DexFuture *
dex_input_stream_read_bytes (GInputStream *stream,
                             gsize         count,
                             int           io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_INPUT_STREAM (stream), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_input_stream_read_bytes_async (stream,
                                   count,
                                   io_priority,
                                   async_pair->cancellable,
                                   dex_input_stream_read_bytes_cb,
                                   dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_data_input_stream_read_line_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  char *line = NULL;

  line = g_data_input_stream_read_line_finish (G_DATA_INPUT_STREAM (object), result, NULL, &error);

  if (error == NULL)
    dex_promise_resolve_string (promise, g_steal_pointer (&line));
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_data_input_stream_read_line:
 * @stream: a [class@Gio.DataInputStream]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Reads a line from the data input stream.
 *
 * Wraps [method@Gio.DataInputStream.read_line_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to a string containing the line (without the line terminator), or %NULL
 *   if the end of the stream is reached.
 *
 * Since: 1.1
 */
DexFuture *
dex_data_input_stream_read_line (GDataInputStream *stream,
                                  int               io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_DATA_INPUT_STREAM (stream));

  promise = dex_promise_new_cancellable ();

  g_data_input_stream_read_line_async (stream,
                                       io_priority,
                                       dex_promise_get_cancellable (promise),
                                       dex_data_input_stream_read_line_cb,
                                       dex_ref (promise));

  return DEX_FUTURE (promise);
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

/**
 * dex_output_stream_write_bytes:
 * @stream: a [class@Gio.InputStream]
 * @bytes: the [struct@GLib.Bytes] to write to the stream
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Writes @bytes to @stream.
 *
 * This function takes a reference to @bytes and may be released after
 * calling this function.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to
 *   a `gint64`.
 */
DexFuture *
dex_output_stream_write_bytes (GOutputStream *stream,
                               GBytes        *bytes,
                               int            io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (stream), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_output_stream_write_bytes_async (stream,
                                     bytes,
                                     io_priority,
                                     async_pair->cancellable,
                                     dex_output_stream_write_bytes_cb,
                                     dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_file_create_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  DexPromise *promise = user_data;
  GFileOutputStream *stream;
  GError *error = NULL;

  if (!(stream = g_file_create_finish (G_FILE (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&stream));

  dex_unref (promise);
}

/**
 * dex_file_create:
 * @file: a [iface@Gio.File]
 * @flags: flags for creating the file
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.File.create] as a [class@Dex.Future].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to
 *   a [class@Gio.FileOutputStream] or rejects with error.
 *
 * Since: 1.1
 */
DexFuture *
dex_file_create (GFile            *file,
                 GFileCreateFlags  flags,
                 int               io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_create_async (file,
                       flags,
                       io_priority,
                       dex_promise_get_cancellable (promise),
                       dex_file_create_cb,
                       dex_ref (promise));
  return DEX_FUTURE (promise);
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

/**
 * dex_file_read:
 * @file: a [iface@Gio.File]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Asynchronously opens a file for reading.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to a [class@Gio.FileInputStream].
 */
DexFuture *
dex_file_read (GFile *file,
               int    io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_file_read_async (file,
                     io_priority,
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

/**
 * dex_file_replace:
 * @file: a [iface@Gio.File]
 * @etag: (nullable): the etag or %NULL
 * @flags: a set of [flags@Gio.FileCreateFlags]
 *
 * Opens a stream that will replace @file on disk when the input
 * stream is closed.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to a [class@Gio.FileInputStream].
 */
DexFuture *
dex_file_replace (GFile            *file,
                  const char       *etag,
                  gboolean          make_backup,
                  GFileCreateFlags  flags,
                  int               io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_file_replace_async (file,
                        etag,
                        make_backup,
                        flags,
                        io_priority,
                        async_pair->cancellable,
                        dex_file_replace_cb,
                        dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_file_replace_contents_bytes_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  char *new_etag = NULL;

  g_assert (G_IS_FILE (object));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if (!g_file_replace_contents_finish (G_FILE (object), result, &new_etag, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_string (promise, g_steal_pointer (&new_etag));

  dex_unref (promise);
}

/**
 * dex_file_replace_contents_bytes:
 * @file: a [iface@Gio.File]
 * @contents: a [struct@GLib.Bytes]
 * @etag: (nullable): the etag or %NULL
 * @make_backup: if a backup file should be created
 * @flags: a set of [flags@Gio.FileCreateFlags]
 *
 * Wraps [method@Gio.File.replace_contents_bytes_async]
 *
 * Returns: (transfer full): a [class@Dex.Future] which resolves to the
 *   new etag. Therefore, it is possible to be %NULL without an
 *   error having occurred.
 */
DexFuture *
dex_file_replace_contents_bytes (GFile            *file,
                                 GBytes           *contents,
                                 const char       *etag,
                                 gboolean          make_backup,
                                 GFileCreateFlags  flags)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));
  dex_return_error_if_fail (contents != NULL);

  promise = dex_promise_new_cancellable ();

  g_file_replace_contents_bytes_async (file,
                                       contents,
                                       etag,
                                       make_backup,
                                       flags,
                                       dex_promise_get_cancellable (promise),
                                       dex_file_replace_contents_bytes_cb,
                                       dex_ref (promise));

  return DEX_FUTURE (promise);
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

/**
 * dex_input_stream_read:
 * @self: a [class@Gio.InputStream]
 * @buffer: (array length=count) (element-type guint8) (out caller-allocates)
 * @count: size in bytes to read from the stream into the @buffer
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Reads @count bytes from an input stream into a pre-allocated @buffer. The
 * @buffer must stay valid for the lifetime of this future.
 *
 * Returns: (transfer full): a [class@Dex.Future] that reads @counts bytes
 *   into @buffer
 */
DexFuture *
dex_input_stream_read (GInputStream *self,
                       gpointer      buffer,
                       gsize         count,
                       int           io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_INPUT_STREAM (self), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_input_stream_read_async (self,
                             buffer,
                             count,
                             io_priority,
                             async_pair->cancellable,
                             dex_input_stream_read_cb,
                             dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_input_stream_skip_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  gssize len;

  len = g_input_stream_skip_finish (G_INPUT_STREAM (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_int64 (async_pair, len);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_input_stream_skip:
 * @self: a [class@Gio.InputStream]
 * @count: the number of bytes to skip
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to the number of bytes skipped as a `gint64`.
 */
DexFuture *
dex_input_stream_skip (GInputStream *self,
                       gsize         count,
                       int           io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_INPUT_STREAM (self), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_input_stream_skip_async (self,
                             count,
                             io_priority,
                             async_pair->cancellable,
                             dex_input_stream_skip_cb,
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

/**
 * dex_output_stream_write:
 * @self: a [class@Gio.OutputStream]
 * @buffer: (array length=count) (element-type guint8)
 * @count: size in bytes to write to the output stream
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to the number of bytes written as a `gint64`
 */
DexFuture *
dex_output_stream_write (GOutputStream *self,
                         gconstpointer  buffer,
                         gsize          count,
                         int            io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (self), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_output_stream_write_async (self,
                               buffer,
                               count,
                               io_priority,
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

/**
 * dex_output_stream_close:
 * @self: a [class@Gio.OutputStream]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to true or rejects with error.
 */
DexFuture *
dex_output_stream_close (GOutputStream *self,
                         int            io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (self), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_output_stream_close_async (self,
                               io_priority,
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

/**
 * dex_input_stream_close:
 * @self: a [class@Gio.InputStream]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to true if successful or rejects with error.
 */
DexFuture *
dex_input_stream_close (GInputStream *self,
                        int           io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_INPUT_STREAM (self), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_input_stream_close_async (self,
                              io_priority,
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

/**
 * dex_output_stream_splice:
 * @output: a [class@Gio.OutputStream]
 * @input: a [class@Gio.InputStream]
 * @flags: a set of [flags@Gio.OutputStreamSpliceFlags]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to the
 *   number of bytes spliced as a `gint64` or rejects with error.
 */
DexFuture *
dex_output_stream_splice (GOutputStream            *output,
                          GInputStream             *input,
                          GOutputStreamSpliceFlags  flags,
                          int                       io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (output), NULL);
  g_return_val_if_fail (G_IS_INPUT_STREAM (input), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_output_stream_splice_async (output,
                                input,
                                flags,
                                io_priority,
                                async_pair->cancellable,
                                dex_output_stream_splice_cb,
                                dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_file_query_info_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  GFileInfo *info;

  info = g_file_query_info_finish (G_FILE (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_object (async_pair, info);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_file_query_info:
 * @file: a [iface@Gio.File]
 * @attributes: an attribute query string (see: [GFile documentation](iface@Gio.File))
 * @flags: a set of [flags@Gio.FileQueryInfoFlags]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to a [class@Gio.FileInfo] or rejects with error.
 */
DexFuture *
dex_file_query_info (GFile               *file,
                     const char          *attributes,
                     GFileQueryInfoFlags  flags,
                     int                  io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_file_query_info_async (file,
                           attributes,
                           flags,
                           io_priority,
                           async_pair->cancellable,
                           dex_file_query_info_cb,
                           dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_file_query_file_type_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  DexPromise *promise = user_data;
  GFileInfo *file_info;
  GError *error = NULL;

  if ((file_info = g_file_query_info_finish (G_FILE (object), result, &error)))
    {
      GValue value = G_VALUE_INIT;

      g_value_init (&value, G_TYPE_FILE_TYPE);
      g_value_set_enum (&value, g_file_info_get_file_type (file_info));

      dex_promise_resolve (promise, &value);

      g_value_unset (&value);
      g_object_unref (file_info);
    }
  else dex_promise_reject (promise, error);

  dex_unref (promise);
}

/**
 * dex_file_query_file_type:
 * @file: a [iface@Gio.File]
 * @flags: a set of [flags@Gio.FileQueryInfoFlags]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [enum@Gio.FileType].
 */
DexFuture *
dex_file_query_file_type (GFile               *file,
                          GFileQueryInfoFlags  flags,
                          int                  io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_query_info_async (file,
                           G_FILE_ATTRIBUTE_STANDARD_TYPE,
                           flags,
                           io_priority,
                           dex_promise_get_cancellable (promise),
                           dex_file_query_file_type_cb,
                           dex_ref (promise));
  return DEX_FUTURE (promise);
}

static void
dex_file_make_directory_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;

  g_file_make_directory_finish (G_FILE (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_boolean (async_pair, TRUE);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_file_make_directory:
 * @file: a [iface@Gio.File]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Asynchronously creates a directory and returns [class@Dex.Future] which
 * can be observed for the result.
 *
 * Returns: (transfer full): a [class@Dex.Future]
 */
DexFuture *
dex_file_make_directory (GFile *file,
                         int    io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_file_make_directory_async (file,
                               io_priority,
                               async_pair->cancellable,
                               dex_file_make_directory_cb,
                               dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

typedef struct
{
  GFile *file;
  DexPromise *promise;
} MakeDirectory;

static void
dex_file_make_directory_with_parents_worker (gpointer data)
{
  MakeDirectory *state = data;
  GCancellable *cancellable = dex_promise_get_cancellable (state->promise);
  GError *error = NULL;

  if (!g_file_make_directory_with_parents (state->file, cancellable, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS) &&
          g_file_query_file_type (state->file, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY)
        {
          g_clear_error (&error);
          dex_promise_resolve_boolean (state->promise, TRUE);
        }
      else
        {
          dex_promise_reject (state->promise, g_steal_pointer (&error));
        }
    }
  else
    {
      dex_promise_resolve_boolean (state->promise, TRUE);
    }

  g_clear_object (&state->file);
  dex_clear (&state->promise);
  g_free (state);
}

/**
 * dex_file_make_directory_with_parents:
 * @file: a [iface@Gio.File]
 *
 * Creates a directory at @file.
 *
 * If @file already exists and is a directory, then the future
 * will resolve to %TRUE.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to
 *   a boolean or rejects with error.
 *
 * Since: 1.0
 */
DexFuture *
dex_file_make_directory_with_parents (GFile *file)
{
  MakeDirectory *state;
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();

  state = g_new0 (MakeDirectory, 1);
  state->file = g_object_ref (file);
  state->promise = dex_ref (promise);

  dex_scheduler_push (dex_thread_pool_scheduler_get_default (),
                      dex_file_make_directory_with_parents_worker,
                      state);

  return DEX_FUTURE (promise);
}

static void
dex_file_enumerate_children_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  GFileEnumerator *enumerator;

  enumerator = g_file_enumerate_children_finish (G_FILE (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_object (async_pair, enumerator);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_file_enumerate_children:
 * @file: a [iface@Gio.File]
 * @attributes: an attribute query string (see: [`GFile` documentation](iface@Gio.File))
 * @flags: a set of [flags@Gio.FileQueryInfoFlags]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.FileEnumerator] or rejects with error.
 */
DexFuture *
dex_file_enumerate_children (GFile               *file,
                             const char          *attributes,
                             GFileQueryInfoFlags  flags,
                             int                  io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_file_enumerate_children_async (file,
                                   attributes,
                                   flags,
                                   io_priority,
                                   async_pair->cancellable,
                                   dex_file_enumerate_children_cb,
                                   dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_file_enumerator_next_files_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  GList *infos;

  infos = g_file_enumerator_next_files_finish (G_FILE_ENUMERATOR (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_boxed (async_pair, DEX_TYPE_FILE_INFO_LIST, infos);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_file_enumerator_next_files:
 * @file_enumerator: a [class@Gio.FileEnumerator]
 * @num_files: the number of files
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Wraps [method@Gio.FileEnumerator.next_files_async].
 *
 * Use [method@Dex.Future.await_boxed] to await for the result of this function.
 *
 * When on a fiber, you can do:
 *
 * ```c
 * g_autolist(GFileInfo) infos = dex_await_boxed (dex_file_enumerator_next_files (enumerator, 100, 0), &error);
 * ```
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to
 *   a [struct@GLib.List] of [class@Gio.FileInfo]
 */
DexFuture *
dex_file_enumerator_next_files (GFileEnumerator *file_enumerator,
                                int              num_files,
                                int              io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_FILE_ENUMERATOR (file_enumerator), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_file_enumerator_next_files_async (file_enumerator,
                                      num_files,
                                      io_priority,
                                      async_pair->cancellable,
                                      dex_file_enumerator_next_files_cb,
                                      dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_file_copy_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;

  g_file_copy_finish (G_FILE (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_boolean (async_pair, TRUE);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_file_copy:
 * @source: a [iface@Gio.File]
 * @destination: a [iface@Gio.File]
 * @flags: a set of [flags@Gio.FileCopyFlags]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Asynchronously copies a file and returns a [class@Dex.Future] which
 * can be observed for the result.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to true if successful otherwise rejects with error.
 */
DexFuture *
dex_file_copy (GFile          *source,
               GFile          *destination,
               GFileCopyFlags  flags,
               int             io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_FILE (source), NULL);
  g_return_val_if_fail (G_IS_FILE (destination), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_file_copy_async (source,
                     destination,
                     flags,
                     io_priority,
                     async_pair->cancellable,
                     NULL, NULL, /* TODO: Progress support */
                     dex_file_copy_cb,
                     dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_file_delete_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  g_file_delete_finish (G_FILE (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_boolean (promise, TRUE);
  else
    dex_promise_reject (promise, error);

  dex_unref (promise);
}

/**
 * dex_file_delete:
 * @file: a [iface@Gio.File]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Asynchronously deletes a file and returns a [class@Dex.Future] which
 * can be observed for the result.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to true or rejects with error.
 */
DexFuture *
dex_file_delete (GFile *file,
                 int    io_priority)
{
  DexPromise *promise;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  promise = dex_promise_new_cancellable ();

  g_file_delete_async (file,
                       io_priority,
                       dex_promise_get_cancellable (promise),
                       dex_file_delete_cb,
                       dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_socket_listener_accept_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GSocketConnection *conn;
  GError *error = NULL;

  conn = g_socket_listener_accept_finish (G_SOCKET_LISTENER (object), result, NULL, &error);

  if (error == NULL)
    dex_async_pair_return_object (async_pair, conn);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_socket_listener_accept:
 * @listener: a [class@Gio.SocketListener]
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to
 *   a [class@Gio.SocketConnection] or rejects with error.
 */
DexFuture *
dex_socket_listener_accept (GSocketListener *listener)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_SOCKET_LISTENER (listener), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_socket_listener_accept_async (listener,
                                  async_pair->cancellable,
                                  dex_socket_listener_accept_cb,
                                  dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_socket_client_connect_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GSocketConnection *conn;
  GError *error = NULL;

  conn = g_socket_client_connect_finish (G_SOCKET_CLIENT (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_object (async_pair, conn);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_socket_client_connect:
 * @socket_client: a [class@Gio.SocketClient]
 * @socket_connectable: a [iface@Gio.SocketConnectable]
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.SocketConnection] or rejects with error.
 */
DexFuture *
dex_socket_client_connect (GSocketClient      *socket_client,
                           GSocketConnectable *socket_connectable)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_SOCKET_CLIENT (socket_client), NULL);
  g_return_val_if_fail (G_IS_SOCKET_CONNECTABLE (socket_connectable), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_socket_client_connect_async (socket_client,
                                 socket_connectable,
                                 async_pair->cancellable,
                                 dex_socket_client_connect_cb,
                                 dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_io_stream_close_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;

  g_io_stream_close_finish (G_IO_STREAM (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_boolean (async_pair, TRUE);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_io_stream_close:
 * @io_stream: a [class@Gio.IOStream]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to
 *   true or rejects with error.
 */
DexFuture *
dex_io_stream_close (GIOStream *io_stream,
                     int        io_priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_IO_STREAM (io_stream), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_io_stream_close_async (io_stream,
                           io_priority,
                           async_pair->cancellable,
                           dex_io_stream_close_cb,
                           dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_resolver_lookup_by_name_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  GList *list;

  list = g_resolver_lookup_by_name_finish (G_RESOLVER (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_boxed (async_pair, DEX_TYPE_INET_ADDRESS_LIST, list);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_resolver_lookup_by_name:
 * @resolver: a [class@Gio.Resolver]
 * @address: the address to look up
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [struct@GLib.List] of [class@Gio.InetAddress].
 */
DexFuture *
dex_resolver_lookup_by_name (GResolver  *resolver,
                             const char *address)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_RESOLVER (resolver), NULL);
  g_return_val_if_fail (address != NULL, NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_resolver_lookup_by_name_async (resolver,
                                   address,
                                   async_pair->cancellable,
                                   dex_resolver_lookup_by_name_cb,
                                   dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_file_load_contents_bytes_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;
  char *contents = NULL;
  gsize len = 0;

  g_file_load_contents_finish (G_FILE (object), result, &contents, &len, NULL, &error);

  if (error == NULL)
    dex_async_pair_return_boxed (async_pair, G_TYPE_BYTES, g_bytes_new_take (contents, len));
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_file_load_contents_bytes:
 * @file: a [iface@Gio.File]
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to a [struct@GLib.Bytes].
 */
DexFuture *
dex_file_load_contents_bytes (GFile *file)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_file_load_contents_async (file,
                              async_pair->cancellable,
                              dex_file_load_contents_bytes_cb,
                              dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_subprocess_wait_check_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GError *error = NULL;

  if (g_subprocess_wait_check_finish (G_SUBPROCESS (object), result, &error))
    dex_async_pair_return_boolean (async_pair, TRUE);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_subprocess_wait_check:
 * @subprocess: a [class@Gio.Subprocess]
 *
 * Creates a future that awaits for @subprocess to complete using
 * [method@Gio.Subprocess.wait_check_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that will resolve when @subprocess
 *   exits cleanly or reject upon signal or non-successful exit.
 *
 * Since: 0.4
 */
DexFuture *
dex_subprocess_wait_check (GSubprocess *subprocess)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_SUBPROCESS (subprocess), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_subprocess_wait_check_async (subprocess,
                                 async_pair->cancellable,
                                 dex_subprocess_wait_check_cb,
                                 dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_file_query_exists_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  DexPromise *promise = user_data;
  GFileInfo *file_info;
  GError *error = NULL;

  file_info = g_file_query_info_finish (G_FILE (object), result, &error);

  if (file_info != NULL)
    dex_promise_resolve_boolean (promise, TRUE);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  g_clear_object (&file_info);

  dex_unref (promise);
}

/**
 * dex_file_query_exists:
 * @file: a [iface@Gio.File]
 *
 * Queries to see if @file exists asynchronously.
 *
 * Returns: (transfer full): a [class@Dex.Future] that will resolve with %TRUE
 *   if the file exists, otherwise reject with error.
 *
 * Since: 0.6
 */
DexFuture *
dex_file_query_exists (GFile *file)
{
  DexPromise *promise;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  promise = dex_promise_new_cancellable ();
  g_file_query_info_async (file,
                           G_FILE_ATTRIBUTE_STANDARD_TYPE,
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           dex_promise_get_cancellable (promise),
                           dex_file_query_exists_cb,
                           dex_ref (promise));
  return DEX_FUTURE (promise);
}

static void
dex_async_initable_init_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_async_initable_init_finish (G_ASYNC_INITABLE (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_object_ref (object));

  dex_unref (promise);
}

/**
 * dex_async_initable_init:
 * @initable: a [iface@Gio.AsyncInitable]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * A helper for [method@Gio.AsyncInitable.init_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to the @initable instance or rejects with error.
 *
 * Since: 1.0
 */
DexFuture *
dex_async_initable_init (GAsyncInitable *initable,
                         int             io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_ASYNC_INITABLE (initable));

  promise = dex_promise_new_cancellable ();
  g_async_initable_init_async (initable,
                               io_priority,
                               dex_promise_get_cancellable (promise),
                               dex_async_initable_init_cb,
                               dex_ref (promise));
  return DEX_FUTURE (promise);
}

static void
dex_file_set_attributes_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  DexPromise *promise = user_data;
  GFileInfo *file_info = NULL;
  GError *error = NULL;

  if (!g_file_set_attributes_finish (G_FILE (object), result, &file_info, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&file_info));

  dex_unref (promise);
}

/**
 * dex_file_set_attributes:
 * @file: a [iface@Gio.File]
 * @file_info: a [class@Gio.FileInfo]
 * @flags: a set of [flags@Gio.FileQueryInfoFlags]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.FileInfo] or rejects with error.
 *
 * Since: 1.0
 */
DexFuture *
dex_file_set_attributes (GFile               *file,
                         GFileInfo           *file_info,
                         GFileQueryInfoFlags  flags,
                         int                  io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));
  dex_return_error_if_fail (G_IS_FILE_INFO (file_info));

  promise = dex_promise_new_cancellable ();
  g_file_set_attributes_async (file,
                               file_info,
                               flags,
                               io_priority,
                               dex_promise_get_cancellable (promise),
                               dex_file_set_attributes_cb,
                               dex_ref (promise));
  return DEX_FUTURE (promise);
}

typedef struct
{
  GDestroyNotify notify;
  gpointer data;
  DexPromise *promise;
} FileMove;

static void
dex_file_move_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  FileMove *state = user_data;
  GError *error = NULL;

  if (!g_file_move_finish (G_FILE (object), result, &error))
    dex_promise_reject (state->promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (state->promise, TRUE);

  if (state->notify)
    state->notify (state->data);
  dex_clear (&state->promise);
  g_free (state);
}

/**
 * dex_file_move:
 * @source: source [iface@Gio.File]
 * @destination: destination [iface@Gio.File]
 * @flags: a set of [flags@Gio.FileCopyFlags]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 * @progress_callback: (scope notified) (closure progress_callback_data):
 *   [callback@Gio.FileProgressCallback] function for updates
 * @progress_callback_data: `gpointer` to user data for the callback function
 * @progress_callback_data_destroy: (nullable): a function to destroy the
 *   @progress_callback_data, or %NULL
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error
 */
DexFuture *
dex_file_move (GFile                 *source,
               GFile                 *destination,
               GFileCopyFlags         flags,
               int                    io_priority,
               GFileProgressCallback  progress_callback,
               gpointer               progress_callback_data,
               GDestroyNotify         progress_callback_data_destroy)
{
  FileMove *state;
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (source));
  dex_return_error_if_fail (G_IS_FILE (destination));

  promise = dex_promise_new_cancellable ();

  state = g_new0 (FileMove, 1);
  state->data = progress_callback_data;
  state->notify = progress_callback_data_destroy;
  state->promise = dex_ref (promise);

  g_file_move_async (source,
                     destination,
                     flags,
                     io_priority,
                     dex_promise_get_cancellable (promise),
                     progress_callback_data,
                     progress_callback_data_destroy,
                     dex_file_move_cb,
                     state);

  return DEX_FUTURE (promise);
}

typedef struct _MkdirWithParents
{
  char *path;
  int mode;
} MkdirWithParents;

static void
mkdir_with_parents_free (MkdirWithParents *state)
{
  g_free (state->path);
  g_free (state);
}

static DexFuture *
dex_mkdir_with_parents_thread (gpointer data)
{
  MkdirWithParents *state = data;

  if (g_mkdir_with_parents (state->path, state->mode) == -1)
    return dex_future_new_for_errno (errno);
  else
    return dex_future_new_for_int (0);
}

/**
 * dex_mkdir_with_parents:
 * @path: a path to a directory to create
 * @mode: the mode for the directory such as `0750`
 *
 * Similar to [func@GLib.mkdir_with_parents] but runs on a dedicated thread.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to 0
 *   if successful, otherwise rejects with error.
 *
 * Since: 1.1
 */
DexFuture *
dex_mkdir_with_parents (const char *path,
                        int         mode)
{
  MkdirWithParents *state;

  dex_return_error_if_fail (path != NULL);

  /* NOTE: It may be wise in the future to do this with io_uring
   *       for the cases where we can to avoid a thread.
   */

  state = g_new0 (MkdirWithParents, 1);
  state->path = g_strdup (path);
  state->mode = mode;

  return dex_thread_spawn ("[mkdir-with-parents]",
                           dex_mkdir_with_parents_thread,
                           state,
                           (GDestroyNotify) mkdir_with_parents_free);
}

static DexFuture *
dex_find_program_in_path_thread (gpointer data)
{
  const char *program = data;
  char *path;

  if ((path = g_find_program_in_path (program)))
    return dex_future_new_take_string (g_steal_pointer (&path));

  return dex_future_new_reject (G_FILE_ERROR,
                                g_file_error_from_errno (ENOENT),
                                "%s", g_strerror (ENOENT));
}

/**
 * dex_find_program_in_path:
 * @program: the name of the executable such as "grep"
 *
 * Locates the first executable named program in the user’s path.
 *
 * This runs [func@GLib.find_program_in_path] on a dedicated thread.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   string containing the path or rejects with error.
 *
 * Since: 1.1
 */
DexFuture *
dex_find_program_in_path (const char *program)
{
  dex_return_error_if_fail (program != NULL);

  return dex_thread_spawn ("[find-program-in-path]",
                           dex_find_program_in_path_thread,
                           g_strdup (program),
                           g_free);
}

static DexFuture *
dex_unlink_thread (gpointer data)
{
  const char *path = data;

  if (g_unlink (path) == -1)
    return dex_future_new_for_errno (errno);

  return dex_future_new_for_int (0);
}

/**
 * dex_unlink:
 * @path: the path to unlink
 *
 * This runs [func@GLib.unlink] on a dedicated thread.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to an
 *   int of 0 on success or rejects with error.
 *
 * Since: 1.1
 */
DexFuture *
dex_unlink (const char *path)
{
  dex_return_error_if_fail (path != NULL);

  return dex_thread_spawn ("[unlink]",
                           dex_unlink_thread,
                           g_strdup (path),
                           g_free);
}

/**
 * dex_fd_watch:
 * @fd: a file-descriptor
 * @events: the POLLOUT|POLLIN style bitmask to watch for
 *
 * Creates a new GSource that will fire when @events is satisfied.
 *
 * This is primarily useful when integrating with legacy systems on a
 * fiber.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to the
 *   revents value when @events is satisfied.
 *
 * Since: 1.1
 */
DexFuture *
dex_fd_watch (int fd,
              int events)
{
  return dex_watch_new (fd, events);
}
