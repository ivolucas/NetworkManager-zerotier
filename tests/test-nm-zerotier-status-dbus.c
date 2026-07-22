/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nm-zerotier-status-dbus.h"
#include "zerotier-cli-wrapper.h"

static void
test_network_info_to_variant_full(void)
{
    const char *json = "["
        "{"
        "\"nwid\": \"8056c2e21c000001\","
        "\"name\": \"test-net\","
        "\"type\": \"PRIVATE\","
        "\"mac\": \"0e:f9:56:9d:f7:09\","
        "\"mtu\": 2800,"
        "\"bridge\": false,"
        "\"broadcastEnabled\": true,"
        "\"allowManaged\": true,"
        "\"allowGlobal\": false,"
        "\"allowDefault\": false,"
        "\"allowDNS\": false,"
        "\"routes\": [{\"target\": \"10.1.0.0/24\", \"via\": null}, "
        "{\"target\": \"0.0.0.0/0\", \"via\": \"10.1.0.1\"}],"
        "\"status\": \"OK\","
        "\"portDeviceName\": \"ztabcdefgh\","
        "\"assignedAddresses\": [\"10.1.2.3/24\", \"fd00::1/88\"]"
        "}"
        "]";

    GError    *error = NULL;
    GPtrArray *networks = zt_cli_parse_networks_json(json, &error);

    g_assert_no_error(error);
    g_assert_nonnull(networks);
    g_assert_cmpuint(networks->len, ==, 1);

    ZtNetworkInfo *info = g_ptr_array_index(networks, 0);
    GVariant      *v = nm_zerotier_status_dbus_network_info_to_variant(info);

    g_assert_nonnull(v);
    g_assert_true(g_variant_is_of_type(v, G_VARIANT_TYPE("a{sv}")));

    GVariant *nwid = g_variant_lookup_value(v, "nwid", G_VARIANT_TYPE_STRING);
    g_assert_cmpstr(g_variant_get_string(nwid, NULL), ==, "8056c2e21c000001");
    g_variant_unref(nwid);

    GVariant *status = g_variant_lookup_value(v, "status", G_VARIANT_TYPE_STRING);
    g_assert_cmpstr(g_variant_get_string(status, NULL), ==, "OK");
    g_variant_unref(status);

    GVariant *iface = g_variant_lookup_value(v, "port_device_name", G_VARIANT_TYPE_STRING);
    g_assert_cmpstr(g_variant_get_string(iface, NULL), ==, "ztabcdefgh");
    g_variant_unref(iface);

    GVariant *name = g_variant_lookup_value(v, "name", G_VARIANT_TYPE_STRING);
    g_assert_cmpstr(g_variant_get_string(name, NULL), ==, "test-net");
    g_variant_unref(name);

    GVariant *type = g_variant_lookup_value(v, "type", G_VARIANT_TYPE_STRING);
    g_assert_cmpstr(g_variant_get_string(type, NULL), ==, "PRIVATE");
    g_variant_unref(type);

    GVariant *mac = g_variant_lookup_value(v, "mac", G_VARIANT_TYPE_STRING);
    g_assert_cmpstr(g_variant_get_string(mac, NULL), ==, "0e:f9:56:9d:f7:09");
    g_variant_unref(mac);

    GVariant *mtu = g_variant_lookup_value(v, "mtu", G_VARIANT_TYPE_INT32);
    g_assert_cmpint(g_variant_get_int32(mtu), ==, 2800);
    g_variant_unref(mtu);

    GVariant *bridge = g_variant_lookup_value(v, "bridge", G_VARIANT_TYPE_BOOLEAN);
    g_assert_false(g_variant_get_boolean(bridge));
    g_variant_unref(bridge);

    GVariant *broadcast = g_variant_lookup_value(v, "broadcast_enabled", G_VARIANT_TYPE_BOOLEAN);
    g_assert_true(g_variant_get_boolean(broadcast));
    g_variant_unref(broadcast);

    GVariant *allow_managed = g_variant_lookup_value(v, "allow_managed", G_VARIANT_TYPE_BOOLEAN);
    g_assert_true(g_variant_get_boolean(allow_managed));
    g_variant_unref(allow_managed);

    GVariant *allow_global = g_variant_lookup_value(v, "allow_global", G_VARIANT_TYPE_BOOLEAN);
    g_assert_false(g_variant_get_boolean(allow_global));
    g_variant_unref(allow_global);

    GVariant *addrs = g_variant_lookup_value(v, "assigned_addresses", G_VARIANT_TYPE_STRING_ARRAY);
    gsize     n_addrs = 0;
    const char **addrs_strv = g_variant_get_strv(addrs, &n_addrs);
    g_assert_cmpuint(n_addrs, ==, 2);
    g_assert_cmpstr(addrs_strv[0], ==, "10.1.2.3/24");
    g_assert_cmpstr(addrs_strv[1], ==, "fd00::1/88");
    g_free(addrs_strv);
    g_variant_unref(addrs);

    GVariant *routes = g_variant_lookup_value(v, "routes", G_VARIANT_TYPE_STRING_ARRAY);
    gsize     n_routes = 0;
    const char **routes_strv = g_variant_get_strv(routes, &n_routes);
    g_assert_cmpuint(n_routes, ==, 2);
    g_assert_cmpstr(routes_strv[0], ==, "10.1.0.0/24");
    g_assert_cmpstr(routes_strv[1], ==, "0.0.0.0/0 via 10.1.0.1");
    g_free(routes_strv);
    g_variant_unref(routes);

    g_variant_unref(v);
    g_ptr_array_unref(networks);
}

static void
test_network_info_to_variant_empty_arrays(void)
{
    const char *json = "["
        "{"
        "\"nwid\": \"8056c2e21c000002\","
        "\"status\": \"NOT_JOINED\","
        "\"portDeviceName\": \"\","
        "\"assignedAddresses\": []"
        "}"
        "]";

    GError    *error = NULL;
    GPtrArray *networks = zt_cli_parse_networks_json(json, &error);

    g_assert_no_error(error);
    g_assert_nonnull(networks);

    ZtNetworkInfo *info = g_ptr_array_index(networks, 0);
    GVariant      *v = nm_zerotier_status_dbus_network_info_to_variant(info);

    GVariant *addrs = g_variant_lookup_value(v, "assigned_addresses", G_VARIANT_TYPE_STRING_ARRAY);
    g_assert_cmpuint(g_variant_get_size(addrs) == 0 || g_variant_n_children(addrs) == 0, ==, TRUE);
    g_variant_unref(addrs);

    GVariant *routes = g_variant_lookup_value(v, "routes", G_VARIANT_TYPE_STRING_ARRAY);
    g_assert_cmpuint(g_variant_n_children(routes), ==, 0);
    g_variant_unref(routes);

    GVariant *name = g_variant_lookup_value(v, "name", G_VARIANT_TYPE_STRING);
    g_assert_cmpstr(g_variant_get_string(name, NULL), ==, "");
    g_variant_unref(name);

    g_variant_unref(v);
    g_ptr_array_unref(networks);
}

int
main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/zerotier-status-dbus/network-info-to-variant-full",
                    test_network_info_to_variant_full);
    g_test_add_func("/zerotier-status-dbus/network-info-to-variant-empty-arrays",
                    test_network_info_to_variant_empty_arrays);

    return g_test_run();
}
