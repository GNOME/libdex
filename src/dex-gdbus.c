/*
 * dex-gdbus.c
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

#include "config.h"

#include "dex-async-pair-private.h"
#include "dex-future-private.h"
#include "dex-future-set.h"
#include "dex-promise.h"

#include "dex-gdbus.h"

static inline DexAsyncPair *
create_async_pair (const char *name)
{
  DexAsyncPair *async_pair;

  async_pair = (DexAsyncPair *)dex_object_create_instance (DEX_TYPE_ASYNC_PAIR);
  dex_future_set_static_name (DEX_FUTURE (async_pair), name);

  return async_pair;
}

static void
dex_dbus_connection_send_message_with_reply_cb (GObject      *object,
                                                GAsyncResult *result,
                                                gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GDBusMessage *message = NULL;
  GError *error = NULL;

  message = g_dbus_connection_send_message_with_reply_finish (G_DBUS_CONNECTION (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_object (async_pair, message);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_dbus_connection_send_message_with_reply:
 * @connection: a [class@Gio.DBusConnection]
 * @message: a [class@Gio.DBusMessage]
 * @flags: a set of [flags@Gio.DBusSendMessageFlags]
 * @timeout_msec: timeout in milliseconds, or -1 for default, or %G_MAXINT
 *   for no timeout.
 * @out_serial: (out) (optional): a location for the message serial number
 *
 * Wrapper for [method@Gio.DBusConnection.send_message_with_reply].
 *
 * Returns: (transfer full): a [class@Dex.Future] that will resolve to a
 *   [class@Gio.DBusMessage] or reject with failure.
 *
 * Since: 0.4
 */
