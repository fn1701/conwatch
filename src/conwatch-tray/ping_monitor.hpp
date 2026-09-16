#pragma once

#include "../shared/gateway_override.hpp"

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

/**
 * @brief Per-interface ping monitor with a color-coded system tray icon.
 *
 * Implements the tray behavior (target resolution, color/glyph rules,
 * gateway fallback) described in README.md.
 *
 * Method groups are split across files by concern:
 *   ping_monitor.cpp             - construction, shortIfaceTag
 *   ping_monitor_addr.cpp        - checksum, interface-address helpers
 *   ping_monitor_socket4.cpp     - IPv4 socket open/send/recv
 *   ping_monitor_socket6.cpp     - IPv6 socket open/send/recv
 *   ping_monitor_tick.cpp        - per-tick target/gateway checks, severity
 *   ping_monitor_status_text.cpp - status/severity computation, tooltip text
 *   ping_monitor_icon.cpp        - QPainter/QIcon pixmap rendering
 */
class PingMonitor : public QObject
{
    Q_OBJECT
public:
    /**
     * @param iface Interface to bind ICMP sockets to.
     * @param target Literal IPv4/IPv6 address or hostname; a hostname
     *   resolving to both an A and AAAA record enables independent v4/v6
     *   checks on the same interface.
     * @param target6 Additional literal IPv6 target (or hostname) merged in
     *   alongside whatever `target` resolves to -- lets a default IPv6
     *   target reach this monitor without overriding a literal-IPv4 `target`.
     * @param label Human-readable name shown in the tray tooltip/menu.
     * @param gatewayIpOverride4 Overrides or disables the IPv4 gateway
     *   fallback check; see GatewayOverride.
     * @param gatewayIpOverride6 Overrides or disables the IPv6 gateway
     *   fallback check; see GatewayOverride.
     */
    PingMonitor(QString iface, QString target, QString target6, QString label, QString gatewayIpOverride4, QString gatewayIpOverride6);

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
        GatewayOverride::Mode gatewayMode = GatewayOverride::Mode::Auto;
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
    void applyGatewayOverride(ProtoTrack &t, const QString &gatewayIpOverride);
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
