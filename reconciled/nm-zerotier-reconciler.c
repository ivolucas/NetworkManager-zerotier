/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-reconciler.h"
#include "nm-zerotier-reconcile-diff.h"
#include "nm-zerotier-service-defaults.h"
#include "zerotier-cli-wrapper.h"
#include "zerotier-network-id.h"

#include <NetworkManager.h>

/* Interface add/remove is the only externally-visible side effect of a
 * `zerotier-cli join`/`leave` run outside NetworkManager (zerotier-one has no
 * event bus of its own). NetworkManager's core already watches netlink and
 * creates/destroys an NMDevice for every interface on the system - including
 * ones it doesn't manage - and NMClient surfaces that as
 * any-device-added/any-device-removed. Debounced, that becomes our trigger to
 * re-check `zerotier-cli listnetworks`, instead of polling it on a fixed
 * timer. active-connection-added/removed covers the other direction: NM
 * connections activated/deactivated by nmcli/GNOME Settings/another client. */
#define DEBOUNCE_MS            750
#define SAFETY_NET_INTERVAL_S  300
#define ACTIVATE_COOLDOWN_S     60

struct _NMZerotierReconciler {
    GObject     parent;
    NMClient   *client;
    guint       debounce_source_id;
    guint       safety_net_source_id;
    gboolean    poll_in_flight;
    gboolean    rerun_requested;
    GHashTable *cooldown_until; /* char* network_id -> gint64* monotonic deadline, owned */
};

G_DEFINE_TYPE(NMZerotierReconciler, nm_zerotier_reconciler, G_TYPE_OBJECT)

static void request_reconcile(NMZerotierReconciler *self);

/* --- resolving NM's current view of zerotier connections/active-connections --- */

typedef struct {
    char               *network_id; /* owned */
    char               *uuid;        /* owned copy, used as the diff "ref" */
    NMConnection       *connection;  /* borrowed, owned by NMClient */
    NMActiveConnection *active;      /* borrowed, owned by NMClient, or NULL */
} ResolvedCandidate;

static void
resolved_candidate_clear(ResolvedCandidate *rc)
{
    g_free(rc->network_id);
    g_free(rc->uuid);
}

/* Returns a newly allocated GArray of ResolvedCandidate (by value, with
 * resolved_candidate_clear as element clear func) for every NM connection
 * profile that is a zerotier VPN connection with a valid network-id. */
static GArray *
resolve_candidates(NMClient *client)
{
    GArray *out = g_array_new(FALSE, FALSE, sizeof(ResolvedCandidate));
    g_array_set_clear_func(out, (GDestroyNotify) resolved_candidate_clear);

    const GPtrArray *connections = nm_client_get_connections(client);
    const GPtrArray *actives = nm_client_get_active_connections(client);

    for (guint i = 0; i < connections->len; i++) {
        NMConnection *connection = g_ptr_array_index(connections, i);
        NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(connection);

        if (!s_vpn)
            continue;
        if (g_strcmp0(nm_setting_vpn_get_service_type(s_vpn), NM_DBUS_SERVICE_ZEROTIER) != 0)
            continue;

        const char *network_id = nm_setting_vpn_get_data_item(s_vpn, NM_ZEROTIER_KEY_NETWORK_ID);
        if (!zt_is_valid_network_id(network_id))
            continue;

        ResolvedCandidate rc = {
            .network_id = g_strdup(network_id),
            .uuid = g_strdup(nm_connection_get_uuid(connection)),
            .connection = connection,
            .active = NULL,
        };

        for (guint j = 0; j < actives->len; j++) {
            NMActiveConnection *ac = g_ptr_array_index(actives, j);
            NMRemoteConnection *ac_connection = nm_active_connection_get_connection(ac);
            NMActiveConnectionState state = nm_active_connection_get_state(ac);

            if (ac_connection && g_strcmp0(nm_connection_get_uuid(NM_CONNECTION(ac_connection)), rc.uuid) == 0
                && (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATING
                    || state == NM_ACTIVE_CONNECTION_STATE_ACTIVATED)) {
                rc.active = ac;
                break;
            }
        }

        g_array_append_val(out, rc);
    }

    return out;
}

static ResolvedCandidate *
find_resolved_by_uuid(GArray *resolved, const char *uuid)
{
    for (guint i = 0; i < resolved->len; i++) {
        ResolvedCandidate *rc = &g_array_index(resolved, ResolvedCandidate, i);
        if (g_strcmp0(rc->uuid, uuid) == 0)
            return rc;
    }
    return NULL;
}

