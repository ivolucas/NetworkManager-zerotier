/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-plugin.h"
#include "nm-zerotier-service-defaults.h"

#include <glib-unix.h>
#include <signal.h>

static gboolean
signal_handler(gpointer user_data)
{
    g_main_loop_quit((GMainLoop *) user_data);
    return G_SOURCE_REMOVE;
}

static void
quit_mainloop(NMVpnServicePlugin *plugin, gpointer user_data)
{
    (void) plugin;
    g_main_loop_quit((GMainLoop *) user_data);
}

int
main(int argc, char *argv[])
{
    /* Our .name file sets supports-multiple-connections=true, so
     * NetworkManager spawns us with a per-connection unique
     * "--bus-name org.freedesktop.NetworkManager.zerotier.ConnectionN"
     * argument instead of the base service name, and waits for exactly
     * that name to appear on the bus. Ignoring argv here (as an earlier
     * version of this file did) makes the daemon own the wrong name: NM
     * times out waiting for the name it asked for, while this process
     * lingers forever, orphaned, owning a name nobody is watching. */
    char    *bus_name = NULL;
    gboolean debug = FALSE;
    GOptionEntry options[] = {
        {"bus-name", 0, 0, G_OPTION_ARG_STRING, &bus_name, "D-Bus service name to own", "NAME"},
        {"debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Don't background/watch peer", NULL},
        G_OPTION_ENTRY_NULL,
    };

    GOptionContext *opt_ctx = g_option_context_new(NULL);
    g_option_context_add_main_entries(opt_ctx, options, NULL);

    GError *opt_error = NULL;
    if (!g_option_context_parse(opt_ctx, &argc, &argv, &opt_error)) {
        g_warning("zerotier: option parsing failed: %s", opt_error->message);
        g_clear_error(&opt_error);
        g_option_context_free(opt_ctx);
        return 1;
    }
    g_option_context_free(opt_ctx);

    (void) debug;

    NMZerotierPlugin *plugin =
        nm_zerotier_plugin_new(bus_name ? bus_name : NM_DBUS_SERVICE_ZEROTIER);
    g_free(bus_name);
    if (!plugin)
        return 1;

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    g_signal_connect(plugin, "quit", G_CALLBACK(quit_mainloop), loop);
    g_unix_signal_add(SIGTERM, signal_handler, loop);
    g_unix_signal_add(SIGINT, signal_handler, loop);

    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    g_object_unref(plugin);

    return 0;
}
