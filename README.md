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

## Usage

```sh
nmcli connection add type vpn vpn-type zerotier ifname -- con-name my-zt-net \
  vpn.data "network-id=<16 hex chars>"
nmcli connection up my-zt-net
```

Or add it as a VPN from GNOME Settings > Network > VPN > "+" once the editor plugin is
installed.

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
