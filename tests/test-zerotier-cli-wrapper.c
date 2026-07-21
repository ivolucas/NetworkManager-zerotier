/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "zerotier-cli-wrapper.h"
#include "zerotier-network-id.h"

static void
test_valid_network_id(void)
{
    g_assert_true(zt_is_valid_network_id("8056c2e21c000001"));
    g_assert_false(zt_is_valid_network_id(NULL));
    g_assert_false(zt_is_valid_network_id(""));
    g_assert_false(zt_is_valid_network_id("8056C2E21C000001")); /* uppercase not allowed */
    g_assert_false(zt_is_valid_network_id("8056c2e21c00000"));  /* too short */
    g_assert_false(zt_is_valid_network_id("8056c2e21c0000011")); /* too long */
    g_assert_false(zt_is_valid_network_id("8056c2e21c00000g")); /* non-hex char */
}

static void
test_parse_networks_json(void)
{
    const char *json = "["
        "{"
        "\"nwid\": \"8056c2e21c000001\","
        "\"name\": \"test-net\","
        "\"status\": \"OK\","
        "\"portDeviceName\": \"ztabcdefgh\","
        "\"assignedAddresses\": [\"10.1.2.3/24\", \"fd00::1/88\"]"
        "},"
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
    g_assert_cmpuint(networks->len, ==, 2);

    ZtNetworkInfo *net0 = g_ptr_array_index(networks, 0);
    g_assert_cmpstr(net0->nwid, ==, "8056c2e21c000001");
    g_assert_cmpstr(net0->status, ==, "OK");
    g_assert_cmpstr(net0->port_device_name, ==, "ztabcdefgh");
    g_assert_cmpuint(net0->assigned_addresses->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(net0->assigned_addresses, 0), ==, "10.1.2.3/24");
    g_assert_cmpstr(g_ptr_array_index(net0->assigned_addresses, 1), ==, "fd00::1/88");

    ZtNetworkInfo *net1 = g_ptr_array_index(networks, 1);
    g_assert_cmpstr(net1->status, ==, "NOT_JOINED");
    g_assert_cmpuint(net1->assigned_addresses->len, ==, 0);

    g_ptr_array_unref(networks);
}

static void
test_parse_networks_json_invalid(void)
{
    GError    *error = NULL;
    GPtrArray *networks = zt_cli_parse_networks_json("not json", &error);

    g_assert_null(networks);
    g_assert_error(error, ZT_CLI_ERROR, ZT_CLI_ERROR_PARSE_FAILED);
    g_clear_error(&error);
}

int
main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/zerotier-cli/valid-network-id", test_valid_network_id);
    g_test_add_func("/zerotier-cli/parse-networks-json", test_parse_networks_json);
    g_test_add_func("/zerotier-cli/parse-networks-json-invalid", test_parse_networks_json_invalid);

    return g_test_run();
}
