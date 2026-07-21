/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-editor-plugin.h"
#include "nm-zerotier-editor.h"
#include "nm-zerotier-service-defaults.h"

#include <gmodule.h>
#include <gtk/gtk.h>

extern GResource *zerotier_resources_get_resource(void);

struct _NMZerotierEditorPlugin {
    GObject parent;
};

enum {
    PROP_0,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_SERVICE,
};

static void nm_zerotier_editor_plugin_iface_init(NMVpnEditorPluginInterface *iface);

G_DEFINE_TYPE_WITH_CODE(
    NMZerotierEditorPlugin,
    nm_zerotier_editor_plugin,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR_PLUGIN, nm_zerotier_editor_plugin_iface_init))

static NMVpnEditor *
get_editor(NMVpnEditorPlugin *plugin, NMConnection *connection, GError **error)
{
    (void) plugin;
    return nm_zerotier_editor_new(connection, error);
}

static NMVpnEditorPluginCapability
get_capabilities(NMVpnEditorPlugin *plugin)
{
    (void) plugin;
    return NM_VPN_EDITOR_PLUGIN_CAPABILITY_NONE;
}

static void
nm_zerotier_editor_plugin_iface_init(NMVpnEditorPluginInterface *iface)
{
    iface->get_editor = get_editor;
    iface->get_capabilities = get_capabilities;
}

/* NM_VPN_EDITOR_PLUGIN_{NAME,DESCRIPTION,SERVICE} are read-only properties
 * declared on the NMVpnEditorPlugin interface itself - libnm's loader
 * queries them right after loading the .so to confirm it's a legitimate,
 * identifiable plugin (nm_vpn_plugin_info_load_editor_plugin() rejects the
 * module with "missing plugin name" otherwise). Trying to set them via
 * g_object_new(), as an earlier version of this file did, silently fails
 * (they're not writable) and leaves them NULL. There's nothing to
 * construct here - just answer with fixed values. */
static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    (void) object;

    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string(value, "zerotier");
        break;
    case PROP_DESCRIPTION:
        g_value_set_string(value, "Join and leave ZeroTier virtual networks.");
        break;
    case PROP_SERVICE:
        g_value_set_string(value, NM_DBUS_SERVICE_ZEROTIER);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
nm_zerotier_editor_plugin_init(NMZerotierEditorPlugin *self)
{
    (void) self;
}

static void
nm_zerotier_editor_plugin_class_init(NMZerotierEditorPluginClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = get_property;

    g_object_class_override_property(object_class, PROP_NAME, NM_VPN_EDITOR_PLUGIN_NAME);
    g_object_class_override_property(object_class, PROP_DESCRIPTION,
                                     NM_VPN_EDITOR_PLUGIN_DESCRIPTION);
    g_object_class_override_property(object_class, PROP_SERVICE, NM_VPN_EDITOR_PLUGIN_SERVICE);
}

G_MODULE_EXPORT NMVpnEditorPlugin *
nm_vpn_editor_plugin_factory(GError **error)
{
    if (error)
        *error = NULL;

    g_resources_register(zerotier_resources_get_resource());

    return NM_VPN_EDITOR_PLUGIN(g_object_new(NM_TYPE_ZEROTIER_EDITOR_PLUGIN, NULL));
}
