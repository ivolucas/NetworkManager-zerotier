/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "zerotier-cli-wrapper.h"

#include <json-glib/json-glib.h>
#include <string.h>

G_DEFINE_QUARK(zt-cli-error-quark, zt_cli_error)

static const char *candidate_paths[] = {
    "/usr/sbin/zerotier-cli",
    "/usr/bin/zerotier-cli",
    NULL,
};

void
zt_network_info_free(ZtNetworkInfo *info)
{
    if (!info)
        return;
    g_free(info->nwid);
    g_free(info->status);
    g_free(info->port_device_name);
    g_free(info->name);
    g_free(info->type);
    g_free(info->mac);
    if (info->assigned_addresses)
        g_ptr_array_unref(info->assigned_addresses);
    if (info->routes)
        g_ptr_array_unref(info->routes);
    g_free(info);
}

static const char *
zt_cli_binary_path(void)
{
    static const char *cached = NULL;
    static gboolean    resolved = FALSE;

    if (resolved)
        return cached;
    resolved = TRUE;

    for (guint i = 0; candidate_paths[i]; i++) {
        if (g_file_test(candidate_paths[i], G_FILE_TEST_IS_EXECUTABLE)) {
            cached = candidate_paths[i];
            return cached;
        }
    }

    cached = g_find_program_in_path("zerotier-cli");
    return cached;
}

gboolean
zt_cli_is_available(void)
{
    return zt_cli_binary_path() != NULL;
}

/* Runs zerotier-cli with the given NULL-terminated argv tail (not including
 * the binary itself). Returns TRUE on exit code 0, capturing stdout in
 * *out_stdout (if non-NULL, must be freed by caller). */
static gboolean
zt_cli_run(char *const *args_tail, char **out_stdout, GError **error)
{
    const char *bin = zt_cli_binary_path();

    if (!bin) {
        g_set_error(error, ZT_CLI_ERROR, ZT_CLI_ERROR_NOT_FOUND,
                    "zerotier-cli not found; is zerotier-one installed?");
        return FALSE;
    }

    GPtrArray *argv = g_ptr_array_new();
    g_ptr_array_add(argv, (gpointer) bin);
    for (guint i = 0; args_tail[i]; i++)
        g_ptr_array_add(argv, args_tail[i]);
    g_ptr_array_add(argv, NULL);

    char *stdout_buf = NULL;
    char *stderr_buf = NULL;
    int   exit_status = 0;
    GError *spawn_error = NULL;

    gboolean ok = g_spawn_sync(NULL, (char **) argv->pdata, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                               &stdout_buf, &stderr_buf, &exit_status, &spawn_error);
    g_ptr_array_unref(argv);

    if (!ok) {
        g_propagate_prefixed_error(error, spawn_error, "failed to run zerotier-cli: ");
        g_free(stdout_buf);
        g_free(stderr_buf);
        return FALSE;
    }

    if (!g_spawn_check_wait_status(exit_status, &spawn_error)) {
        g_set_error(error, ZT_CLI_ERROR, ZT_CLI_ERROR_EXEC_FAILED,
                    "zerotier-cli exited with an error: %s%s%s", spawn_error->message,
                    (stderr_buf && *stderr_buf) ? ": " : "", (stderr_buf && *stderr_buf) ? stderr_buf : "");
        g_clear_error(&spawn_error);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return FALSE;
    }

    g_free(stderr_buf);
    if (out_stdout)
        *out_stdout = stdout_buf;
    else
        g_free(stdout_buf);
    return TRUE;
}

