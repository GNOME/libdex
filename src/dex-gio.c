/*
 * dex-gio.c
 *
 * Copyright 2022-2023 Christian Hergert <christian@sourceandstack.com>
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

#include <stdarg.h>

#include <glib/gstdio.h>

#include "dex-async-pair-private.h"
#include "dex-future-private.h"
#include "dex-future-set.h"
#include "dex-gio.h"
#include "dex-promise.h"
#include "dex-socket-wait-private.h"
#include "dex-thread-pool-scheduler.h"
#include "dex-thread.h"
#include "dex-watch-private.h"

typedef struct _DexFileInfoList DexFileInfoList;
typedef struct _DexSrvTargetList DexSrvTargetList;
typedef struct _DexTlsCertificateList DexTlsCertificateList;
typedef struct _DexVariantList DexVariantList;

#define DEX_TYPE_SRV_TARGET_LIST (dex_srv_target_list_get_type())
#define DEX_TYPE_TLS_CERTIFICATE_LIST (dex_tls_certificate_list_get_type())
#define DEX_TYPE_VARIANT_LIST (dex_variant_list_get_type())

GType dex_srv_target_list_get_type (void) G_GNUC_CONST;
GType dex_tls_certificate_list_get_type (void) G_GNUC_CONST;
GType dex_variant_list_get_type (void) G_GNUC_CONST;

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

static DexSrvTargetList *
dex_srv_target_list_copy (DexSrvTargetList *list)
{
  return (DexSrvTargetList *)g_list_copy_deep ((GList *)list, (GCopyFunc)g_srv_target_copy, NULL);
}

static void
dex_srv_target_list_free (DexSrvTargetList *list)
{
  GList *real_list = (GList *)list;
  g_resolver_free_targets (real_list);
}

G_DEFINE_BOXED_TYPE (DexSrvTargetList, dex_srv_target_list, dex_srv_target_list_copy, dex_srv_target_list_free)

static DexTlsCertificateList *
dex_tls_certificate_list_copy (DexTlsCertificateList *list)
{
  return (DexTlsCertificateList *)g_list_copy_deep ((GList *)list, (GCopyFunc)g_object_ref, NULL);
}

static void
dex_tls_certificate_list_free (DexTlsCertificateList *list)
{
  GList *real_list = (GList *)list;
  g_list_free_full (real_list, g_object_unref);
}

G_DEFINE_BOXED_TYPE (DexTlsCertificateList, dex_tls_certificate_list, dex_tls_certificate_list_copy, dex_tls_certificate_list_free)

static DexVariantList *
dex_variant_list_copy (DexVariantList *list)
{
  return (DexVariantList *)g_list_copy_deep ((GList *)list, (GCopyFunc)g_variant_ref, NULL);
}

static void
dex_variant_list_free (DexVariantList *list)
{
  GList *real_list = (GList *)list;
  g_list_free_full (real_list, (GDestroyNotify)g_variant_unref);
}

G_DEFINE_BOXED_TYPE (DexVariantList, dex_variant_list, dex_variant_list_copy, dex_variant_list_free)

static inline DexAsyncPair *
create_async_pair (const char *name)
{
  DexAsyncPair *async_pair;

  async_pair = (DexAsyncPair *)dex_object_create_instance (DEX_TYPE_ASYNC_PAIR);
  dex_future_set_static_name (DEX_FUTURE (async_pair), name);

  return async_pair;
}

static void
dex_app_info_launch_uris_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_app_info_launch_uris_finish (G_APP_INFO (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_app_info_launch_uris:
 * @appinfo: a [iface@Gio.AppInfo]
 * @uris: (element-type utf8): URIs to launch
 * @context: (nullable): a [class@Gio.AppLaunchContext], or %NULL
 *
 * Wraps [method@Gio.AppInfo.launch_uris_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_app_info_launch_uris (GAppInfo          *appinfo,
                          GList             *uris,
                          GAppLaunchContext *context)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_APP_INFO (appinfo));

  promise = dex_promise_new_cancellable ();

  g_app_info_launch_uris_async (appinfo,
                                uris,
                                context,
                                dex_promise_get_cancellable (promise),
                                dex_app_info_launch_uris_cb,
                                dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_app_info_get_default_for_type_cb (GObject      *object,
                                      GAsyncResult *result,
                                      gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GAppInfo *app_info;

  g_assert (object == NULL);

  app_info = g_app_info_get_default_for_type_finish (result, &error);

  if (error == NULL)
    dex_promise_resolve_object (promise, g_steal_pointer (&app_info));
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_app_info_get_default_for_type:
 * @content_type: a content type
 * @must_support_uris: whether the app must support URIs
 *
 * Wraps [func@Gio.AppInfo.get_default_for_type_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [iface@Gio.AppInfo] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_app_info_get_default_for_type (const char *content_type,
                                   gboolean    must_support_uris)
{
  DexPromise *promise;

  dex_return_error_if_fail (content_type != NULL);

  promise = dex_promise_new_cancellable ();

  g_app_info_get_default_for_type_async (content_type,
                                         must_support_uris,
                                         dex_promise_get_cancellable (promise),
                                         dex_app_info_get_default_for_type_cb,
                                         dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_app_info_get_default_for_uri_scheme_cb (GObject      *object,
                                            GAsyncResult *result,
                                            gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GAppInfo *app_info;

  g_assert (object == NULL);

  app_info = g_app_info_get_default_for_uri_scheme_finish (result, &error);

  if (error == NULL)
    dex_promise_resolve_object (promise, g_steal_pointer (&app_info));
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_app_info_get_default_for_uri_scheme:
 * @uri_scheme: a URI scheme
 *
 * Wraps [func@Gio.AppInfo.get_default_for_uri_scheme_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [iface@Gio.AppInfo] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_app_info_get_default_for_uri_scheme (const char *uri_scheme)
{
  DexPromise *promise;

  dex_return_error_if_fail (uri_scheme != NULL);

  promise = dex_promise_new_cancellable ();

  g_app_info_get_default_for_uri_scheme_async (uri_scheme,
                                               dex_promise_get_cancellable (promise),
                                               dex_app_info_get_default_for_uri_scheme_cb,
                                               dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_app_info_launch_default_for_uri_cb (GObject      *object,
                                        GAsyncResult *result,
                                        gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  g_assert (object == NULL);

  if (!g_app_info_launch_default_for_uri_finish (result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_app_info_launch_default_for_uri:
 * @uri: a URI to launch
 * @context: (nullable): a [class@Gio.AppLaunchContext], or %NULL
 *
 * Wraps [func@Gio.AppInfo.launch_default_for_uri_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_app_info_launch_default_for_uri (const char        *uri,
                                     GAppLaunchContext *context)
{
  DexPromise *promise;

  dex_return_error_if_fail (uri != NULL);

  promise = dex_promise_new_cancellable ();

  g_app_info_launch_default_for_uri_async (uri,
                                           context,
                                           dex_promise_get_cancellable (promise),
                                           dex_app_info_launch_default_for_uri_cb,
                                           dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_async_initable_new_cb (GObject      *object,
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
 * dex_async_initable_new:
 * @object_type: a type implementing [iface@Gio.AsyncInitable]
 * @io_priority: priority for the IO operation
 * @first_property_name: (nullable): first property name, or %NULL
 * @...: property name and value pairs
 *
 * Creates and asynchronously initializes a new object.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to the
 *   newly initialized [class@GObject.Object] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_async_initable_new (GType       object_type,
                        int         io_priority,
                        const char *first_property_name,
                        ...)
{
  DexPromise *promise;
  GObject *object;
  va_list args;

  dex_return_error_if_fail (g_type_is_a (object_type, G_TYPE_ASYNC_INITABLE));

  va_start (args, first_property_name);
  object = g_object_new_valist (object_type, first_property_name, args);
  va_end (args);

  promise = dex_promise_new_cancellable ();

  g_async_initable_init_async (G_ASYNC_INITABLE (object),
                               io_priority,
                               dex_promise_get_cancellable (promise),
                               dex_async_initable_new_cb,
                               dex_ref (promise));

  g_object_unref (object);

  return DEX_FUTURE (promise);
}

static void
dex_file_new_tmp_dir_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GFile *file;

  g_assert (object == NULL);
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if (!(file = g_file_new_tmp_dir_finish (result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&file));

  dex_unref (promise);
}

/**
 * dex_file_new_tmp_dir:
 * @tmpl: (nullable): template for the directory name, or %NULL
 * @io_priority: priority for the IO operation
 *
 * Wraps [func@Gio.File.new_tmp_dir_async] as a [class@Dex.Future].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [iface@Gio.File] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_new_tmp_dir (const char *tmpl,
                      int         io_priority)
{
  DexPromise *promise;

  promise = dex_promise_new_cancellable ();

  g_file_new_tmp_dir_async (tmpl,
                            io_priority,
                            dex_promise_get_cancellable (promise),
                            dex_file_new_tmp_dir_cb,
                            dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_buffered_input_stream_fill_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  gssize len;

  len = g_buffered_input_stream_fill_finish (G_BUFFERED_INPUT_STREAM (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_int64 (promise, len);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_buffered_input_stream_fill:
 * @stream: a [class@Gio.BufferedInputStream]
 * @count: the number of bytes to fill, or -1
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.BufferedInputStream.fill_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to the number
 *   of bytes read as a `gint64`, or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_buffered_input_stream_fill (GBufferedInputStream *stream,
                                gssize                count,
                                int                   io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_BUFFERED_INPUT_STREAM (stream));

  promise = dex_promise_new_cancellable ();

  g_buffered_input_stream_fill_async (stream,
                                      count,
                                      io_priority,
                                      dex_promise_get_cancellable (promise),
                                      dex_buffered_input_stream_fill_cb,
                                      dex_ref (promise));

  return DEX_FUTURE (promise);
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

static void
dex_data_input_stream_read_line_utf8_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  char *line = NULL;

  line = g_data_input_stream_read_line_finish_utf8 (G_DATA_INPUT_STREAM (object),
                                                    result,
                                                    NULL,
                                                    &error);

  if (error == NULL)
    dex_promise_resolve_string (promise, g_steal_pointer (&line));
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

static void
dex_data_input_stream_read_upto_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  char *data = NULL;

  data = g_data_input_stream_read_upto_finish (G_DATA_INPUT_STREAM (object),
                                               result,
                                               NULL,
                                               &error);

  if (error == NULL)
    dex_promise_resolve_string (promise, g_steal_pointer (&data));
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

/**
 * dex_data_input_stream_read_line_utf8:
 * @stream: a [class@Gio.DataInputStream]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Reads a UTF-8 line from the data input stream.
 *
 * Wraps [method@Gio.DataInputStream.read_line_async] with
 * [method@Gio.DataInputStream.read_line_finish_utf8].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to a string containing the line (without the line terminator), or %NULL
 *   if the end of the stream is reached.
 *
 * Since: 1.2
 */
