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

#include "dex-features.h"
#include "dex-future.h"

G_BEGIN_DECLS

#ifdef DEX_FEATURE_GDBUS_CODEGEN
/**
 * DexDBusInterfaceSkeletonFlags:
 * @DEX_DBUS_INTERFACE_SKELETON_FLAGS_NONE: No flags set.
 * @DEX_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_FIBER: Each method invocation is
 *   handled in a fiber dedicated to the invocation. This means that the method implementation can
 *   use dex_await or similar. Authorization for method invocations uses the same fiber.
 *   This can not be used in combination with METHOD_INVOCATIONS_IN_THREAD and trying to do so leads
 *   to a runtime error.
 *
 * Flags describing the behavior of a #GDBusInterfaceSkeleton instance.
 *
 * Since: 1.1
 */
typedef enum
{
  DEX_DBUS_INTERFACE_SKELETON_FLAGS_NONE = 0,
  DEX_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_FIBER = (1 << 0),
} DexDBusInterfaceSkeletonFlags;

#define DEX_TYPE_DBUS_INTERFACE_SKELETON (dex_dbus_interface_skeleton_get_type ())
DEX_AVAILABLE_IN_1_1
G_DECLARE_DERIVABLE_TYPE (DexDBusInterfaceSkeleton,
                          dex_dbus_interface_skeleton,
                          DEX, DBUS_INTERFACE_SKELETON,
                          GDBusInterfaceSkeleton)

struct _DexDBusInterfaceSkeletonClass
{
  GDBusInterfaceSkeletonClass parent_class;
};

DEX_AVAILABLE_IN_1_1
void                          dex_dbus_interface_skeleton_cancel    (DexDBusInterfaceSkeleton      *interface_);
DEX_AVAILABLE_IN_1_1
DexDBusInterfaceSkeletonFlags dex_dbus_interface_skeleton_get_flags (DexDBusInterfaceSkeleton      *interface_);
DEX_AVAILABLE_IN_1_1
void                          dex_dbus_interface_skeleton_set_flags (DexDBusInterfaceSkeleton      *interface_,
                                                                     DexDBusInterfaceSkeletonFlags  flags);
#endif /* DEX_FEATURE_GDBUS_CODEGEN */

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
