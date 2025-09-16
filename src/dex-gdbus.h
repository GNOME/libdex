/*
 * dex-gdbus.h
 *
 * Copyright 2025 Red Hat, Inc.
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
# include <gio/gunixfdlist.h>
#endif

#include "dex-future.h"

G_BEGIN_DECLS

DEX_AVAILABLE_IN_ALL
DexFuture *dex_bus_get                                 (GBusType                  bus_type) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_1
void       dex_bus_own_name_on_connection              (GDBusConnection          *connection,
                                                        const char               *name,
                                                        GBusNameOwnerFlags        flags,
                                                        DexFuture               **out_name_acquired_future,
                                                        DexFuture               **out_name_lost_future);
DEX_AVAILABLE_IN_ALL
DexFuture *dex_dbus_connection_call                    (GDBusConnection          *connection,
                                                        const char               *bus_name,
                                                        const char               *object_path,
                                                        const char               *interface_name,
                                                        const char               *method_name,
                                                        GVariant                 *parameters,
                                                        const GVariantType       *reply_type,
                                                        GDBusCallFlags            flags,
                                                        int                       timeout_msec) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_dbus_connection_close                   (GDBusConnection          *connection) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_dbus_connection_send_message_with_reply (GDBusConnection          *connection,
                                                        GDBusMessage             *message,
                                                        GDBusSendMessageFlags     flags,
                                                        int                       timeout_msec,
                                                        guint32                  *out_serial) G_GNUC_WARN_UNUSED_RESULT;
#ifdef G_OS_UNIX
DEX_AVAILABLE_IN_ALL
DexFuture *dex_dbus_connection_call_with_unix_fd_list  (GDBusConnection          *connection,
                                                        const char               *bus_name,
                                                        const char               *object_path,
                                                        const char               *interface_name,
                                                        const char               *method_name,
                                                        GVariant                 *parameters,
                                                        const GVariantType       *reply_type,
                                                        GDBusCallFlags            flags,
                                                        int                       timeout_msec,
                                                        GUnixFDList              *fd_list) G_GNUC_WARN_UNUSED_RESULT;
#endif

G_END_DECLS