DexFuture *
dex_data_input_stream_read_line_utf8 (GDataInputStream *stream,
                                      int               io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_DATA_INPUT_STREAM (stream));

  promise = dex_promise_new_cancellable ();

  g_data_input_stream_read_line_async (stream,
                                       io_priority,
                                       dex_promise_get_cancellable (promise),
                                       dex_data_input_stream_read_line_utf8_cb,
                                       dex_ref (promise));

  return DEX_FUTURE (promise);
}

/**
 * dex_data_input_stream_read_upto:
 * @stream: a [class@Gio.DataInputStream]
 * @stop_chars: characters to terminate the read
 * @stop_chars_len: length of @stop_chars, or -1 if @stop_chars is nul-terminated
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Reads data from the stream until one of @stop_chars is found.
 *
 * Wraps [method@Gio.DataInputStream.read_upto_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to a string containing the read data, not including the stop character, or
 *   %NULL if the end of the stream is reached.
 *
 * Since: 1.2
 */
DexFuture *
dex_data_input_stream_read_upto (GDataInputStream *stream,
                                 const char       *stop_chars,
                                 gssize            stop_chars_len,
                                 int               io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_DATA_INPUT_STREAM (stream));
  dex_return_error_if_fail (stop_chars != NULL);

  promise = dex_promise_new_cancellable ();

  g_data_input_stream_read_upto_async (stream,
                                       stop_chars,
                                       stop_chars_len,
                                       io_priority,
                                       dex_promise_get_cancellable (promise),
                                       dex_data_input_stream_read_upto_cb,
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

typedef struct
{
  DexPromise *promise;
  GOutputVector *vectors;
} OutputStreamWritevAll;

static void
output_stream_writev_all_free (OutputStreamWritevAll *state)
{
  dex_clear (&state->promise);
  g_free (state->vectors);
  g_free (state);
}

static void
dex_output_stream_writev_all_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  OutputStreamWritevAll *state = user_data;
  GError *error = NULL;
  gsize bytes_written = 0;

  if (!g_output_stream_writev_all_finish (G_OUTPUT_STREAM (object),
                                          result,
                                          &bytes_written,
                                          &error))
    dex_promise_reject (state->promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_uint64 (state->promise, bytes_written);

  output_stream_writev_all_free (state);
}

/**
 * dex_output_stream_writev_all:
 * @stream: a [class@Gio.OutputStream]
 * @vectors: (array length=n_vectors): vectors to write to @stream
 * @n_vectors: the number of vectors to write
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Writes all bytes in @vectors to @stream.
 *
 * This function copies the @vectors array before starting the operation, but
 * the buffers referenced by the vectors must remain valid until the returned
 * future completes.
 *
 * Wraps [method@Gio.OutputStream.writev_all_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a `guint64`
 *   containing the number of bytes written, or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_output_stream_writev_all (GOutputStream       *stream,
                              const GOutputVector *vectors,
                              gsize                n_vectors,
                              int                  io_priority)
{
  OutputStreamWritevAll *state;
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_OUTPUT_STREAM (stream));
  dex_return_error_if_fail (vectors != NULL || n_vectors == 0);

  promise = dex_promise_new_cancellable ();

  state = g_new0 (OutputStreamWritevAll, 1);
  state->promise = dex_ref (promise);
  state->vectors = g_memdup2 (vectors, sizeof *vectors * n_vectors);

  g_output_stream_writev_all_async (stream,
                                    state->vectors,
                                    n_vectors,
                                    io_priority,
                                    dex_promise_get_cancellable (promise),
                                    dex_output_stream_writev_all_cb,
                                    state);

  return DEX_FUTURE (promise);
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
dex_file_append_to_cb (GObject      *object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  DexPromise *promise = user_data;
  GFileOutputStream *stream;
  GError *error = NULL;

  if (!(stream = g_file_append_to_finish (G_FILE (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&stream));

  dex_unref (promise);
}

/**
 * dex_file_append_to:
 * @file: a [iface@Gio.File]
 * @flags: flags for appending to the file
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.File.append_to_async] as a [class@Dex.Future].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.FileOutputStream] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_append_to (GFile            *file,
                    GFileCreateFlags  flags,
                    int               io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_append_to_async (file,
                          flags,
                          io_priority,
                          dex_promise_get_cancellable (promise),
                          dex_file_append_to_cb,
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
dex_file_open_readwrite_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  DexPromise *promise = user_data;
  GFileIOStream *stream;
  GError *error = NULL;

  if (!(stream = g_file_open_readwrite_finish (G_FILE (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&stream));

  dex_unref (promise);
}

/**
 * dex_file_open_readwrite:
 * @file: a [iface@Gio.File]
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.File.open_readwrite_async] as a [class@Dex.Future].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.FileIOStream] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_open_readwrite (GFile *file,
                         int    io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_open_readwrite_async (file,
                               io_priority,
                               dex_promise_get_cancellable (promise),
                               dex_file_open_readwrite_cb,
                               dex_ref (promise));
  return DEX_FUTURE (promise);
}

static void
dex_file_create_readwrite_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  DexPromise *promise = user_data;
  GFileIOStream *stream;
  GError *error = NULL;

  if (!(stream = g_file_create_readwrite_finish (G_FILE (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&stream));

  dex_unref (promise);
}

/**
 * dex_file_create_readwrite:
 * @file: a [iface@Gio.File]
 * @flags: flags for creating the file
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.File.create_readwrite_async] as a [class@Dex.Future].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.FileIOStream] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_create_readwrite (GFile            *file,
                           GFileCreateFlags  flags,
                           int               io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_create_readwrite_async (file,
                                 flags,
                                 io_priority,
                                 dex_promise_get_cancellable (promise),
                                 dex_file_create_readwrite_cb,
                                 dex_ref (promise));
  return DEX_FUTURE (promise);
}

static void
dex_file_replace_readwrite_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  DexPromise *promise = user_data;
  GFileIOStream *stream;
  GError *error = NULL;

  if (!(stream = g_file_replace_readwrite_finish (G_FILE (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&stream));

  dex_unref (promise);
}

/**
 * dex_file_replace_readwrite:
 * @file: a [iface@Gio.File]
 * @etag: (nullable): the etag or %NULL
 * @make_backup: if a backup file should be created
 * @flags: flags for replacing the file
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.File.replace_readwrite_async] as a [class@Dex.Future].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.FileIOStream] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_replace_readwrite (GFile            *file,
                            const char       *etag,
                            gboolean          make_backup,
                            GFileCreateFlags  flags,
                            int               io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_replace_readwrite_async (file,
                                  etag,
                                  make_backup,
                                  flags,
                                  io_priority,
                                  dex_promise_get_cancellable (promise),
                                  dex_file_replace_readwrite_cb,
                                  dex_ref (promise));
  return DEX_FUTURE (promise);
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

typedef struct
{
  DexPromise *promise;
  GBytes *bytes;
} FileReplaceContents;

static void
dex_file_replace_contents_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  FileReplaceContents *state = user_data;
  GError *error = NULL;
  char *new_etag = NULL;

  g_assert (G_IS_FILE (object));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (state->promise));

  if (!g_file_replace_contents_finish (G_FILE (object), result, &new_etag, &error))
    dex_promise_reject (state->promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_string (state->promise, g_steal_pointer (&new_etag));

  dex_clear (&state->promise);
  g_clear_pointer (&state->bytes, g_bytes_unref);
  g_free (state);
}

/**
 * dex_file_replace_contents:
 * @file: a [iface@Gio.File]
 * @contents: (array length=length) (element-type guint8): the contents to write
 * @length: the length of @contents
 * @etag: (nullable): the etag or %NULL
 * @make_backup: if a backup file should be created
 * @flags: a set of [flags@Gio.FileCreateFlags]
 *
 * Wraps [method@Gio.File.replace_contents_async].
 *
 * This function copies @contents into a [struct@GLib.Bytes] so the caller does
 * not need to keep the buffer alive for the duration of the operation.
 *
 * Returns: (transfer full): a [class@Dex.Future] which resolves to the new
 *   etag, or %NULL if no etag is available.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_replace_contents (GFile            *file,
                           const char       *contents,
                           gsize             length,
                           const char       *etag,
                           gboolean          make_backup,
                           GFileCreateFlags  flags)
{
  FileReplaceContents *state;
  DexPromise *promise;
  gconstpointer data;
  gsize size;

  dex_return_error_if_fail (G_IS_FILE (file));
  dex_return_error_if_fail (contents != NULL || length == 0);

  promise = dex_promise_new_cancellable ();

  state = g_new0 (FileReplaceContents, 1);
  state->promise = dex_ref (promise);
  state->bytes = g_bytes_new (contents, length);

  data = g_bytes_get_data (state->bytes, &size);

  g_file_replace_contents_async (file,
                                 data,
                                 size,
                                 etag,
                                 make_backup,
                                 flags,
                                 dex_promise_get_cancellable (promise),
                                 dex_file_replace_contents_cb,
                                 state);

  return DEX_FUTURE (promise);
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
 * @count: (in): size in bytes to read from the stream into the @buffer
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
dex_output_stream_flush_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_output_stream_flush_finish (G_OUTPUT_STREAM (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_output_stream_flush:
 * @self: a [class@Gio.OutputStream]
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.OutputStream.flush_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_output_stream_flush (GOutputStream *self,
                         int            io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_OUTPUT_STREAM (self));

  promise = dex_promise_new_cancellable ();

  g_output_stream_flush_async (self,
                               io_priority,
                               dex_promise_get_cancellable (promise),
                               dex_output_stream_flush_cb,
                               dex_ref (promise));

  return DEX_FUTURE (promise);
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
dex_file_query_filesystem_info_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  DexPromise *promise = user_data;
  GFileInfo *info;
  GError *error = NULL;

  if (!(info = g_file_query_filesystem_info_finish (G_FILE (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&info));

  dex_unref (promise);
}

/**
 * dex_file_query_filesystem_info:
 * @file: a [iface@Gio.File]
 * @attributes: an attribute query string
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.File.query_filesystem_info_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.FileInfo] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_query_filesystem_info (GFile      *file,
                                const char *attributes,
                                int         io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_query_filesystem_info_async (file,
                                      attributes,
                                      io_priority,
                                      dex_promise_get_cancellable (promise),
                                      dex_file_query_filesystem_info_cb,
                                      dex_ref (promise));
  return DEX_FUTURE (promise);
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
dex_file_query_default_handler_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  DexPromise *promise = user_data;
  GAppInfo *app_info;
  GError *error = NULL;

  if (!(app_info = g_file_query_default_handler_finish (G_FILE (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&app_info));

  dex_unref (promise);
}

/**
 * dex_file_query_default_handler:
 * @file: a [iface@Gio.File]
 * @io_priority: priority for the IO operation
 *
 * Wraps [func@Gio.File.query_default_handler_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [iface@Gio.AppInfo] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_query_default_handler (GFile *file,
                                int    io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_query_default_handler_async (file,
                                      io_priority,
                                      dex_promise_get_cancellable (promise),
                                      dex_file_query_default_handler_cb,
                                      dex_ref (promise));
  return DEX_FUTURE (promise);
}

static void
dex_file_find_enclosing_mount_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  DexPromise *promise = user_data;
  GMount *mount;
  GError *error = NULL;

  if (!(mount = g_file_find_enclosing_mount_finish (G_FILE (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&mount));

  dex_unref (promise);
}

/**
 * dex_file_find_enclosing_mount:
 * @file: a [iface@Gio.File]
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.File.find_enclosing_mount_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [iface@Gio.Mount] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_find_enclosing_mount (GFile *file,
                               int    io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_find_enclosing_mount_async (file,
                                     io_priority,
                                     dex_promise_get_cancellable (promise),
                                     dex_file_find_enclosing_mount_cb,
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

static void
dex_file_make_symbolic_link_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_file_make_symbolic_link_finish (G_FILE (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_file_make_symbolic_link:
 * @file: a [iface@Gio.File]
 * @symlink_value: the path for the target of the symbolic link
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.File.make_symbolic_link_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_make_symbolic_link (GFile      *file,
                             const char *symlink_value,
                             int         io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));
  dex_return_error_if_fail (symlink_value != NULL);

  promise = dex_promise_new_cancellable ();
  g_file_make_symbolic_link_async (file,
                                   symlink_value,
                                   io_priority,
                                   dex_promise_get_cancellable (promise),
                                   dex_file_make_symbolic_link_cb,
                                   dex_ref (promise));
  return DEX_FUTURE (promise);
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
dex_file_enumerator_close_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_file_enumerator_close_finish (G_FILE_ENUMERATOR (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_file_enumerator_close:
 * @file_enumerator: a [class@Gio.FileEnumerator]
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.FileEnumerator.close_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_enumerator_close (GFileEnumerator *file_enumerator,
                           int              io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE_ENUMERATOR (file_enumerator));

  promise = dex_promise_new_cancellable ();

  g_file_enumerator_close_async (file_enumerator,
                                 io_priority,
                                 dex_promise_get_cancellable (promise),
                                 dex_file_enumerator_close_cb,
                                 dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_file_input_stream_query_info_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GFileInfo *info;

  if (!(info = g_file_input_stream_query_info_finish (G_FILE_INPUT_STREAM (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&info));

  dex_unref (promise);
}

/**
 * dex_file_input_stream_query_info:
 * @stream: a [class@Gio.FileInputStream]
 * @attributes: file attributes to query
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.FileInputStream.query_info_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.FileInfo] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_input_stream_query_info (GFileInputStream *stream,
                                  const char       *attributes,
                                  int               io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE_INPUT_STREAM (stream));
  dex_return_error_if_fail (attributes != NULL);

  promise = dex_promise_new_cancellable ();

  g_file_input_stream_query_info_async (stream,
                                        attributes,
                                        io_priority,
                                        dex_promise_get_cancellable (promise),
                                        dex_file_input_stream_query_info_cb,
                                        dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_file_io_stream_query_info_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GFileInfo *info;

  if (!(info = g_file_io_stream_query_info_finish (G_FILE_IO_STREAM (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&info));

  dex_unref (promise);
}

/**
 * dex_file_io_stream_query_info:
 * @stream: a [class@Gio.FileIOStream]
 * @attributes: file attributes to query
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.FileIOStream.query_info_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.FileInfo] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_io_stream_query_info (GFileIOStream *stream,
                               const char   *attributes,
                               int           io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE_IO_STREAM (stream));
  dex_return_error_if_fail (attributes != NULL);

  promise = dex_promise_new_cancellable ();

  g_file_io_stream_query_info_async (stream,
                                     attributes,
                                     io_priority,
                                     dex_promise_get_cancellable (promise),
                                     dex_file_io_stream_query_info_cb,
                                     dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_file_output_stream_query_info_cb (GObject      *object,
                                      GAsyncResult *result,
                                      gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GFileInfo *info;

  if (!(info = g_file_output_stream_query_info_finish (G_FILE_OUTPUT_STREAM (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&info));

  dex_unref (promise);
}

/**
 * dex_file_output_stream_query_info:
 * @stream: a [class@Gio.FileOutputStream]
 * @attributes: file attributes to query
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.FileOutputStream.query_info_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.FileInfo] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_output_stream_query_info (GFileOutputStream *stream,
                                   const char        *attributes,
                                   int                io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE_OUTPUT_STREAM (stream));
  dex_return_error_if_fail (attributes != NULL);

  promise = dex_promise_new_cancellable ();

  g_file_output_stream_query_info_async (stream,
                                         attributes,
                                         io_priority,
                                         dex_promise_get_cancellable (promise),
                                         dex_file_output_stream_query_info_cb,
                                         dex_ref (promise));

  return DEX_FUTURE (promise);
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

typedef struct
{
  GDestroyNotify notify;
  gpointer data;
  DexPromise *promise;
} FileCopy;

static void
dex_file_copy_with_progress_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  FileCopy *state = user_data;
  GError *error = NULL;

  if (!g_file_copy_finish (G_FILE (object), result, &error))
    dex_promise_reject (state->promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (state->promise, TRUE);

  if (state->notify != NULL)
    state->notify (state->data);
  dex_clear (&state->promise);
  g_free (state);
}

/**
 * dex_file_copy_with_progress:
 * @source: source [iface@Gio.File]
 * @destination: destination [iface@Gio.File]
 * @flags: a set of [flags@Gio.FileCopyFlags]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 * @progress_callback: (nullable) (scope notified) (closure progress_callback_data):
 *   [callback@Gio.FileProgressCallback] function for updates
 * @progress_callback_data: `gpointer` to user data for the callback function
 * @progress_callback_data_destroy: (nullable): a function to destroy the
 *   @progress_callback_data, or %NULL
 *
 * Wraps [method@Gio.File.copy_async] with progress callback support.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error
 *
 * Since: 1.2
 */
DexFuture *
dex_file_copy_with_progress (GFile                 *source,
                             GFile                 *destination,
                             GFileCopyFlags         flags,
                             int                    io_priority,
                             GFileProgressCallback  progress_callback,
                             gpointer               progress_callback_data,
                             GDestroyNotify         progress_callback_data_destroy)
{
  FileCopy *state;
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (source));
  dex_return_error_if_fail (G_IS_FILE (destination));

  promise = dex_promise_new_cancellable ();

  state = g_new0 (FileCopy, 1);
  state->data = progress_callback_data;
  state->notify = progress_callback_data_destroy;
  state->promise = dex_ref (promise);

  g_file_copy_async (source,
                     destination,
                     flags,
                     io_priority,
                     dex_promise_get_cancellable (promise),
                     progress_callback,
                     progress_callback_data,
                     dex_file_copy_with_progress_cb,
                     state);

  return DEX_FUTURE (promise);
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
dex_file_trash_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_file_trash_finish (G_FILE (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_file_trash:
 * @file: a [iface@Gio.File]
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.File.trash_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_trash (GFile *file,
                int    io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_trash_async (file,
                      io_priority,
                      dex_promise_get_cancellable (promise),
                      dex_file_trash_cb,
                      dex_ref (promise));
  return DEX_FUTURE (promise);
}

/**
 * dex_socket_wait:
 * @socket: a [class@Gio.Socket]
 * @condition: a [flags@GLib.IOCondition] to wait for
 *
 * Creates a [class@Dex.Future] that resolves when @socket satisfies
 * @condition.
 *
 * The future resolves to the [flags@GLib.IOCondition] reported by the socket
 * source. This may include additional conditions such as %G_IO_HUP or
 * %G_IO_ERR.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [flags@GLib.IOCondition].
 *
 * Since: 1.2
 */
DexFuture *
dex_socket_wait (GSocket      *socket,
                 GIOCondition  condition)
{
  dex_return_error_if_fail (G_IS_SOCKET (socket));
  dex_return_error_if_fail (condition != 0);

  return dex_socket_wait_new (socket, condition);
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
dex_socket_client_connect_to_host_cb (GObject      *object,
                                      GAsyncResult *result,
                                      gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GSocketConnection *conn;

  if (!(conn = g_socket_client_connect_to_host_finish (G_SOCKET_CLIENT (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&conn));

  dex_unref (promise);
}

/**
 * dex_socket_client_connect_to_host:
 * @socket_client: a [class@Gio.SocketClient]
 * @host_and_port: host and optional port to connect to
 * @default_port: default port to use
 *
 * Wraps [method@Gio.SocketClient.connect_to_host_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.SocketConnection] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_socket_client_connect_to_host (GSocketClient *socket_client,
                                   const char    *host_and_port,
                                   guint16        default_port)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_SOCKET_CLIENT (socket_client));
  dex_return_error_if_fail (host_and_port != NULL);

  promise = dex_promise_new_cancellable ();

  g_socket_client_connect_to_host_async (socket_client,
                                         host_and_port,
                                         default_port,
                                         dex_promise_get_cancellable (promise),
                                         dex_socket_client_connect_to_host_cb,
                                         dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_socket_client_connect_to_service_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GSocketConnection *conn;

  if (!(conn = g_socket_client_connect_to_service_finish (G_SOCKET_CLIENT (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&conn));

  dex_unref (promise);
}

/**
 * dex_socket_client_connect_to_service:
 * @socket_client: a [class@Gio.SocketClient]
 * @domain: domain to connect to
 * @service: service to connect to
 *
 * Wraps [method@Gio.SocketClient.connect_to_service_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.SocketConnection] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_socket_client_connect_to_service (GSocketClient *socket_client,
                                      const char    *domain,
                                      const char    *service)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_SOCKET_CLIENT (socket_client));
  dex_return_error_if_fail (domain != NULL);
  dex_return_error_if_fail (service != NULL);

  promise = dex_promise_new_cancellable ();

  g_socket_client_connect_to_service_async (socket_client,
                                            domain,
                                            service,
                                            dex_promise_get_cancellable (promise),
                                            dex_socket_client_connect_to_service_cb,
                                            dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_socket_client_connect_to_uri_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GSocketConnection *conn;

  if (!(conn = g_socket_client_connect_to_uri_finish (G_SOCKET_CLIENT (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&conn));

  dex_unref (promise);
}

/**
 * dex_socket_client_connect_to_uri:
 * @socket_client: a [class@Gio.SocketClient]
 * @uri: URI to connect to
 * @default_port: default port to use
 *
 * Wraps [method@Gio.SocketClient.connect_to_uri_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.SocketConnection] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_socket_client_connect_to_uri (GSocketClient *socket_client,
                                  const char    *uri,
                                  guint16        default_port)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_SOCKET_CLIENT (socket_client));
  dex_return_error_if_fail (uri != NULL);

  promise = dex_promise_new_cancellable ();

  g_socket_client_connect_to_uri_async (socket_client,
                                        uri,
                                        default_port,
                                        dex_promise_get_cancellable (promise),
                                        dex_socket_client_connect_to_uri_cb,
                                        dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_socket_connection_connect_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_socket_connection_connect_finish (G_SOCKET_CONNECTION (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_socket_connection_connect:
 * @connection: a [class@Gio.SocketConnection]
 * @address: a [class@Gio.SocketAddress]
 *
 * Wraps [method@Gio.SocketConnection.connect_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_socket_connection_connect (GSocketConnection *connection,
                               GSocketAddress    *address)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_SOCKET_CONNECTION (connection));
  dex_return_error_if_fail (G_IS_SOCKET_ADDRESS (address));

  promise = dex_promise_new_cancellable ();

  g_socket_connection_connect_async (connection,
                                     address,
                                     dex_promise_get_cancellable (promise),
                                     dex_socket_connection_connect_cb,
                                     dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_socket_address_enumerator_next_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GSocketAddress *address;

  if (!(address = g_socket_address_enumerator_next_finish (G_SOCKET_ADDRESS_ENUMERATOR (object), result, &error)))
    {
      if (error != NULL)
        dex_promise_reject (promise, g_steal_pointer (&error));
      else
        dex_promise_resolve_object (promise, NULL);
    }
  else
    {
      dex_promise_resolve_object (promise, g_steal_pointer (&address));
    }

  dex_unref (promise);
}

/**
 * dex_socket_address_enumerator_next:
 * @enumerator: a [class@Gio.SocketAddressEnumerator]
 *
 * Wraps [method@Gio.SocketAddressEnumerator.next_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.SocketAddress], %NULL at the end of the enumeration, or
 *   rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_socket_address_enumerator_next (GSocketAddressEnumerator *enumerator)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_SOCKET_ADDRESS_ENUMERATOR (enumerator));

  promise = dex_promise_new_cancellable ();

  g_socket_address_enumerator_next_async (enumerator,
                                          dex_promise_get_cancellable (promise),
                                          dex_socket_address_enumerator_next_cb,
                                          dex_ref (promise));

  return DEX_FUTURE (promise);
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
dex_io_stream_splice_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  g_assert (object == NULL);

  if (!g_io_stream_splice_finish (result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_io_stream_splice:
 * @stream1: a [class@Gio.IOStream]
 * @stream2: a [class@Gio.IOStream]
 * @flags: splice flags
 * @io_priority: priority for the IO operation
 *
 * Wraps [func@Gio.IOStream.splice_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_io_stream_splice (GIOStream           *stream1,
                      GIOStream           *stream2,
                      GIOStreamSpliceFlags flags,
                      int                  io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_IO_STREAM (stream1));
  dex_return_error_if_fail (G_IS_IO_STREAM (stream2));

  promise = dex_promise_new_cancellable ();

  g_io_stream_splice_async (stream1,
                            stream2,
                            flags,
                            io_priority,
                            dex_promise_get_cancellable (promise),
                            dex_io_stream_splice_cb,
                            dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_tls_connection_handshake_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  g_assert (G_IS_TLS_CONNECTION (object));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if (g_tls_connection_handshake_finish (G_TLS_CONNECTION (object), result, &error))
    dex_promise_resolve_boolean (promise, TRUE);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_tls_connection_handshake:
 * @tls_connection: a [class@Gio.TlsConnection]
 * @io_priority: the [IO priority][iface@Gio.AsyncResult#io-priority] of the
 *   request
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to
 *   true or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_tls_connection_handshake (GTlsConnection *tls_connection,
                              int             io_priority)
{
  DexPromise *promise;

  g_return_val_if_fail (G_IS_TLS_CONNECTION (tls_connection), NULL);

  promise = dex_promise_new_cancellable ();

  g_tls_connection_handshake_async (tls_connection,
                                    io_priority,
                                    dex_promise_get_cancellable (promise),
                                    dex_tls_connection_handshake_cb,
                                    dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_dtls_connection_handshake_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_dtls_connection_handshake_finish (G_DTLS_CONNECTION (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_dtls_connection_handshake:
 * @dtls_connection: a [iface@Gio.DtlsConnection]
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.DtlsConnection.handshake_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_dtls_connection_handshake (GDtlsConnection *dtls_connection,
                               int              io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_DTLS_CONNECTION (dtls_connection));

  promise = dex_promise_new_cancellable ();

  g_dtls_connection_handshake_async (dtls_connection,
                                     io_priority,
                                     dex_promise_get_cancellable (promise),
                                     dex_dtls_connection_handshake_cb,
                                     dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_dtls_connection_shutdown_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_dtls_connection_shutdown_finish (G_DTLS_CONNECTION (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_dtls_connection_shutdown:
 * @dtls_connection: a [iface@Gio.DtlsConnection]
 * @shutdown_read: whether to shut down the read side
 * @shutdown_write: whether to shut down the write side
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.DtlsConnection.shutdown_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_dtls_connection_shutdown (GDtlsConnection *dtls_connection,
                              gboolean         shutdown_read,
                              gboolean         shutdown_write,
                              int              io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_DTLS_CONNECTION (dtls_connection));

  promise = dex_promise_new_cancellable ();

  g_dtls_connection_shutdown_async (dtls_connection,
                                    shutdown_read,
                                    shutdown_write,
                                    io_priority,
                                    dex_promise_get_cancellable (promise),
                                    dex_dtls_connection_shutdown_cb,
                                    dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_dtls_connection_close_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_dtls_connection_close_finish (G_DTLS_CONNECTION (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_dtls_connection_close:
 * @dtls_connection: a [iface@Gio.DtlsConnection]
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.DtlsConnection.close_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_dtls_connection_close (GDtlsConnection *dtls_connection,
                           int              io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_DTLS_CONNECTION (dtls_connection));

  promise = dex_promise_new_cancellable ();

  g_dtls_connection_close_async (dtls_connection,
                                 io_priority,
                                 dex_promise_get_cancellable (promise),
                                 dex_dtls_connection_close_cb,
                                 dex_ref (promise));

  return DEX_FUTURE (promise);
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
dex_resolver_lookup_by_name_with_flags_cb (GObject      *object,
                                           GAsyncResult *result,
                                           gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GList *list;

  list = g_resolver_lookup_by_name_with_flags_finish (G_RESOLVER (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_boxed (promise, DEX_TYPE_INET_ADDRESS_LIST, list);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_resolver_lookup_by_name_with_flags:
 * @resolver: a [class@Gio.Resolver]
 * @address: hostname to look up
 * @flags: lookup flags
 *
 * Wraps [method@Gio.Resolver.lookup_by_name_with_flags_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [struct@GLib.List] of [class@Gio.InetAddress] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_resolver_lookup_by_name_with_flags (GResolver                *resolver,
                                        const char               *address,
                                        GResolverNameLookupFlags  flags)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_RESOLVER (resolver));
  dex_return_error_if_fail (address != NULL);

  promise = dex_promise_new_cancellable ();

  g_resolver_lookup_by_name_with_flags_async (resolver,
                                              address,
                                              flags,
                                              dex_promise_get_cancellable (promise),
                                              dex_resolver_lookup_by_name_with_flags_cb,
                                              dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_resolver_lookup_by_address_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  char *name;

  if (!(name = g_resolver_lookup_by_address_finish (G_RESOLVER (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_string (promise, g_steal_pointer (&name));

  dex_unref (promise);
}

/**
 * dex_resolver_lookup_by_address:
 * @resolver: a [class@Gio.Resolver]
 * @address: a [class@Gio.InetAddress]
 *
 * Wraps [method@Gio.Resolver.lookup_by_address_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a hostname
 *   string or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_resolver_lookup_by_address (GResolver    *resolver,
                                GInetAddress *address)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_RESOLVER (resolver));
  dex_return_error_if_fail (G_IS_INET_ADDRESS (address));

  promise = dex_promise_new_cancellable ();

  g_resolver_lookup_by_address_async (resolver,
                                      address,
                                      dex_promise_get_cancellable (promise),
                                      dex_resolver_lookup_by_address_cb,
                                      dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_resolver_lookup_service_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GList *list;

  list = g_resolver_lookup_service_finish (G_RESOLVER (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_boxed (promise, DEX_TYPE_SRV_TARGET_LIST, list);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_resolver_lookup_service:
 * @resolver: a [class@Gio.Resolver]
 * @service: service name
 * @protocol: protocol name
 * @domain: domain name
 *
 * Wraps [method@Gio.Resolver.lookup_service_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [struct@GLib.List] of [class@Gio.SrvTarget] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_resolver_lookup_service (GResolver  *resolver,
                             const char *service,
                             const char *protocol,
                             const char *domain)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_RESOLVER (resolver));
  dex_return_error_if_fail (service != NULL);
  dex_return_error_if_fail (protocol != NULL);
  dex_return_error_if_fail (domain != NULL);

  promise = dex_promise_new_cancellable ();

  g_resolver_lookup_service_async (resolver,
                                   service,
                                   protocol,
                                   domain,
                                   dex_promise_get_cancellable (promise),
                                   dex_resolver_lookup_service_cb,
                                   dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_resolver_lookup_records_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GList *list;

  list = g_resolver_lookup_records_finish (G_RESOLVER (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_boxed (promise, DEX_TYPE_VARIANT_LIST, list);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_resolver_lookup_records:
 * @resolver: a [class@Gio.Resolver]
 * @rrname: DNS record name
 * @record_type: DNS record type
 *
 * Wraps [method@Gio.Resolver.lookup_records_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [struct@GLib.List] of [struct@GLib.Variant] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_resolver_lookup_records (GResolver           *resolver,
                             const char          *rrname,
                             GResolverRecordType  record_type)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_RESOLVER (resolver));
  dex_return_error_if_fail (rrname != NULL);

  promise = dex_promise_new_cancellable ();

  g_resolver_lookup_records_async (resolver,
                                   rrname,
                                   record_type,
                                   dex_promise_get_cancellable (promise),
                                   dex_resolver_lookup_records_cb,
                                   dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_network_monitor_can_reach_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_network_monitor_can_reach_finish (G_NETWORK_MONITOR (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_network_monitor_can_reach:
 * @monitor: a [iface@Gio.NetworkMonitor]
 * @connectable: a [iface@Gio.SocketConnectable]
 *
 * Wraps [method@Gio.NetworkMonitor.can_reach_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_network_monitor_can_reach (GNetworkMonitor    *monitor,
                               GSocketConnectable *connectable)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_NETWORK_MONITOR (monitor));
  dex_return_error_if_fail (G_IS_SOCKET_CONNECTABLE (connectable));

  promise = dex_promise_new_cancellable ();

  g_network_monitor_can_reach_async (monitor,
                                     connectable,
                                     dex_promise_get_cancellable (promise),
                                     dex_network_monitor_can_reach_cb,
                                     dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_proxy_connect_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GIOStream *stream;

  if (!(stream = g_proxy_connect_finish (G_PROXY (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&stream));

  dex_unref (promise);
}

/**
 * dex_proxy_connect:
 * @proxy: a [iface@Gio.Proxy]
 * @connection: a [class@Gio.IOStream]
 * @proxy_address: a [class@Gio.ProxyAddress]
 *
 * Wraps [method@Gio.Proxy.connect_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.IOStream] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_proxy_connect (GProxy        *proxy,
                   GIOStream     *connection,
                   GProxyAddress *proxy_address)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_PROXY (proxy));
  dex_return_error_if_fail (G_IS_IO_STREAM (connection));
  dex_return_error_if_fail (G_IS_PROXY_ADDRESS (proxy_address));

  promise = dex_promise_new_cancellable ();

  g_proxy_connect_async (proxy,
                         connection,
                         proxy_address,
                         dex_promise_get_cancellable (promise),
                         dex_proxy_connect_cb,
                         dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_proxy_resolver_lookup_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  char **proxies;

  if (!(proxies = g_proxy_resolver_lookup_finish (G_PROXY_RESOLVER (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boxed (promise, G_TYPE_STRV, g_steal_pointer (&proxies));

  dex_unref (promise);
}

/**
 * dex_proxy_resolver_lookup:
 * @resolver: a [iface@Gio.ProxyResolver]
 * @uri: a URI
 *
 * Wraps [method@Gio.ProxyResolver.lookup_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   string vector of proxy URIs or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_proxy_resolver_lookup (GProxyResolver *resolver,
                           const char     *uri)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_PROXY_RESOLVER (resolver));
  dex_return_error_if_fail (uri != NULL);

  promise = dex_promise_new_cancellable ();

  g_proxy_resolver_lookup_async (resolver,
                                 uri,
                                 dex_promise_get_cancellable (promise),
                                 dex_proxy_resolver_lookup_cb,
                                 dex_ref (promise));

  return DEX_FUTURE (promise);
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

typedef struct
{
  GFileReadMoreCallback callback;
  GDestroyNotify notify;
  gpointer data;
  DexPromise *promise;
} FileLoadPartialContents;

static gboolean
dex_file_load_partial_contents_read_more_cb (const char *file_contents,
                                             goffset     file_size,
                                             gpointer    callback_data)
{
  FileLoadPartialContents *state = callback_data;

  g_assert (state != NULL);

  if (state->callback == NULL)
    return FALSE;

  return state->callback (file_contents, file_size, state->data);
}

static void
dex_file_load_partial_contents_free (FileLoadPartialContents *state)
{
  if (state->notify != NULL)
    state->notify (state->data);

  dex_clear (&state->promise);
  g_free (state);
}

static void
dex_file_load_partial_contents_bytes_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  FileLoadPartialContents *state = user_data;
  GError *error = NULL;
  char *contents = NULL;
  gsize len = 0;

  if (!g_file_load_partial_contents_finish (G_FILE (object), result, &contents, &len, NULL, &error))
    dex_promise_reject (state->promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boxed (state->promise, G_TYPE_BYTES, g_bytes_new_take (contents, len));

  dex_file_load_partial_contents_free (state);
}

/**
 * dex_file_load_partial_contents_bytes:
 * @file: a [iface@Gio.File]
 * @read_more_callback: (nullable) (scope notified) (closure read_more_callback_data):
 *   callback to determine if more bytes should be loaded
 * @read_more_callback_data: user data for @read_more_callback
 * @read_more_callback_data_destroy: (nullable): destroys @read_more_callback_data
 *
 * Wraps [method@Gio.File.load_partial_contents_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [struct@GLib.Bytes] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_load_partial_contents_bytes (GFile                 *file,
                                      GFileReadMoreCallback  read_more_callback,
                                      gpointer               read_more_callback_data,
                                      GDestroyNotify         read_more_callback_data_destroy)
{
  FileLoadPartialContents *state;
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();

  state = g_new0 (FileLoadPartialContents, 1);
  state->callback = read_more_callback;
  state->notify = read_more_callback_data_destroy;
  state->data = read_more_callback_data;
  state->promise = dex_ref (promise);

  g_file_load_partial_contents_async (file,
                                      dex_promise_get_cancellable (promise),
                                      dex_file_load_partial_contents_read_more_cb,
                                      dex_file_load_partial_contents_bytes_cb,
                                      state);

  return DEX_FUTURE (promise);
}

static void
dex_file_load_bytes_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GBytes *bytes;

  if (!(bytes = g_file_load_bytes_finish (G_FILE (object), result, NULL, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boxed (promise, G_TYPE_BYTES, g_steal_pointer (&bytes));

  dex_unref (promise);
}

/**
 * dex_file_load_bytes:
 * @file: a [iface@Gio.File]
 *
 * Wraps [method@Gio.File.load_bytes_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [struct@GLib.Bytes] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_load_bytes (GFile *file)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));

  promise = dex_promise_new_cancellable ();
  g_file_load_bytes_async (file,
                           dex_promise_get_cancellable (promise),
                           dex_file_load_bytes_cb,
                           dex_ref (promise));
  return DEX_FUTURE (promise);
}

static void
dex_subprocess_wait_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_subprocess_wait_finish (G_SUBPROCESS (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_subprocess_wait:
 * @subprocess: a [class@Gio.Subprocess]
 *
 * Wraps [method@Gio.Subprocess.wait_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_subprocess_wait (GSubprocess *subprocess)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_SUBPROCESS (subprocess));

  promise = dex_promise_new_cancellable ();

  g_subprocess_wait_async (subprocess,
                           dex_promise_get_cancellable (promise),
                           dex_subprocess_wait_cb,
                           dex_ref (promise));

  return DEX_FUTURE (promise);
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
dex_tls_database_verify_chain_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GTlsCertificateFlags flags;

  flags = g_tls_database_verify_chain_finish (G_TLS_DATABASE (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_uint (promise, flags);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_tls_database_verify_chain:
 * @database: a [class@Gio.TlsDatabase]
 * @chain: a [class@Gio.TlsCertificate]
 * @purpose: certificate purpose
 * @identity: (nullable): expected peer identity, or %NULL
 * @interaction: (nullable): a [class@Gio.TlsInteraction], or %NULL
 * @flags: verification flags
 *
 * Wraps [method@Gio.TlsDatabase.verify_chain_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to
 *   [flags@Gio.TlsCertificateFlags] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_tls_database_verify_chain (GTlsDatabase            *database,
                               GTlsCertificate         *chain,
                               const char              *purpose,
                               GSocketConnectable      *identity,
                               GTlsInteraction         *interaction,
                               GTlsDatabaseVerifyFlags  flags)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_TLS_DATABASE (database));
  dex_return_error_if_fail (G_IS_TLS_CERTIFICATE (chain));
  dex_return_error_if_fail (purpose != NULL);
  dex_return_error_if_fail (identity == NULL || G_IS_SOCKET_CONNECTABLE (identity));
  dex_return_error_if_fail (interaction == NULL || G_IS_TLS_INTERACTION (interaction));

  promise = dex_promise_new_cancellable ();

  g_tls_database_verify_chain_async (database,
                                     chain,
                                     purpose,
                                     identity,
                                     interaction,
                                     flags,
                                     dex_promise_get_cancellable (promise),
                                     dex_tls_database_verify_chain_cb,
                                     dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_tls_database_lookup_certificate_for_handle_cb (GObject      *object,
                                                  GAsyncResult *result,
                                                  gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GTlsCertificate *certificate;

  certificate = g_tls_database_lookup_certificate_for_handle_finish (G_TLS_DATABASE (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_object (promise, g_steal_pointer (&certificate));
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_tls_database_lookup_certificate_for_handle:
 * @database: a [class@Gio.TlsDatabase]
 * @handle: certificate handle
 * @interaction: (nullable): a [class@Gio.TlsInteraction], or %NULL
 * @flags: lookup flags
 *
 * Wraps [method@Gio.TlsDatabase.lookup_certificate_for_handle_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.TlsCertificate] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_tls_database_lookup_certificate_for_handle (GTlsDatabase           *database,
                                                const char             *handle,
                                                GTlsInteraction        *interaction,
                                                GTlsDatabaseLookupFlags flags)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_TLS_DATABASE (database));
  dex_return_error_if_fail (handle != NULL);
  dex_return_error_if_fail (interaction == NULL || G_IS_TLS_INTERACTION (interaction));

  promise = dex_promise_new_cancellable ();

  g_tls_database_lookup_certificate_for_handle_async (database,
                                                      handle,
                                                      interaction,
                                                      flags,
                                                      dex_promise_get_cancellable (promise),
                                                      dex_tls_database_lookup_certificate_for_handle_cb,
                                                      dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_tls_database_lookup_certificate_issuer_cb (GObject      *object,
                                               GAsyncResult *result,
                                               gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GTlsCertificate *certificate;

  certificate = g_tls_database_lookup_certificate_issuer_finish (G_TLS_DATABASE (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_object (promise, g_steal_pointer (&certificate));
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_tls_database_lookup_certificate_issuer:
 * @database: a [class@Gio.TlsDatabase]
 * @certificate: a [class@Gio.TlsCertificate]
 * @interaction: (nullable): a [class@Gio.TlsInteraction], or %NULL
 * @flags: lookup flags
 *
 * Wraps [method@Gio.TlsDatabase.lookup_certificate_issuer_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.TlsCertificate] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_tls_database_lookup_certificate_issuer (GTlsDatabase           *database,
                                            GTlsCertificate        *certificate,
                                            GTlsInteraction        *interaction,
                                            GTlsDatabaseLookupFlags flags)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_TLS_DATABASE (database));
  dex_return_error_if_fail (G_IS_TLS_CERTIFICATE (certificate));
  dex_return_error_if_fail (interaction == NULL || G_IS_TLS_INTERACTION (interaction));

  promise = dex_promise_new_cancellable ();

  g_tls_database_lookup_certificate_issuer_async (database,
                                                  certificate,
                                                  interaction,
                                                  flags,
                                                  dex_promise_get_cancellable (promise),
                                                  dex_tls_database_lookup_certificate_issuer_cb,
                                                  dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_tls_database_lookup_certificates_issued_by_cb (GObject      *object,
                                                   GAsyncResult *result,
                                                   gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GList *certificates;

  certificates = g_tls_database_lookup_certificates_issued_by_finish (G_TLS_DATABASE (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_boxed (promise, DEX_TYPE_TLS_CERTIFICATE_LIST, certificates);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_tls_database_lookup_certificates_issued_by:
 * @database: a [class@Gio.TlsDatabase]
 * @issuer_raw_dn: issuer distinguished name bytes
 * @interaction: (nullable): a [class@Gio.TlsInteraction], or %NULL
 * @flags: lookup flags
 *
 * Wraps [method@Gio.TlsDatabase.lookup_certificates_issued_by_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [struct@GLib.List] of [class@Gio.TlsCertificate] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_tls_database_lookup_certificates_issued_by (GTlsDatabase           *database,
                                                GByteArray             *issuer_raw_dn,
                                                GTlsInteraction        *interaction,
                                                GTlsDatabaseLookupFlags flags)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_TLS_DATABASE (database));
  dex_return_error_if_fail (issuer_raw_dn != NULL);
  dex_return_error_if_fail (interaction == NULL || G_IS_TLS_INTERACTION (interaction));

  promise = dex_promise_new_cancellable ();

  g_tls_database_lookup_certificates_issued_by_async (database,
                                                      issuer_raw_dn,
                                                      interaction,
                                                      flags,
                                                      dex_promise_get_cancellable (promise),
                                                      dex_tls_database_lookup_certificates_issued_by_cb,
                                                      dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_tls_interaction_ask_password_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GTlsInteractionResult interaction_result;

  interaction_result = g_tls_interaction_ask_password_finish (G_TLS_INTERACTION (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_enum (promise, G_TYPE_TLS_INTERACTION_RESULT, interaction_result);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_tls_interaction_ask_password:
 * @interaction: a [class@Gio.TlsInteraction]
 * @password: a [class@Gio.TlsPassword]
 *
 * Wraps [method@Gio.TlsInteraction.ask_password_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [enum@Gio.TlsInteractionResult] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_tls_interaction_ask_password (GTlsInteraction *interaction,
                                  GTlsPassword    *password)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_TLS_INTERACTION (interaction));
  dex_return_error_if_fail (G_IS_TLS_PASSWORD (password));

  promise = dex_promise_new_cancellable ();

  g_tls_interaction_ask_password_async (interaction,
                                        password,
                                        dex_promise_get_cancellable (promise),
                                        dex_tls_interaction_ask_password_cb,
                                        dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_tls_interaction_request_certificate_cb (GObject      *object,
                                            GAsyncResult *result,
                                            gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GTlsInteractionResult interaction_result;

  interaction_result = g_tls_interaction_request_certificate_finish (G_TLS_INTERACTION (object), result, &error);

  if (error == NULL)
    dex_promise_resolve_enum (promise, G_TYPE_TLS_INTERACTION_RESULT, interaction_result);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));

  dex_unref (promise);
}

/**
 * dex_tls_interaction_request_certificate:
 * @interaction: a [class@Gio.TlsInteraction]
 * @connection: a [class@Gio.TlsConnection]
 * @flags: certificate request flags
 *
 * Wraps [method@Gio.TlsInteraction.request_certificate_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [enum@Gio.TlsInteractionResult] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_tls_interaction_request_certificate (GTlsInteraction             *interaction,
                                         GTlsConnection              *connection,
                                         GTlsCertificateRequestFlags  flags)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_TLS_INTERACTION (interaction));
  dex_return_error_if_fail (G_IS_TLS_CONNECTION (connection));

  promise = dex_promise_new_cancellable ();

  g_tls_interaction_request_certificate_async (interaction,
                                               connection,
                                               flags,
                                               dex_promise_get_cancellable (promise),
                                               dex_tls_interaction_request_certificate_cb,
                                               dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_permission_acquire_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_permission_acquire_finish (G_PERMISSION (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_permission_acquire:
 * @permission: a [class@Gio.Permission]
 *
 * Wraps [method@Gio.Permission.acquire_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_permission_acquire (GPermission *permission)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_PERMISSION (permission));

  promise = dex_promise_new_cancellable ();

  g_permission_acquire_async (permission,
                              dex_promise_get_cancellable (promise),
                              dex_permission_acquire_cb,
                              dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_permission_release_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_permission_release_finish (G_PERMISSION (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_permission_release:
 * @permission: a [class@Gio.Permission]
 *
 * Wraps [method@Gio.Permission.release_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_permission_release (GPermission *permission)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_PERMISSION (permission));

  promise = dex_promise_new_cancellable ();

  g_permission_release_async (permission,
                              dex_promise_get_cancellable (promise),
                              dex_permission_release_cb,
                              dex_ref (promise));

  return DEX_FUTURE (promise);
}

#ifdef G_OS_UNIX
static void
dex_unix_connection_send_credentials_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_unix_connection_send_credentials_finish (G_UNIX_CONNECTION (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_unix_connection_send_credentials:
 * @connection: a `GUnixConnection`
 *
 * Wraps `g_unix_connection_send_credentials_async()`.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to %TRUE
 *   or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_unix_connection_send_credentials (GUnixConnection *connection)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_UNIX_CONNECTION (connection));

  promise = dex_promise_new_cancellable ();

  g_unix_connection_send_credentials_async (connection,
                                            dex_promise_get_cancellable (promise),
                                            dex_unix_connection_send_credentials_cb,
                                            dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
dex_unix_connection_receive_credentials_cb (GObject      *object,
                                            GAsyncResult *result,
                                            gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GCredentials *credentials;

  if (!(credentials = g_unix_connection_receive_credentials_finish (G_UNIX_CONNECTION (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&credentials));

  dex_unref (promise);
}

/**
 * dex_unix_connection_receive_credentials:
 * @connection: a `GUnixConnection`
 *
 * Wraps `g_unix_connection_receive_credentials_async()`.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.Credentials] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_unix_connection_receive_credentials (GUnixConnection *connection)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_UNIX_CONNECTION (connection));

  promise = dex_promise_new_cancellable ();

  g_unix_connection_receive_credentials_async (connection,
                                               dex_promise_get_cancellable (promise),
                                               dex_unix_connection_receive_credentials_cb,
                                               dex_ref (promise));

  return DEX_FUTURE (promise);
}
#endif

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
dex_file_set_display_name_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;
  GFile *file;

  if (!(file = g_file_set_display_name_finish (G_FILE (object), result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&file));

  dex_unref (promise);
}

/**
 * dex_file_set_display_name:
 * @file: a [iface@Gio.File]
 * @display_name: a new display name
 * @io_priority: priority for the IO operation
 *
 * Wraps [method@Gio.File.set_display_name_async].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to the renamed
 *   [iface@Gio.File] or rejects with error.
 *
 * Since: 1.2
 */
DexFuture *
dex_file_set_display_name (GFile      *file,
                           const char *display_name,
                           int         io_priority)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_FILE (file));
  dex_return_error_if_fail (display_name != NULL);

  promise = dex_promise_new_cancellable ();
  g_file_set_display_name_async (file,
                                 display_name,
                                 io_priority,
                                 dex_promise_get_cancellable (promise),
                                 dex_file_set_display_name_cb,
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
                     progress_callback,
                     progress_callback_data,
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
