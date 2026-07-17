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
