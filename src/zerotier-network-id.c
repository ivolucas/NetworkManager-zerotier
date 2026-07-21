/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "zerotier-network-id.h"

#include <string.h>

gboolean
zt_is_valid_network_id(const char *id)
{
    if (!id || strlen(id) != 16)
        return FALSE;
    for (guint i = 0; i < 16; i++) {
        char c = id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return FALSE;
    }
    return TRUE;
}
