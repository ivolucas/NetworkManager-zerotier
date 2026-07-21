/* SPDX-License-Identifier: GPL-2.0-or-later */

/* ZeroTier network authorization happens on the network controller's side
 * (approving the device), not via a client-supplied secret, so this
 * plugin has nothing to prompt for. Without this helper, gnome-control-
 * center falls back to fetching secrets from NetworkManager over D-Bus,
 * which - with no secret agent around to answer for this connection -
 * blocks the "edit connection" dialog for a ~30s timeout before it gives
 * up and shows the (already correctly pre-filled from static connection
 * data) editor anyway. Declaring "auth-dialog" + "supports-external-ui-
 * mode=true" in the .name file lets the caller run this small local
 * helper instead, which answers immediately: nothing printed to stdout
 * means "no secrets needed".
 */

#include <NetworkManager.h>

int
main(int argc, char *argv[])
{
    gboolean retry = FALSE;
    gboolean allow_interaction = FALSE;
    gboolean external_ui_mode = FALSE;
    char    *vpn_uuid = NULL;
    char    *vpn_name = NULL;
    char    *vpn_service = NULL;

    GOptionEntry entries[] = {
        {"reprompt", 'r', 0, G_OPTION_ARG_NONE, &retry, "Reprompt for passwords", NULL},
        {"uuid", 'u', 0, G_OPTION_ARG_STRING, &vpn_uuid, "UUID of VPN connection", NULL},
        {"name", 'n', 0, G_OPTION_ARG_STRING, &vpn_name, "Name of VPN connection", NULL},
        {"service", 's', 0, G_OPTION_ARG_STRING, &vpn_service, "VPN service type", NULL},
        {"allow-interaction", 'i', 0, G_OPTION_ARG_NONE, &allow_interaction,
         "Allow user interaction", NULL},
        {"external-ui-mode", 0, 0, G_OPTION_ARG_NONE, &external_ui_mode, "External UI mode",
         NULL},
        G_OPTION_ENTRY_NULL,
    };

    GOptionContext *ctx = g_option_context_new("- ZeroTier auth dialog helper");
    g_option_context_add_main_entries(ctx, entries, NULL);

    GError *error = NULL;
    if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        g_clear_error(&error);
        g_option_context_free(ctx);
        return 1;
    }
    g_option_context_free(ctx);

    (void) retry;
    (void) allow_interaction;
    (void) external_ui_mode;

    /* Callers write the connection's data/secrets to our stdin before
     * waiting for a reply, the same as any secrets-capable plugin's
     * auth-dialog would read; drain it so we don't leave the caller
     * blocked on a full pipe. We never need any of it. */
    GHashTable *data = NULL;
    GHashTable *secrets = NULL;
    nm_vpn_service_plugin_read_vpn_details(0, &data, &secrets);
    g_clear_pointer(&data, g_hash_table_unref);
    g_clear_pointer(&secrets, g_hash_table_unref);

    g_free(vpn_uuid);
    g_free(vpn_name);
    g_free(vpn_service);

    /* Nothing printed to stdout = nothing to prompt for. */
    return 0;
}
