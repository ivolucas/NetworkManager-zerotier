/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-editor.h"
#include "nm-zerotier-service-defaults.h"
#include "zerotier-cli-wrapper.h"
#include "zerotier-network-id.h"

#include <gtk/gtk.h>

struct _NMZerotierEditor {
    GObject    parent;
    GtkWidget *widget;
    GtkWidget *network_id_entry;
    GtkWidget *network_id_error_label;
    GtkWidget *allow_default_check;
    GtkWidget *status_section;
    GtkWidget *name_value_label;
    GtkWidget *type_value_label;
    GtkWidget *status_value_label;
    GtkWidget *interface_value_label;
    GtkWidget *mac_value_label;
    GtkWidget *mtu_value_label;
    GtkWidget *addresses_value_label;
    GtkWidget *routes_value_label;
    GtkWidget *permissions_value_label;
    GtkWidget *features_value_label;
    GtkWidget *refresh_button;
};

static void nm_zerotier_editor_iface_init(NMVpnEditorInterface *iface);

G_DEFINE_TYPE_WITH_CODE(NMZerotierEditor,
                        nm_zerotier_editor,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR, nm_zerotier_editor_iface_init))

static void
entry_changed_cb(GtkEditable *editable, gpointer user_data)
{
    NMZerotierEditor *self = NM_ZEROTIER_EDITOR(user_data);
    const char       *text = gtk_editable_get_text(editable);
    gboolean          empty = !text || !*text;
    gboolean          valid = empty || zt_is_valid_network_id(text);

    gtk_widget_set_visible(self->network_id_error_label, !valid);

    g_signal_emit_by_name(self, "changed");
}

static void
allow_default_toggled_cb(GtkCheckButton *button, gpointer user_data)
{
    (void) button;
    g_signal_emit_by_name(NM_ZEROTIER_EDITOR(user_data), "changed");
}

static GObject *
get_widget(NMVpnEditor *editor)
{
    return G_OBJECT(NM_ZEROTIER_EDITOR(editor)->widget);
}

static gboolean
update_connection(NMVpnEditor *editor, NMConnection *connection, GError **error)
{
    NMZerotierEditor *self = NM_ZEROTIER_EDITOR(editor);
    const char        *network_id = gtk_editable_get_text(GTK_EDITABLE(self->network_id_entry));

    if (!zt_is_valid_network_id(network_id)) {
        g_set_error(error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_INVALID_PROPERTY,
                   "Network ID must be exactly 16 hex characters (0-9, a-f)");
        return FALSE;
    }

    NMSettingVpn *s_vpn = nm_connection_get_setting_vpn(connection);
    if (!s_vpn) {
        s_vpn = NM_SETTING_VPN(nm_setting_vpn_new());
        nm_connection_add_setting(connection, NM_SETTING(s_vpn));
    }

    g_object_set(s_vpn, NM_SETTING_VPN_SERVICE_TYPE, NM_DBUS_SERVICE_ZEROTIER, NULL);
    nm_setting_vpn_add_data_item(s_vpn, NM_ZEROTIER_KEY_NETWORK_ID, network_id);

    gboolean allow_default =
        gtk_check_button_get_active(GTK_CHECK_BUTTON(self->allow_default_check));
    nm_setting_vpn_add_data_item(s_vpn, NM_ZEROTIER_KEY_ALLOW_DEFAULT,
                                 allow_default ? "yes" : "no");

    return TRUE;
}

/* Joins a GPtrArray of borrowed (not owned/freed) strings with ", ", or
 * returns a copy of empty_placeholder if the array is empty. */
static char *
join_strings(GPtrArray *arr, const char *empty_placeholder)
{
    if (!arr || arr->len == 0)
        return g_strdup(empty_placeholder);

    char **strv = g_new0(char *, arr->len + 1);
    for (guint i = 0; i < arr->len; i++)
        strv[i] = g_ptr_array_index(arr, i);
    char *joined = g_strjoinv(", ", strv);
    g_free(strv);
    return joined;
}

