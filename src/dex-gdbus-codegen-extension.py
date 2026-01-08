# dex gdbus-codegen extension
#
# Copyright 2025 Red Hat, Inc.
#
# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: LGPL-2.1-or-later


# This is supposed to be loaded as an extension to gdbus-codegen, by invoking
# gdbus-codegen --extension-path=path/to/dex-gdbus-codegen-extension.py


def init(args, options):
    # We can require a certain glib version which will support at least
    # some version of the codegen, but we might also have a newer version
    # which breaks backwards compatibility.
    # Here we just ensure that the glib codegen is compatible with this
    # extension. If a new version gets released, we have to somehow adjust
    # the code accordingly, but ensure it still works with older versions.
    assert options["version"] <= 1
    pass

class HeaderCodeGenerator:
    def __init__(self, generator):
        self.ifaces = generator.ifaces
        self.outfile = generator.outfile
        self.symbol_decorator = generator.symbol_decorator
        self.generate_autocleanup = generator.generate_autocleanup
        self.glib_min_required = generator.glib_min_required

        generator.skeleton_type_camel = "DexDBusInterfaceSkeleton"

    def generate_method_call(self, i, m):
        if len(m.out_args) > 0:
            result_type = f"{i.camel_name}{m.name}Result"
            function_prefix = f"{i.name_lower}_{m.name_lower}_result"
            type_name = f"{i.ns_upper}TYPE_{i.name_upper}_{m.name_upper}_RESULT"

            self.outfile.write("\n")
            self.outfile.write("typedef struct _%s\n" % result_type)
            self.outfile.write("{\n")
            for a in m.out_args:
                self.outfile.write("  %s %s;\n" % (a.ctype_out, a.name))
            if m.unix_fd:
                self.outfile.write("  GUnixFDList *fd_list;\n")
            self.outfile.write("} %s;\n" % result_type)
            self.outfile.write("\n")

            self.outfile.write(
                "#define %s (%s_get_type ())\n" % (type_name, function_prefix)
            )
            if self.symbol_decorator is not None:
                self.outfile.write("%s\n" % self.symbol_decorator)
            self.outfile.write(
                "GType %s_get_type (void) G_GNUC_CONST;\n" % function_prefix
            )

            self.outfile.write("\n")
            if self.symbol_decorator is not None:
                self.outfile.write("%s\n" % self.symbol_decorator)
            self.outfile.write(
                "%s * %s_copy (%s *result);\n"
                % (result_type, function_prefix, result_type)
            )
            self.outfile.write(
                "void %s_free (%s *result);\n" % (function_prefix, result_type)
            )

            if self.generate_autocleanup in ("objects", "all"):
                self.outfile.write(
                    "G_DEFINE_AUTOPTR_CLEANUP_FUNC (%s, %s_free)\n"
                    % (result_type, function_prefix)
                )
                self.outfile.write("\n")

            self.outfile.write("\n")

        if self.symbol_decorator is not None:
            self.outfile.write("%s\n" % self.symbol_decorator)
        if m.deprecated:
            self.outfile.write("G_GNUC_DEPRECATED ")
        self.outfile.write(
            "DexFuture * %s_call_%s_future (\n"
            "    %s *proxy" % (i.name_lower, m.name_lower, i.camel_name)
        )
        for a in m.in_args:
            self.outfile.write(",\n    %sarg_%s" % (a.ctype_in, a.name))
        if self.glib_min_required >= (2, 64):
            self.outfile.write(
                ",\n    GDBusCallFlags call_flags"
                ",\n    gint timeout_msec"
            )
        if m.unix_fd:
            self.outfile.write(",\n    GUnixFDList *fd_list")
        self.outfile.write(");\n")
        self.outfile.write("\n")

    def generate_proxy(self, i):
        if self.symbol_decorator is not None:
            self.outfile.write("%s\n" % self.symbol_decorator)
        if i.deprecated:
            self.outfile.write("G_GNUC_DEPRECATED ")
        self.outfile.write(
            "DexFuture * %s_proxy_new_future (\n"
            "    GDBusConnection     *connection,\n"
            "    GDBusProxyFlags      flags,\n"
            "    const gchar         *name,\n"
            "    const gchar         *object_path);\n" % (i.name_lower)
        )

    def generate_signal_wait(self, i, s):
        if len(s.args) > 0:
            signal_type = f"{i.camel_name}{s.name}Signal"
            function_prefix = f"{i.name_lower}_{s.name_lower}_signal"
            type_name = f"{i.ns_upper}TYPE_{i.name_upper}_{s.name_upper}_SIGNAL"

            self.outfile.write("\n")
            self.outfile.write("typedef struct _%s\n" % signal_type)
            self.outfile.write("{\n")
            for a in s.args:
                self.outfile.write("  %s %s;\n" % (a.ctype_out, a.name))
            # FIXME: fd passing?
            self.outfile.write("} %s;\n" % signal_type)
            self.outfile.write("\n")

            self.outfile.write(
                "#define %s (%s_get_type ())\n" % (type_name, function_prefix)
            )
            if self.symbol_decorator is not None:
                self.outfile.write("%s\n" % self.symbol_decorator)
            self.outfile.write(
                "GType %s_get_type (void) G_GNUC_CONST;\n" % function_prefix
            )

            self.outfile.write("\n")
            if self.symbol_decorator is not None:
                self.outfile.write("%s\n" % self.symbol_decorator)
            self.outfile.write(
                "%s * %s_copy (%s *signal);\n"
                % (signal_type, function_prefix, signal_type)
            )
            self.outfile.write(
                "void %s_free (%s *signal);\n" % (function_prefix, signal_type)
            )

            if self.generate_autocleanup in ("objects", "all"):
                self.outfile.write(
                    "G_DEFINE_AUTOPTR_CLEANUP_FUNC (%s, %s_free)\n"
                    % (signal_type, function_prefix)
                )
                self.outfile.write("\n")

            self.outfile.write("\n")

        if self.symbol_decorator is not None:
            self.outfile.write("%s\n" % self.symbol_decorator)
        if s.deprecated:
            self.outfile.write("G_GNUC_DEPRECATED ")
        self.outfile.write(
            "DexFuture * %s_wait_%s_future (%s *proxy);\n"
            % (i.name_lower, s.name_lower, i.camel_name)
        )
        self.outfile.write("\n")

    def generate_signal_monitor(self, i):
        if len(i.signals) == 0:
            return

        self.outfile.write(
            "typedef enum _%sSignals\n"
            "{\n"
            % i.camel_name
        )
        for n, s in enumerate(i.signals):
            self.outfile.write(
                "  %s%s_SIGNAL_%s = (%d << 0),\n"
                % (i.ns_upper, i.name_upper, s.name_upper, n + 1)
            )
        self.outfile.write(
            "} %sSignals;\n\n"
            % i.camel_name
        )

        self.outfile.write(
            "struct _%sSignalMonitor\n"
            "{\n"
            "  GObject parent_instance;\n"
            "\n"
            "  %s *object;\n"
            % (i.camel_name, i.camel_name)
        )
        for s in i.signals:
            self.outfile.write(
                "  DexChannel *%s_channel;\n"
                "  gulong %s_signal_id;\n"
                % (s.name_lower, s.name_lower)
            )
        self.outfile.write("};\n\n")

        self.outfile.write(
            "#define %sTYPE_%s_SIGNAL_MONITOR (%s_signal_monitor_get_type())\n"
            % (i.ns_upper, i.name_upper, i.name_lower)
        )

        self.outfile.write(
            "G_DECLARE_FINAL_TYPE (%sSignalMonitor,\n"
            "                      %s_signal_monitor,\n"
            "                      %s, %s_SIGNAL_MONITOR,\n"
            "                      GObject)\n\n"
            % (i.camel_name, i.name_lower, i.ns_upper[:-1], i.name_upper)
        )

        self.outfile.write(
            "%sSignalMonitor *\n"
            "%s_signal_monitor_new (\n"
            "  %s *object,\n"
            "  %sSignals signals);\n\n"
            % (i.camel_name, i.name_lower, i.camel_name, i.camel_name)
        )
        self.outfile.write(
            "void\n"
            "%s_signal_monitor_cancel (%sSignalMonitor *self);\n\n"
            % (i.name_lower, i.camel_name)
        )
        for s in i.signals:
            self.outfile.write(
                "DexFuture *\n"
                "%s_signal_monitor_next_%s (%sSignalMonitor *self);\n\n"
                % (i.name_lower, s.name_lower, i.camel_name)
            )

    def generate_includes(self):
        self.outfile.write("#include <libdex.h>\n")

    def declare_types(self):
        for i in self.ifaces:
            self.generate_proxy(i)
            for m in i.methods:
                self.generate_method_call(i, m)
            for s in i.signals:
                self.generate_signal_wait(i, s)
            self.generate_signal_monitor(i)


