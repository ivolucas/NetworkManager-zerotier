/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_ZEROTIER_EDITOR_H
#define NM_ZEROTIER_EDITOR_H

#include <NetworkManager.h>

G_BEGIN_DECLS

#define NM_TYPE_ZEROTIER_EDITOR (nm_zerotier_editor_get_type())

G_DECLARE_FINAL_TYPE(NMZerotierEditor, nm_zerotier_editor, NM, ZEROTIER_EDITOR, GObject)

NMVpnEditor *nm_zerotier_editor_new(NMConnection *connection, GError **error);

G_END_DECLS

#endif /* NM_ZEROTIER_EDITOR_H */
