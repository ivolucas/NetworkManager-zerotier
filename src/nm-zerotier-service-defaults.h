/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_ZEROTIER_SERVICE_DEFAULTS_H
#define NM_ZEROTIER_SERVICE_DEFAULTS_H

#define NM_DBUS_SERVICE_ZEROTIER "org.freedesktop.NetworkManager.zerotier"

/* NMSettingVpn data item keys */
#define NM_ZEROTIER_KEY_NETWORK_ID    "network-id"    /* string, 16 lowercase hex chars */
#define NM_ZEROTIER_KEY_ALLOW_DEFAULT "allow-default" /* "yes" / "no" */

#endif /* NM_ZEROTIER_SERVICE_DEFAULTS_H */
