#include "ping_monitor.hpp"

#include "gateway_resolve.hpp"

#include <net/if.h>
#include <netinet/in.h>

namespace
{
constexpr int FailStreakForGatewayCheck = 10;
constexpr int FailStreakForSocketRecreate = 10;
constexpr int LocalAddressRecheckTicks = 10; // ~10s at the 1s tick interval
} // namespace

// Refreshes hasLocalAddress for `t` if due for a recheck. Rechecks are
// throttled to avoid a getifaddrs() call every tick while a protocol has no
// address -- once addressed, no further rechecking is needed until the
// process restarts (which happens naturally on interface down/up via
// process_manager.cpp).
void PingMonitor::maybeRecheckLocalAddress(ProtoTrack &t, int family)
{
    if (!t.configured || t.hasLocalAddress)
        return;
    if (t.recheckTicksLeft > 0) {
        t.recheckTicksLeft--;
        return;
    }
    t.hasLocalAddress = ifaceHasAddress(m_iface.toStdString(), family);
    t.recheckTicksLeft = LocalAddressRecheckTicks;
}

// Checks reachability of the interface's default gateway for `family`, only
// called while the target itself is failing -- an extra ping incurred only
// during an outage, not adding to steady-state per-tick cost. The gateway
// address is resolved once (via the routing table) and cached; a route
// change (e.g. gateway renumbering) is picked up naturally on the next
// process restart, same as target resolution.
void PingMonitor::checkGateway4(ProtoTrack &t)
{
    if (!t.gatewayResolved) {
        t.gatewayResolved = true;
        if (auto gw = resolveGateway(m_iface.toStdString(), AF_INET))
            t.gatewayIp = QString::fromStdString(*gw);
    }
    if (t.gatewayIp.isEmpty()) {
        t.gatewayReachable = false;
        return;
    }
    t.gatewayReachable = sendPing4(t.gatewayIp) && recvReply4();
}

void PingMonitor::checkGateway6(ProtoTrack &t)
{
    if (!t.gatewayResolved) {
        t.gatewayResolved = true;
        if (auto gw = resolveGateway(m_iface.toStdString(), AF_INET6))
            t.gatewayIp = QString::fromStdString(*gw);
    }
    if (t.gatewayIp.isEmpty()) {
        t.gatewayReachable = false;
        return;
    }
    t.gatewayReachable = sendPing6(t.gatewayIp) && recvReply6();
}

void PingMonitor::tickV4()
{
    maybeRecheckLocalAddress(m_v4, AF_INET);
    if (!m_v4.configured || !m_v4.hasLocalAddress)
        return;

    bool ok = sendPing4(m_v4.targetIp) && recvReply4();
    if (ok) {
        m_v4.failStreak = 0;
        m_v4.gatewayReachable = false;
    } else {
        m_v4.failStreak++;
        if (m_v4.failStreak % FailStreakForSocketRecreate == 0)
            openSocket4();
        if (m_v4.failStreak >= FailStreakForGatewayCheck)
            checkGateway4(m_v4);
    }
}

void PingMonitor::tickV6()
{
    maybeRecheckLocalAddress(m_v6, AF_INET6);
    if (!m_v6.configured || !m_v6.hasLocalAddress)
        return;

    bool ok = sendPing6(m_v6.targetIp) && recvReply6();
    if (ok) {
        m_v6.failStreak = 0;
        m_v6.gatewayReachable = false;
    } else {
        m_v6.failStreak++;
        if (m_v6.failStreak % FailStreakForSocketRecreate == 0)
            openSocket6();
        if (m_v6.failStreak >= FailStreakForGatewayCheck)
            checkGateway6(m_v6);
    }
}

PingMonitor::ProtoState PingMonitor::stateOf(const ProtoTrack &t)
{
    if (!t.configured || !t.hasLocalAddress)
        return ProtoState::NoLocalAddress;
    return t.failStreak == 0 ? ProtoState::Healthy : ProtoState::Failing;
}

// Severity while failing: below FailStreakForGatewayCheck consecutive
// losses, always yellow. At/above that threshold, the target check hands
// off entirely to the gateway check -- blue if the gateway responds (local
// network fine, problem is upstream of the gateway or specific to the
// target), red if it doesn't (mirrors the target check's own
// healthy/unhealthy logic, just aimed at the gateway). There is no separate
// loss-count-based red: yellow transitions directly to blue or red at the
// threshold, never both.
PingMonitor::Severity PingMonitor::severityOf(const ProtoTrack &t)
{
    if (t.failStreak < FailStreakForGatewayCheck)
        return Severity::Yellow;
    return t.gatewayReachable ? Severity::Blue : Severity::Red;
}

void PingMonitor::tick()
{
    tickV4();
    tickV6();

    auto state = std::make_tuple(m_v4.failStreak, m_v4.hasLocalAddress, m_v4.gatewayReachable, m_v6.failStreak, m_v6.hasLocalAddress, m_v6.gatewayReachable);
    if (state != m_lastTickState) {
        m_lastTickState = state;
        renderTray();
    }
}