/* --- cooldown: don't hammer ActivateConnection for a network that keeps
 * failing to come up (e.g. never authorized on the controller) --- */

static gboolean
in_cooldown(NMZerotierReconciler *self, const char *network_id)
{
    gint64 *deadline = g_hash_table_lookup(self->cooldown_until, network_id);
    return deadline && g_get_monotonic_time() < *deadline;
}

static void
start_cooldown(NMZerotierReconciler *self, const char *network_id)
{
    gint64 *deadline = g_new(gint64, 1);
    *deadline = g_get_monotonic_time() + (gint64) ACTIVATE_COOLDOWN_S * G_USEC_PER_SEC;
    g_hash_table_replace(self->cooldown_until, g_strdup(network_id), deadline);
}

static void
clear_cooldown(NMZerotierReconciler *self, const char *network_id)
{
    g_hash_table_remove(self->cooldown_until, network_id);
}

/* --- applying actions: normal NMClient calls, same as any other NM client
 * (nmcli, GNOME Settings) would make - the reconciler has no special access
 * to the per-connection VPN plugin process --- */

typedef struct {
    NMZerotierReconciler *reconciler; /* borrowed, lives at least as long as NMClient's mainloop */
    char                  *network_id;
} ActionContext;

static ActionContext *
action_context_new(NMZerotierReconciler *self, const char *network_id)
{
    ActionContext *ctx = g_new0(ActionContext, 1);
    ctx->reconciler = self;
    ctx->network_id = g_strdup(network_id);
    return ctx;
}

static void
action_context_free(ActionContext *ctx)
{
    g_free(ctx->network_id);
    g_free(ctx);
}

static void
activate_finished_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    ActionContext *ctx = user_data;
    GError        *error = NULL;

    if (!nm_client_activate_connection_finish(NM_CLIENT(source), res, &error)) {
        g_warning("zerotier-reconciled: failed to activate connection for network %s: %s",
                 ctx->network_id, error->message);
        g_clear_error(&error);
    } else {
        clear_cooldown(ctx->reconciler, ctx->network_id);
        g_message("zerotier-reconciled: activated NM connection for externally-joined network %s",
                 ctx->network_id);
    }

    action_context_free(ctx);
}

static void
deactivate_finished_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    ActionContext *ctx = user_data;
    GError        *error = NULL;

    if (!nm_client_deactivate_connection_finish(NM_CLIENT(source), res, &error)) {
        g_warning("zerotier-reconciled: failed to deactivate connection for network %s: %s",
                 ctx->network_id, error->message);
        g_clear_error(&error);
    } else {
        g_message("zerotier-reconciled: deactivated NM connection for externally-left network %s",
                 ctx->network_id);
    }

    action_context_free(ctx);
}

/* --- auto-importing networks joined externally with no matching NM
 * connection profile at all yet (as opposed to apply_action() above, which
 * only (de)activates profiles that already exist). Gated on the tun
 * interface actually existing (zerotier-one has assigned it a
 * portDeviceName) - being visible in NetworkManager is just surfacing state
 * that's already real at that point, not creating a new tunnel. Mirrors
 * contrib/import-zerotier-networks.sh's "zt-<name-or-nwid>" naming and
 * leaves allow-default unset (same safe default). --- */

static void
import_finished_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    ActionContext *ctx = user_data;
    GError        *error = NULL;

    if (!nm_client_add_and_activate_connection_finish(NM_CLIENT(source), res, &error)) {
        g_warning("zerotier-reconciled: failed to import connection for network %s: %s",
                 ctx->network_id, error->message);
        g_clear_error(&error);
    } else {
        clear_cooldown(ctx->reconciler, ctx->network_id);
        g_message("zerotier-reconciled: imported and activated a new NM connection for "
                 "externally-joined network %s", ctx->network_id);
    }

    action_context_free(ctx);
}

static void
import_network(NMZerotierReconciler *self, const ZtNetworkInfo *info)
{
    if (in_cooldown(self, info->nwid))
        return;
    start_cooldown(self, info->nwid);

    const char *display_name = (info->name && *info->name) ? info->name : info->nwid;
    char       *label = g_strdup_printf("zt-%s", display_name);
    char       *uuid = nm_utils_uuid_generate();

    NMConnection *connection = nm_simple_connection_new();

    NMSetting *s_con = nm_setting_connection_new();
    g_object_set(s_con,
                NM_SETTING_CONNECTION_ID, label,
                NM_SETTING_CONNECTION_UUID, uuid,
                NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME,
                NULL);
    nm_connection_add_setting(connection, s_con);

    NMSetting *s_vpn = nm_setting_vpn_new();
    g_object_set(s_vpn, NM_SETTING_VPN_SERVICE_TYPE, NM_DBUS_SERVICE_ZEROTIER, NULL);
    nm_setting_vpn_add_data_item(NM_SETTING_VPN(s_vpn), NM_ZEROTIER_KEY_NETWORK_ID, info->nwid);
    nm_connection_add_setting(connection, s_vpn);

    nm_client_add_and_activate_connection_async(self->client, connection, NULL, NULL, NULL,
                                                import_finished_cb,
                                                action_context_new(self, info->nwid));

    g_object_unref(connection);
    g_free(uuid);
    g_free(label);
}

