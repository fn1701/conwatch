# conwatch (formerly nettray)

A minimal per-interface connection monitor for Linux desktops with a
system tray. Pings a target once per second over a raw ICMP socket
bound to a specific network interface and shows a color-coded system
tray icon:

- **green** — last ping succeeded
- **yellow** — 1-9 consecutive losses
- **red** — 10+ consecutive losses

After 10 consecutive losses the raw socket is closed and recreated
(re-resolving the interface index and rebinding), to avoid a socket
left stale by an interface renumbering event — WiFi roam, DHCP
renewal, or a VPN tunnel being torn down and re-created with a new
`ifindex`.

Built with Qt6 (`QSystemTrayIcon`) and a raw `AF_INET`/`SOCK_RAW`
socket bound via `SO_BINDTODEVICE`.

## Historical setup (superseded)

> This section documents the original, hand-wired setup this project
> started from. It has since been replaced by a netlink-driven
> auto-discovery watcher (see later commits / the main README) that
> removes the need for hardcoded interface names and NetworkManager
> specifically. Kept here for reference.

Two independent instances ran, one per interface:

| Instance | Interface | Target | Started by |
|---|---|---|---|
| WiFi | `wlan0` | `1.1.1.1` | systemd user service, always on |
| VPN | `wg0` (WireGuard, example name) | `1.1.1.1` | systemd templated user service, started/stopped by a NetworkManager dispatcher hook |

This was a hardcoded, two-interface setup — adding another interface
meant creating another unit and/or editing the dispatcher script by
hand.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build
```

Dependencies: `qt6-base`, `cmake`, `gcc` (or another C++17 compiler).

## Install

Raw ICMP sockets require `CAP_NET_RAW`. Rather than running as root,
the capability is granted directly on the installed binary:

```sh
sudo install -m 755 build/nettray /usr/local/bin/nettray
sudo setcap cap_net_raw+ep /usr/local/bin/nettray
getcap /usr/local/bin/nettray   # sanity check: should print cap_net_raw=ep
```

`/usr/local/bin` is used (not `~/.local/bin`) so the binary and its
capability are available system-wide, not just for one user.

## systemd user services (historical)

Two unit files (under `systemd/`, installed to
`~/.config/systemd/user/`):

**`nettray-wlan0.service`** — always-on WiFi monitor:

```ini
[Unit]
Description=Nettray ping monitor - WiFi (wlan0)
After=graphical-session.target network.target
PartOf=graphical-session.target

[Service]
ExecStart=/usr/local/bin/nettray wlan0 1.1.1.1 WiFi
Restart=on-failure
RestartSec=3

[Install]
WantedBy=graphical-session.target
```

Enabled to start automatically with the graphical session:

```sh
systemctl --user daemon-reload
systemctl --user enable --now nettray-wlan0.service
```

**`nettray-vpn@.service`** — a *template* unit (note the `@`), so it
can be instantiated for any interface name via `%i`:

```ini
[Unit]
Description=Nettray ping monitor - VPN (%i)
After=graphical-session.target
PartOf=graphical-session.target

[Service]
ExecStart=/usr/local/bin/nettray %i 1.1.1.1 VPN
Restart=on-failure
RestartSec=3
```

This one was not enabled directly — it was started/stopped on demand
(see below), e.g. as `nettray-vpn@wg0.service`.

## VPN interface lifecycle: NetworkManager dispatcher hook (historical)

The WireGuard interface only exists while the VPN is connected, so its
monitor needed to start/stop in step with the tunnel. This was handled
by a NetworkManager dispatcher script (root-owned, since NM dispatcher
scripts always run as root):

**`/etc/NetworkManager/dispatcher.d/10-nettray-vpn.sh`** (checked into
this repo as `10-nettray-vpn.sh`):

```sh
#!/bin/bash
IFACE="$1"
ACTION="$2"

TARGET_USER="your-username"
USER_UID="$(id -u "$TARGET_USER")"

[[ "$IFACE" == "wg0" ]] || exit 0

run_as_user() {
    sudo -u "$TARGET_USER" XDG_RUNTIME_DIR="/run/user/$USER_UID" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$USER_UID/bus" \
        systemctl --user "$@"
}

case "$ACTION" in
    up)
        run_as_user start "nettray-vpn@${IFACE}.service"
        ;;
    down|pre-down)
        run_as_user stop "nettray-vpn@${IFACE}.service"
        ;;
esac
```

Installed with:

```sh
sudo install -m 755 10-nettray-vpn.sh /etc/NetworkManager/dispatcher.d/10-nettray-vpn.sh
```

NetworkManager auto-discovers and runs any executable script dropped
into `dispatcher.d/` — no separate registration step needed.

### Why this script looked the way it did (lessons learned)

Two things did *not* work as initially expected, in case this pattern
is reused elsewhere:

1. **`CONNECTION_TYPE` is not reliably set.** NetworkManager's
   dispatcher documentation describes a `CONNECTION_TYPE` environment
   variable, and an earlier version of this script filtered on
   `[[ "$CONNECTION_TYPE" == "wireguard" ]]`. In practice, on the
   system this was developed on, `CONNECTION_TYPE` came through
   **empty** for `up`/`down` actions (confirmed by temporarily logging
   all dispatcher env vars via `logger`). The script matched on
   `$IFACE` (the first positional argument, always populated) instead.

2. **Detecting "is this a WireGuard interface" via sysfs doesn't work
   everywhere either.** `/sys/class/net/<iface>/wireguard` is
   documented as the way to detect a kernel WireGuard netdev, but it
   did not exist for the tested interface despite `ip -d link show`
   and `nmcli` both correctly reporting the type as `wireguard`. Not
   fully explained — possibly a NetworkManager-managed WireGuard
   connection doesn't expose the same sysfs layout as one created
   directly via `wg-quick`/`ip link add type wireguard`. Matching on
   the known interface name sidestepped the issue entirely.

## Known limitations of this historical setup

- Hardcoded to exactly two interfaces. Adding a third meant creating
  another static or templated unit and, for anything that isn't
  always-up, another dispatcher filter branch.
- The VPN lifecycle handling was NetworkManager-specific. It would not
  work unmodified on a system using systemd-networkd or no network
  manager at all.

These limitations are what motivated the netlink-driven watcher that
replaces this setup — see later history for that design.
