/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-editor.h"
#include "nm-zerotier-service-defaults.h"
#include "zerotier-network-id.h"

#include <gtk/gtk.h>

struct _NMZerotierEditor {
    GObject    parent;
    GtkWidget *widget;
    GtkWidget *network_id_entry;
    GtkWidget *network_id_error_label;
    GtkWidget *allow_default_check;
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

    g_object_ref(self->widget);
    g_object_unref(builder);

    NMSettingVpn *s_vpn = connection ? nm_connection_get_setting_vpn(connection) : NULL;
    if (s_vpn) {
        const char *network_id = nm_setting_vpn_get_data_item(s_vpn, NM_ZEROTIER_KEY_NETWORK_ID);
        const char *allow_default =
            nm_setting_vpn_get_data_item(s_vpn, NM_ZEROTIER_KEY_ALLOW_DEFAULT);

        if (network_id)
            gtk_editable_set_text(GTK_EDITABLE(self->network_id_entry), network_id);
        if (g_strcmp0(allow_default, "yes") == 0)
            gtk_check_button_set_active(GTK_CHECK_BUTTON(self->allow_default_check), TRUE);
    }

    g_signal_connect(self->network_id_entry, "changed", G_CALLBACK(entry_changed_cb), self);
    g_signal_connect(self->allow_default_check, "toggled",
                     G_CALLBACK(allow_default_toggled_cb), self);

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
