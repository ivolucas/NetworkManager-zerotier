/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_ZEROTIER_STATUS_DBUS_H
#define NM_ZEROTIER_STATUS_DBUS_H

#include <gio/gio.h>

#include "zerotier-cli-wrapper.h"

G_BEGIN_DECLS

typedef struct _NMZerotierStatusDBus NMZerotierStatusDBus;

/* Owns org.zerotier.nm.Status on the system bus and serves
 * GetNetworkStatus by shelling out to zerotier-cli on demand (via
 * zt_cli_find_network(), the same wrapper the reconciler's own poll loop
 * uses). This is a separate, request-driven call per D-Bus method
 * invocation, NOT a second poller - see CLAUDE.md's "Bidirectional
 * reconciliation" design note. Each request runs zerotier-cli on a GTask
 * worker thread so a slow/hung subprocess can't stall the daemon's main
 * loop (and therefore can't stall the reconciler's own debounce/NMClient
 * signal handling, which shares the same main loop).
 *
 * Lets the unprivileged GTK4 connection-editor (properties/) query live
 * ZeroTier network status without needing zerotier-one's root-owned
 * authtoken.secret itself. Hosted inside nm-zerotier-reconciled (the only
 * persistent root daemon this plugin has) but deliberately named/scoped
 * apart from reconciliation - this is a read-only status query, unrelated
 * to the reconciler's own drift-correction responsibilities. */
NMZerotierStatusDBus *nm_zerotier_status_dbus_new(void);
void                  nm_zerotier_status_dbus_free(NMZerotierStatusDBus *self);

/* Builds the "a{sv}" info payload for a single ZtNetworkInfo. Exposed
 * (non-static) so it can be exercised directly in unit tests without a
 * running D-Bus daemon - same rationale as zt_cli_parse_networks_json()
 * being kept separate from zt_cli_list_networks(). */
GVariant *nm_zerotier_status_dbus_network_info_to_variant(const ZtNetworkInfo *info);

G_END_DECLS

#endif /* NM_ZEROTIER_STATUS_DBUS_H */