static void
import_orphan_networks(NMZerotierReconciler *self, GPtrArray *networks, GArray *resolved)
{
    GHashTable *known_network_ids = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < resolved->len; i++) {
        ResolvedCandidate *rc = &g_array_index(resolved, ResolvedCandidate, i);
        g_hash_table_add(known_network_ids, rc->network_id);
    }

    for (guint i = 0; i < networks->len; i++) {
        ZtNetworkInfo *info = g_ptr_array_index(networks, i);

        if (!info->port_device_name || !*info->port_device_name)
            continue; /* no tun interface yet - nothing real to surface */
        if (g_hash_table_contains(known_network_ids, info->nwid))
            continue; /* already has a matching NM connection profile */

        import_network(self, info);
    }

    g_hash_table_unref(known_network_ids);
}

static void
apply_action(NMZerotierReconciler *self, const ZtReconcileAction *action, GArray *resolved)
{
    ResolvedCandidate *rc = find_resolved_by_uuid(resolved, action->ref);
    if (!rc)
        return;

    if (action->type == ZT_RECONCILE_ACTIVATE) {
        if (in_cooldown(self, rc->network_id))
            return;
        start_cooldown(self, rc->network_id);
        nm_client_activate_connection_async(self->client, rc->connection, NULL, NULL, NULL,
                                            activate_finished_cb,
                                            action_context_new(self, rc->network_id));
    } else {
        nm_client_deactivate_connection_async(self->client, rc->active, NULL, deactivate_finished_cb,
                                              action_context_new(self, rc->network_id));
    }
}

/* --- the reconcile pass itself: poll zerotier-cli on a worker thread, then
 * diff against NM's current state on the main thread --- */

static void
list_networks_thread_func(GTask *task, gpointer source_object, gpointer task_data,
                          GCancellable *cancellable)
{
    (void) source_object;
    (void) task_data;
    (void) cancellable;

    GError    *error = NULL;
    GPtrArray *networks = zt_cli_list_networks(&error);

    if (!networks) {
        g_task_return_error(task, error);
        return;
    }
    g_task_return_pointer(task, networks, (GDestroyNotify) g_ptr_array_unref);
}

static void
reconcile_task_finished_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    NMZerotierReconciler *self = NM_ZEROTIER_RECONCILER(source);

    (void) user_data;
    self->poll_in_flight = FALSE;

    GError    *error = NULL;
    GPtrArray *networks = g_task_propagate_pointer(G_TASK(res), &error);

    if (error) {
        g_debug("zerotier-reconciled: transient error listing networks: %s", error->message);
        g_clear_error(&error);
    } else {
        GHashTable *joined = g_hash_table_new(g_str_hash, g_str_equal);
        for (guint i = 0; i < networks->len; i++) {
            ZtNetworkInfo *info = g_ptr_array_index(networks, i);
            g_hash_table_add(joined, info->nwid);
        }

        GArray *resolved = resolve_candidates(self->client);
        GArray *candidates = g_array_new(FALSE, FALSE, sizeof(ZtReconcileCandidate));
        for (guint i = 0; i < resolved->len; i++) {
            ResolvedCandidate    *rc = &g_array_index(resolved, ResolvedCandidate, i);
            ZtReconcileCandidate  c = {
                .network_id = rc->network_id,
                .ref = rc->uuid,
                .nm_active = rc->active != NULL,
            };
            g_array_append_val(candidates, c);
        }

        GPtrArray *actions = zt_reconcile_compute_actions(candidates, joined);
        for (guint i = 0; i < actions->len; i++)
            apply_action(self, g_ptr_array_index(actions, i), resolved);

        import_orphan_networks(self, networks, resolved);

        g_ptr_array_unref(actions);
        g_array_unref(candidates);
        g_array_unref(resolved);
        g_hash_table_unref(joined);
        g_ptr_array_unref(networks);
    }

    if (self->rerun_requested) {
        self->rerun_requested = FALSE;
        request_reconcile(self);
    }
}

