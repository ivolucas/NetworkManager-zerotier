/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_ZEROTIER_RECONCILE_DIFF_H
#define NM_ZEROTIER_RECONCILE_DIFF_H

#include <glib.h>

G_BEGIN_DECLS

/* Pure diffing logic for the reconciler, kept free of libnm/D-Bus types so it
 * can be exercised in unit tests without a running NetworkManager or
 * zerotier-one. The reconciler translates real NMConnection/NMActiveConnection
 * objects into these plain structs, calls zt_reconcile_compute_actions(), then
 * translates the resulting actions back to real D-Bus calls. */

typedef struct {
    const char *network_id; /* borrowed */
    const char *ref;         /* borrowed opaque id (e.g. connection UUID) the
                              * caller uses to map an action back to the real
                              * NMConnection/NMActiveConnection object */
    gboolean    nm_active;   /* TRUE if NM currently considers this connection
                              * ACTIVATING or ACTIVATED */
} ZtReconcileCandidate;

typedef enum {
    ZT_RECONCILE_ACTIVATE,
    ZT_RECONCILE_DEACTIVATE,
} ZtReconcileActionType;

typedef struct {
    ZtReconcileActionType type;
    const char           *ref; /* borrowed, points into the matching candidate */
} ZtReconcileAction;

/* candidates: GArray of ZtReconcileCandidate (by value).
 * joined_network_ids: set of currently-joined network-id strings (GHashTable
 * used as a set, e.g. g_hash_table_new(g_str_hash, g_str_equal) with NULL
 * values).
 * Returns a newly allocated GPtrArray of newly allocated ZtReconcileAction*
 * (free with g_ptr_array_unref, element destroy notify is zt_reconcile_action_free). */
GPtrArray *zt_reconcile_compute_actions(GArray *candidates, GHashTable *joined_network_ids);

void zt_reconcile_action_free(ZtReconcileAction *action);

G_END_DECLS

#endif /* NM_ZEROTIER_RECONCILE_DIFF_H */