static void
set_unavailable_labels(NMZerotierEditor *self, const char *reason)
{
    gtk_label_set_text(GTK_LABEL(self->name_value_label), "—");
    gtk_label_set_text(GTK_LABEL(self->type_value_label), "—");
    gtk_label_set_text(GTK_LABEL(self->status_value_label), reason);
    gtk_label_set_text(GTK_LABEL(self->interface_value_label), "—");
    gtk_label_set_text(GTK_LABEL(self->mac_value_label), "—");
    gtk_label_set_text(GTK_LABEL(self->mtu_value_label), "—");
    gtk_label_set_text(GTK_LABEL(self->addresses_value_label), "—");
    gtk_label_set_text(GTK_LABEL(self->routes_value_label), "—");
    gtk_label_set_text(GTK_LABEL(self->permissions_value_label), "—");
    gtk_label_set_text(GTK_LABEL(self->features_value_label), "—");
}

/* Read-only snapshot of the network's current state, fetched synchronously
 * (a local zerotier-cli round-trip, typically well under 100ms) once when
 * the dialog opens - not live-updated while it stays open. Only meaningful
 * for an existing connection with a valid network id; a brand-new,
 * not-yet-saved connection has nothing to report. */
static void
populate_status_section(NMZerotierEditor *self, const char *network_id)
{
    if (!network_id || !zt_is_valid_network_id(network_id)) {
        gtk_widget_set_visible(self->status_section, FALSE);
        return;
    }

    gtk_widget_set_visible(self->status_section, TRUE);

    GError        *error = NULL;
    ZtNetworkInfo *info = zt_cli_find_network(network_id, &error);

    if (error) {
        set_unavailable_labels(self, "Unavailable");
        gtk_label_set_text(GTK_LABEL(self->addresses_value_label), error->message);
        g_clear_error(&error);
        return;
    }

    if (!info) {
        set_unavailable_labels(self, "Not currently joined");
        return;
    }

    gtk_label_set_text(GTK_LABEL(self->name_value_label),
                       (info->name && *info->name) ? info->name : "—");
    gtk_label_set_text(GTK_LABEL(self->type_value_label),
                       (info->type && *info->type) ? info->type : "—");
    gtk_label_set_text(GTK_LABEL(self->status_value_label),
                       (info->status && *info->status) ? info->status : "Unknown");
    gtk_label_set_text(GTK_LABEL(self->interface_value_label),
                       (info->port_device_name && *info->port_device_name)
                           ? info->port_device_name
                           : "—");
    gtk_label_set_text(GTK_LABEL(self->mac_value_label),
                       (info->mac && *info->mac) ? info->mac : "—");

    char *mtu_text = g_strdup_printf("%d", info->mtu);
    gtk_label_set_text(GTK_LABEL(self->mtu_value_label), mtu_text);
    g_free(mtu_text);

    char *addresses_text = join_strings(info->assigned_addresses, "—");
    gtk_label_set_text(GTK_LABEL(self->addresses_value_label), addresses_text);
    g_free(addresses_text);

    char *routes_text = join_strings(info->routes, "—");
    gtk_label_set_text(GTK_LABEL(self->routes_value_label), routes_text);
    g_free(routes_text);

    GPtrArray *permissions = g_ptr_array_new();
    if (info->allow_managed)
        g_ptr_array_add(permissions, (gpointer) "Managed");
    if (info->allow_global)
        g_ptr_array_add(permissions, (gpointer) "Global");
    if (info->allow_default)
        g_ptr_array_add(permissions, (gpointer) "Default route");
    if (info->allow_dns)
        g_ptr_array_add(permissions, (gpointer) "DNS");
    char *permissions_text = join_strings(permissions, "None");
    gtk_label_set_text(GTK_LABEL(self->permissions_value_label), permissions_text);
    g_free(permissions_text);
    g_ptr_array_unref(permissions);

    GPtrArray *features = g_ptr_array_new();
    if (info->bridge)
        g_ptr_array_add(features, (gpointer) "Bridge");
    if (info->broadcast_enabled)
        g_ptr_array_add(features, (gpointer) "Broadcast");
    char *features_text = join_strings(features, "None");
    gtk_label_set_text(GTK_LABEL(self->features_value_label), features_text);
    g_free(features_text);
    g_ptr_array_unref(features);

    zt_network_info_free(info);
}