class CodeGenerator:
    def __init__(self, generator):
        self.ifaces = generator.ifaces
        self.outfile = generator.outfile
        self.symbol_decoration_define = generator.symbol_decoration_define
        self.glib_min_required = generator.glib_min_required
        self.docbook_gen = generator.docbook_gen
        self.write_gtkdoc_deprecated_and_since_and_close = generator.write_gtkdoc_deprecated_and_since_and_close

        generator.skeleton_type_upper = "DEX_TYPE_DBUS_INTERFACE_SKELETON"

    def generate_proxy(self, i):
        self.outfile.write(
            "static void\n"
            "%s_proxy_new_future_cb (\n"
            "    GObject      *object G_GNUC_UNUSED,\n"
            "    GAsyncResult *result,\n"
            "    gpointer      user_data)\n"
            "{\n"
            "  DexPromise *promise = DEX_PROMISE (user_data);\n"
            "  %s *proxy;\n"
            "  GError *error = NULL;\n"
            "  G_GNUC_BEGIN_IGNORE_DEPRECATIONS\n"
            "  proxy = %s_proxy_new_finish (result, &error);\n"
            "  G_GNUC_END_IGNORE_DEPRECATIONS\n"
            "  if (proxy == NULL)\n"
            "    dex_promise_reject (promise, error);\n"
            "  else\n"
            "    dex_promise_resolve_object (promise, proxy);\n"
            "}\n"
            "\n" % (i.name_lower, i.camel_name, i.name_lower)
        )
        self.outfile.write(
            self.docbook_gen.expand(
                "/**\n"
                " * %s_proxy_new_future:\n"
                " * @connection: A #GDBusConnection.\n"
                " * @flags: Flags from the #GDBusProxyFlags enumeration.\n"
                " * @name: (nullable): A bus name (well-known or unique) or %%NULL if @connection is not a message bus connection.\n"
                " * @object_path: An object path.\n"
                " *\n"
                " * Returns a future which resolves to a proxy for the D-Bus interface #%s. See g_dbus_proxy_new() for more details.\n"
                " *\n"
                " * See %s_proxy_new_sync() for the synchronous, blocking version of this constructor.\n"
                % (i.name_lower, i.name, i.name_lower),
                False,
            )
        )
        self.write_gtkdoc_deprecated_and_since_and_close(i, self.outfile, 0)
        self.outfile.write(
            "DexFuture *\n"
            "%s_proxy_new_future (\n"
            "    GDBusConnection *connection,\n"
            "    GDBusProxyFlags  flags,\n"
            "    const gchar     *name,\n"
            "    const gchar     *object_path)\n"
            "{\n"
            "  DexPromise *promise = dex_promise_new_cancellable ();\n"
            "  G_GNUC_BEGIN_IGNORE_DEPRECATIONS\n"
            "  %s_proxy_new (\n"
            "    connection,\n"
            "    flags,\n"
            "    name,\n"
            "    object_path,\n"
            "    dex_promise_get_cancellable (promise),\n"
            "    %s_proxy_new_future_cb,\n"
            "    dex_ref (promise));\n"
            "  G_GNUC_END_IGNORE_DEPRECATIONS\n"
            "  return DEX_FUTURE (promise);\n"
            "}\n"
            "\n" % (i.name_lower, i.name_lower, i.name_lower)
        )

    def generate_method_call(self, i, m):
        result_type = f"{i.camel_name}{m.name}Result"
        result_function_prefix = f"{i.name_lower}_{m.name_lower}_result"
        result_type_name = (
            f"{i.ns_upper}TYPE_{i.name_upper}_{m.name_upper}_RESULT"
        )
        call_function_prefix = f"{i.name_lower}_call_{m.name_lower}"

        if len(m.out_args) > 0:
            self.outfile.write(
                "%s *\n"
                "%s_copy (%s *r)\n"
                "{\n"
                "  %s *n = g_new0 (%s, 1);\n"
                % (
                    result_type,
                    result_function_prefix,
                    result_type,
                    result_type,
                    result_type,
                )
            )
            for a in m.out_args:
                if a.copy_func:
                    self.outfile.write(
                        "  n->%s = r->%s ? %s (r->%s) : NULL;\n"
                        % (a.name, a.name, a.copy_func, a.name)
                    )
                else:
                    self.outfile.write("  n->%s = r->%s;\n" % (a.name, a.name))
            if m.unix_fd:
                self.outfile.write(
                    "  n->fd_list = r->fd_list ? g_object_ref (r->fd_list) : NULL;\n"
                )
            self.outfile.write("  return n;\n" "}\n" "\n")
            self.outfile.write(
                "void\n"
                "%s_free (%s *r)\n"
                "{\n" % (result_function_prefix, result_type)
            )
            for a in m.out_args:
                if a.free_func:
                    self.outfile.write(
                        "  %s (r->%s);\n" % (a.free_func, a.name)
                    )
            if m.unix_fd:
                self.outfile.write("  g_clear_object (&r->fd_list);\n")
            self.outfile.write("  free (r);\n" "}\n" "\n")
            self.outfile.write(
                "G_DEFINE_BOXED_TYPE (\n"
                "  %s,\n"
                "  %s,\n"
                "  %s_copy,\n"
                "  %s_free)\n"
                "\n"
                % (
                    result_type,
                    result_function_prefix,
                    result_function_prefix,
                    result_function_prefix,
                )
            )
        self.outfile.write(
            "static void\n"
            "%s_future_cb (\n"
            "    GObject      *object,\n"
            "    GAsyncResult *result,\n"
            "    gpointer      user_data)\n"
            "{\n"
            "  DexPromise *promise = DEX_PROMISE (user_data);\n"
            "  GError *error = NULL;\n" % call_function_prefix
        )
        if len(m.out_args) > 0:
            self.outfile.write("  %s *r;\n" % result_type)
        if m.unix_fd:
            self.outfile.write("  GUnixFDList *fd_list;\n")
        for a in m.out_args:
            self.outfile.write("  %s arg_%s;\n" % (a.ctype_out, a.name))
        self.outfile.write(
            "  G_GNUC_BEGIN_IGNORE_DEPRECATIONS\n"
            "  gboolean success = %s_finish (\n"
            "    %s%s (object),\n"
            "" % (call_function_prefix, i.ns_upper, i.name_upper)
        )
        for a in m.out_args:
            self.outfile.write("    &arg_%s,\n" % a.name)
        if m.unix_fd:
            self.outfile.write("    &fd_list,\n")
        self.outfile.write(
            "    result,\n"
            "    &error);\n"
            "  G_GNUC_END_IGNORE_DEPRECATIONS\n"
            "  if (!success)\n"
            "    return dex_promise_reject (promise, error);\n"
        )
        if len(m.out_args) > 0:
            self.outfile.write("  r = g_new0 (%s, 1);\n" % result_type)
            for a in m.out_args:
                self.outfile.write("  r->%s = arg_%s;\n" % (a.name, a.name))
            if m.unix_fd:
                self.outfile.write("  r->fd_list = fd_list;\n")
            self.outfile.write(
                "  dex_promise_resolve_boxed (\n"
                "    promise,\n"
                "    %s,\n"
                "    r);\n"
                "}\n\n" % result_type_name
            )
        else:
            self.outfile.write(
                "  dex_promise_resolve_boolean (promise, TRUE);\n" "}\n\n"
            )

        self.outfile.write(
            "/**\n"
            " * %s_future:\n"
            " * @proxy: A #%sProxy.\n" % (call_function_prefix, i.camel_name)
        )
        for a in m.in_args:
            self.outfile.write(
                " * @arg_%s: Argument to pass with the method invocation.\n"
                % (a.name)
            )
        if self.glib_min_required >= (2, 64):
            self.outfile.write(
                " * @call_flags: Flags from the #GDBusCallFlags enumeration. If you want to allow interactive\n"
                "       authorization be sure to set %G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION.\n"
                ' * @timeout_msec: The timeout in milliseconds (with %G_MAXINT meaning "infinite") or\n'
                "       -1 to use the proxy default timeout.\n"
            )
        if m.unix_fd:
            self.outfile.write(
                " * @fd_list: (nullable): A #GUnixFDList or %NULL.\n"
            )
        self.outfile.write(
            self.docbook_gen.expand(
                " *\n"
                " * Invokes the %s.%s() D-Bus method on @proxy and returns a future representing the invocation.\n"
                " *\n" % (i.name, m.name),
                False,
            )
        )
        if len(m.out_args) > 0:
            self.outfile.write(
                " * The future resolves to the boxed #%s on success.\n"
                " *\n" % result_type,
            )
        else:
            self.outfile.write(
                " * The future resolves to %TRUE on success.\n" " *\n"
            )
        self.outfile.write(
            self.docbook_gen.expand(
                " * Returns: (transfer full): The future\n"
                " *\n"
                " * See %s_call_%s_sync() for the synchronous, blocking version of this method.\n"
                % (i.name_lower, m.name_lower),
                False,
            )
        )
        self.write_gtkdoc_deprecated_and_since_and_close(m, self.outfile, 0)

        self.outfile.write(
            "DexFuture *\n"
            "%s_future (\n"
            "    %s *proxy" % (call_function_prefix, i.camel_name)
        )
        for a in m.in_args:
            self.outfile.write(",\n    %sarg_%s" % (a.ctype_in, a.name))
        if self.glib_min_required >= (2, 64):
            self.outfile.write(",\n    GDBusCallFlags call_flags" ",\n    gint timeout_msec")
        if m.unix_fd:
            self.outfile.write(",\n    GUnixFDList *fd_list")
        self.outfile.write(
            ")\n"
            "{\n"
            "  DexPromise *promise = dex_promise_new_cancellable ();\n"
            "  G_GNUC_BEGIN_IGNORE_DEPRECATIONS\n"
            "  %s (\n"
            "    proxy" % call_function_prefix
        )
        for a in m.in_args:
            self.outfile.write(",\n    arg_%s" % a.name)
        if self.glib_min_required >= (2, 64):
            self.outfile.write(
                ",\n    call_flags" ",\n    timeout_msec"
            )
        if m.unix_fd:
            self.outfile.write(",\n    fd_list")
        self.outfile.write(
            ",\n    dex_promise_get_cancellable (promise),\n"
            "    %s_future_cb,\n"
            "    dex_ref (promise));\n"
            "  G_GNUC_END_IGNORE_DEPRECATIONS\n"
            "  return DEX_FUTURE (promise);\n"
            "}\n"
            "\n" % call_function_prefix
        )

    def generate_signal_wait(self, i, s):
        signal_type = f"{i.camel_name}{s.name}Signal"
        signal_function_prefix = f"{i.name_lower}_{s.name_lower}_signal"
        signal_type_name = (
            f"{i.ns_upper}TYPE_{i.name_upper}_{s.name_upper}_SIGNAL"
        )
        wait_function_prefix = f"{i.name_lower}_wait_{s.name_lower}"

        if len(s.args) > 0:
            self.outfile.write(
                "%s *\n"
                "%s_copy (%s *r)\n"
                "{\n"
                "  %s *n = g_new0 (%s, 1);\n"
                % (
                    signal_type,
                    signal_function_prefix,
                    signal_type,
                    signal_type,
                    signal_type,
                )
            )
            for a in s.args:
                if a.copy_func:
                    self.outfile.write(
                        "  n->%s = r->%s ? %s (r->%s) : NULL;\n"
                        % (a.name, a.name, a.copy_func, a.name)
                    )
                else:
                    self.outfile.write("  n->%s = r->%s;\n" % (a.name, a.name))
            # FIXME: fd passing?
            self.outfile.write("  return n;\n" "}\n" "\n")
            self.outfile.write(
                "void\n"
                "%s_free (%s *r)\n"
                "{\n" % (signal_function_prefix, signal_type)
            )
            for a in s.args:
                if a.free_func:
                    self.outfile.write(
                        "  %s (r->%s);\n" % (a.free_func, a.name)
                    )
            # FIXME: fd passing?
            self.outfile.write("  free (r);\n" "}\n" "\n")
            self.outfile.write(
                "G_DEFINE_BOXED_TYPE (\n"
                "  %s,\n"
                "  %s,\n"
                "  %s_copy,\n"
                "  %s_free)\n"
                "\n"
                % (
                    signal_type,
                    signal_function_prefix,
                    signal_function_prefix,
                    signal_function_prefix,
                )
            )
            self.outfile.write("\n")

        self.outfile.write(
            "static void\n"
            "_%s_signalled_cb (\n"
            "    %s *proxy"
            % (wait_function_prefix, i.camel_name)
        )
        for a in s.args:
            self.outfile.write(",\n    %sarg_%s" % (a.ctype_in, a.name))
        self.outfile.write(
            ",\n    gpointer user_data)\n"
            "{\n"
            "  GDBusFutureSignalData *data = user_data;\n"
        )
        if len(s.args) > 0:
            self.outfile.write(
                "  %s *signal = g_new0 (%s, 1);\n"
                % (signal_type, signal_type, )
            )
        for a in s.args:
            if a.copy_func:
                self.outfile.write(
                    "  signal->%s = arg_%s ? %s (arg_%s) : NULL;\n"
                    % (a.name, a.name, a.copy_func, a.name)
                )
            else:
                self.outfile.write("  signal->%s = arg_%s;\n" % (a.name, a.name))
        self.outfile.write(
            "  g_cancellable_disconnect (dex_promise_get_cancellable (data->promise),\n"
            "                            data->cancelled_handler_id);\n"
            "  data->cancelled_handler_id = 0;\n"
        )
        if len(s.args) > 0:
            self.outfile.write(
                "  dex_promise_resolve_boxed (data->promise, %s, signal);\n"
                % (signal_type_name)
            )
        else:
            self.outfile.write(
                "  dex_promise_resolve_boolean (data->promise, TRUE);\n"
            )
        self.outfile.write(
            "  gdbus_future_signal_data_free (data);\n"
            "}\n\n"
        )

        self.outfile.write(
            "/**\n"
            " * %s_future:\n"
            " * @proxy: A #%sProxy.\n" % (wait_function_prefix, i.camel_name)
        )
        self.outfile.write(
            self.docbook_gen.expand(
                " *\n"
                " * Waits for a %s::%s() D-Bus signal on @proxy and returns a future representing the signal emission.\n"
                " *\n" % (i.name, s.name_hyphen),
                False,
            )
        )
        if len(s.args) > 0:
            self.outfile.write(
                " * The future resolves to the boxed #%s on success.\n"
                " *\n" % signal_type,
            )
        else:
            self.outfile.write(
                " * The future resolves to %TRUE on success.\n" " *\n"
            )
        self.outfile.write(
            self.docbook_gen.expand(
                " * Returns: (transfer full): The future\n",
                False,
            )
        )
        self.write_gtkdoc_deprecated_and_since_and_close(s, self.outfile, 0)

        self.outfile.write(
            "DexFuture *\n"
            "%s_future (%s *proxy)" % (wait_function_prefix, i.camel_name)
        )
        self.outfile.write(
            "{\n"
            "  GDBusFutureSignalData *data = g_new0 (GDBusFutureSignalData, 1);\n"
            "  data->proxy = G_DBUS_PROXY (g_object_ref (proxy));\n"
            "  data->promise = dex_promise_new_cancellable ();\n"
            "  data->cancelled_handler_id =\n"
            "    g_cancellable_connect (dex_promise_get_cancellable (data->promise),\n"
            "                           G_CALLBACK (gdbus_future_signal_cancelled_cb),\n"
            "                           data, NULL);\n"
            "  data->signalled_handler_id =\n"
            "    g_signal_connect (proxy, \"%s\",\n"
            "                      G_CALLBACK (_%s_signalled_cb),\n" # FIXME!
            "                      data);\n"
            "  return DEX_FUTURE (dex_ref (data->promise));\n"
            "}\n"
            % (s.name_hyphen, wait_function_prefix)
        )

    def generate_signal_monitor(self, i):
        if len(i.signals) == 0:
            return

        self.outfile.write(
            "G_DEFINE_FINAL_TYPE (%sSignalMonitor,\n"
            "                     %s_signal_monitor,\n"
            "                     G_TYPE_OBJECT)\n\n"
            % (i.camel_name, i.name_lower)
        )

        for s in i.signals:
            self.outfile.write(
                "static void\n"
                "%s_signal_monitor_%s_cb (\n"
                "  %sSignalMonitor *self,\n"
                % (i.name_lower, s.name_lower, i.camel_name)
            )
            for a in s.args:
                self.outfile.write("  %sarg_%s,\n" % (a.ctype_in, a.name))
            self.outfile.write(
                "  %s *object)\n"
                "{\n"
                % (i.camel_name)
            )
            if len(s.args) > 0:
                self.outfile.write(
                    "  %s%sSignal *signal = NULL;\n"
                    % (i.camel_name, s.name)
                )
            self.outfile.write(
                "  DexFuture *future = NULL;\n"
                "\n"
                "  if (self->%s_channel == NULL || !dex_channel_can_send (self->%s_channel))\n"
                "    return;\n"
                "\n"
                % (s.name_lower, s.name_lower)
            )
            if len(s.args) > 0:
                self.outfile.write(
                    "  signal = g_new0 (%s%sSignal, 1);\n"
                    % (i.camel_name, s.name)
                )
            for a in s.args:
                if a.copy_func:
                    self.outfile.write(
                        "  signal->%s = arg_%s ? %s (arg_%s) : NULL;\n"
                        % (a.name, a.name, a.copy_func, a.name)
                    )
                else:
                    self.outfile.write("  signal->%s = arg_%s;\n" % (a.name, a.name))
            if len(s.args) > 0:
                self.outfile.write(
                    "  future = dex_future_new_take_boxed (%sTYPE_%s_%s_SIGNAL,\n"
                    "                                      g_steal_pointer (&signal));\n"
                    "\n"
                    % (i.ns_upper, i.name_upper, s.name_upper)
                )
            else:
                self.outfile.write(
                    "  future = dex_future_new_true ();\n"
                    "\n"
                )
            self.outfile.write(
                "  dex_future_disown (dex_channel_send (self->%s_channel, g_steal_pointer (&future)));\n"
                "}\n\n"
                % (s.name_lower)
            )

        self.outfile.write(
            "static void\n"
            "%s_signal_monitor_finalize (GObject *object)\n"
            "{\n"
            "  %sSignalMonitor *self = %s%s_SIGNAL_MONITOR (object);\n"
            "\n"
            % (i.name_lower, i.camel_name, i.ns_upper, i.name_upper)
        )
        for s in i.signals:
            self.outfile.write(
                "  g_clear_signal_handler (&self->%s_signal_id, self->object);\n"
                "  if (self->%s_channel)\n"
                "    dex_channel_close_send (self->%s_channel);\n"
                "  dex_clear (&self->%s_channel);\n"
                "\n"
                % (s.name_lower, s.name_lower, s.name_lower, s.name_lower)
            )
        self.outfile.write(
            "  g_clear_object (&self->object);\n"
            "  G_OBJECT_CLASS (%s_signal_monitor_parent_class)->finalize (object);\n"
            "}\n\n"
            % (i.name_lower)
        )

        self.outfile.write(
            "static void\n"
            "%s_signal_monitor_class_init (%sSignalMonitorClass *klass)\n"
            "{\n"
            "  GObjectClass *object_class = G_OBJECT_CLASS (klass);\n"
            "\n"
            "  object_class->finalize = %s_signal_monitor_finalize;\n"
            "}\n\n"
            % (i.name_lower, i.camel_name, i.name_lower)
        )
        self.outfile.write(
            "static void\n"
            "%s_signal_monitor_init (%sSignalMonitor *self)\n"
            "{\n"
            "}\n\n"
            % (i.name_lower, i.camel_name)
        )
        self.outfile.write(
            "%sSignalMonitor *\n"
            "%s_signal_monitor_new (\n"
            "  %s *object,\n"
            "  %sSignals signals)\n"
            "{\n"
            "  %sSignalMonitor *self = NULL;\n"
            "\n"
            "  self = g_object_new (%sTYPE_%s_SIGNAL_MONITOR, NULL);\n"
            "  g_set_object (&self->object, object);\n"
            % (i.camel_name, i.name_lower, i.camel_name, i.camel_name, i.camel_name, i.ns_upper, i.name_upper)
        )
        for s in i.signals:
            self.outfile.write(
                "  if (signals & %s%s_SIGNAL_%s)\n"
                "    {\n"
                "      self->%s_channel = dex_channel_new (0);\n"
                "      self->%s_signal_id =\n"
                "        g_signal_connect_swapped (self->object,\n"
                "                                  \"%s\",\n"
                "                                  G_CALLBACK (%s_signal_monitor_%s_cb),\n"
                "                                  self);\n"
                "    }\n"
                %
                (
                    i.ns_upper, i.name_upper, s.name_upper,
                    s.name_lower, s.name_lower,
                    s.name_hyphen,
                    i.name_lower, s.name_lower
                )
            )
        self.outfile.write(
            "  return g_steal_pointer (&self);\n"
            "}\n\n"
        )

        self.outfile.write(
            "void\n"
            "%s_signal_monitor_cancel (%sSignalMonitor *self)\n"
            "{\n"
            "  g_return_if_fail (%sIS_%s_SIGNAL_MONITOR (self));\n"
            "\n"
            % (i.name_lower, i.camel_name, i.ns_upper, i.name_upper)
        )
        for s in i.signals:
            self.outfile.write(
                "  g_clear_signal_handler (&self->%s_signal_id, self->object);\n"
                "  if (self->%s_channel)\n"
                "    dex_channel_close_send (self->%s_channel);\n"
                % (s.name_lower, s.name_lower, s.name_lower)
            )
        self.outfile.write(
            "\n"
            "  g_clear_object (&self->object);\n"
            "}\n\n"
        )
        for s in i.signals:
            self.outfile.write(
                "DexFuture *\n"
                "%s_signal_monitor_next_%s (%sSignalMonitor *self)\n"
                "{\n"
                "  g_return_val_if_fail (%sIS_%s_SIGNAL_MONITOR (self), NULL);\n"
                "\n"
                "  if (self->%s_channel != NULL)\n"
                "    return dex_channel_receive (self->%s_channel);\n"
                "\n"
                "  return dex_future_new_reject (G_IO_ERROR,\n"
                "                                G_IO_ERROR_CANCELLED,\n"
                "                                \"Monitoring cancelled\");\n"
                "}\n\n"
                % (i.name_lower, s.name_lower, i.camel_name, i.ns_upper, i.name_upper, s.name_lower, s.name_lower)
            )

    def generate_body_preamble(self):
        self.outfile.write(
            "typedef struct\n"
            "{\n"
            "  GDBusProxy *proxy;\n"
            "  DexPromise *promise;\n"
            "  gulong cancelled_handler_id;\n"
            "  guint signalled_handler_id;\n"
            "} GDBusFutureSignalData;\n"
            "\n"
            "static void\n"
            "gdbus_future_signal_data_free (GDBusFutureSignalData *data)\n"
            "{\n"
            "  g_signal_handler_disconnect (data->proxy,\n"
            "                               data->signalled_handler_id);\n"
            "  g_clear_object (&data->proxy);\n"
            "  g_clear_pointer (&data->promise, dex_unref);\n"
            "  free (data);\n"
            "}\n"
            "\n"
            "G_GNUC_UNUSED static void\n"
            "gdbus_future_signal_cancelled_cb (GCancellable *cancellable,\n"
            "                                  gpointer      user_data)\n"
            "{\n"
            "  GDBusFutureSignalData *data = user_data;\n"
            "  dex_promise_reject (data->promise,\n"
            "                      g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,\n"
            "                                           \"Cancelled\"));\n"
            "  gdbus_future_signal_data_free (data);\n"
            "}\n"
        )

    def generate(self):
        for i in self.ifaces:
            self.generate_proxy(i)
            for m in i.methods:
                self.generate_method_call(i, m)
            for s in i.signals:
                self.generate_signal_wait(i, s)
            self.generate_signal_monitor(i)
