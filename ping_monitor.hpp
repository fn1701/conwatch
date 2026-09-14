#pragma once

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QPixmap>
#include <QString>

#include <map>
#include <tuple>
#include <utility>

class QAction;
class QSystemTrayIcon;
struct ifaddrs;

// PingMonitor: per-interface ping monitor with a color-coded system tray icon.
//
// <target> may be a literal IPv4 address, a literal IPv6 address, or a
// hostname (resolved via DNS, which may yield both an A and AAAA record --
// enabling independent v4/v6 checks on the same interface). <target6>, if
// non-empty, is an additional literal IPv6 target (or hostname) merged in
// alongside whatever <target> resolves to -- this is how a default IPv6
// target (config's default_target6/target6) reaches conwatch-tray without
// overriding a literal-IPv4 default_target. Each resolved protocol gets
// its own raw-socket ICMP ping, once per second, bound to <interface>.
// Tray icon color reflects best-of status across protocols that have both
// a resolved target and a local address of that family on this interface
// (green as long as at least one is healthy); once every active protocol
// is failing, the color takes the worse of their individual severities:
//   green  - last ping succeeded (at least one active protocol)
//   yellow - 1-9 consecutive losses
//   blue   - 10+ consecutive losses, but the interface's default gateway
//            for that protocol responds -- local network is fine, problem
//            is upstream of the gateway or specific to the target
//   red    - 10+ consecutive losses, gateway unreachable too
//
// The glyph ("4", "6", "4/6", or blank) reflects which protocol(s) are
// CURRENTLY succeeding, not just configured -- it shrinks/grows as
// protocols fail/recover.
//
// After 10 consecutive losses on a protocol, that protocol's raw socket is
// closed and recreated (re-resolving the interface index and rebinding) to
// avoid a socket left stale by an interface renumber/reassociation (e.g.
// DHCP renewal, wifi roam, or the interface disappearing and reappearing
// with a new ifindex, as happens when a VPN tunnel is torn down and
// re-created).
//
// Method groups are split across files by concern:
//   ping_monitor.cpp        - construction, shortIfaceTag
//   ping_monitor_socket.cpp - socket open/send/recv for v4 and v6
//   ping_monitor_tick.cpp   - per-tick target/gateway checks, severity
//   ping_monitor_render.cpp - tray icon/tooltip rendering
class PingMonitor : public QObject
{
    Q_OBJECT
public:
    PingMonitor(QString iface, QString target, QString target6, QString label);

private:
    enum class Severity {
        Green,
        Blue,
        Yellow,
        Red
    };
    enum class ProtoState {
        NoLocalAddress,
        Failing,
        Healthy
    };

    struct ProtoTrack {
        bool configured = false;
        QString targetIp;
        int sock = -1;
        int failStreak = 0;
        bool hasLocalAddress = false;
        int recheckTicksLeft = 0;
        // Gateway reachability, checked only while the target itself is
        // failing (see tickV4()/tickV6()) -- an extra ping incurred only
        // during an outage, not adding to steady-state per-tick cost.
        QString gatewayIp;
        bool gatewayResolved = false;
        bool gatewayReachable = false;
    };

    // Stateless helpers, private-static per project convention (see
    // CLAUDE.md) even though they don't touch PingMonitor's instance state.
    static uint16_t checksum(void *buf, int len);
    static bool ifaceHasAddress(const std::string &iface, int family);
    static bool matchesIfaceAddress(const struct ifaddrs *a, const std::string &iface, int family);
    static QString shortIfaceTag(const QString &iface);
    static ProtoState stateOf(const ProtoTrack &t);
    static Severity severityOf(const ProtoTrack &t);

    void resolveInitialTargets(const QString &target, const QString &target6);
    void setupTray();

    void openSocket4();
    void openSocket6();
    bool sendPing4(const QString &destIp);
    bool recvReply4();
    static bool isMatchingReply4(const char *buf, ssize_t n, uint16_t pid);
    bool sendPing6(const QString &destIp);
    bool recvReply6();
    static bool isMatchingReply6(const char *buf, ssize_t n, uint16_t pid);

    void maybeRecheckLocalAddress(ProtoTrack &t, int family);
    void checkGateway4(ProtoTrack &t);
    void checkGateway6(ProtoTrack &t);
    void tickV4();
    void tickV6();
    void tick();

    void renderTray();
    static QString glyphFor(ProtoState v4State, ProtoState v6State, Severity v4Severity, Severity v6Severity);
    static Severity worstSeverityOf(ProtoState v4State, ProtoState v6State, Severity v4Severity, Severity v6Severity);
    QString statusTextFor(ProtoState v4State, ProtoState v6State) const;
    static QString protoStatusText(const QString &tag, ProtoState state, const ProtoTrack &t);
    void applyStatus(const QString &status);
    QPixmap renderIcon(int size, const QColor &qc, const QString &glyph);
    void paintIconBase(QPainter &p, int size, const QColor &qc);
    void paintIconGlyph(QPainter &p, int size, const QString &glyph);
    static QColor qColorFor(Severity worst);
    QIcon &iconFor(Severity worst, const QString &glyph);
    void setIcon(Severity worst, const QString &glyph);

    QString m_iface;
    QString m_label;
    QString m_ifaceTag;
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_statusAction = nullptr;
    ProtoTrack m_v4;
    ProtoTrack m_v6;
    uint16_t m_pid = 0;
    uint16_t m_seq4 = 0;
    uint16_t m_seq6 = 0;
    Severity m_currentSeverity = Severity::Yellow;
    QString m_currentGlyph = "\x01"; // sentinel, never equals a real glyph, forces first render
    std::map<std::pair<int, QString>, QIcon> m_iconCache;
    QString m_currentTooltip;
    // (v4 failStreak, v4 hasLocalAddress, v4 gatewayReachable, v6 failStreak,
    // v6 hasLocalAddress, v6 gatewayReachable) as of the last tick that
    // actually changed something -- lets tick() skip renderTray() entirely
    // (and thus touching the tray widget at all) on ticks where nothing
    // changed, instead of calling it every second and relying on
    // renderTray()'s own internal no-op checks.
    std::tuple<int, bool, bool, int, bool, bool> m_lastTickState{-1, false, false, -1, false, false};
};
