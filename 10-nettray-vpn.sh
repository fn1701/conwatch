#!/bin/bash
# NetworkManager dispatcher hook: start/stop the nettray VPN monitor
# as wireguard interfaces come up/down. Runs as root via NM, so the
# systemd --user unit is controlled via machinectl/su for the desktop user.

IFACE="$1"
ACTION="$2"

TARGET_USER="your-username"
USER_UID="$(id -u "$TARGET_USER")"

# Only care about the wireguard interface (CONNECTION_TYPE is not
# reliably populated by NetworkManager for up/down actions, so match
# on the known interface name directly).
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