DexFuture *
dex_dbus_connection_send_message_with_reply (GDBusConnection       *connection,
                                             GDBusMessage          *message,
                                             GDBusSendMessageFlags  flags,
                                             int                    timeout_msec,
                                             guint32               *out_serial)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  g_return_val_if_fail (G_IS_DBUS_MESSAGE (message), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_dbus_connection_send_message_with_reply (connection,
                                             message,
                                             flags,
                                             timeout_msec,
                                             out_serial,
                                             async_pair->cancellable,
                                             dex_dbus_connection_send_message_with_reply_cb,
                                             dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

static void
dex_dbus_connection_call_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GVariant *reply = NULL;
  GError *error = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_variant (async_pair, reply);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_dbus_connection_call:
 * @connection: a [class@Gio.DBusConnection]
 * @bus_name: (nullable): a unique or well-known bus name or %NULL if
 *   @connection is not a message bus connection
 * @object_path: path of remote object
 * @interface_name: D-Bus interface to invoke method on
 * @method_name: the name of the method to invoke
 * @parameters: (nullable): a [struct@GLib.Variant] tuple with parameters for
 *   the method or %NULL if not passing parameters
 * @reply_type: (nullable): the expected type of the reply (which will be a
 *   tuple), or %NULL
 * @flags: flags from the [flags@Gio.DBusCallFlags] enumeration
 * @timeout_msec: the timeout in milliseconds, -1 to use the default
 *   timeout or %G_MAXINT for no timeout
 *
 * Wrapper for [method@Gio.DBusConnection.call].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [struct@GLib.Variant] or rejects with error.
 *
 * Since: 0.4
 */
DexFuture *
dex_dbus_connection_call (GDBusConnection    *connection,
                          const char         *bus_name,
                          const char         *object_path,
                          const char         *interface_name,
                          const char         *method_name,
                          GVariant           *parameters,
                          const GVariantType *reply_type,
                          GDBusCallFlags      flags,
                          int                 timeout_msec)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);

  async_pair = create_async_pair (G_STRFUNC);

  g_dbus_connection_call (connection,
                          bus_name,
                          object_path,
                          interface_name,
                          method_name,
                          parameters,
                          reply_type,
                          flags,
                          timeout_msec,
                          async_pair->cancellable,
                          dex_dbus_connection_call_cb,
                          dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

#ifdef G_OS_UNIX
static void
dex_dbus_connection_call_with_unix_fd_list_cb (GObject      *object,
                                               GAsyncResult *result,
                                               gpointer      user_data)
{
  DexFutureSet *future_set = user_data;
  DexAsyncPair *async_pair;
  DexPromise *promise;
  GUnixFDList *fd_list = NULL;
  GVariant *reply = NULL;
  GError *error = NULL;

  g_assert (G_IS_DBUS_CONNECTION (object));
  g_assert (DEX_IS_FUTURE_SET (future_set));

  async_pair = DEX_ASYNC_PAIR (dex_future_set_get_future_at (future_set, 0));
  promise = DEX_PROMISE (dex_future_set_get_future_at (future_set, 1));

  g_assert (DEX_IS_ASYNC_PAIR (async_pair));
  g_assert (DEX_IS_PROMISE (promise));

  reply = g_dbus_connection_call_with_unix_fd_list_finish (G_DBUS_CONNECTION (object), &fd_list, result, &error);

  g_assert (!fd_list || G_IS_UNIX_FD_LIST (fd_list));
  g_assert (reply != NULL || error != NULL);

  if (error == NULL)
    {
      dex_promise_resolve_object (promise, fd_list);
      dex_async_pair_return_variant (async_pair, reply);
    }
  else
    {
      dex_promise_reject (promise, g_error_copy (error));
      dex_async_pair_return_error (async_pair, error);
    }

  dex_unref (future_set);
}

/**
 * dex_dbus_connection_call_with_unix_fd_list:
 * @connection: a [class@Gio.DBusConnection]
 * @bus_name: (nullable): a unique or well-known bus name or %NULL if
 *   @connection is not a message bus connection
 * @object_path: path of remote object
 * @interface_name: D-Bus interface to invoke method on
 * @method_name: the name of the method to invoke
 * @parameters: (nullable): a [struct@GLib.Variant] tuple with parameters for
 *   the method or %NULL if not passing parameters
 * @reply_type: (nullable): the expected type of the reply (which will be a
 *   tuple), or %NULL
 * @flags: flags from the [flags@Gio.DBusCallFlags] enumeration
 * @timeout_msec: the timeout in milliseconds, -1 to use the default
 *   timeout or %G_MAXINT for no timeout
 * @fd_list: (nullable): a [class@Gio.UnixFDList]
 *
 * Wrapper for [method@Gio.DBusConnection.call_with_unix_fd_list].
 *
 * Returns: (transfer full): a [class@Dex.FutureSet] that resolves to a
 *   [struct@GLib.Variant].
 *
 *   The [class@Dex.Future] containing the resulting [class@Gio.UnixFDList] can
 *   be retrieved with [method@Dex.FutureSet.get_future_at] with an index of 1.
 *
 * Since: 0.4
 */
DexFuture *
dex_dbus_connection_call_with_unix_fd_list (GDBusConnection    *connection,
                                            const char         *bus_name,
                                            const char         *object_path,
                                            const char         *interface_name,
                                            const char         *method_name,
                                            GVariant           *parameters,
                                            const GVariantType *reply_type,
                                            GDBusCallFlags      flags,
                                            int                 timeout_msec,
                                            GUnixFDList        *fd_list)
{
  DexAsyncPair *async_pair;
  DexPromise *promise;
  DexFuture *ret;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  g_return_val_if_fail (!fd_list || G_IS_UNIX_FD_LIST (fd_list), NULL);

  /* Will hold our GVariant result */
  async_pair = create_async_pair (G_STRFUNC);

  /* Will hold our GUnixFDList result */
  promise = dex_promise_new ();

  /* Sent to user. Resolving will contain variant. */
  ret = dex_future_all (DEX_FUTURE (async_pair), DEX_FUTURE (promise), NULL);

  g_dbus_connection_call_with_unix_fd_list (connection,
                                            bus_name,
                                            object_path,
                                            interface_name,
                                            method_name,
                                            parameters,
                                            reply_type,
                                            flags,
                                            timeout_msec,
                                            fd_list,
                                            async_pair->cancellable,
                                            dex_dbus_connection_call_with_unix_fd_list_cb,
                                            dex_ref (ret));

  return ret;
}
#endif

static void
dex_bus_get_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GDBusConnection *bus;
  GError *error = NULL;

  bus = g_bus_get_finish (result, &error);

  if (error == NULL)
    dex_async_pair_return_object (async_pair, bus);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

/**
 * dex_bus_get:
 * @bus_type: the [enum@Gio.BusType]
 *
 * Wrapper for [func@Gio.bus_get].
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [class@Gio.DBusConnection] or rejects with error.
 *
 * Since: 0.4
 */
DexFuture *
dex_bus_get (GBusType bus_type)
{
  DexAsyncPair *async_pair;

  async_pair = create_async_pair (G_STRFUNC);

  g_bus_get (bus_type,
             async_pair->cancellable,
             dex_bus_get_cb,
             dex_ref (async_pair));

  return DEX_FUTURE (async_pair);
}

typedef struct
{
  DexPromise *name_acquired;
  gulong acquired_cancelled_id;
  DexPromise *name_lost;
  gulong lost_cancelled_id;
  guint own_name_id;
} BusOwnNameData;

static void
dex_bus_own_name_data_free (BusOwnNameData *data)
{

  g_signal_handler_disconnect (dex_promise_get_cancellable (data->name_acquired),
                               data->acquired_cancelled_id);
  g_signal_handler_disconnect (dex_promise_get_cancellable (data->name_lost),
                               data->lost_cancelled_id);

  if (dex_future_is_pending (DEX_FUTURE (data->name_acquired)))
    {
      dex_promise_reject (data->name_acquired,
                          g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED,
                                               "Failed to acquire dbus name"));
    }

  if (dex_future_is_pending (DEX_FUTURE (data->name_lost)))
    {
      dex_promise_reject (data->name_lost,
                          g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED,
                                               "Lost dbus name"));
    }

  g_clear_pointer (&data->name_acquired, dex_unref);
  g_clear_pointer (&data->name_lost, dex_unref);
  free (data);
}

