#!/usr/bin/env bash
# Creates a NetworkManager VPN connection (vpn-type=zerotier) for every
# ZeroTier network already joined via zerotier-cli (e.g. joined manually, by
# another tool, or before this plugin existed), so they show up in
# `nmcli connection show` / GNOME Settings without having to add each one by
# hand. Safe to re-run: networks that already have a matching NetworkManager
# connection (matched by network id, not name) are skipped.
set -euo pipefail

if ! command -v jq >/dev/null 2>&1; then
    echo "This script requires jq (sudo apt install jq)." >&2
    exit 1
fi
if ! command -v zerotier-cli >/dev/null 2>&1; then
    echo "zerotier-cli not found; is zerotier-one installed?" >&2
    exit 1
fi

echo "Looking for existing zerotier NetworkManager connections..."
declare -A existing_ids
while IFS= read -r uuid; do
    [ -n "$uuid" ] || continue
    service_type=$(nmcli -g vpn.service-type connection show "$uuid" 2>/dev/null || true)
    [ "$service_type" = "org.freedesktop.NetworkManager.zerotier" ] || continue

    vpn_data=$(nmcli -g vpn.data connection show "$uuid" 2>/dev/null || true)
    network_id=$(grep -oE '[0-9a-f]{16}' <<<"$vpn_data" | head -n1 || true)
    [ -n "$network_id" ] && existing_ids["$network_id"]=1
done < <(nmcli -t -f UUID connection show)

echo "Reading networks currently joined via zerotier-cli..."
networks=$(zerotier-cli -j listnetworks)

created=0
skipped=0

while IFS= read -r net; do
    [ -n "$net" ] || continue
    nwid=$(jq -r '.nwid' <<<"$net")
    name=$(jq -r '.name // empty' <<<"$net")
    label="zt-${name:-$nwid}"

    if [ -n "${existing_ids[$nwid]:-}" ]; then
        echo "  skip $nwid (already has an NM connection)"
        skipped=$((skipped + 1))
        continue
    fi

    echo "  create '$label' for network $nwid"
    nmcli connection add type vpn vpn-type zerotier ifname -- con-name "$label" \
        vpn.data "network-id=$nwid" >/dev/null
    created=$((created + 1))
done < <(jq -c '.[]' <<<"$networks")

echo "Done: $created connection(s) created, $skipped already present."
if [ "$created" -gt 0 ]; then
    echo "New connections default to not using this network as the default route" \
        "(same as the ZeroTier daemon's own default); edit them in GNOME Settings" \
        "or with 'nmcli connection modify <name> vpn.data +allow-default=yes' to change that."
fi
