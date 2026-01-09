/* test-dbus.c
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

#include <libdex.h>
#include <stdint.h>

#include "dex-fiber-private.h"
#include "dex-test-dbus-foo.h"

struct _DexTestFoo
{
  DexTestDbusFooSkeleton parent_instance;
};

struct _DexTestFooClass
{
  DexTestDbusFooSkeletonClass parent_class;
};

static void dex_test_dbus_foo_iface_init (DexTestDbusFooIface *iface);

#define DEX_TEST_TYPE_FOO (dex_test_foo_get_type ())
G_DECLARE_FINAL_TYPE (DexTestFoo,
                      dex_test_foo,
                      DEX_TEST, FOO,
                      DexTestDbusFooSkeleton)

G_DEFINE_TYPE_WITH_CODE (DexTestFoo,
                         dex_test_foo,
                         DEX_TEST_DBUS_TYPE_FOO_SKELETON,
                         G_IMPLEMENT_INTERFACE (DEX_TEST_DBUS_TYPE_FOO,
                                                dex_test_dbus_foo_iface_init));

static GDBusConnection *
get_new_session_connection_sync (void)
{
  GDBusConnection *connection;
  char *session_address;
  GError *error = NULL;

  session_address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (session_address);

  connection = g_dbus_connection_new_for_address_sync (session_address,
                                                       G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                       G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                                       NULL, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  g_free (session_address);
  return connection;
}

static gboolean
handle_foo (DexTestDbusFoo        *object,
            GDBusMethodInvocation *invocation)
{
  g_usleep (G_USEC_PER_SEC / 10);

  dex_test_dbus_foo_complete_foo (object, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_bar (DexTestDbusFoo        *object,
            GDBusMethodInvocation *invocation,
            const char * const    *i1,
            uint32_t               i2)
{
  GVariantBuilder builder =
    G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);

  g_usleep (G_USEC_PER_SEC / 10);

  g_variant_builder_add (&builder, "{sv}", "foo", g_variant_new_uint32 (3));
  g_variant_builder_add (&builder, "{sv}", "bar", g_variant_new_boolean (FALSE));

  dex_test_dbus_foo_complete_bar (object, invocation, 42, g_variant_builder_end (&builder));

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_emit_foo_bar (DexTestDbusFoo        *object,
                     GDBusMethodInvocation *invocation)
{
  dex_test_dbus_foo_emit_foo_bar (object);
  dex_test_dbus_foo_complete_emit_foo_bar (object, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_fiber (DexTestDbusFoo        *object,
              GDBusMethodInvocation *invocation)
{
  dex_await (dex_timeout_new_msec (100), NULL);
  dex_test_dbus_foo_complete_fiber (object, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
dex_test_dbus_foo_iface_init (DexTestDbusFooIface *iface)
{
  iface->handle_foo = handle_foo;
  iface->handle_bar = handle_bar;
  iface->handle_emit_foo_bar = handle_emit_foo_bar;
  iface->handle_fiber = handle_fiber;
}

static void
dex_test_foo_init (DexTestFoo *foo G_GNUC_UNUSED)
{
}

static void
dex_test_foo_class_init (DexTestFooClass *klass G_GNUC_UNUSED)
{
}

typedef struct _FooServiceData
{
  GMainLoop *main_loop;
  DexTestFoo *foo;
  guint own_name_id;
  guint timeout_id;
  gboolean acquired;
} FooServiceData;

static gboolean
send_signal_cb (gpointer user_data)
{
  FooServiceData *data = user_data;
  GVariantBuilder builder =
    G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);

  g_variant_builder_add (&builder, "{sv}", "foo", g_variant_new_uint32 (3));
  g_variant_builder_add (&builder, "{sv}", "bar", g_variant_new_boolean (FALSE));

  dex_test_dbus_foo_emit_baz (DEX_TEST_DBUS_FOO (data->foo), 11, g_variant_builder_end (&builder));

  return G_SOURCE_CONTINUE;
}

static void
bus_name_acquired_cb (GDBusConnection *connection,
                      const gchar     *name,
                      gpointer         user_data)
{
  FooServiceData *data = user_data;
  DexTestFoo *foo = data->foo;
  GError *error = NULL;

  g_assert_true (g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (foo),
                                                   connection,
                                                   "/org/example/foo",
                                                   &error));
  g_assert_no_error (error);

  data->timeout_id = g_timeout_add (100, send_signal_cb, data);

  data->acquired = TRUE;
}

static void
bus_name_lost_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  FooServiceData *data = user_data;

  g_main_loop_quit (data->main_loop);
}

static void
foo_service_cancelled_cb (GCancellable *cancellable,
                          gpointer      user_data)
{
  FooServiceData *data = user_data;

  g_clear_handle_id (&data->own_name_id, g_bus_unown_name);
  g_main_loop_quit (data->main_loop);
}

static void
run_foo_service_in_thread (GTask        *task,
                           gpointer      source_object,
                           gpointer      task_data,
                           GCancellable *cancellable)
{
  FooServiceData data;
  GMainLoop *main_loop;
  GMainContext *main_context;
  GDBusConnection *connection;
  DexTestFoo *foo;
  GError *error = NULL;

  main_context = g_main_context_new ();
  main_loop = g_main_loop_new (main_context, FALSE);
  g_assert_nonnull (main_loop);

  foo = g_object_new (DEX_TEST_TYPE_FOO, NULL);
  g_assert_nonnull (foo);

  data.main_loop = main_loop;
  data.foo = foo;
  data.acquired = FALSE;
  data.own_name_id = 0;
  data.timeout_id = 0;

  connection = get_new_session_connection_sync ();
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  data.own_name_id =
    g_bus_own_name_on_connection (connection, "org.example.Foo",
                                  G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                  G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                  bus_name_acquired_cb,
                                  bus_name_lost_cb,
                                  &data, NULL);

  g_cancellable_connect (cancellable,
                         G_CALLBACK (foo_service_cancelled_cb),
                         &data, NULL);
  if (!g_cancellable_is_cancelled (cancellable))
    g_main_loop_run (main_loop);

  if (data.acquired)
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (data.foo));

  g_clear_handle_id (&data.timeout_id, g_source_remove);

  g_main_loop_unref (main_loop);
  g_main_context_unref (main_context);
  g_clear_object (&foo);
  g_clear_object (&connection);

  g_task_return_boolean (task, TRUE);
}

static void
run_foo_service_cancelled_cb (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      data)
{
  GTask *task = G_TASK (res);
  GCancellable *cancellable;

  cancellable = g_task_get_task_data (task);
  g_task_set_task_data (task, NULL, NULL);
  g_clear_object (&cancellable);
}

static GTask *
run_foo_service (void)
{
  GTask *task = NULL;
  GCancellable *cancellable = g_cancellable_new ();

  task = g_task_new (NULL, cancellable, run_foo_service_cancelled_cb, NULL);
  g_task_set_task_data (task, cancellable, NULL);
  g_task_run_in_thread (task, run_foo_service_in_thread);

  return task;
}

static void
stop_foo_service (GTask *task)
{
  GCancellable *cancellable;

  cancellable = g_task_get_task_data (task);
  g_cancellable_cancel (cancellable);

  while (g_task_get_task_data (task) != NULL)
    g_main_context_iteration (NULL, TRUE);

  g_clear_object (&task);
}

static void
test_gdbus_proxy_create (void)
{
  GDBusConnection *connection;
  DexFuture *future;
  const GValue *value;
  DexTestDbusFoo *proxy;
  GError *error = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  future = dex_test_dbus_foo_proxy_new_future (connection,
                                               G_DBUS_PROXY_FLAGS_NONE,
                                               "org.example.Foo",
                                               "/org/example/foo");

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_OBJECT (value));
  proxy = g_value_get_object (value);
  g_assert_true (DEX_TEST_DBUS_IS_FOO (proxy));

  g_clear_error (&error);
  dex_clear (&future);
  g_clear_object (&connection);
}

static void
test_gdbus_method_call_simple (void)
{
  GDBusConnection *connection;
  DexFuture *future;
  const GValue *value;
  DexTestDbusFoo *proxy;
  GError *error = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  proxy = dex_test_dbus_foo_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.example.Foo",
                                            "/org/example/foo",
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  future = dex_test_dbus_foo_call_foo_future (proxy);

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_error (error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN);
  g_assert_null (value);

  g_clear_error (&error);
  dex_clear (&future);
  g_clear_object (&proxy);
  g_clear_object (&connection);
}

static void
test_gdbus_method_call_result (void)
{
  GDBusConnection *connection;
  DexFuture *future;
  const GValue *value;
  DexTestDbusFoo *proxy;
  GTask *foo_service;
  GError *error = NULL;
  char *name;

  foo_service = run_foo_service ();

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  proxy = dex_test_dbus_foo_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.example.Foo",
                                            "/org/example/foo",
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  while (!(name = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy))))
    g_main_context_iteration (NULL, TRUE);
  g_free (name);

  future = dex_test_dbus_foo_call_foo_future (proxy);

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_BOOLEAN (value));
  g_assert_true (g_value_get_boolean (value));

  g_clear_error (&error);
  dex_clear (&future);
  g_clear_object (&proxy);
  g_clear_object (&connection);
  stop_foo_service (foo_service);
}

static void
test_gdbus_method_call_cancel (void)
{
  GDBusConnection *connection;
  DexFuture *future;
  DexPromise *promise;
  const GValue *value;
  DexTestDbusFoo *proxy;
  GTask *foo_service;
  GError *error = NULL;
  char *name;

  foo_service = run_foo_service ();

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  proxy = dex_test_dbus_foo_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.example.Foo",
                                            "/org/example/foo",
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  while (!(name = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy))))
    g_main_context_iteration (NULL, TRUE);
  g_free (name);

  promise = dex_promise_new ();
  future = dex_future_first (DEX_FUTURE (promise),
                             dex_test_dbus_foo_call_foo_future (proxy),
                             NULL);

  dex_promise_reject (promise, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled"));

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_null (value);

  g_clear_error (&error);
  dex_clear (&future);
  g_clear_object (&proxy);
  g_clear_object (&connection);
  stop_foo_service (foo_service);
}

static void
test_gdbus_method_call_complex (void)
{
  GTask *foo_service;
  GDBusConnection *connection;
  DexTestDbusFoo *proxy;
  DexFuture *future;
  const GValue *value;
  DexTestDbusFooBarResult *result;
  GError *error = NULL;
  char *name;

  foo_service = run_foo_service ();

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  proxy = dex_test_dbus_foo_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.example.Foo",
                                            "/org/example/foo",
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  while (!(name = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy))))
    g_main_context_iteration (NULL, TRUE);
  g_free (name);

  future = dex_test_dbus_foo_call_bar_future (proxy,
                                              (const gchar * []) {
                                                "foo",
                                                "bar",
                                                NULL,
                                              },
                                              42);

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_BOXED (value));
  result = g_value_get_boxed (value);

  {
    uint32_t foo;
    gboolean bar;

    g_assert_nonnull (result);
    g_assert_cmpint (result->o1, == , 42);

    g_assert_nonnull (result->o2);
    g_assert_true (g_variant_lookup (result->o2, "foo", "u", &foo));
    g_assert_cmpint (foo, == , 3);
    g_assert_true (g_variant_lookup (result->o2, "bar", "b", &bar));
    g_assert_true (!bar);
  }

  g_clear_error (&error);
  dex_clear (&future);
  g_clear_object (&proxy);
  g_clear_object (&connection);
  stop_foo_service (foo_service);
}

static void
test_gdbus_signal_wait_simple (void)
{
  GTask *foo_service;
  GDBusConnection *connection;
  DexTestDbusFoo *proxy;
  DexFuture *future;
  const GValue *value;
  DexTestDbusFooBazSignal *result;
  GError *error = NULL;
  char *name;

  foo_service = run_foo_service ();

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  proxy = dex_test_dbus_foo_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.example.Foo",
                                            "/org/example/foo",
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  while (!(name = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy))))
    g_main_context_iteration (NULL, TRUE);
  g_free (name);

  future = dex_test_dbus_foo_wait_baz_future (proxy);

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_BOXED (value));
  result = g_value_get_boxed (value);

  {
    uint32_t foo;
    gboolean bar;

    g_assert_nonnull (result);
    g_assert_cmpint (result->s1, == , 11);

    g_assert_nonnull (result->s2);
    g_assert_true (g_variant_lookup (result->s2, "foo", "u", &foo));
    g_assert_cmpint (foo, == , 3);
    g_assert_true (g_variant_lookup (result->s2, "bar", "b", &bar));
    g_assert_true (!bar);
  }

  g_clear_error (&error);
  dex_clear (&future);
  g_clear_object (&proxy);
  g_clear_object (&connection);
  stop_foo_service (foo_service);
}

static void
test_gdbus_signal_wait_cancel (void)
{
  GTask *foo_service;
  GDBusConnection *connection;
  DexTestDbusFoo *proxy;
  DexPromise *promise;
  DexFuture *future;
  const GValue *value;
  GError *error = NULL;
  char *name;

  foo_service = run_foo_service ();

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  proxy = dex_test_dbus_foo_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.example.Foo",
                                            "/org/example/foo",
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  while (!(name = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy))))
    g_main_context_iteration (NULL, TRUE);
  g_free (name);

  promise = dex_promise_new ();
  future = dex_future_first (DEX_FUTURE (promise),
                             dex_test_dbus_foo_wait_baz_future (proxy),
                             NULL);

  dex_promise_reject (promise, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled"));

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_null (value);

  g_clear_error (&error);
  dex_clear (&future);
  g_clear_object (&proxy);
  g_clear_object (&connection);
  stop_foo_service (foo_service);
}

static void
test_gdbus_signal_monitor_basic (void)
{
  GTask *foo_service;
  GDBusConnection *connection;
  DexTestDbusFoo *proxy;
  DexFuture *future;
  const GValue *value;
  DexTestDbusFooSignalMonitor *signal_monitor;
  GError *error = NULL;
  char *name;

  foo_service = run_foo_service ();

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  proxy = dex_test_dbus_foo_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.example.Foo",
                                            "/org/example/foo",
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  while (!(name = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy))))
    g_main_context_iteration (NULL, TRUE);
  g_free (name);

  signal_monitor = dex_test_dbus_foo_signal_monitor_new (proxy, DEX_TEST_DBUS_FOO_SIGNAL_FOO_BAR);

  future = dex_test_dbus_foo_call_emit_foo_bar_future (proxy);

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);
  dex_clear (&future);

  future = dex_test_dbus_foo_signal_monitor_next_foo_bar (signal_monitor);
  g_assert_true (dex_future_get_status (future) == DEX_FUTURE_STATUS_RESOLVED);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_BOOLEAN (value));
  g_assert_true (g_value_get_boolean (value));

  dex_clear (&future);
  future = dex_test_dbus_foo_signal_monitor_next_foo_bar (signal_monitor);
  g_assert_true (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING);

  g_clear_error (&error);
  dex_clear (&future);
  g_clear_object (&signal_monitor);
  g_clear_object (&proxy);
  g_clear_object (&connection);
  stop_foo_service (foo_service);
}

static void
test_gdbus_signal_monitor_cancel (void)
{
  GTask *foo_service;
  GDBusConnection *connection;
  DexTestDbusFoo *proxy;
  DexFuture *future;
  DexPromise *promise;
  const GValue *value;
  DexTestDbusFooSignalMonitor *signal_monitor;
  GError *error = NULL;
  char *name;

  foo_service = run_foo_service ();

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  proxy = dex_test_dbus_foo_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.example.Foo",
                                            "/org/example/foo",
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  while (!(name = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy))))
    g_main_context_iteration (NULL, TRUE);
  g_free (name);

  signal_monitor = dex_test_dbus_foo_signal_monitor_new (proxy, DEX_TEST_DBUS_FOO_SIGNAL_FOO_BAR);

  promise = dex_promise_new ();
  future = dex_future_first (DEX_FUTURE (promise),
                             dex_test_dbus_foo_signal_monitor_next_foo_bar (signal_monitor),
                             NULL);

  dex_promise_reject (promise, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled"));

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_null (value);

  g_clear_error (&error);
  dex_clear (&future);
  g_clear_object (&signal_monitor);
  g_clear_object (&proxy);
  g_clear_object (&connection);
  stop_foo_service (foo_service);
}

static DexFuture *
fiber_basic (gpointer user_data)
{
  GDBusConnection *connection = NULL;
  DexTestDbusFoo *proxy = NULL;
  GError *error = NULL;

  connection = dex_await_object (dex_bus_get (G_BUS_TYPE_SESSION), &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  proxy = dex_await_object (dex_test_dbus_foo_proxy_new_future (connection,
                                                                G_DBUS_PROXY_FLAGS_NONE,
                                                                "org.example.Foo",
                                                                "/org/example/foo"),
                            &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  {
    gboolean foo_result;

    foo_result = dex_await_boolean (dex_test_dbus_foo_call_foo_future (proxy), &error);
    g_assert_no_error (error);
    g_assert_true (foo_result);
  }

  {
    DexTestDbusFooBazSignal *baz_result;
    uint32_t foo;
    gboolean bar;

    baz_result = dex_await_boxed (dex_test_dbus_foo_wait_baz_future (proxy), &error);
    g_assert_no_error (error);
    g_assert_nonnull (baz_result);

    g_assert_cmpint (baz_result->s1, == , 11);
    g_assert_nonnull (baz_result->s2);
    g_assert_true (g_variant_lookup (baz_result->s2, "foo", "u", &foo));
    g_assert_cmpint (foo, == , 3);
    g_assert_true (g_variant_lookup (baz_result->s2, "bar", "b", &bar));
    g_assert_true (!bar);

    g_clear_pointer (&baz_result, dex_test_dbus_foo_baz_signal_free);
  }

  {
    DexTestDbusFooSignalMonitor *signal_monitor;
    gboolean result;

    signal_monitor = dex_test_dbus_foo_signal_monitor_new (proxy, DEX_TEST_DBUS_FOO_SIGNAL_FOO_BAR);

    result = dex_await_boolean (dex_test_dbus_foo_call_emit_foo_bar_future (proxy), &error);
    g_assert_no_error (error);
    g_assert_true (result);

    result = dex_await_boolean (dex_test_dbus_foo_signal_monitor_next_foo_bar (signal_monitor), &error);
    g_assert_no_error (error);
    g_assert_true (result);

    g_clear_object (&signal_monitor);
  }

  return dex_future_new_true ();
}

static void
test_gdbus_fiber_basic (void)
{
  GTask *foo_service;
  DexFuture *future;
  const GValue *value;
  GError *error = NULL;

  foo_service = run_foo_service ();

  future = dex_scheduler_spawn (NULL, 0, fiber_basic, NULL, NULL);
  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_BOOLEAN (value));
  g_assert_true (g_value_get_boolean (value));

  g_clear_error (&error);
  dex_clear (&future);
  stop_foo_service (foo_service);
}

static DexFuture *
fiber_service (gpointer user_data)
{
  GDBusConnection *connection = NULL;
  DexTestFoo *foo = NULL;
  DexFuture *name_acquired = NULL;
  DexFuture *name_lost = NULL;
  GError *error = NULL;

  connection = dex_await_object (dex_bus_get (G_BUS_TYPE_SESSION), &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  dex_bus_own_name_on_connection (connection,
                                  "org.example.Foo",
                                  G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                  G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                  &name_acquired,
                                  &name_lost);

  dex_await (g_steal_pointer (&name_acquired), &error);
  g_assert_no_error (error);

  foo = g_object_new (DEX_TEST_TYPE_FOO, NULL);
  dex_dbus_interface_skeleton_set_flags (DEX_DBUS_INTERFACE_SKELETON (foo),
                                         DEX_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_FIBER);

  g_assert_true (g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (foo),
                                                   connection,
                                                   "/org/example/foo",
                                                   &error));
  g_assert_no_error (error);

  dex_await (g_steal_pointer (&name_lost), &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);

  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (foo));
  dex_dbus_interface_skeleton_cancel (DEX_DBUS_INTERFACE_SKELETON (foo));

  return dex_future_new_true ();
}

static void
call_fiber_cb (GObject      *source_object,
               GAsyncResult *res,
               gpointer      data)
{
  gboolean *done = data;
  GError *error = NULL;

  g_assert_true (dex_test_dbus_foo_call_fiber_finish (DEX_TEST_DBUS_FOO (source_object), res, &error));
  g_assert_no_error (error);

  *done = TRUE;
}

static void
test_gdbus_fiber_service (void)
{
  GTask *foo_service;
  GDBusConnection *connection;
  DexTestDbusFoo *proxy;
  DexFuture *future;
  const GValue *value;
  GError *error = NULL;
  char *name;

  future = dex_scheduler_spawn (NULL, 0, fiber_service, NULL, NULL);

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  proxy = dex_test_dbus_foo_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.example.Foo",
                                            "/org/example/foo",
                                            NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (proxy);

  while (!(name = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy))))
    g_main_context_iteration (NULL, TRUE);
  g_free (name);

  {
    gboolean done = FALSE;
    dex_test_dbus_foo_call_fiber (proxy, NULL, call_fiber_cb, &done);

    while (!done)
      g_main_context_iteration (NULL, TRUE);
  }

  /* kick the fiber service off the bus */
  foo_service = run_foo_service ();

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_BOOLEAN (value));
  g_assert_true (g_value_get_boolean (value));

  g_clear_error (&error);
  dex_clear (&future);
  stop_foo_service (foo_service);
}

