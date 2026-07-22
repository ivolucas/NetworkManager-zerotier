/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-status-dbus.h"

#include "nm-zerotier-status-defaults.h"
#include "zerotier-network-id.h"

static const char introspection_xml[] =
    "<node>"
    "  <interface name='" NM_ZEROTIER_STATUS_IFACE_NAME "'>"
    "    <method name='GetNetworkStatus'>"
    "      <arg type='s' name='network_id' direction='in'/>"
    "      <arg type='b' name='found' direction='out'/>"
    "      <arg type='a{sv}' name='info' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

struct _NMZerotierStatusDBus {
    guint            owner_id;
    guint            registration_id;
    GDBusNodeInfo   *introspection_data;
    GDBusConnection *connection; /* borrowed, valid only while owning the name */
};

/* --- error domain, mapped from ZtCliError so GetNetworkStatus callers see a
 * distinguishable error name over D-Bus instead of a generic failure --- */

typedef enum {
    NM_ZEROTIER_STATUS_DBUS_ERROR_CLI_UNAVAILABLE, /* zerotier-cli not found/spawn failed */
    NM_ZEROTIER_STATUS_DBUS_ERROR_CLI_FAILED,      /* zerotier-cli ran, exited nonzero, or bad output */
} NMZerotierStatusDBusError;

#define NM_ZEROTIER_STATUS_DBUS_ERROR (nm_zerotier_status_dbus_error_quark())

static GQuark
nm_zerotier_status_dbus_error_quark(void)
{
    static volatile gsize quark = 0;
    static const GDBusErrorEntry entries[] = {
        {NM_ZEROTIER_STATUS_DBUS_ERROR_CLI_UNAVAILABLE,
         "org.zerotier.nm.Status1.Error.CliUnavailable"},
        {NM_ZEROTIER_STATUS_DBUS_ERROR_CLI_FAILED,
         "org.zerotier.nm.Status1.Error.CliFailed"},
    };

    g_dbus_error_register_error_domain("nm-zerotier-status-dbus-error-quark", &quark, entries,
                                       G_N_ELEMENTS(entries));
    return (GQuark) quark;
}

/* Joins an "as" GVariant array from a GPtrArray of owned strings. */
static GVariant *
strv_variant_from_ptr_array(GPtrArray *arr)
{
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
    if (arr) {
        for (guint i = 0; i < arr->len; i++)
            g_variant_builder_add(&b, "s", (const char *) g_ptr_array_index(arr, i));
    }
    return g_variant_builder_end(&b);
}

GVariant *
nm_zerotier_status_dbus_network_info_to_variant(const ZtNetworkInfo *info)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

    g_variant_builder_add(&builder, "{sv}", "nwid", g_variant_new_string(info->nwid ? info->nwid : ""));
    g_variant_builder_add(&builder, "{sv}", "status",
                          g_variant_new_string(info->status ? info->status : ""));
    g_variant_builder_add(&builder, "{sv}", "port_device_name",
                          g_variant_new_string(info->port_device_name ? info->port_device_name : ""));
    g_variant_builder_add(&builder, "{sv}", "assigned_addresses",
                          strv_variant_from_ptr_array(info->assigned_addresses));
    g_variant_builder_add(&builder, "{sv}", "name", g_variant_new_string(info->name ? info->name : ""));
    g_variant_builder_add(&builder, "{sv}", "type", g_variant_new_string(info->type ? info->type : ""));
    g_variant_builder_add(&builder, "{sv}", "mac", g_variant_new_string(info->mac ? info->mac : ""));
    g_variant_builder_add(&builder, "{sv}", "mtu", g_variant_new_int32(info->mtu));
    g_variant_builder_add(&builder, "{sv}", "bridge", g_variant_new_boolean(info->bridge));
    g_variant_builder_add(&builder, "{sv}", "broadcast_enabled",
                          g_variant_new_boolean(info->broadcast_enabled));
    g_variant_builder_add(&builder, "{sv}", "allow_managed", g_variant_new_boolean(info->allow_managed));
    g_variant_builder_add(&builder, "{sv}", "allow_global", g_variant_new_boolean(info->allow_global));
    g_variant_builder_add(&builder, "{sv}", "allow_default", g_variant_new_boolean(info->allow_default));
    g_variant_builder_add(&builder, "{sv}", "allow_dns", g_variant_new_boolean(info->allow_dns));
    g_variant_builder_add(&builder, "{sv}", "routes", strv_variant_from_ptr_array(info->routes));

    return g_variant_builder_end(&builder);
}

/* --- GetNetworkStatus request handling: run zt_cli_find_network() on a
 * worker thread so a slow/hung zerotier-cli subprocess doesn't stall the
 * daemon's main loop. --- */

typedef struct {
    GDBusMethodInvocation *invocation; /* owned (ref held) across the async hop */
    char                   *network_id; /* owned */
} GetStatusRequest;

static void
get_status_request_free(GetStatusRequest *req)
{
    g_clear_object(&req->invocation);
    g_free(req->network_id);
    g_free(req);
}