static ZtNetworkInfo *
zt_network_info_from_json(JsonObject *obj)
{
    ZtNetworkInfo *info = g_new0(ZtNetworkInfo, 1);

    info->nwid = g_strdup(json_object_has_member(obj, "nwid")
                              ? json_object_get_string_member(obj, "nwid")
                              : "");
    info->status = g_strdup(json_object_has_member(obj, "status")
                                ? json_object_get_string_member(obj, "status")
                                : "");
    info->port_device_name = g_strdup(json_object_has_member(obj, "portDeviceName")
                                          ? json_object_get_string_member(obj, "portDeviceName")
                                          : "");

    info->assigned_addresses = g_ptr_array_new_with_free_func(g_free);
    if (json_object_has_member(obj, "assignedAddresses")) {
        JsonArray *addrs = json_object_get_array_member(obj, "assignedAddresses");
        guint      len = json_array_get_length(addrs);
        for (guint i = 0; i < len; i++)
            g_ptr_array_add(info->assigned_addresses,
                            g_strdup(json_array_get_string_element(addrs, i)));
    }

    info->name = g_strdup(json_object_has_member(obj, "name")
                              ? json_object_get_string_member(obj, "name")
                              : "");
    info->type = g_strdup(json_object_has_member(obj, "type")
                              ? json_object_get_string_member(obj, "type")
                              : "");
    info->mac = g_strdup(json_object_has_member(obj, "mac")
                             ? json_object_get_string_member(obj, "mac")
                             : "");
    info->mtu = json_object_has_member(obj, "mtu")
                    ? (int) json_object_get_int_member(obj, "mtu")
                    : 0;
    info->bridge =
        json_object_has_member(obj, "bridge") && json_object_get_boolean_member(obj, "bridge");
    info->broadcast_enabled = json_object_has_member(obj, "broadcastEnabled")
                                  && json_object_get_boolean_member(obj, "broadcastEnabled");
    info->allow_managed = json_object_has_member(obj, "allowManaged")
                              && json_object_get_boolean_member(obj, "allowManaged");
    info->allow_global = json_object_has_member(obj, "allowGlobal")
                             && json_object_get_boolean_member(obj, "allowGlobal");
    info->allow_default = json_object_has_member(obj, "allowDefault")
                              && json_object_get_boolean_member(obj, "allowDefault");
    info->allow_dns = json_object_has_member(obj, "allowDNS")
                          && json_object_get_boolean_member(obj, "allowDNS");

    info->routes = g_ptr_array_new_with_free_func(g_free);
    if (json_object_has_member(obj, "routes")) {
        JsonArray *routes = json_object_get_array_member(obj, "routes");
        guint      len = json_array_get_length(routes);
        for (guint i = 0; i < len; i++) {
            JsonObject *route = json_array_get_object_element(routes, i);
            if (!route || !json_object_has_member(route, "target"))
                continue;
            const char *target = json_object_get_string_member(route, "target");
            JsonNode   *via_node =
                json_object_has_member(route, "via") ? json_object_get_member(route, "via") : NULL;
            const char *via = (via_node && json_node_get_value_type(via_node) == G_TYPE_STRING)
                                  ? json_node_get_string(via_node)
                                  : NULL;
            g_ptr_array_add(info->routes,
                            via ? g_strdup_printf("%s via %s", target, via) : g_strdup(target));
        }
    }

    return info;
}

GPtrArray *
zt_cli_parse_networks_json(const char *json_text, GError **error)
{
    JsonParser *parser = json_parser_new();
    GError     *parse_error = NULL;

    if (!json_parser_load_from_data(parser, json_text, -1, &parse_error)) {
        g_set_error(error, ZT_CLI_ERROR, ZT_CLI_ERROR_PARSE_FAILED,
                    "could not parse zerotier-cli listnetworks output: %s",
                    parse_error->message);
        g_clear_error(&parse_error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || json_node_get_node_type(root) != JSON_NODE_ARRAY) {
        g_set_error(error, ZT_CLI_ERROR, ZT_CLI_ERROR_PARSE_FAILED,
                    "unexpected zerotier-cli listnetworks output (not a JSON array)");
        g_object_unref(parser);
        return NULL;
    }

    JsonArray *arr = json_node_get_array(root);
    guint      len = json_array_get_length(arr);
    GPtrArray *result = g_ptr_array_new_with_free_func((GDestroyNotify) zt_network_info_free);

    for (guint i = 0; i < len; i++) {
        JsonObject *obj = json_array_get_object_element(arr, i);
        if (obj)
            g_ptr_array_add(result, zt_network_info_from_json(obj));
    }

    g_object_unref(parser);
    return result;
}

GPtrArray *
zt_cli_list_networks(GError **error)
{
    char *out = NULL;
    char *args_tail[] = {"-j", "listnetworks", NULL};

    if (!zt_cli_run(args_tail, &out, error))
        return NULL;

    GPtrArray *result = zt_cli_parse_networks_json(out, error);
    g_free(out);
    return result;
}

ZtNetworkInfo *
zt_cli_find_network(const char *network_id, GError **error)
{
    GPtrArray *networks = zt_cli_list_networks(error);
    if (!networks)
        return NULL;

    ZtNetworkInfo *found = NULL;
    for (guint i = 0; i < networks->len; i++) {
        ZtNetworkInfo *info = g_ptr_array_index(networks, i);
        if (g_strcmp0(info->nwid, network_id) == 0) {
            /* steal it out of the array before freeing the rest */
            found = info;
            g_ptr_array_index(networks, i) = NULL;
            break;
        }
    }

    g_ptr_array_unref(networks);
    return found;
}

gboolean
zt_cli_join(const char *network_id, GError **error)
{
    char *args_tail[] = {"join", (char *) network_id, NULL};
    return zt_cli_run(args_tail, NULL, error);
}

gboolean
zt_cli_leave(const char *network_id, GError **error)
{
    char *args_tail[] = {"leave", (char *) network_id, NULL};
    return zt_cli_run(args_tail, NULL, error);
}

gboolean
zt_cli_set_allow_default(const char *network_id, gboolean allow, GError **error)
{
    char *setting = g_strdup_printf("allowDefault=%s", allow ? "true" : "false");
    char *args_tail[] = {"set", (char *) network_id, setting, NULL};
    gboolean ok = zt_cli_run(args_tail, NULL, error);
    g_free(setting);
    return ok;
}
