/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-plugin.h"
#include "nm-zerotier-service-defaults.h"
#include "zerotier-cli-wrapper.h"
#include "zerotier-network-id.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#define POLL_INTERVAL_MS  500
#define CONNECT_TIMEOUT_S  30

/* All zerotier-cli invocations block on a subprocess + local socket
 * round-trip (zt_cli_run() uses g_spawn_sync()). NetworkManager calls our
 * connect()/disconnect() vfuncs synchronously while dispatching the
 * corresponding D-Bus method call, and expects a prompt reply (a real-world
 * hang here made NM give up with "timed out waiting for the VPN to
 * activate" after 5s). So every zerotier-cli call is run on a GTask worker
 * thread, keeping the GMainContext that runs our D-Bus service free. */

struct _NMZerotierPlugin {
    NMVpnServicePlugin parent;
    char    *network_id;
    guint    poll_source_id;
    gint64   deadline_us;
    gboolean poll_in_flight;
    gboolean allow_default;
};

G_DEFINE_TYPE(NMZerotierPlugin, nm_zerotier_plugin, NM_TYPE_VPN_SERVICE_PLUGIN)

static void
cancel_poll(NMZerotierPlugin *self)
{
    if (self->poll_source_id) {
        g_source_remove(self->poll_source_id);
        self->poll_source_id = 0;
    }
}

/* Adds "address"/"prefix" (v4) or "address"/"prefix" (v6) to a{sv} builder
 * for the given "ip/prefixlen" CIDR string. Returns the address family found,
 * or AF_UNSPEC if the string could not be parsed. */
static int
add_config_for_cidr(GVariantBuilder *v4, GVariantBuilder *v6, const char *cidr)
{
    char **parts = g_strsplit(cidr, "/", 2);
    if (!parts[0] || !parts[1]) {
        g_strfreev(parts);
        return AF_UNSPEC;
    }

    guint32 prefix = (guint32) g_ascii_strtoull(parts[1], NULL, 10);
    int     family = AF_UNSPEC;

    struct in_addr  a4;
    struct in6_addr a6;

    if (inet_pton(AF_INET, parts[0], &a4) == 1) {
        family = AF_INET;
        g_variant_builder_add(v4, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_ADDRESS,
                              g_variant_new_uint32(a4.s_addr));
        g_variant_builder_add(v4, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_PREFIX,
                              g_variant_new_uint32(prefix));
    } else if (inet_pton(AF_INET6, parts[0], &a6) == 1) {
        family = AF_INET6;
        g_variant_builder_add(v6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_ADDRESS,
                              g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, a6.s6_addr, 16, 1));
        g_variant_builder_add(v6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_PREFIX,
                              g_variant_new_uint32(prefix));
    }

    g_strfreev(parts);
    return family;
}

/* NetworkManager rejects a VPN config that has no "external gateway"
 * address (it uses that address to add an explicit host route so the
 * tunnel's own transport traffic isn't captured by its own routes).
 * ZeroTier has no single VPN server to report here - it's a P2P mesh - but
 * its UDP transport traffic genuinely does leave through the host's normal
 * default gateway, so that's what we report: the added host route is a
 * correct no-op rather than an arbitrary placeholder.
 * Returns the gateway in network byte order, or 0 if none could be found
 * (e.g. no default route at all). */
static guint32
get_default_ipv4_gateway(void)
{
    char *contents = NULL;
    if (!g_file_get_contents("/proc/net/route", &contents, NULL, NULL))
        return 0;

    guint32 gateway = 0;
    char  **lines = g_strsplit(contents, "\n", -1);

    for (guint i = 1; lines[i]; i++) {
        char  iface[64];
        unsigned long destination, gw, flags;

        if (sscanf(lines[i], "%63s %lx %lx %lx", iface, &destination, &gw, &flags) == 4
            && destination == 0 && (flags & 0x2) /* RTF_GATEWAY */) {
            gateway = (guint32) gw;
            break;
        }
    }

    g_strfreev(lines);
    g_free(contents);
    return gateway;
}