static void
refresh_button_clicked_cb(GtkButton *button, gpointer user_data)
{
    NMZerotierEditor *self = NM_ZEROTIER_EDITOR(user_data);

    /* populate_status_section() blocks briefly on a zerotier-cli
     * round-trip; disable the button for that short window so repeated
     * clicks don't pile up (there's nothing to cancel/queue here). */
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    populate_status_section(self, gtk_editable_get_text(GTK_EDITABLE(self->network_id_entry)));
    gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);
}

static void
nm_zerotier_editor_iface_init(NMVpnEditorInterface *iface)
{
    iface->get_widget = get_widget;
    iface->update_connection = update_connection;
}

NMVpnEditor *
nm_zerotier_editor_new(NMConnection *connection, GError **error)
{
    NMZerotierEditor *self = g_object_new(NM_TYPE_ZEROTIER_EDITOR, NULL);

    GtkBuilder *builder = gtk_builder_new_from_resource("/org/zerotier/nm/nm-connection-editor.ui");

    self->widget =
        GTK_WIDGET(gtk_builder_get_object(builder, "zerotier_editor_widget"));
    self->network_id_entry = GTK_WIDGET(gtk_builder_get_object(builder, "network_id_entry"));
    self->network_id_error_label =
        GTK_WIDGET(gtk_builder_get_object(builder, "network_id_error_label"));
    self->allow_default_check =
        GTK_WIDGET(gtk_builder_get_object(builder, "allow_default_check"));
    self->status_section = GTK_WIDGET(gtk_builder_get_object(builder, "status_section"));
    self->name_value_label = GTK_WIDGET(gtk_builder_get_object(builder, "name_value_label"));
    self->type_value_label = GTK_WIDGET(gtk_builder_get_object(builder, "type_value_label"));
    self->status_value_label = GTK_WIDGET(gtk_builder_get_object(builder, "status_value_label"));
    self->interface_value_label =
        GTK_WIDGET(gtk_builder_get_object(builder, "interface_value_label"));
    self->mac_value_label = GTK_WIDGET(gtk_builder_get_object(builder, "mac_value_label"));
    self->mtu_value_label = GTK_WIDGET(gtk_builder_get_object(builder, "mtu_value_label"));
    self->addresses_value_label =
        GTK_WIDGET(gtk_builder_get_object(builder, "addresses_value_label"));
    self->routes_value_label = GTK_WIDGET(gtk_builder_get_object(builder, "routes_value_label"));
    self->permissions_value_label =
        GTK_WIDGET(gtk_builder_get_object(builder, "permissions_value_label"));
    self->features_value_label =
        GTK_WIDGET(gtk_builder_get_object(builder, "features_value_label"));
    self->refresh_button = GTK_WIDGET(gtk_builder_get_object(builder, "refresh_button"));

    g_object_ref(self->widget);
    g_object_unref(builder);

    NMSettingVpn *s_vpn = connection ? nm_connection_get_setting_vpn(connection) : NULL;
    const char   *network_id = NULL;
    if (s_vpn) {
        network_id = nm_setting_vpn_get_data_item(s_vpn, NM_ZEROTIER_KEY_NETWORK_ID);
        const char *allow_default =
            nm_setting_vpn_get_data_item(s_vpn, NM_ZEROTIER_KEY_ALLOW_DEFAULT);

        if (network_id)
            gtk_editable_set_text(GTK_EDITABLE(self->network_id_entry), network_id);
        if (g_strcmp0(allow_default, "yes") == 0)
            gtk_check_button_set_active(GTK_CHECK_BUTTON(self->allow_default_check), TRUE);
    }
    populate_status_section(self, network_id);

    g_signal_connect(self->network_id_entry, "changed", G_CALLBACK(entry_changed_cb), self);
    g_signal_connect(self->allow_default_check, "toggled",
                     G_CALLBACK(allow_default_toggled_cb), self);
    g_signal_connect(self->refresh_button, "clicked", G_CALLBACK(refresh_button_clicked_cb), self);

    (void) error;
    return NM_VPN_EDITOR(self);
}

static void
nm_zerotier_editor_init(NMZerotierEditor *self)
{
    (void) self;
}

static void
dispose(GObject *object)
{
    NMZerotierEditor *self = NM_ZEROTIER_EDITOR(object);

    g_clear_object(&self->widget);

    G_OBJECT_CLASS(nm_zerotier_editor_parent_class)->dispose(object);
}

static void
nm_zerotier_editor_class_init(NMZerotierEditorClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = dispose;
}
