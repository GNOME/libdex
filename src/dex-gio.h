/*
 * dex-gio.h
 *
 * Copyright 2022 Christian Hergert <christian@sourceandstack.com>
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

#pragma once

#include <gio/gio.h>

#ifdef G_OS_UNIX
# include <gio/gunixconnection.h>
#endif

#include "dex-future.h"

G_BEGIN_DECLS

#define DEX_TYPE_FILE_INFO_LIST (dex_file_info_list_get_type())
#define DEX_TYPE_INET_ADDRESS_LIST (dex_inet_address_list_get_type())

DEX_AVAILABLE_IN_ALL
GType      dex_file_info_list_get_type                    (void);
DEX_AVAILABLE_IN_ALL
GType      dex_inet_address_list_get_type                 (void);
DEX_AVAILABLE_IN_1_2
DexFuture *dex_app_info_launch_uris                       (GAppInfo                    *appinfo,
                                                           GList                       *uris,
                                                           GAppLaunchContext           *context) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_app_info_get_default_for_type              (const char                  *content_type,
                                                           gboolean                     must_support_uris) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_app_info_get_default_for_uri_scheme        (const char                  *uri_scheme) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_app_info_launch_default_for_uri            (const char                  *uri,
                                                           GAppLaunchContext           *context) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_async_initable_new                         (GType                        object_type,
                                                           int                          io_priority,
                                                           const char                  *first_property_name,
                                                           ...) G_GNUC_NULL_TERMINATED G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_new_tmp_dir                           (const char                  *tmpl,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_make_directory                        (GFile                       *file,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_make_directory_with_parents           (GFile                       *file) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_copy                                  (GFile                       *source,
                                                           GFile                       *destination,
                                                           GFileCopyFlags               flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_copy_with_progress                    (GFile                       *source,
                                                           GFile                       *destination,
                                                           GFileCopyFlags               flags,
                                                           int                          io_priority,
                                                           GFileProgressCallback        progress_callback,
                                                           gpointer                     progress_callback_data,
                                                           GDestroyNotify               progress_callback_data_destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_delete                                (GFile                       *file,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_trash                                 (GFile                       *file,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_read                                  (GFile                       *file,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_append_to                             (GFile                       *file,
                                                           GFileCreateFlags             flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_load_contents_bytes                   (GFile                       *file) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_load_partial_contents_bytes           (GFile                       *file,
                                                           GFileReadMoreCallback        read_more_callback,
                                                           gpointer                     read_more_callback_data,
                                                           GDestroyNotify               read_more_callback_data_destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_load_bytes                            (GFile                       *file) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_query_info                            (GFile                       *file,
                                                           const char                  *attributes,
                                                           GFileQueryInfoFlags          flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_query_filesystem_info                 (GFile                       *file,
                                                           const char                  *attributes,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_query_file_type                       (GFile                       *file,
                                                           GFileQueryInfoFlags          flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_query_default_handler                 (GFile                       *file,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_find_enclosing_mount                  (GFile                       *file,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_set_attributes                        (GFile                       *file,
                                                           GFileInfo                   *file_info,
                                                           GFileQueryInfoFlags          flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_set_display_name                      (GFile                       *file,
                                                           const char                  *display_name,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_1
DexFuture *dex_file_create                                (GFile                       *file,
                                                           GFileCreateFlags             flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_open_readwrite                        (GFile                       *file,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_create_readwrite                      (GFile                       *file,
                                                           GFileCreateFlags             flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_replace                               (GFile                       *file,
                                                           const char                  *etag,
                                                           gboolean                     make_backup,
                                                           GFileCreateFlags             flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_replace_readwrite                     (GFile                       *file,
                                                           const char                  *etag,
                                                           gboolean                     make_backup,
                                                           GFileCreateFlags             flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_replace_contents                      (GFile                       *file,
                                                           const char                  *contents,
                                                           gsize                        length,
                                                           const char                  *etag,
                                                           gboolean                     make_backup,
                                                           GFileCreateFlags             flags) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_replace_contents_bytes                (GFile                       *file,
                                                           GBytes                      *contents,
                                                           const char                  *etag,
                                                           gboolean                     make_backup,
                                                           GFileCreateFlags             flags) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_make_symbolic_link                    (GFile                       *file,
                                                           const char                  *symlink_value,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_enumerate_children                    (GFile                       *file,
                                                           const char                  *attributes,
                                                           GFileQueryInfoFlags          flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_enumerator_next_files                 (GFileEnumerator             *file_enumerator,
                                                           int                          num_files,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_enumerator_close                      (GFileEnumerator             *file_enumerator,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_input_stream_query_info               (GFileInputStream            *stream,
                                                           const char                  *attributes,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_io_stream_query_info                  (GFileIOStream               *stream,
                                                           const char                  *attributes,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_file_output_stream_query_info              (GFileOutputStream           *stream,
                                                           const char                  *attributes,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_buffered_input_stream_fill                 (GBufferedInputStream        *stream,
                                                           gssize                       count,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_input_stream_close                         (GInputStream                *self,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_input_stream_read                          (GInputStream                *self,
                                                           gpointer                     buffer,
                                                           gsize                        count,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_input_stream_skip                          (GInputStream                *self,
                                                           gsize                        count,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_input_stream_read_bytes                    (GInputStream                *stream,
                                                           gsize                        count,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_1
DexFuture *dex_data_input_stream_read_line                (GDataInputStream            *stream,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_data_input_stream_read_line_utf8           (GDataInputStream            *stream,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_data_input_stream_read_upto                (GDataInputStream            *stream,
                                                           const char                  *stop_chars,
                                                           gssize                       stop_chars_len,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_output_stream_close                        (GOutputStream               *self,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_output_stream_flush                        (GOutputStream               *self,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_output_stream_splice                       (GOutputStream               *output,
                                                           GInputStream                *input,
                                                           GOutputStreamSpliceFlags     flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_output_stream_write                        (GOutputStream               *self,
                                                           gconstpointer                buffer,
                                                           gsize                        count,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_output_stream_writev_all                   (GOutputStream               *stream,
                                                           const GOutputVector         *vectors,
                                                           gsize                        n_vectors,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_output_stream_write_bytes                  (GOutputStream               *stream,
                                                           GBytes                      *bytes,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_socket_wait                                (GSocket                     *socket,
                                                           GIOCondition                 condition) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_socket_listener_accept                     (GSocketListener             *listener) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_socket_client_connect                      (GSocketClient               *socket_client,
                                                           GSocketConnectable          *socket_connectable) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_socket_client_connect_to_host              (GSocketClient               *socket_client,
                                                           const char                  *host_and_port,
                                                           guint16                      default_port) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_socket_client_connect_to_service           (GSocketClient               *socket_client,
                                                           const char                  *domain,
                                                           const char                  *service) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_socket_client_connect_to_uri               (GSocketClient               *socket_client,
                                                           const char                  *uri,
                                                           guint16                      default_port) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_socket_connection_connect                  (GSocketConnection           *connection,
                                                           GSocketAddress              *address) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_socket_address_enumerator_next             (GSocketAddressEnumerator    *enumerator) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_io_stream_close                            (GIOStream                   *io_stream,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_io_stream_splice                           (GIOStream                   *stream1,
                                                           GIOStream                   *stream2,
                                                           GIOStreamSpliceFlags         flags,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_tls_connection_handshake                   (GTlsConnection              *tls_connection,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_dtls_connection_handshake                  (GDtlsConnection             *dtls_connection,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_dtls_connection_shutdown                   (GDtlsConnection             *dtls_connection,
                                                           gboolean                     shutdown_read,
                                                           gboolean                     shutdown_write,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_dtls_connection_close                      (GDtlsConnection             *dtls_connection,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_resolver_lookup_by_name                    (GResolver                   *resolver,
                                                           const char                  *address) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_resolver_lookup_by_name_with_flags         (GResolver                   *resolver,
                                                           const char                  *address,
                                                           GResolverNameLookupFlags     flags) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_resolver_lookup_by_address                 (GResolver                   *resolver,
                                                           GInetAddress                *address) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_resolver_lookup_service                    (GResolver                   *resolver,
                                                           const char                  *service,
                                                           const char                  *protocol,
                                                           const char                  *domain) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_resolver_lookup_records                    (GResolver                   *resolver,
                                                           const char                  *rrname,
                                                           GResolverRecordType          record_type) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_network_monitor_can_reach                  (GNetworkMonitor             *monitor,
                                                           GSocketConnectable          *connectable) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_proxy_connect                              (GProxy                      *proxy,
                                                           GIOStream                   *connection,
                                                           GProxyAddress               *proxy_address) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_proxy_resolver_lookup                      (GProxyResolver              *resolver,
                                                           const char                  *uri) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_subprocess_wait_check                      (GSubprocess                 *subprocess) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_subprocess_wait                            (GSubprocess                 *subprocess) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_tls_database_verify_chain                  (GTlsDatabase                *database,
                                                           GTlsCertificate             *chain,
                                                           const char                  *purpose,
                                                           GSocketConnectable          *identity,
                                                           GTlsInteraction             *interaction,
                                                           GTlsDatabaseVerifyFlags      flags) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_tls_database_lookup_certificate_for_handle (GTlsDatabase                *database,
                                                           const char                  *handle,
                                                           GTlsInteraction             *interaction,
                                                           GTlsDatabaseLookupFlags      flags) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_tls_database_lookup_certificate_issuer     (GTlsDatabase                *database,
                                                           GTlsCertificate             *certificate,
                                                           GTlsInteraction             *interaction,
                                                           GTlsDatabaseLookupFlags      flags) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_tls_database_lookup_certificates_issued_by (GTlsDatabase                *database,
                                                           GByteArray                  *issuer_raw_dn,
                                                           GTlsInteraction             *interaction,
                                                           GTlsDatabaseLookupFlags      flags) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_tls_interaction_ask_password               (GTlsInteraction             *interaction,
                                                           GTlsPassword                *password) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_tls_interaction_request_certificate        (GTlsInteraction             *interaction,
                                                           GTlsConnection              *connection,
                                                           GTlsCertificateRequestFlags  flags) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_permission_acquire                         (GPermission                 *permission) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_permission_release                         (GPermission                 *permission) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_unix_connection_send_credentials           (GUnixConnection             *connection) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture *dex_unix_connection_receive_credentials        (GUnixConnection             *connection) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_query_exists                          (GFile                       *file) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_file_move                                  (GFile                       *source,
                                                           GFile                       *destination,
                                                           GFileCopyFlags               flags,
                                                           int                          io_priority,
                                                           GFileProgressCallback        progress_callback,
                                                           gpointer                     progress_callback_data,
                                                           GDestroyNotify               progress_callback_data_destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_async_initable_init                        (GAsyncInitable              *initable,
                                                           int                          io_priority) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_1
DexFuture *dex_mkdir_with_parents                         (const char                  *path,
                                                           int                          mode) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_1
DexFuture *dex_find_program_in_path                       (const char                  *program) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_1
DexFuture *dex_unlink                                     (const char                  *path) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_1
DexFuture *dex_fd_watch                                   (int                          fd,
                                                           int                          events) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