static void
dex_bus_name_acquired_cb (GDBusConnection *connection,
                          const char      *name,
                          gpointer         user_data)
{
  BusOwnNameData *data = user_data;

  dex_promise_resolve_boolean (data->name_acquired, TRUE);
}

static void
dex_bus_name_lost_cb (GDBusConnection *connection,
                      const char      *name,
                      gpointer         user_data)
{
  BusOwnNameData *data = user_data;

  g_clear_handle_id (&data->own_name_id, g_bus_unown_name);
}

static void
dex_bus_name_cancelled_cb (GCancellable *cancellable,
                           gpointer      user_data)
{
  BusOwnNameData *data = user_data;

  g_clear_handle_id (&data->own_name_id, g_bus_unown_name);
}

/**
 * dex_bus_own_name_on_connection:
 * @connection: The [class@Gio.DBusConnection] to own a name on.
 * @name: The well-known name to own.
 * @flags: a set of flags with ownership options.
 * @out_name_acquired_future: (out) (optional): a location for the name acquired future
 * @out_name_lost_future: (out) (optional): a location for the name lost future
 *
 * Wrapper for [func@Gio.bus_own_name].
 *
 * Asks the D-Bus broker to own the well-known name @name on the connection @connection.
 *
 * @out_name_acquired_future is a future that awaits owning the name and either
 * resolves to true, or rejects with an error.
 *
 * @out_name_lost_future is a future that rejects when the name was lost.
 *
 * If either future is canceled, the name will be unowned.
 *
 * Since: 1.1
 */
void
dex_bus_own_name_on_connection (GDBusConnection     *connection,
                                const char          *name,
                                GBusNameOwnerFlags   flags,
                                DexFuture          **out_name_acquired_future,
                                DexFuture          **out_name_lost_future)
{
  BusOwnNameData *data = g_new0 (BusOwnNameData, 1);

  data->name_acquired = dex_promise_new_cancellable ();
  data->name_lost = dex_promise_new_cancellable ();

  data->acquired_cancelled_id =
    g_signal_connect_swapped (dex_promise_get_cancellable (data->name_acquired),
                              "cancelled",
                              G_CALLBACK (dex_bus_name_cancelled_cb),
                              data);

  data->lost_cancelled_id =
    g_signal_connect_swapped (dex_promise_get_cancellable (data->name_lost),
                              "cancelled",
                              G_CALLBACK (dex_bus_name_cancelled_cb),
                              data);
  data->own_name_id =
    g_bus_own_name_on_connection (connection,
                                  name,
                                  flags,
                                  dex_bus_name_acquired_cb,
                                  dex_bus_name_lost_cb,
                                  data,
                                  (GDestroyNotify) dex_bus_own_name_data_free);

  if (out_name_acquired_future)
    *out_name_acquired_future = DEX_FUTURE (dex_ref (data->name_acquired));
  if (out_name_lost_future)
    *out_name_lost_future = DEX_FUTURE (dex_ref (data->name_lost));
}

static void
dex_dbus_connection_close_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  DexPromise *promise = user_data;
  GError *error = NULL;

  if (!g_dbus_connection_close_finish (G_DBUS_CONNECTION (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);

  dex_unref (promise);
}

/**
 * dex_dbus_connection_close:
 * @connection: a [class@Gio.DBusConnection]
 *
 * Asynchronously closes a connection.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves
 *   to `true` or rejects with error.
 *
 * Since: 1.0
 */
DexFuture *
dex_dbus_connection_close (GDBusConnection *connection)
{
  DexPromise *promise;

  dex_return_error_if_fail (G_IS_DBUS_CONNECTION (connection));

  promise = dex_promise_new_cancellable ();
  g_dbus_connection_close (connection,
                           dex_promise_get_cancellable (promise),
                           dex_dbus_connection_close_cb,
                           dex_ref (promise));
  return DEX_FUTURE (promise);
}
