#!/usr/bin/env bash
# Integration test for netlink-driven interface detection and process
# lifecycle. Requires root (or CAP_NET_ADMIN) to create a veth pair;
# not run as part of the gtest suite, invoked separately in CI as a
# privileged step.
#
# Exercises the real `conwatch` binary end-to-end against a veth pair
# it did not have to be told about -- covering RTM_NEWLINK/RTM_DELLINK
# parsing and ProcessManager's fork/exec/SIGTERM/SIGCHLD handling. The
# real conwatch-tray binary needs Qt + a tray host (D-Bus
# StatusNotifierWatcher) that doesn't exist in a headless CI
# container, so this test substitutes a trivial stub in its place --
# see FAKE_TRAY below.

set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "This test requires root (needs CAP_NET_ADMIN for veth). Re-run with sudo." >&2
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONWATCH_BIN="${CONWATCH_BIN:-$REPO_ROOT/build/conwatch}"
VETH_A="cwtest-a"
VETH_B="cwtest-b"
CONFIG_DIR="$(mktemp -d)"
FAKE_TRAY_BIN="/usr/local/bin/conwatch-tray"
FAKE_TRAY_BACKUP=""

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

cleanup() {
    set +e
    kill "$CONWATCH_PID" 2>/dev/null
    wait "$CONWATCH_PID" 2>/dev/null
    ip link del "$VETH_A" 2>/dev/null
    rm -rf "$CONFIG_DIR"
    if [[ -n "$FAKE_TRAY_BACKUP" ]]; then
        mv -f "$FAKE_TRAY_BACKUP" "$FAKE_TRAY_BIN" 2>/dev/null
    else
        rm -f "$FAKE_TRAY_BIN"
    fi
}
trap cleanup EXIT

[[ -x "$CONWATCH_BIN" ]] || fail "conwatch binary not found at $CONWATCH_BIN (build first)"

# --- Install a fake conwatch-tray: a stub that just sleeps, standing
# in for the real Qt/tray binary so ProcessManager's fork/exec/kill
# path is exercised without needing a display or D-Bus tray host.
if [[ -e "$FAKE_TRAY_BIN" ]]; then
    FAKE_TRAY_BACKUP="$(mktemp)"
    mv "$FAKE_TRAY_BIN" "$FAKE_TRAY_BACKUP"
fi
cat > "$FAKE_TRAY_BIN" <<'EOF'
#!/usr/bin/env bash
# Stand-in for the real conwatch-tray in integration tests: just idles.
trap 'exit 0' TERM INT
while true; do sleep 3600 & wait $!; done
EOF
chmod 755 "$FAKE_TRAY_BIN"

# --- Config: only watch our test interfaces, so the real host's
# interfaces (wlan0/eth0/etc.) can't spawn conwatch-tray instances
# during the test and confuse the assertions below.
export XDG_CONFIG_HOME="$CONFIG_DIR"
mkdir -p "$CONFIG_DIR/conwatch"
cat > "$CONFIG_DIR/conwatch/config.yaml" <<EOF
default_target: "127.0.0.1"
include: ["$VETH_A"]
exclude: []
interfaces: {}
EOF

# --- veth pair, initially down.
ip link add "$VETH_A" type veth peer name "$VETH_B" 2>/dev/null || fail "could not create veth pair"
ip link set "$VETH_B" up # peer must be up for the pair to report carrier
ip link set "$VETH_A" down

"$CONWATCH_BIN" &
CONWATCH_PID=$!
sleep 1
kill -0 "$CONWATCH_PID" 2>/dev/null || fail "conwatch did not start"

echo "--- Test 1: interface up -> conwatch-tray spawned ---"
ip link set "$VETH_A" up
for _ in $(seq 1 20); do
    pgrep -f "conwatch-tray $VETH_A " >/dev/null && break
    sleep 0.2
done
pgrep -f "conwatch-tray $VETH_A " >/dev/null || fail "conwatch-tray was not spawned for $VETH_A after link up"
echo "OK: conwatch-tray spawned for $VETH_A"

echo "--- Test 2: interface down -> conwatch-tray killed ---"
ip link set "$VETH_A" down
for _ in $(seq 1 20); do
    pgrep -f "conwatch-tray $VETH_A " >/dev/null || break
    sleep 0.2
done
pgrep -f "conwatch-tray $VETH_A " >/dev/null && fail "conwatch-tray still running for $VETH_A after link down"
echo "OK: conwatch-tray stopped for $VETH_A after link down"

echo "--- Test 3: interface up again -> respawned ---"
ip link set "$VETH_A" up
for _ in $(seq 1 20); do
    pgrep -f "conwatch-tray $VETH_A " >/dev/null && break
    sleep 0.2
done
pgrep -f "conwatch-tray $VETH_A " >/dev/null || fail "conwatch-tray was not respawned for $VETH_A"
echo "OK: conwatch-tray respawned for $VETH_A"

echo "--- Test 4: interface removed entirely (RTM_DELLINK) -> killed ---"
ip link del "$VETH_A"
for _ in $(seq 1 20); do
    pgrep -f "conwatch-tray $VETH_A " >/dev/null || break
    sleep 0.2
done
pgrep -f "conwatch-tray $VETH_A " >/dev/null && fail "conwatch-tray still running after veth deletion"
echo "OK: conwatch-tray stopped after interface removal"

echo "--- Test 5: clean shutdown leaves no orphans ---"
ip link add "$VETH_A" type veth peer name "$VETH_B" 2>/dev/null
ip link set "$VETH_B" up
ip link set "$VETH_A" up
for _ in $(seq 1 20); do
    pgrep -f "conwatch-tray $VETH_A " >/dev/null && break
    sleep 0.2
done
pgrep -f "conwatch-tray $VETH_A " >/dev/null || fail "setup for shutdown test failed: conwatch-tray never started"

kill -TERM "$CONWATCH_PID"
for _ in $(seq 1 30); do
    kill -0 "$CONWATCH_PID" 2>/dev/null || break
    sleep 0.2
done
kill -0 "$CONWATCH_PID" 2>/dev/null && fail "conwatch did not exit after SIGTERM"
sleep 0.5
pgrep -f "conwatch-tray " >/dev/null && fail "orphaned conwatch-tray process(es) after conwatch shutdown"
echo "OK: clean shutdown, no orphaned conwatch-tray processes"

echo
echo "All integration tests passed."
