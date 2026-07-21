/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-reconcile-diff.h"

#include <stdarg.h>

static GHashTable *
joined_set(const char *first, ...)
{
    GHashTable *set = g_hash_table_new(g_str_hash, g_str_equal);
    va_list     args;

    va_start(args, first);
    for (const char *id = first; id; id = va_arg(args, const char *))
        g_hash_table_add(set, (gpointer) id);
    va_end(args);

    return set;
}

static const ZtReconcileAction *
find_action(GPtrArray *actions, const char *ref)
{
    for (guint i = 0; i < actions->len; i++) {
        const ZtReconcileAction *action = g_ptr_array_index(actions, i);
        if (g_strcmp0(action->ref, ref) == 0)
            return action;
    }
    return NULL;
}

static void
test_joined_externally_activates(void)
{
    ZtReconcileCandidate c = { .network_id = "8056c2e21c000001", .ref = "conn-a", .nm_active = FALSE };
    GArray *arr = g_array_new(FALSE, FALSE, sizeof(ZtReconcileCandidate));
    g_array_append_val(arr, c);

    GHashTable *joined = joined_set("8056c2e21c000001", NULL);
    GPtrArray  *actions = zt_reconcile_compute_actions(arr, joined);

    g_assert_cmpuint(actions->len, ==, 1);
    const ZtReconcileAction *action = find_action(actions, "conn-a");
    g_assert_nonnull(action);
    g_assert_cmpint(action->type, ==, ZT_RECONCILE_ACTIVATE);

    g_ptr_array_unref(actions);
    g_hash_table_unref(joined);
    g_array_unref(arr);
}

static void
test_left_externally_deactivates(void)
{
    ZtReconcileCandidate c = { .network_id = "8056c2e21c000002", .ref = "conn-b", .nm_active = TRUE };
    GArray *arr = g_array_new(FALSE, FALSE, sizeof(ZtReconcileCandidate));
    g_array_append_val(arr, c);

    GHashTable *joined = joined_set(NULL); /* nothing joined */
    GPtrArray  *actions = zt_reconcile_compute_actions(arr, joined);

    g_assert_cmpuint(actions->len, ==, 1);
    const ZtReconcileAction *action = find_action(actions, "conn-b");
    g_assert_nonnull(action);
    g_assert_cmpint(action->type, ==, ZT_RECONCILE_DEACTIVATE);

    g_ptr_array_unref(actions);
    g_hash_table_unref(joined);
    g_array_unref(arr);
}

static void
test_already_in_sync_no_actions(void)
{
    ZtReconcileCandidate active_and_joined = {
        .network_id = "8056c2e21c000001", .ref = "conn-a", .nm_active = TRUE
    };
    ZtReconcileCandidate inactive_and_not_joined = {
        .network_id = "8056c2e21c000002", .ref = "conn-b", .nm_active = FALSE
    };
    GArray *arr = g_array_new(FALSE, FALSE, sizeof(ZtReconcileCandidate));
    g_array_append_val(arr, active_and_joined);
    g_array_append_val(arr, inactive_and_not_joined);

    GHashTable *joined = joined_set("8056c2e21c000001", NULL);
    GPtrArray  *actions = zt_reconcile_compute_actions(arr, joined);

    g_assert_cmpuint(actions->len, ==, 0);

    g_ptr_array_unref(actions);
    g_hash_table_unref(joined);
    g_array_unref(arr);
}

int
main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/zerotier-reconcile-diff/joined-externally-activates", test_joined_externally_activates);
    g_test_add_func("/zerotier-reconcile-diff/left-externally-deactivates", test_left_externally_deactivates);
    g_test_add_func("/zerotier-reconcile-diff/already-in-sync-no-actions", test_already_in_sync_no_actions);

    return g_test_run();
}
