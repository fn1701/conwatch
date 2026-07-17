# Future plans

Ideas under consideration, not yet scheduled. Kept here so design
decisions already discussed aren't re-litigated from scratch later.

## conwatch / conwatch-tray split over D-Bus

Currently `conwatch` (the watcher daemon) spawns `conwatch-tray`
directly via `fork()`+`execlp()` — one process managing another's
lifecycle, no IPC beyond that. The planned split turns this into two
independently-runnable programs connected by a D-Bus session service:

- `conwatch` exposes its state (which interfaces are monitored, their
  current status/loss streak) via a D-Bus session service — signals
  for state changes (interface added/removed, status changed),
  properties for current snapshot queries.
- `conwatch-tray` becomes a D-Bus client (`QDBusConnection`) rather
  than a process spawned with interface/target/label as argv. It
  subscribes to `conwatch`'s signals and renders tray icon(s)
  accordingly.
- This allows `conwatch-tray` to be started/stopped/restarted
  independently of the daemon (e.g. by the desktop session) without
  losing monitoring state, and would allow multiple UI clients (or a
  future non-Qt client) to observe the same daemon.
- Decided already: D-Bus session service is the IPC mechanism (not a
  Unix socket protocol or shared memory) — standard on every target
  desktop (KDE Plasma and other Wayland/X11 DEs), and Qt has first-class
  support via `QDBusConnection` without extra dependencies.

## Configurable single vs. per-interface tray icon

Decided already: support both, selectable via config. Two UI modes:

- **Per-interface** (current behavior): one tray icon per monitored
  interface, each showing that interface's own status.
- **Single aggregate icon**: one tray icon reflecting the worst status
  across all monitored interfaces, with a menu/popup listing each
  interface's individual status.

This becomes a `conwatch-tray` (or its D-Bus-client successor) config
option once the split above exists, since aggregating requires
`conwatch-tray` to see all interfaces' state rather than just the one
it was launched for.

### Identifying which interface a tray icon represents

With multiple per-interface icons on screen at once, and IPv6 support
(below) adding a per-protocol dimension per icon, there needs to be a
way to tell icons apart at a glance beyond hover tooltip. Options to
explore, not yet decided: label text baked into the tooltip (already
possible today via argv `label`), vs. something visible on the icon
itself (e.g. interface name initial). Lower priority than the
protocol glyph below, which has a concrete starting design.

### Protocol status glyph (4 / 4/6 / 6)

Once IPv6 checks exist, the tray icon should show which protocol(s)
are currently working on that interface, not just a red/yellow/green
loss-severity color. `setColor()` in `conwatch-tray.cpp` (currently
`drawEllipse`-only, no text) is the natural place to extend, since it
already draws into a `QPixmap` via `QPainter` — this needs
`QPainter::drawText` added there.

Decided already: start with **protocol-only glyph** — replace the
plain colored circle with a drawn "4", "6", or "4/6" label reflecting
which of IPv4/IPv6 currently have working ping on that interface, and
build/deploy that first before layering color back on top. Color
(loss severity) as an overlay on top of the glyph is a likely next
step once the glyph itself works, but is explicitly deferred until
after the glyph-only version is validated.

This requires the `Color` enum (currently loss-severity-only:
`Green`/`Yellow`/`Red`) to gain a protocol dimension — either two
independent per-protocol states composed at draw time, or a combined
state enum covering all v4/v6-up/down permutations. Depends on the
IPv6 check existing first (above), since there's no v6 status to
render until then.

## Additional diagnostic checks

Being considered, contingent on not sacrificing the battery-conscious
design (event-driven, no polling loops beyond the existing 1s ping
timer):

- **Packet loss monitoring/logging** — persist loss-streak history
  (not just the live tray color) for later review, e.g. to a small
  rotating log file per interface.
- **DNS check** — resolve a fixed hostname periodically/alongside the
  ping, to catch DNS-only outages (interface up, ICMP working, DNS
  broken) that the current ping-only check can't see.
- **IPv6 check** — parallel ICMP6 ping, since a network can have
  working IPv4 and broken/absent IPv6 (or vice versa) independently.
  The current `PingMonitor` in `conwatch-tray.cpp` is IPv4-only
  end-to-end (raw `AF_INET` socket, hand-rolled `iphdr`/`icmphdr`
  checksum) — this isn't a copy-paste extension, since ICMPv6 raw
  sockets get their checksum computed by the kernel via the pseudo-
  header rather than manually. Needs a second target/label per
  interface in config (`resolveTarget`/`resolveLabel` in
  `watcher/config.cpp` currently resolve one target per interface),
  and a parallel v6 socket+send+recv path alongside the v4 one so
  both protocols are checked independently on the same interface.
- **MTU check** — detect path MTU issues (e.g. via a DF-flagged ping
  at a known size) that manifest as large-packet failures despite
  small pings succeeding — a known VPN/tunnel failure mode.

Each of these is naturally an additional signal on the same
per-interface state that `conwatch`/`conwatch-tray` already track, so
they fit the existing model rather than requiring new architecture —
mainly a question of how much additional per-tick work is acceptable.

## Detail popup / standalone window

A click-to-expand view beyond the current right-click menu's plain
status line, once there's more than one signal (ping + DNS + IPv6 +
MTU + loss history) to show per interface:

- **Popup** (attached to the tray icon click) for a quick glance —
  likely the first target given it fits Qt's existing tray API
  (`QMenu`/a lightweight `QWidget` popup) without new window
  management.
- **Standalone window** as a heavier alternative/addition, useful if
  historical loss graphs or per-check detail end up too dense for a
  popup.

Both are gated on the additional checks above existing, since a
detail view showing only "ping: ok" isn't meaningfully different from
today's tray tooltip.
