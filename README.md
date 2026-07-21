# NetworkManager-zerotier

A NetworkManager VPN plugin for [ZeroTier](https://www.zerotier.com/), so ZeroTier
networks can be joined/left and shown as VPN connections in `nmcli`, `nm-connection-editor`,
and GNOME Settings > Network, the same way OpenVPN or OpenConnect connections are.

This plugin does not implement any tunneling itself. It talks to an already-running
system `zerotier-one` daemon (via `zerotier-cli`) to join/leave networks and reports the
resulting interface and assigned addresses back to NetworkManager.

## Components

- `src/nm-zerotier-service` — root-privileged D-Bus service daemon
  (`org.freedesktop.NetworkManager.zerotier`), D-Bus-activated by NetworkManager on
  connect.
- `reconciled/nm-zerotier-reconciled` — root-privileged systemd system service, running
  continuously (independent of any single VPN connection's lifecycle). Reconciles NM's
  view of ZeroTier VPN connections against `zerotier-one`'s actual joined-network state in
  both directions: if a network gets joined via `zerotier-cli join` outside NM and a
  matching NM connection profile exists, it activates that connection; if a network gets
  left externally while its NM connection is active, it deactivates it. See "Bidirectional
  reconciliation" below.
- `properties/libnm-vpn-plugin-zerotier.so` — GTK4 connection editor plugin loaded by
  `nm-connection-editor` / GNOME Settings.
- `auth-dialog/nm-zerotier-auth-dialog` — small helper invoked by GNOME Settings when
  opening a connection for editing. This plugin has no secrets to provide, so it always
  answers immediately instead of GNOME Settings waiting on a D-Bus secret-agent timeout.

## Requirements

- NetworkManager + `libnm` (development headers: `libnm-dev` / `NetworkManager-libnm-devel`)
- GTK4 (`libgtk-4-dev`)
- `json-glib` (`libjson-glib-dev`)
- `zerotier-one` / `zerotier-cli` installed and running

## Building

```sh
meson setup build --prefix=/usr
ninja -C build
sudo ninja -C build install
sudo systemctl restart NetworkManager
sudo systemctl daemon-reload
sudo systemctl enable --now nm-zerotier-reconciled
```

See `contrib/dev-install.sh` for a one-shot rebuild+reinstall+restart script used during
development.

## Packaging (.deb)

```sh
sudo apt install debhelper lintian   # once
dpkg-buildpackage -us -uc -b
```

Produces `../network-manager-zerotier_<version>_amd64.deb`, installable with
`sudo apt install ../network-manager-zerotier_<version>_amd64.deb` (or `dpkg -i` +
`apt-get install -f` to pull in dependencies). Restart NetworkManager afterwards to pick
up the new plugin, as the postinst script will remind you to do.

CI (`.github/workflows/build.yml`) builds and tests this on both Ubuntu 22.04 and 24.04
(their `libnm`/`glib`/`gtk4`/`systemd` versions differ enough that a single `.deb` can't
target both). Pushing a `v*` tag additionally attaches both `.deb`s to that tag's GitHub
release automatically.

## Usage

```sh
nmcli connection add type vpn vpn-type zerotier ifname -- con-name my-zt-net \
  vpn.data "network-id=<16 hex chars>"
nmcli connection up my-zt-net
```

Or add it as a VPN from GNOME Settings > Network > VPN > "+" once the editor plugin is
installed.

## Bidirectional reconciliation

`nm-zerotier-reconciled` keeps NM's view of ZeroTier VPN connections in sync with
`zerotier-one`'s actual joined-network state, regardless of which side changed:

- **NM-side changes are event-driven.** It subscribes to NetworkManager's own D-Bus
  signals (connection activated/deactivated), so it reacts immediately.
- **`zerotier-one`-side changes are detected via interface add/remove.** `zerotier-one`
  has no event bus of its own, but joining/leaving a network creates/destroys a kernel
  network interface, which NetworkManager's core already notices (netlink) and surfaces
  as a device add/remove signal. The daemon uses that as a trigger to check
  `zerotier-cli listnetworks` (debounced ~750ms, to let `zerotier-one` settle), instead of
  polling on a fixed timer. A one-shot poll at startup covers networks already joined
  before the daemon started, and a sparse ~5 minute poll is kept as a fallback in case a
  device event is ever missed.
- If activating a connection keeps failing (e.g. the device was never authorized on the
  network's controller), the daemon backs off for 60s per network-id before retrying,
  instead of retrying on every trigger.
- **Networks joined externally with no matching NM connection profile at all are
  imported automatically**, once their tun interface actually exists (i.e. once there's
  real state to surface, not just a pending join) - same "zt-\<name-or-nwid\>" naming and
  safe `allow-default`-unset defaults as `contrib/import-zerotier-networks.sh`, which
  remains available for an immediate one-off import without waiting for a trigger.

## Known limitations

- No secrets/authentication UI. ZeroTier network authorization happens on the network
  controller side (approving the device), not via a client-supplied password, so there is
  nothing for this plugin to prompt for.
- **Disconnecting this VPN connection runs `zerotier-cli leave`, which removes the
  network from the entire machine's `zerotier-one` daemon state — not just this
  NetworkManager connection's view of it.** If the same ZeroTier network is also managed
  by the [`zerotier-gnome-indicator`](https://codeberg.org/anantmk/zerotier-gnome-indicator)
  GNOME Shell extension, or by another user session on the same machine, disconnecting
  here disconnects it for them too. ZeroTier network membership is global to the host,
  while NetworkManager's VPN model assumes each connection profile owns its tunnel
  exclusively. Avoid running both against the same network at once.
- GTK4 editor only; no GTK3 build for older `nm-connection-editor` versions.

## License

GPL-2.0-or-later, matching `libnm` and upstream NetworkManager VPN plugins.
