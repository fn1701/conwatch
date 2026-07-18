# Future plans

Ideas under consideration, not yet scheduled. Kept here so design
decisions already discussed aren't re-litigated from scratch later.

## Protocol status glyph (4 / 4/6 / 6)

Implemented: the icon's top-right glyph shows which of IPv4/IPv6 are
*currently* succeeding (not just configured) — "4", "6", "4/6", or
blank if all active protocols are failing. The circle color is the
worst-of loss severity across protocols that have both a resolved
target and a local address on the interface. See `renderTray()` /
`renderIcon()` in `conwatch-tray.cpp`.

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