static void
report_ip_config(NMZerotierPlugin *self, ZtNetworkInfo *info)
{
    GVariantBuilder config, ip4, ip6;
    gboolean        have_ip4 = FALSE, have_ip6 = FALSE;

    g_variant_builder_init(&ip4, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_init(&ip6, G_VARIANT_TYPE_VARDICT);

    /* Without this, NetworkManager's own default policy is free to make
     * this VPN the default route (0.0.0.0/0 via the zt interface) even
     * though ZeroTier is a private overlay with no general internet
     * access - which silently breaks all other internet traffic. Tie it
     * to the same "allow-default" choice the user made for ZeroTier's own
     * allowDefault, so there's one on/off switch instead of two. */
    g_variant_builder_add(&ip4, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_NEVER_DEFAULT,
                          g_variant_new_boolean(!self->allow_default));
    g_variant_builder_add(&ip6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_NEVER_DEFAULT,
                          g_variant_new_boolean(!self->allow_default));

    for (guint i = 0; i < info->assigned_addresses->len; i++) {
        const char *cidr = g_ptr_array_index(info->assigned_addresses, i);
        int         family = add_config_for_cidr(&ip4, &ip6, cidr);

        if (family == AF_INET)
            have_ip4 = TRUE;
        else if (family == AF_INET6)
            have_ip6 = TRUE;
    }

    g_variant_builder_init(&config, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&config, "{sv}", NM_VPN_PLUGIN_CONFIG_TUNDEV,
                          g_variant_new_string(info->port_device_name));
    g_variant_builder_add(&config, "{sv}", NM_VPN_PLUGIN_CONFIG_HAS_IP4,
                          g_variant_new_boolean(have_ip4));
    g_variant_builder_add(&config, "{sv}", NM_VPN_PLUGIN_CONFIG_HAS_IP6,
                          g_variant_new_boolean(have_ip6));

    guint32 gateway = get_default_ipv4_gateway();
    if (gateway != 0)
        g_variant_builder_add(&config, "{sv}", NM_VPN_PLUGIN_CONFIG_EXT_GATEWAY,
                              g_variant_new_uint32(gateway));

    nm_vpn_service_plugin_set_config(NM_VPN_SERVICE_PLUGIN(self), g_variant_builder_end(&config));

    if (have_ip6)
        nm_vpn_service_plugin_set_ip6_config(NM_VPN_SERVICE_PLUGIN(self), g_variant_builder_end(&ip6));
    else
        g_variant_builder_clear(&ip6);

    if (have_ip4)
        nm_vpn_service_plugin_set_ip4_config(NM_VPN_SERVICE_PLUGIN(self), g_variant_builder_end(&ip4));
    else
        g_variant_builder_clear(&ip4);
}

/* --- polling: runs zt_cli_find_network() on a worker thread every tick --- */

static void
poll_thread_func(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void) source_object;
    (void) cancellable;
    const char *network_id = task_data;
    GError     *error = NULL;
    ZtNetworkInfo *info = zt_cli_find_network(network_id, &error);

    if (error) {
        g_task_return_error(task, error);
        return;
    }

    /* info may legitimately be NULL: not joined yet from zerotier-one's
     * point of view. That's not an error, just "not ready". */
    g_task_return_pointer(task, info, (GDestroyNotify) zt_network_info_free);
}

static void
poll_task_finished_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    NMZerotierPlugin *self = NM_ZEROTIER_PLUGIN(source);

    (void) user_data;
    self->poll_in_flight = FALSE;

    /* We may have been disconnected (or timed out) while this task was
     * in flight; if so, poll_source_id is already 0 and there's nothing
     * left to do with a late result. */
    if (!self->poll_source_id)
        return;

    GError        *error = NULL;
    ZtNetworkInfo *info = g_task_propagate_pointer(G_TASK(res), &error);

    if (error) {
        g_debug("zerotier: transient error polling network status: %s", error->message);
        g_clear_error(&error);
        return;
    }

    if (!info)
        return;

    gboolean ready = (g_strcmp0(info->status, "OK") == 0 || g_strcmp0(info->status, "ONLINE") == 0)
                     && info->assigned_addresses->len > 0 && info->port_device_name
                     && *info->port_device_name;

    if (ready) {
        report_ip_config(self, info);
        cancel_poll(self);
    }

    zt_network_info_free(info);
}