static void
get_status_thread_func(GTask *task, gpointer source_object, gpointer task_data,
                       GCancellable *cancellable)
{
    GetStatusRequest *req = task_data;
    GError            *error = NULL;
    ZtNetworkInfo      *info;

    (void) source_object;
    (void) cancellable;

    info = zt_cli_find_network(req->network_id, &error);
    if (error) {
        g_task_return_error(task, error);
        return;
    }
    /* info may legitimately be NULL here (network not currently joined) -
     * g_task_return_pointer() with a NULL result is fine as long as no error
     * was set; g_task_propagate_pointer() distinguishes that from failure. */
    g_task_return_pointer(task, info, (GDestroyNotify) zt_network_info_free);
}

static void
get_status_task_finished_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    GTask                 *task = G_TASK(res);
    GetStatusRequest       *req = g_task_get_task_data(task);
    GDBusMethodInvocation  *invocation = req->invocation;
    GError                 *error = NULL;
    ZtNetworkInfo           *info;

    (void) source;
    (void) user_data;

    info = g_task_propagate_pointer(task, &error);

    if (error) {
        NMZerotierStatusDBusError mapped =
            g_error_matches(error, ZT_CLI_ERROR, ZT_CLI_ERROR_NOT_FOUND)
                ? NM_ZEROTIER_STATUS_DBUS_ERROR_CLI_UNAVAILABLE
                : NM_ZEROTIER_STATUS_DBUS_ERROR_CLI_FAILED;
        g_dbus_method_invocation_return_error(invocation, NM_ZEROTIER_STATUS_DBUS_ERROR, mapped,
                                              "%s", error->message);
        g_clear_error(&error);
    } else if (!info) {
        GVariantBuilder empty;

        g_variant_builder_init(&empty, G_VARIANT_TYPE("a{sv}"));
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(b@a{sv})", FALSE, g_variant_builder_end(&empty)));
    } else {
        GVariant *v = nm_zerotier_status_dbus_network_info_to_variant(info);

        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b@a{sv})", TRUE, v));
        zt_network_info_free(info);
    }
}

static void
handle_method_call(GDBusConnection *connection, const char *sender, const char *object_path,
                   const char *interface_name, const char *method_name, GVariant *parameters,
                   GDBusMethodInvocation *invocation, gpointer user_data)
{
    const char        *network_id = NULL;
    GetStatusRequest  *req;
    GTask             *task;

    (void) connection;
    (void) sender;
    (void) object_path;
    (void) interface_name;
    (void) user_data;

    if (g_strcmp0(method_name, "GetNetworkStatus") != 0) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "Unknown method %s", method_name);
        return;
    }

    g_variant_get(parameters, "(&s)", &network_id);

    if (!zt_is_valid_network_id(network_id)) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                              "Invalid ZeroTier network id");
        return;
    }

    req = g_new0(GetStatusRequest, 1);
    req->invocation = g_object_ref(invocation);
    req->network_id = g_strdup(network_id);

    task = g_task_new(NULL, NULL, get_status_task_finished_cb, NULL);
    g_task_set_task_data(task, req, (GDestroyNotify) get_status_request_free);
    g_task_run_in_thread(task, get_status_thread_func);
    g_object_unref(task);
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = handle_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

static void
on_bus_acquired(GDBusConnection *connection, const char *name, gpointer user_data)
{
    NMZerotierStatusDBus *self = user_data;
    GError                 *error = NULL;

    (void) name;

    self->connection = connection;
    self->registration_id = g_dbus_connection_register_object(
        connection, NM_ZEROTIER_STATUS_OBJECT_PATH, self->introspection_data->interfaces[0],
        &interface_vtable, self, NULL, &error);

    if (self->registration_id == 0) {
        g_warning("zerotier-reconciled: failed to register D-Bus object: %s", error->message);
        g_clear_error(&error);
    }
}

static void
on_name_lost(GDBusConnection *connection, const char *name, gpointer user_data)
{
    /* Expected if another instance already owns the name, or the system bus
     * policy denies ownership (misconfigured .conf) - log and keep running;
     * the reconciler's own NM reconciliation is unaffected, this only
     * disables the editor's status query. */
    (void) connection;
    (void) user_data;

    g_warning("zerotier-reconciled: lost or failed to acquire D-Bus name %s", name);
}

NMZerotierStatusDBus *
nm_zerotier_status_dbus_new(void)
{
    NMZerotierStatusDBus *self = g_new0(NMZerotierStatusDBus, 1);

    self->introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    g_assert(self->introspection_data != NULL); /* static XML, a parse failure is a build-time bug */

    self->owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM, NM_ZEROTIER_STATUS_BUS_NAME,
                                    G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired, NULL, on_name_lost,
                                    self, NULL);

    return self;
}

void
nm_zerotier_status_dbus_free(NMZerotierStatusDBus *self)
{
    if (!self)
        return;

    if (self->registration_id && self->connection)
        g_dbus_connection_unregister_object(self->connection, self->registration_id);
    if (self->owner_id)
        g_bus_unown_name(self->owner_id);
    g_clear_pointer(&self->introspection_data, g_dbus_node_info_unref);
    g_free(self);
}
