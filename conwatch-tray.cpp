// conwatch-tray: per-interface ping monitor with a color-coded system tray icon.
//
// Usage: conwatch-tray <interface> <target> <target6> [label]
//
// <target> may be a literal IPv4 address, a literal IPv6 address, or a
// hostname (resolved via DNS, which may yield both an A and AAAA record --
// enabling independent v4/v6 checks on the same interface). <target6>, if
// non-empty, is an additional literal IPv6 target (or hostname) merged in
// alongside whatever <target> resolves to -- this is how a default IPv6
// target (config's default_target6/target6) reaches conwatch-tray without
// overriding a literal-IPv4 default_target. Each resolved protocol gets
// its own raw-socket ICMP ping, once per second, bound to <interface>.
// Tray icon color reflects worst-of loss severity across
// protocols that have both a resolved target and a local address of that
// family on this interface:
//   green  - last ping succeeded (all active protocols)
//   yellow - 1-9 consecutive losses (any active protocol)
//   red    - 10+ consecutive losses (any active protocol)
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

#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QIcon>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <map>
#include <tuple>
#include <utility>

#include "target_resolve.hpp"

namespace {

constexpr int kFailStreakForRed = 10;
constexpr int kFailStreakForSocketRecreate = 10;
constexpr int kLocalAddressRecheckTicks = 10; // ~10s at the 1s tick interval

uint16_t checksum(void *buf, int len) {
    uint16_t *p = static_cast<uint16_t *>(buf);
    uint32_t sum = 0;
    for (; len > 1; len -= 2) sum += *p++;
    if (len == 1) sum += *reinterpret_cast<uint8_t *>(p);
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

// True if `iface` currently has at least one non-link-local address of
// `family` (AF_INET or AF_INET6) assigned. Link-local IPv6 (fe80::/10) is
// excluded since it can't reach an external ping target.
bool ifaceHasAddress(const std::string &iface, int family) {
    struct ifaddrs *addrs = nullptr;
    if (getifaddrs(&addrs) != 0) return false;

    bool found = false;
    for (struct ifaddrs *a = addrs; a != nullptr; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != family) continue;
        if (iface != a->ifa_name) continue;

        if (family == AF_INET6) {
            auto *sin6 = reinterpret_cast<struct sockaddr_in6 *>(a->ifa_addr);
            if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) continue;
        }
        found = true;
        break;
    }
    freeifaddrs(addrs);
    return found;
}

} // namespace

class PingMonitor : public QObject {
    Q_OBJECT
public:
    PingMonitor(QString iface, QString target, QString target6, QString label)
        : m_iface(std::move(iface)), m_label(std::move(label)) {
        m_pid = static_cast<uint16_t>(getpid() & 0xffff);
        m_ifaceTag = shortIfaceTag(m_iface);

        ResolvedTargets resolved = resolveTargets(target.toStdString());
        if (resolved.v4) {
            m_v4.targetIp = QString::fromStdString(*resolved.v4);
            m_v4.configured = true;
        }
        if (resolved.v6) {
            m_v6.targetIp = QString::fromStdString(*resolved.v6);
            m_v6.configured = true;
        }

        // target6, if provided, is merged in additively -- it never
        // overrides a v6 target already resolved from `target` above (a
        // literal-v6 or hostname-with-AAAA `target` takes precedence).
        if (!m_v6.configured && !target6.isEmpty()) {
            ResolvedTargets resolved6 = resolveTargets(target6.toStdString());
            if (resolved6.v6) {
                m_v6.targetIp = QString::fromStdString(*resolved6.v6);
                m_v6.configured = true;
            }
        }

        m_tray = new QSystemTrayIcon(this);
        auto *menu = new QMenu();
        m_statusAction = menu->addAction(QString("%1: starting...").arg(m_label));
        m_statusAction->setEnabled(false);
        menu->addSeparator();
        menu->addAction("Quit", qApp, &QCoreApplication::quit);
        m_tray->setContextMenu(menu);
        m_tray->setToolTip(m_label);

        renderTray();
        m_tray->show();

        if (m_v4.configured) openSocket4();
        if (m_v6.configured) openSocket6();

        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &PingMonitor::tick);
        timer->start(1000);
    }