static void
request_reconcile(NMZerotierReconciler *self)
{
    if (!self->client)
        return; /* not connected to NM yet */

    if (self->poll_in_flight) {
        self->rerun_requested = TRUE;
        return;
    }

    self->poll_in_flight = TRUE;

    GTask *task = g_task_new(self, NULL, reconcile_task_finished_cb, NULL);
    g_task_run_in_thread(task, list_networks_thread_func);
    g_object_unref(task);
}

/* --- debounce: coalesce a burst of NM signals into one reconcile pass --- */

static gboolean
debounce_fire_cb(gpointer user_data)
{
    NMZerotierReconciler *self = NM_ZEROTIER_RECONCILER(user_data);

    self->debounce_source_id = 0;
    request_reconcile(self);
    return G_SOURCE_REMOVE;
}

static void
schedule_debounced_reconcile(NMZerotierReconciler *self)
{
    if (self->debounce_source_id)
        g_source_remove(self->debounce_source_id);
    self->debounce_source_id = g_timeout_add(DEBOUNCE_MS, debounce_fire_cb, self);
}

static void
on_device_changed(NMClient *client, NMDevice *device, gpointer user_data)
{
    (void) client;
    (void) device;
    schedule_debounced_reconcile(NM_ZEROTIER_RECONCILER(user_data));
}

static void
on_active_connection_changed(NMClient *client, NMActiveConnection *active, gpointer user_data)
{
    (void) client;
    (void) active;
    schedule_debounced_reconcile(NM_ZEROTIER_RECONCILER(user_data));
}

/* --- startup poll (baseline) and sparse safety-net poll (fallback against a
 * missed/coalesced device event) --- */

static gboolean
startup_poll_idle_cb(gpointer user_data)
{
    request_reconcile(NM_ZEROTIER_RECONCILER(user_data));
    return G_SOURCE_REMOVE;
}

static gboolean
safety_net_tick_cb(gpointer user_data)
{
    request_reconcile(NM_ZEROTIER_RECONCILER(user_data));
    return G_SOURCE_CONTINUE;
}

/* --- connecting to NetworkManager --- */

static void
client_ready_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    NMZerotierReconciler *self = NM_ZEROTIER_RECONCILER(user_data);
    GError               *error = NULL;

    (void) source;

    self->client = nm_client_new_finish(res, &error);
    if (!self->client) {
        g_warning("zerotier-reconciled: failed to connect to NetworkManager: %s", error->message);
        g_clear_error(&error);
        return;
    }

    g_signal_connect(self->client, NM_CLIENT_ANY_DEVICE_ADDED, G_CALLBACK(on_device_changed), self);
    g_signal_connect(self->client, NM_CLIENT_ANY_DEVICE_REMOVED, G_CALLBACK(on_device_changed), self);
    g_signal_connect(self->client, NM_CLIENT_ACTIVE_CONNECTION_ADDED,
                     G_CALLBACK(on_active_connection_changed), self);
    g_signal_connect(self->client, NM_CLIENT_ACTIVE_CONNECTION_REMOVED,
                     G_CALLBACK(on_active_connection_changed), self);

    g_idle_add(startup_poll_idle_cb, self);
    self->safety_net_source_id = g_timeout_add_seconds(SAFETY_NET_INTERVAL_S, safety_net_tick_cb, self);
}

NMZerotierReconciler *
nm_zerotier_reconciler_new(void)
{
    NMZerotierReconciler *self = g_object_new(NM_TYPE_ZEROTIER_RECONCILER, NULL);

    nm_client_new_async(NULL, client_ready_cb, self);
    return self;
}

static void
nm_zerotier_reconciler_init(NMZerotierReconciler *self)
{
    self->cooldown_until = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

static void
dispose(GObject *object)
{
    NMZerotierReconciler *self = NM_ZEROTIER_RECONCILER(object);

    if (self->debounce_source_id) {
        g_source_remove(self->debounce_source_id);
        self->debounce_source_id = 0;
    }
    if (self->safety_net_source_id) {
        g_source_remove(self->safety_net_source_id);
        self->safety_net_source_id = 0;
    }
    g_clear_pointer(&self->cooldown_until, g_hash_table_unref);
    g_clear_object(&self->client);

    G_OBJECT_CLASS(nm_zerotier_reconciler_parent_class)->dispose(object);
}

static void
nm_zerotier_reconciler_class_init(NMZerotierReconcilerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = dispose;
}
