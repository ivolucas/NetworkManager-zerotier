/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_ZEROTIER_PLUGIN_H
#define NM_ZEROTIER_PLUGIN_H

#include <NetworkManager.h>

G_BEGIN_DECLS

#define NM_TYPE_ZEROTIER_PLUGIN (nm_zerotier_plugin_get_type())

G_DECLARE_FINAL_TYPE(NMZerotierPlugin, nm_zerotier_plugin, NM, ZEROTIER_PLUGIN, NMVpnServicePlugin)

NMZerotierPlugin *nm_zerotier_plugin_new(const char *bus_name);

G_END_DECLS

#endif /* NM_ZEROTIER_PLUGIN_H */
