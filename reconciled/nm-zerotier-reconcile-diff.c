/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-reconcile-diff.h"

void
zt_reconcile_action_free(ZtReconcileAction *action)
{
    g_free(action);
}

GPtrArray *
zt_reconcile_compute_actions(GArray *candidates, GHashTable *joined_network_ids)
{
    GPtrArray *actions = g_ptr_array_new_with_free_func((GDestroyNotify) zt_reconcile_action_free);

    for (guint i = 0; i < candidates->len; i++) {
        const ZtReconcileCandidate *c = &g_array_index(candidates, ZtReconcileCandidate, i);
        gboolean joined = g_hash_table_contains(joined_network_ids, c->network_id);

        if (joined && !c->nm_active) {
            ZtReconcileAction *action = g_new0(ZtReconcileAction, 1);
            action->type = ZT_RECONCILE_ACTIVATE;
            action->ref = c->ref;
            g_ptr_array_add(actions, action);
        } else if (!joined && c->nm_active) {
            ZtReconcileAction *action = g_new0(ZtReconcileAction, 1);
            action->type = ZT_RECONCILE_DEACTIVATE;
            action->ref = c->ref;
            g_ptr_array_add(actions, action);
        }
    }

    return actions;
}
