/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_ZEROTIER_EDITOR_PLUGIN_H
#define NM_ZEROTIER_EDITOR_PLUGIN_H

#include <NetworkManager.h>

G_BEGIN_DECLS

#define NM_TYPE_ZEROTIER_EDITOR_PLUGIN (nm_zerotier_editor_plugin_get_type())

G_DECLARE_FINAL_TYPE(NMZerotierEditorPlugin,
                     nm_zerotier_editor_plugin,
                     NM,
                     ZEROTIER_EDITOR_PLUGIN,
                     GObject)

G_END_DECLS

#endif /* NM_ZEROTIER_EDITOR_PLUGIN_H */