int
main (int argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/GDBus/proxy/create", test_gdbus_proxy_create);
  g_test_add_func ("/Dex/TestSuite/GDBus/method_call/simple", test_gdbus_method_call_simple);
  g_test_add_func ("/Dex/TestSuite/GDBus/method_call/result", test_gdbus_method_call_result);
  g_test_add_func ("/Dex/TestSuite/GDBus/method_call/cancel", test_gdbus_method_call_cancel);
  g_test_add_func ("/Dex/TestSuite/GDBus/method_call/complex", test_gdbus_method_call_complex);
  g_test_add_func ("/Dex/TestSuite/GDBus/signal_wait/simple", test_gdbus_signal_wait_simple);
  g_test_add_func ("/Dex/TestSuite/GDBus/signal_wait/cancel", test_gdbus_signal_wait_cancel);
  g_test_add_func ("/Dex/TestSuite/GDBus/signal_monitor/basic", test_gdbus_signal_monitor_basic);
  g_test_add_func ("/Dex/TestSuite/GDBus/signal_monitor/cancel", test_gdbus_signal_monitor_cancel);
  g_test_add_func ("/Dex/TestSuite/GDBus/fiber/basic", test_gdbus_fiber_basic);
  g_test_add_func ("/Dex/TestSuite/GDBus/fiber/service", test_gdbus_fiber_service);
  return g_test_run ();
}