static gboolean
poll_tick_cb(gpointer user_data)
{
    NMZerotierPlugin *self = NM_ZEROTIER_PLUGIN(user_data);

    if (g_get_monotonic_time() > self->deadline_us) {
        g_warning("zerotier: timed out waiting for network %s to come online "
                  "(is this device authorized on the network's controller?)",
                  self->network_id);
        self->poll_source_id = 0;
        nm_vpn_service_plugin_failure(NM_VPN_SERVICE_PLUGIN(self), NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED);
        return G_SOURCE_REMOVE;
    }

    if (self->poll_in_flight)
        return G_SOURCE_CONTINUE; /* previous poll still running on its worker thread */

    self->poll_in_flight = TRUE;

    GTask *task = g_task_new(self, NULL, poll_task_finished_cb, NULL);
    g_task_set_task_data(task, g_strdup(self->network_id), g_free);
    g_task_run_in_thread(task, poll_thread_func);
    g_object_unref(task);

    return G_SOURCE_CONTINUE;
}

/* --- connect: join (+ optional allowDefault) run on a worker thread --- */

typedef struct {
    char    *network_id;
    gboolean set_allow_default;
} JoinTaskData;

static void
join_task_data_free(JoinTaskData *data)
{
    g_free(data->network_id);
    g_free(data);
}

static void
join_thread_func(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void) source_object;
    (void) cancellable;
    JoinTaskData *data = task_data;
    GError       *error = NULL;

    if (!zt_cli_join(data->network_id, &error)) {
        g_task_return_error(task, error);
        return;
    }

    if (data->set_allow_default) {
        GError *allow_error = NULL;
        if (!zt_cli_set_allow_default(data->network_id, TRUE, &allow_error)) {
            g_warning("zerotier: failed to set allowDefault on %s: %s", data->network_id,
                     allow_error->message);
            g_clear_error(&allow_error);
        }
    }

    g_task_return_boolean(task, TRUE);
}

static void
join_task_finished_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    NMZerotierPlugin *self = NM_ZEROTIER_PLUGIN(source);
    GError           *error = NULL;

    (void) user_data;

    if (!g_task_propagate_boolean(G_TASK(res), &error)) {
        /* Don't fail the connection here: the poll loop is already
         * running and will time out on its own if the network never
         * appears, which gives one uniform failure path instead of two. */
        g_warning("zerotier: failed to join network %s: %s", self->network_id, error->message);
        g_clear_error(&error);
    }
}

static gboolean
start_connect(NMZerotierPlugin *self, NMConnection *connection, GError **error)
{
    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(connection);
    const char   *network_id = s_vpn ? nm_setting_vpn_get_data_item(s_vpn, NM_ZEROTIER_KEY_NETWORK_ID) : NULL;

    if (!zt_is_valid_network_id(network_id)) {
        g_set_error(error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
                   "'%s' is not a valid ZeroTier network id (expected 16 hex characters)",
                   network_id ? network_id : "(none)");
        return FALSE;
    }

    if (!zt_cli_is_available()) {
        g_set_error(error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
                   "zerotier-cli not found; is zerotier-one installed?");
        return FALSE;
    }

    g_free(self->network_id);
    self->network_id = g_strdup(network_id);

    const char *allow_default = nm_setting_vpn_get_data_item(s_vpn, NM_ZEROTIER_KEY_ALLOW_DEFAULT);
    self->allow_default = (g_strcmp0(allow_default, "yes") == 0);

    JoinTaskData *data = g_new0(JoinTaskData, 1);
    data->network_id = g_strdup(self->network_id);
    data->set_allow_default = self->allow_default;

    GTask *task = g_task_new(self, NULL, join_task_finished_cb, NULL);
    g_task_set_task_data(task, data, (GDestroyNotify) join_task_data_free);
    g_task_run_in_thread(task, join_thread_func);
    g_object_unref(task);

    self->deadline_us = g_get_monotonic_time() + (gint64) CONNECT_TIMEOUT_S * G_USEC_PER_SEC;
    cancel_poll(self);
    self->poll_in_flight = FALSE;
    self->poll_source_id = g_timeout_add(POLL_INTERVAL_MS, poll_tick_cb, self);

    return TRUE;
}

