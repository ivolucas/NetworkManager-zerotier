/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-reconciler.h"

#include <glib-unix.h>
#include <signal.h>

/* Unlike nm-zerotier-service (D-Bus-activated by NetworkManager once per
 * active connection, living only as long as that connection does), this is a
 * long-running system daemon started once at boot by its own systemd unit -
 * see data/nm-zerotier-reconciled.service. It continuously reconciles NM's
 * view of zerotier VPN connections against zerotier-one's actual joined-
 * network state in both directions, which requires holding a live D-Bus
 * connection independent of any single connection's lifecycle. */

static gboolean
signal_handler(gpointer user_data)
{
    g_main_loop_quit((GMainLoop *) user_data);
    return G_SOURCE_REMOVE;
}

int
main(int argc, char *argv[])
{
    gboolean debug = FALSE;
    GOptionEntry options[] = {
        {"debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Don't background", NULL},
        G_OPTION_ENTRY_NULL,
    };

    GOptionContext *opt_ctx = g_option_context_new(NULL);
    g_option_context_add_main_entries(opt_ctx, options, NULL);

    GError *opt_error = NULL;
    if (!g_option_context_parse(opt_ctx, &argc, &argv, &opt_error)) {
        g_warning("zerotier-reconciled: option parsing failed: %s", opt_error->message);
        g_clear_error(&opt_error);
        g_option_context_free(opt_ctx);
        return 1;
    }
    g_option_context_free(opt_ctx);

    (void) debug;

    NMZerotierReconciler *reconciler = nm_zerotier_reconciler_new();

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    g_unix_signal_add(SIGTERM, signal_handler, loop);
    g_unix_signal_add(SIGINT, signal_handler, loop);

    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    g_object_unref(reconciler);

    return 0;
}
