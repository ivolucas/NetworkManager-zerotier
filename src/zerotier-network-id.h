/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ZEROTIER_NETWORK_ID_H
#define ZEROTIER_NETWORK_ID_H

#include <glib.h>

G_BEGIN_DECLS

/* A ZeroTier network id is exactly 16 lowercase hex characters. Shared
 * between the daemon (validating before join) and the GTK editor (live
 * validation while typing), so both agree on the same rule without pulling
 * in json-glib (needed elsewhere in the CLI wrapper) just for this check. */
gboolean zt_is_valid_network_id(const char *id);

G_END_DECLS

#endif /* ZEROTIER_NETWORK_ID_H */