static gboolean
real_connect(NMVpnServicePlugin *plugin, NMConnection *connection, GError **error)
{
    return start_connect(NM_ZEROTIER_PLUGIN(plugin), connection, error);
}

static gboolean
real_connect_interactive(NMVpnServicePlugin *plugin, NMConnection *connection,
                         GVariant *details, GError **error)
{
    /* No interactive hints are needed for v1 - a ZeroTier network id is
     * either valid or it isn't, there's nothing to prompt for. */
    (void) details;
    return start_connect(NM_ZEROTIER_PLUGIN(plugin), connection, error);
}

static gboolean
real_need_secrets(NMVpnServicePlugin *plugin, NMConnection *connection,
                  const char **setting_name, GError **error)
{
    (void) plugin;
    (void) connection;
    (void) error;
    /* ZeroTier network authorization happens on the controller side, not via
     * a client-supplied secret. */
    *setting_name = NULL;
    return FALSE;
}

/* --- disconnect: leave run on a worker thread, best-effort --- */

static void
leave_thread_func(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void) source_object;
    (void) cancellable;
    const char *network_id = task_data;
    GError     *error = NULL;

    if (!zt_cli_leave(network_id, &error)) {
        g_task_return_error(task, error);
        return;
    }
    g_task_return_boolean(task, TRUE);
}

static void
leave_task_finished_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    NMZerotierPlugin *self = NM_ZEROTIER_PLUGIN(source);
    GError           *error = NULL;

    (void) user_data;

    if (!g_task_propagate_boolean(G_TASK(res), &error)) {
        g_warning("zerotier: failed to leave network %s: %s", self->network_id, error->message);
        g_clear_error(&error);
    }
}

static gboolean
real_disconnect(NMVpnServicePlugin *plugin, GError **error)
{
    NMZerotierPlugin *self = NM_ZEROTIER_PLUGIN(plugin);

    cancel_poll(self);

    if (self->network_id) {
        GTask *task = g_task_new(self, NULL, leave_task_finished_cb, NULL);
        g_task_set_task_data(task, g_strdup(self->network_id), g_free);
        g_task_run_in_thread(task, leave_thread_func);
        g_object_unref(task);
    }

    /* Best-effort: don't block NM's teardown waiting for the leave to
     * finish, and don't fail disconnect over it either. */
    (void) error;
    return TRUE;
}

NMZerotierPlugin *
nm_zerotier_plugin_new(const char *bus_name)
{
    GError            *error = NULL;
    NMZerotierPlugin  *plugin;

    plugin = (NMZerotierPlugin *) g_initable_new(NM_TYPE_ZEROTIER_PLUGIN, NULL, &error,
                                                 NM_VPN_SERVICE_PLUGIN_DBUS_SERVICE_NAME, bus_name,
                                                 NM_VPN_SERVICE_PLUGIN_DBUS_WATCH_PEER, TRUE,
                                                 NULL);
    if (!plugin) {
        g_warning("zerotier: failed to initialize plugin: %s", error->message);
        g_clear_error(&error);
    }

    return plugin;
}

static void
nm_zerotier_plugin_init(NMZerotierPlugin *self)
{
    self->network_id = NULL;
    self->poll_source_id = 0;
    self->poll_in_flight = FALSE;
    self->allow_default = FALSE;
}

static void
dispose(GObject *object)
{
    NMZerotierPlugin *self = NM_ZEROTIER_PLUGIN(object);

    cancel_poll(self);
    g_clear_pointer(&self->network_id, g_free);

    G_OBJECT_CLASS(nm_zerotier_plugin_parent_class)->dispose(object);
}

static void
nm_zerotier_plugin_class_init(NMZerotierPluginClass *klass)
{
    GObjectClass            *object_class = G_OBJECT_CLASS(klass);
    NMVpnServicePluginClass *plugin_class = NM_VPN_SERVICE_PLUGIN_CLASS(klass);

    object_class->dispose = dispose;

    plugin_class->connect = real_connect;
    plugin_class->connect_interactive = real_connect_interactive;
    plugin_class->need_secrets = real_need_secrets;
    plugin_class->disconnect = real_disconnect;
}
