/*
 * dbus.c
 *
 * Copyright 2025 Red Hat, Inc.
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

#include <libdex.h>


#include "dex-dbus-ping-pong.h"

struct _DexPingPong
{
  DexDbusPingPongSkeleton parent_instance;
};

struct DexPingPongClass
{
  DexDbusPingPongSkeletonClass parent_class;
};

static void dex_dbus_ping_pong_iface_init (DexDbusPingPongIface *iface);

#define DEX_TYPE_PING_PONG (dex_ping_pong_get_type ())
G_DECLARE_FINAL_TYPE (DexPingPong,
                      dex_ping_pong,
                      DEX, PING_PONG,
                      DexDbusPingPongSkeleton)

G_DEFINE_TYPE_WITH_CODE (DexPingPong,
                         dex_ping_pong,
                         DEX_DBUS_TYPE_PING_PONG_SKELETON,
                         G_IMPLEMENT_INTERFACE (DEX_DBUS_TYPE_PING_PONG,
                                                dex_dbus_ping_pong_iface_init));

static gboolean
handle_ping (DexDbusPingPong       *object,
             GDBusMethodInvocation *invocation,
             const char            *ping)
{
  g_print ("service: %s\n", ping);

  dex_await (dex_timeout_new_seconds (1), NULL);
  dex_dbus_ping_pong_complete_ping (object, invocation, "pong");

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
dex_dbus_ping_pong_iface_init (DexDbusPingPongIface *iface)
{
  iface->handle_ping = handle_ping;
}

static void
dex_ping_pong_init (DexPingPong *pp G_GNUC_UNUSED)
{
}

static void
dex_ping_pong_class_init (DexPingPongClass *klass G_GNUC_UNUSED)
{
}

static DexFuture *
emit_reloading_signals_fiber (gpointer user_data)
{
  DexDbusPingPong *pp = DEX_DBUS_PING_PONG (user_data);

  while (TRUE)
    {
      g_auto(GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
      g_autoptr(GError) error = NULL;

      g_variant_builder_add (&builder, "{sv}", "test",
                             g_variant_new_int32 (g_random_int_range (0, 100)));

      dex_dbus_ping_pong_emit_reloading (pp, TRUE, g_variant_builder_end (&builder));

      if (!dex_await (dex_timeout_new_seconds (g_random_int_range (1, 4)), &error) &&
          !g_error_matches (error, DEX_ERROR, DEX_ERROR_TIMED_OUT))
        return dex_future_new_for_error (g_steal_pointer (&error));
    }
  g_assert_not_reached ();
}

static DexFuture *
run_service_fiber (gpointer user_data G_GNUC_UNUSED)
{
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(DexFuture) name_acquired = NULL;
  g_autoptr(DexFuture) name_lost = NULL;
  g_autoptr(DexPingPong) pp = NULL;
  g_autoptr(GError) error = NULL;

  connection = dex_await_object (dex_bus_get (G_BUS_TYPE_SESSION), &error);
  if (!connection)
    return dex_future_new_for_error (g_steal_pointer (&error));

  dex_bus_own_name_on_connection (connection,
                                  "org.example.PingPong",
                                  G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                  G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                  &name_acquired,
                                  &name_lost);

  if (!dex_await (g_steal_pointer (&name_acquired), &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  pp = g_object_new (DEX_TYPE_PING_PONG, NULL);
  dex_dbus_interface_skeleton_set_flags (DEX_DBUS_INTERFACE_SKELETON (pp),
                                         DEX_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_FIBER);

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (pp),
                                         connection,
                                         "/org/example/pingpong",
                                         &error))
    return dex_future_new_for_error (g_steal_pointer (&error));



  dex_await (dex_future_first (g_steal_pointer (&name_lost),
                               dex_scheduler_spawn (NULL, 0,
                                                    emit_reloading_signals_fiber,
                                                    g_object_ref (pp),
                                                    g_object_unref),
                               NULL),
             NULL);

  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (pp));
  dex_dbus_interface_skeleton_cancel (DEX_DBUS_INTERFACE_SKELETON (pp));

  return dex_future_new_true ();
}

static DexFuture *
ping_fiber (gpointer user_data)
{
  g_autoptr(DexDbusPingPong) pp = g_object_ref (user_data);

  while (TRUE)
    {
      g_autoptr(DexDbusPingPongPingResult) res = NULL;
      g_autoptr(GError) error = NULL;

      dex_await (dex_timeout_new_seconds (1), NULL);

      res = dex_await_boxed (dex_dbus_ping_pong_call_ping_future (pp, "ping"), &error);
      if (!res)
        return dex_future_new_for_error (g_steal_pointer (&error));

      g_print ("client: %s\n", res->pong);
    }
  g_assert_not_reached ();
}

static DexFuture *
wait_for_reload_fiber (gpointer user_data)
{
  g_autoptr(DexDbusPingPong) pp = user_data;
  g_autoptr(DexDbusPingPongSignalMonitor) signal_monitor =
    dex_dbus_ping_pong_signal_monitor_new (pp, DEX_DBUS_PING_PONG_SIGNAL_RELOADING);

  while (TRUE)
    {
      g_autoptr(DexDbusPingPongReloadingSignal) reloading = NULL;
      g_autoptr(GError) error = NULL;
      int test;

      reloading = dex_await_boxed (dex_future_first (dex_dbus_ping_pong_signal_monitor_next_reloading (signal_monitor),
                                                     dex_timeout_new_seconds (10),
                                                     NULL),
                                   &error);
      if (!reloading)
        return dex_future_new_for_error (g_steal_pointer (&error));

      g_variant_lookup (reloading->options, "test", "i", &test);
      g_print ("signal: received reloading, active = %d, options[test] = %d\n",
               reloading->active,
               test);
    }
  g_assert_not_reached ();
}

static DexFuture *
run_client_fiber (gpointer user_data G_GNUC_UNUSED)
{
  g_autoptr(DexDbusPingPong) pp = NULL;

  {
    g_autoptr(GDBusConnection) connection = NULL;
    g_autoptr(GError) error = NULL;

    connection = dex_await_object (dex_bus_get (G_BUS_TYPE_SESSION), &error);
    if (!connection)
      return dex_future_new_for_error (g_steal_pointer (&error));

    pp = dex_await_object (dex_dbus_ping_pong_proxy_new_future (connection,
                                                                G_DBUS_PROXY_FLAGS_NONE,
                                                                "org.example.PingPong",
                                                                "/org/example/pingpong"),
                           &error);
    if (!pp)
      return dex_future_new_for_error (g_steal_pointer (&error));
  }

  {
    g_autoptr(GError) error = NULL;

    if (!dex_await (dex_future_all (dex_scheduler_spawn (NULL, 0,
                                                         ping_fiber,
                                                         pp, NULL),
                                    dex_scheduler_spawn (NULL, 0,
                                                         wait_for_reload_fiber,
                                                         pp, NULL),
                                    NULL),
                    &error))
      return dex_future_new_for_error (g_steal_pointer (&error));
  }

  return dex_future_new_true ();
}

static DexFuture *
quit_cb (DexFuture *future,
         gpointer   user_data)
{
  GMainLoop *main_loop = user_data;
  g_autoptr (GError) error = NULL;

  if (!dex_await (dex_ref (future), &error))
    g_printerr ("%s\n", error->message);

  while (g_main_context_iteration (g_main_loop_get_context (main_loop), FALSE));
  g_main_loop_quit (main_loop);

  return NULL;
}

int
main (int    argc G_GNUC_UNUSED,
      char **argv G_GNUC_UNUSED)
{
  g_autoptr(GMainLoop) main_loop = NULL;
  g_autoptr(DexScheduler) thread_pool = NULL;
  g_autoptr(DexFuture) future = NULL;

  dex_init ();

  main_loop = g_main_loop_new (NULL, FALSE);
  thread_pool = dex_thread_pool_scheduler_new ();

  future = dex_future_first (dex_scheduler_spawn (NULL, 0,
                                                  run_service_fiber,
                                                  NULL, NULL),
                             dex_scheduler_spawn (NULL, 0,
                                                  run_client_fiber,
                                                  NULL, NULL),
                             dex_unix_signal_new (SIGINT),
                             NULL);
  future = dex_future_finally (future, quit_cb, main_loop, NULL);

  g_main_loop_run (main_loop);

  return EXIT_SUCCESS;
}
