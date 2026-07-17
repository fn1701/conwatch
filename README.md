# conwatch

A minimal per-interface connection monitor for Linux desktops with a
system tray. `conwatch-tray` pings a target once per second over a raw
ICMP socket bound to a specific network interface and shows a
color-coded system tray icon:

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

`conwatch` (the watcher daemon) automatically discovers which
interfaces to monitor by listening directly to kernel netlink
link-state events, and spawns one `conwatch-tray` instance per
eligible interface — no manual per-interface systemd units, no
NetworkManager dependency. See below.

## Components

- **`conwatch-tray`** — the ping/tray-icon binary. Takes an interface,
  a target IP, and a label: `conwatch-tray wlan0 1.1.1.1 WiFi`.
- **`conwatch`** — a headless daemon (no Qt) that watches for
  interfaces coming up/down via `NETLINK_ROUTE`, applies an
  include/exclude filter from a YAML config, and spawns/kills
  `conwatch-tray` child processes accordingly.

These are currently one process spawning another (not yet split via
D-Bus) — see the future-plans notes for the intended `conwatch` /
`conwatch-tray` IPC split.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build
```

Dependencies: `qt6-base`, `yaml-cpp`, `cmake`, `gcc` (or another
C++17 compiler).

## Tests

Pure-logic unit tests (config parsing, include/exclude precedence,
glob-based eligibility) run under gtest and don't require root or a
real network:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCONWATCH_BUILD_TESTS=ON .
cmake --build build
ctest --test-dir build --output-on-failure
```

Additional dependency: `gtest`.

Netlink event handling and process spawning are exercised manually
(see Known limitations) — they need `CAP_NET_ADMIN`/root to create
test interfaces (`ip link add ... type veth`) and aren't covered by
the current CI job.

GitHub Actions (`.github/workflows/ci.yml`) builds the full project
and runs this suite on every push/PR, using Arch Linux to match the
target platform.

## Install

Raw ICMP sockets require `CAP_NET_RAW`. Rather than running as root,
the capability is granted directly on the installed `conwatch-tray`
binary (the watcher itself needs no special capabilities — only
netlink and `fork`/`exec`, both unprivileged):

```sh
sudo install -m 755 build/conwatch-tray /usr/local/bin/conwatch-tray
sudo setcap cap_net_raw+ep /usr/local/bin/conwatch-tray
getcap /usr/local/bin/conwatch-tray   # sanity check: should print cap_net_raw=ep

sudo install -m 755 build/conwatch /usr/local/bin/conwatch
```

`/usr/local/bin` is used (not `~/.local/bin`) so both binaries and the
capability are available system-wide, not just for one user.

## Run as a systemd user service

```sh
mkdir -p ~/.config/systemd/user
cp systemd/conwatch.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now conwatch.service
```

The watcher runs as a single always-on service; it manages
`conwatch-tray` child processes directly rather than through systemd.

## Configuration

On first run, `conwatch` creates `~/.config/conwatch/config.yaml` (or
`$XDG_CONFIG_HOME/conwatch/config.yaml`) with full documented
defaults if it doesn't already exist:

```yaml
# Default ping target for any monitored interface without its own
# "target" under interfaces: below.
default_target: "1.1.1.1"

# Exactly one of include / exclude should be populated; leave the
# other empty ([]). Populating both with real (non-"*") patterns is a
# startup error, by design -- a silently-guessed precedence would
# hide misconfiguration in a background daemon nobody's watching a
# terminal for.

# If non-empty, ONLY interfaces matching one of these glob patterns
# are monitored (allow-list mode).
include: []

# Interfaces matching any of these glob patterns are never monitored.
# Defaults cover loopback and common virtual/container interfaces.
exclude:
  - "lo"
  - "docker*"
  - "podman*"
  - "virbr*"
  - "veth*"
  - "br-*"

# Per-interface overrides, keyed by exact interface name. Optional.
# interfaces:
#   wlan0:
#     label: "WiFi"
#   wg0:
#     label: "VPN"
#     target: "10.10.0.1"
interfaces: {}
```

Config is read once at startup; edit and restart the service to apply
changes (`systemctl --user restart conwatch.service`).

## How interface detection works

`conwatch` opens a `NETLINK_ROUTE` socket subscribed to `RTMGRP_LINK`
and parses `RTM_NEWLINK`/`RTM_DELLINK` messages. An interface is
considered "operationally up" only when
`IFF_UP && IFF_RUNNING && IFF_LOWER_UP` all hold — the `IFF_LOWER_UP`
bit specifically distinguishes an admin-up-but-unplugged ethernet port
(or a WiFi radio that's on but not associated) from an interface that's
actually usable. On startup, an `RTM_GETLINK` dump request populates
initial state for interfaces already up (e.g. WiFi at boot), so nothing
is missed waiting for the next state change.

This is independent of NetworkManager, systemd-networkd, or any other
network management daemon — it works directly against the kernel.

## Known limitations

- No live config reload — changes require restarting the watcher.
- `include`/`exclude` patterns are matched with `fnmatch(3)` glob
  syntax (e.g. `docker*`), not full regex.
- Per-interface `conwatch-tray` children are spawned via
  `fork()`+`execlp()` from a fixed path (`/usr/local/bin/conwatch-tray`);
  a different install location currently requires editing
  `process_manager.cpp`.

## History

This project started as a hand-wired two-interface setup (one always-on
WiFi monitor, one VPN monitor started/stopped by a NetworkManager
dispatcher script), under the earlier name `nettray`. See earlier
commits in this repository's history for that design and the lessons
learned while building it — kept for reference since some of the
NetworkManager dispatcher quirks documented there may resurface in
other projects.
