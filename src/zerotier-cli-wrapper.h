/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ZEROTIER_CLI_WRAPPER_H
#define ZEROTIER_CLI_WRAPPER_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ZT_CLI_ERROR_NOT_FOUND,   /* zerotier-cli binary not found on the system */
    ZT_CLI_ERROR_EXEC_FAILED, /* zerotier-cli ran but exited non-zero, or spawn failed */
    ZT_CLI_ERROR_PARSE_FAILED /* output could not be parsed as expected JSON */
} ZtCliError;

#define ZT_CLI_ERROR (zt_cli_error_quark())
GQuark zt_cli_error_quark(void);

typedef struct {
    char     *nwid;
    char     *status;            /* e.g. "OK", "NOT_JOINED", "REQUESTING_CONFIGURATION" */
    char     *port_device_name;  /* e.g. "ztXXXXXXXX", may be empty */
    GPtrArray *assigned_addresses; /* array of owned "a.b.c.d/nn" or "ipv6/nn" strings */
} ZtNetworkInfo;

void zt_network_info_free(ZtNetworkInfo *info);

/* Returns TRUE if zerotier-cli was found on the system (cached after first call). */
gboolean zt_cli_is_available(void);

/* Runs `zerotier-cli -j listnetworks`. Returns a newly allocated GPtrArray of
 * ZtNetworkInfo* (free with g_ptr_array_unref), or NULL with *error set. */
GPtrArray *zt_cli_list_networks(GError **error);

/* Parses the JSON text produced by `zerotier-cli -j listnetworks` into a
 * GPtrArray of ZtNetworkInfo*. Exposed separately from zt_cli_list_networks()
 * so it can be exercised directly in unit tests without spawning a process. */
GPtrArray *zt_cli_parse_networks_json(const char *json_text, GError **error);

/* Convenience: list networks and return the one matching network_id, or NULL
 * if not present (not an error - the daemon may not have processed the join yet). */
ZtNetworkInfo *zt_cli_find_network(const char *network_id, GError **error);

gboolean zt_cli_join(const char *network_id, GError **error);
gboolean zt_cli_leave(const char *network_id, GError **error);
gboolean zt_cli_set_allow_default(const char *network_id, gboolean allow, GError **error);

G_END_DECLS

#endif /* ZEROTIER_CLI_WRAPPER_H */
