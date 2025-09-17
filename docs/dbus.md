Title: D-Bus

By default, integration exists for D-Bus to connect to a bus, own a name, and perform method calls, asynchronously.

See [func@Dex.bus_get], [func@Dex.bus_own_name_on_connection], [func@Dex.dbus_connection_call], [func@Dex.dbus_connection_call_with_unix_fd_list], [func@Dex.dbus_connection_send_message_with_reply] and [func@Dex.dbus_connection_close].

# Enabling GDBus codegen

Integration with GDBus based codegen can be enabled, by calling `gdbus-codegen` with `--extension-path=path/to/dex-gdbus-codegen-extension.py`.
The installed file path can be queried via `pkgconf --variable=gdbus_codegen_extension libdex-1`:

```meson
libdex_dep = dependency('libdex-1')

ext = libdex_dep.get_pkgconfig_variable('gdbus_codegen_extension')
dbus_ping_pong = gnome.gdbus_codegen(
  'dex-dbus-ping-pong',
  sources: ['org.example.PingPong.xml'],
  interface_prefix: 'org.example',
  namespace: 'DexDbus',
  extra_args: [f'--extension-path=@ext@'],
)
```

# Proxy Futures

With the GDBus codegen enabled, for every proxy, a `${name}_new_future` function is generated in addition to the sync and async/finish variants. The returned future resolves into the proxy object.

```c
  g_autoptr(DexDbusPingPong) *pp = NULL;
  pp = dex_await_object (dex_dbus_ping_pong_proxy_new_future (connection,
                                                              G_DBUS_PROXY_FLAGS_NONE,
                                                              "org.example.PingPong",
                                                              "/org/example/pingpong"),
                         &error);
```

For every method, a `${name}_call_${method}_future` function is generated in addition to the sync and async/finish variants. The returned future resolves into the boxed type `${Name}${Method}Result` which contains the results of the call.

```c
  g_autoptr(DexDbusPingPongPingResult) result = NULL;

  result = dex_await_boxed (dex_dbus_ping_pong_call_ping_future (pp, "ping"), &error);
  g_assert (result);
  g_print ("%s\n", result->pong);
```

For every signal, a `${name}_wait_${signal}_future` function is generated. The future gets resolved when the signal got emitted, and the future resolves into the boxed type `${Name}${Signal}Signal` which contains the results of the signal emission.

```c
  g_autoptr(DexDbusPingPongReloadingSignal) signal = NULL;

  signal = dex_await_boxed (dex_dbus_ping_pong_wait_reloading_future (pp), &error);
  g_assert (signal);
  g_print ("%s\n", signal->active);
```

For every `Dex.DBusInterfaceSkeleton`, a corresponding `${Name}SignalMonitor` class is generated.
Objects of the class will listen to the specified signals, and a call to ``${Name}SignalMonitor::next${signal}` returns a future that will resolve either immediately when a signal was emitted previously, or when the next signal arrives.
This can be useful when it is important to not miss signal emissions.

```c
  g_autoptr(DexDbusPingPongSignalMonitor) signal_monitor =
    dex_dbus_ping_pong_signal_monitor_new (pp, DEX_DBUS_PING_PONG_SIGNAL_RELOADING);

  signal = dex_await_boxed (dex_dbus_ping_pong_signal_monitor_next_reloading (signal_monitor), &error);
  g_assert (signal);
  g_print ("%s\n", signal->active);
```

# InterfaceSkeleton Fiber Dispatching

With the GDBus codegen enabled, all generated `${Name}Skeleton`s that application code derives from to implement a service derive from `DexDBusInterfaceSkeleton` instead of directly from `GDBusInterfaceSkeleton`.
This allows application code to enable handling method invocations in fibers, by setting [flags@Dex.DBusInterfaceSkeletonFlags.HANDLE_METHOD_INVOCATIONS_IN_FIBER] with [method@Dex.DBusInterfaceSkeleton.set_flags].

```c
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

static DexPingPong *
get_ping_pong (void)
{
  g_autoptr(DexPingPong) pp = NULL;
  pp = g_object_new (DEX_TYPE_PING_PONG, NULL);
  dex_dbus_interface_skeleton_set_flags (DEX_DBUS_INTERFACE_SKELETON (pp),
                                         DEX_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_FIBER);
  return g_steal_pointer (&pp);
}
```