private:
    enum class Severity { Green, Yellow, Red };
    enum class ProtoState { NoLocalAddress, Failing, Healthy };

    struct ProtoTrack {
        bool configured = false;
        QString targetIp;
        int sock = -1;
        int failStreak = 0;
        bool hasLocalAddress = false;
        int recheckTicksLeft = 0;
    };

    // Short (<=3 char) tag for an interface name, for display on a small
    // icon: leading letters (up to 2) plus the trailing digit run, e.g.
    // "wlan0" -> "wl0", "enp196s0f4u1u4" -> "en4".
    static QString shortIfaceTag(const QString &iface) {
        QString letters;
        for (QChar c : iface) {
            if (!c.isLetter()) break;
            letters += c;
        }
        letters = letters.left(2);

        QString trailingDigits;
        int i = iface.size() - 1;
        while (i >= 0 && iface[i].isDigit()) {
            trailingDigits.prepend(iface[i]);
            --i;
        }

        return letters + trailingDigits.right(1);
    }

    void openSocket4() {
        if (m_v4.sock >= 0) close(m_v4.sock);
        m_v4.sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (m_v4.sock < 0) {
            perror("socket(AF_INET)");
            return;
        }
        if (setsockopt(m_v4.sock, SOL_SOCKET, SO_BINDTODEVICE, m_iface.toUtf8().constData(),
                        m_iface.toUtf8().size()) < 0) {
            perror("SO_BINDTODEVICE (v4)");
        }
        struct timeval tv { 0, 800 * 1000 }; // 800ms recv timeout
        setsockopt(m_v4.sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    void openSocket6() {
        if (m_v6.sock >= 0) close(m_v6.sock);
        m_v6.sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
        if (m_v6.sock < 0) {
            perror("socket(AF_INET6)");
            return;
        }
        if (setsockopt(m_v6.sock, SOL_SOCKET, SO_BINDTODEVICE, m_iface.toUtf8().constData(),
                        m_iface.toUtf8().size()) < 0) {
            perror("SO_BINDTODEVICE (v6)");
        }
        struct timeval tv { 0, 800 * 1000 };
        setsockopt(m_v6.sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    bool sendPing4() {
        if (m_v4.sock < 0) return false;

        struct sockaddr_in dst {};
        dst.sin_family = AF_INET;
        if (inet_pton(AF_INET, m_v4.targetIp.toUtf8().constData(), &dst.sin_addr) != 1) {
            return false;
        }

        struct {
            struct icmphdr hdr;
            char payload[16];
        } packet {};
        packet.hdr.type = ICMP_ECHO;
        packet.hdr.code = 0;
        packet.hdr.un.echo.id = m_pid;
        packet.hdr.un.echo.sequence = m_seq4++;
        std::memset(packet.payload, 0x42, sizeof(packet.payload));
        packet.hdr.checksum = 0;
        packet.hdr.checksum = checksum(&packet, sizeof(packet));

        ssize_t n = sendto(m_v4.sock, &packet, sizeof(packet), 0,
                            reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
        return n == sizeof(packet);
    }

    bool recvReply4() {
        if (m_v4.sock < 0) return false;

        char buf[512];
        struct sockaddr_in from {};
        socklen_t fromLen = sizeof(from);

        // Drain any replies waiting; accept if any matches our pid.
        while (true) {
            ssize_t n = recvfrom(m_v4.sock, buf, sizeof(buf), 0,
                                  reinterpret_cast<struct sockaddr *>(&from), &fromLen);
            if (n <= 0) return false;

            auto *ip = reinterpret_cast<struct iphdr *>(buf);
            int ipHeaderLen = ip->ihl * 4;
            if (n < ipHeaderLen + static_cast<ssize_t>(sizeof(struct icmphdr))) continue;

            auto *icmp = reinterpret_cast<struct icmphdr *>(buf + ipHeaderLen);
            if (icmp->type == ICMP_ECHOREPLY && icmp->un.echo.id == m_pid) {
                return true;
            }
        }
    }

    bool sendPing6() {
        if (m_v6.sock < 0) return false;

        struct sockaddr_in6 dst {};
        dst.sin6_family = AF_INET6;
        if (inet_pton(AF_INET6, m_v6.targetIp.toUtf8().constData(), &dst.sin6_addr) != 1) {
            return false;
        }

        struct {
            struct icmp6_hdr hdr;
            char payload[16];
        } packet {};
        packet.hdr.icmp6_type = ICMP6_ECHO_REQUEST;
        packet.hdr.icmp6_code = 0;
        packet.hdr.icmp6_id = m_pid;
        packet.hdr.icmp6_seq = m_seq6++;
        std::memset(packet.payload, 0x42, sizeof(packet.payload));
        // Leave icmp6_cksum as 0 -- the kernel computes/fills the ICMPv6
        // checksum in-kernel via the pseudo-header for raw AF_INET6 sockets.

        ssize_t n = sendto(m_v6.sock, &packet, sizeof(packet), 0,
                            reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
        return n == sizeof(packet);
    }

    bool recvReply6() {
        if (m_v6.sock < 0) return false;

        char buf[512];
        struct sockaddr_in6 from {};
        socklen_t fromLen = sizeof(from);

        // Unlike IPv4 raw sockets, an ICMPv6 raw socket's payload does not
        // include the IPv6 header -- it starts directly at icmp6_hdr.
        while (true) {
            ssize_t n = recvfrom(m_v6.sock, buf, sizeof(buf), 0,
                                  reinterpret_cast<struct sockaddr *>(&from), &fromLen);
            if (n <= 0) return false;
            if (n < static_cast<ssize_t>(sizeof(struct icmp6_hdr))) continue;

            auto *icmp6 = reinterpret_cast<struct icmp6_hdr *>(buf);
            if (icmp6->icmp6_type == ICMP6_ECHO_REPLY && icmp6->icmp6_id == m_pid) {
                return true;
            }
        }
    }

    // Refreshes hasLocalAddress for `t` if due for a recheck. Rechecks are
    // throttled to avoid a getifaddrs() call every tick while a protocol
    // has no address -- once addressed, no further rechecking is needed
    // until the process restarts (which happens naturally on interface
    // down/up via process_manager.cpp).
    void maybeRecheckLocalAddress(ProtoTrack &t, int family) {
        if (!t.configured || t.hasLocalAddress) return;
        if (t.recheckTicksLeft > 0) {
            t.recheckTicksLeft--;
            return;
        }
        t.hasLocalAddress = ifaceHasAddress(m_iface.toStdString(), family);
        t.recheckTicksLeft = kLocalAddressRecheckTicks;
    }

    void tickV4() {
        maybeRecheckLocalAddress(m_v4, AF_INET);
        if (!m_v4.configured || !m_v4.hasLocalAddress) return;

        bool ok = sendPing4() && recvReply4();
        if (ok) {
            m_v4.failStreak = 0;
        } else {
            m_v4.failStreak++;
            if (m_v4.failStreak % kFailStreakForSocketRecreate == 0) openSocket4();
        }
    }

    void tickV6() {
        maybeRecheckLocalAddress(m_v6, AF_INET6);
        if (!m_v6.configured || !m_v6.hasLocalAddress) return;

        bool ok = sendPing6() && recvReply6();
        if (ok) {
            m_v6.failStreak = 0;
        } else {
            m_v6.failStreak++;
            if (m_v6.failStreak % kFailStreakForSocketRecreate == 0) openSocket6();
        }
    }

    static ProtoState stateOf(const ProtoTrack &t) {
        if (!t.configured || !t.hasLocalAddress) return ProtoState::NoLocalAddress;
        return t.failStreak == 0 ? ProtoState::Healthy : ProtoState::Failing;
    }

    static Severity severityOf(const ProtoTrack &t) {
        return t.failStreak >= kFailStreakForRed ? Severity::Red : Severity::Yellow;
    }

    void tick() {
        tickV4();
        tickV6();

        auto state = std::make_tuple(m_v4.failStreak, m_v4.hasLocalAddress,
                                      m_v6.failStreak, m_v6.hasLocalAddress);
        if (state != m_lastTickState) {
            m_lastTickState = state;
            renderTray();
        }
    }

    void renderTray() {
        ProtoState v4State = stateOf(m_v4);
        ProtoState v6State = stateOf(m_v6);

        QString glyph;
        if (v4State == ProtoState::Healthy) glyph += "4";
        if (v6State == ProtoState::Healthy) {
            if (!glyph.isEmpty()) glyph += "/";
            glyph += "6";
        }

        Severity worst = Severity::Green;
        if (v4State != ProtoState::NoLocalAddress && severityWorse(severityOf(m_v4), worst)) {
            worst = v4State == ProtoState::Healthy ? Severity::Green : severityOf(m_v4);
        }
        if (v6State != ProtoState::NoLocalAddress && severityWorse(severityOf(m_v6), worst)) {
            worst = v6State == ProtoState::Healthy ? Severity::Green : severityOf(m_v6);
        }

        setIcon(worst, glyph);

        QStringList parts;
        if (v4State != ProtoState::NoLocalAddress) {
            parts << (v4State == ProtoState::Healthy
                          ? "v4 connected"
                          : QString("v4: %1 consecutive losses").arg(m_v4.failStreak));
        }
        if (v6State != ProtoState::NoLocalAddress) {
            parts << (v6State == ProtoState::Healthy
                          ? "v6 connected"
                          : QString("v6: %1 consecutive losses").arg(m_v6.failStreak));
        }
        QString status = parts.isEmpty() ? QString("%1: no target resolved").arg(m_label)
                                          : QString("%1: %2").arg(m_label, parts.join(", "));
        m_statusAction->setText(status);
        // Only touch the tooltip when its text actually changes -- calling
        // setToolTip() every tick (even with identical text) makes some
        // tray hosts (e.g. Plasma) tear down and re-show an already-open
        // tooltip, which looks like it vanishing mid-hover.
        if (status != m_currentTooltip) {
            m_currentTooltip = status;
            m_tray->setToolTip(status);
        }
    }

    // True if `candidate` is a worse (or equal-and-nontrivial) severity
    // than `current`, for worst-of aggregation across protocols.
    static bool severityWorse(Severity candidate, Severity current) {
        auto rank = [](Severity s) {
            switch (s) {
                case Severity::Green: return 0;
                case Severity::Yellow: return 1;
                case Severity::Red: return 2;
            }
            return 0;
        };
        return rank(candidate) >= rank(current);
    }

    QPixmap renderIcon(int kSize, const QColor &qc, const QString &glyph) {
        QPixmap pix(kSize, kSize);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);

        p.setBrush(qc);
        p.setPen(QColor(0, 0, 0, 60));
        p.drawEllipse(kSize / 16, kSize / 16, kSize - kSize / 8, kSize - kSize / 8);

        QFont ifaceFont = p.font();
        ifaceFont.setBold(true);
        ifaceFont.setPixelSize(kSize * 26 / 64);
        p.setFont(ifaceFont);
        p.setPen(Qt::black);
        p.drawText(QRect(0, 0, kSize, kSize), Qt::AlignCenter, m_ifaceTag);

        if (!glyph.isEmpty()) {
            QFont protoFont = p.font();
            protoFont.setBold(true);
            protoFont.setPixelSize(kSize * 22 / 64);
            p.setFont(protoFont);
            p.setPen(Qt::white);
            QFontMetrics protoMetrics(protoFont);
            QRect protoInk = protoMetrics.tightBoundingRect(glyph);
            // Shift so the glyph's actual ink (not its font-metrics box) is
            // flush against the top-right pixel corner of the canvas.
            int protoX = kSize - protoInk.width() - protoInk.left();
            int protoY = -protoInk.top();
            p.drawText(protoX, protoY, glyph);
        }
        p.end();

        return pix;
    }

    // Icons only vary by (severity, glyph) -- a small fixed set (3
    // severities x a handful of glyph strings) for a given process's
    // fixed m_ifaceTag. Caching by that key means QPainter only runs
    // once per distinct combo ever seen, not once per tick/transition --
    // a flapping connection re-uses cached QIcons instead of repainting
    // 7 pixmap sizes on every bounce.
    QIcon &iconFor(Severity worst, const QString &glyph) {
        auto key = std::make_pair(static_cast<int>(worst), glyph);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end()) return it->second;

        QColor qc;
        switch (worst) {
            case Severity::Green:  qc = QColor(0x2e, 0xcc, 0x71); break;
            case Severity::Yellow: qc = QColor(0xf1, 0xc4, 0x0f); break;
            case Severity::Red:    qc = QColor(0xe7, 0x4c, 0x3c); break;
        }

        // Render at several standard tray/panel sizes so the desktop shell
        // can pick the closest match instead of scaling one fixed bitmap.
        QIcon icon;
        for (int size : {16, 22, 24, 32, 48, 64, 128}) {
            icon.addPixmap(renderIcon(size, qc, glyph));
        }

        return m_iconCache.emplace(key, std::move(icon)).first->second;
    }

    void setIcon(Severity worst, const QString &glyph) {
        if (worst == m_currentSeverity && glyph == m_currentGlyph) return;
        m_currentSeverity = worst;
        m_currentGlyph = glyph;
        m_tray->setIcon(iconFor(worst, glyph));
    }

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
    // (v4 failStreak, v4 hasLocalAddress, v6 failStreak, v6 hasLocalAddress)
    // as of the last tick that actually changed something -- lets tick()
    // skip renderTray() entirely (and thus touching the tray widget at all)
    // on ticks where nothing changed, instead of calling it every second
    // and relying on renderTray()'s own internal no-op checks.
    std::tuple<int, bool, int, bool> m_lastTickState{-1, false, -1, false};
};

#include "conwatch-tray.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <interface> <target> [target6] [label]\n", argv[0]);
        return 1;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        fprintf(stderr, "No system tray available\n");
        return 1;
    }

    QString iface = argv[1];
    QString target = argv[2];
    QString target6 = argc >= 4 ? argv[3] : QString();
    QString label = argc >= 5 ? argv[4] : iface;

    PingMonitor monitor(iface, target, target6, label);

    return app.exec();
}
