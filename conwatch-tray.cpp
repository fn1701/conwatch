// conwatch-tray: per-interface ping monitor with a color-coded system tray icon.
//
// Usage: conwatch-tray <interface> <target-ip> [label]
//
// Sends one ICMP echo per second on a raw socket bound to <interface>,
// via <target-ip>. Tray icon color reflects recent loss:
//   green  - last ping succeeded
//   yellow - 1-9 consecutive losses
//   red    - 10+ consecutive losses
//
// After 10 consecutive losses the raw socket is closed and recreated
// (re-resolving the interface index and rebinding) to avoid a socket
// left stale by an interface renumber/reassociation (e.g. DHCP renewal,
// wifi roam, or the interface disappearing and reappearing with a new
// ifindex, as happens when a VPN tunnel is torn down and re-created).

#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QPainter>
#include <QPixmap>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

namespace {

constexpr int kFailStreakForRed = 10;
constexpr int kFailStreakForSocketRecreate = 10;

uint16_t checksum(void *buf, int len) {
    uint16_t *p = static_cast<uint16_t *>(buf);
    uint32_t sum = 0;
    for (; len > 1; len -= 2) sum += *p++;
    if (len == 1) sum += *reinterpret_cast<uint8_t *>(p);
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

} // namespace

class PingMonitor : public QObject {
    Q_OBJECT
public:
    PingMonitor(QString iface, QString targetIp, QString label)
        : m_iface(std::move(iface)), m_targetIp(std::move(targetIp)), m_label(std::move(label)) {
        m_pid = static_cast<uint16_t>(getpid() & 0xffff);

        m_tray = new QSystemTrayIcon(this);
        auto *menu = new QMenu();
        m_statusAction = menu->addAction(QString("%1: starting...").arg(m_label));
        m_statusAction->setEnabled(false);
        menu->addSeparator();
        menu->addAction("Quit", qApp, &QCoreApplication::quit);
        m_tray->setContextMenu(menu);
        m_tray->setToolTip(QString("%1 (%2 via %3)").arg(m_label, m_targetIp, m_iface));

        setColor(Color::Yellow);
        m_tray->show();

        openSocket();

        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &PingMonitor::tick);
        timer->start(1000);
    }

private:
    enum class Color { Green, Yellow, Red };

    void openSocket() {
        if (m_sock >= 0) close(m_sock);
        m_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (m_sock < 0) {
            perror("socket");
            return;
        }
        if (setsockopt(m_sock, SOL_SOCKET, SO_BINDTODEVICE, m_iface.toUtf8().constData(),
                        m_iface.toUtf8().size()) < 0) {
            perror("SO_BINDTODEVICE");
        }
        struct timeval tv { 0, 800 * 1000 }; // 800ms recv timeout
        setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    bool sendPing() {
        if (m_sock < 0) return false;

        struct sockaddr_in dst {};
        dst.sin_family = AF_INET;
        if (inet_pton(AF_INET, m_targetIp.toUtf8().constData(), &dst.sin_addr) != 1) {
            return false;
        }

        struct {
            struct icmphdr hdr;
            char payload[16];
        } packet {};
        packet.hdr.type = ICMP_ECHO;
        packet.hdr.code = 0;
        packet.hdr.un.echo.id = m_pid;
        packet.hdr.un.echo.sequence = m_seq++;
        std::memset(packet.payload, 0x42, sizeof(packet.payload));
        packet.hdr.checksum = 0;
        packet.hdr.checksum = checksum(&packet, sizeof(packet));

        ssize_t n = sendto(m_sock, &packet, sizeof(packet), 0,
                            reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
        return n == sizeof(packet);
    }

    bool recvReply() {
        if (m_sock < 0) return false;

        char buf[512];
        struct sockaddr_in from {};
        socklen_t fromLen = sizeof(from);

        // Drain any replies waiting; accept if any matches our pid/seq.
        while (true) {
            ssize_t n = recvfrom(m_sock, buf, sizeof(buf), 0,
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

    void tick() {
        bool ok = sendPing() && recvReply();

        if (ok) {
            m_failStreak = 0;
            setColor(Color::Green);
        } else {
            m_failStreak++;
            setColor(m_failStreak >= kFailStreakForRed ? Color::Red : Color::Yellow);

            if (m_failStreak > 0 && m_failStreak % kFailStreakForSocketRecreate == 0) {
                openSocket();
            }
        }

        QString status = ok
            ? QString("%1: connected").arg(m_label)
            : QString("%1: %2 consecutive losses").arg(m_label).arg(m_failStreak);
        m_statusAction->setText(status);
        m_tray->setToolTip(status);
    }

    void setColor(Color c) {
        if (c == m_currentColor) return;
        m_currentColor = c;

        QColor qc;
        switch (c) {
            case Color::Green:  qc = QColor(0x2e, 0xcc, 0x71); break;
            case Color::Yellow: qc = QColor(0xf1, 0xc4, 0x0f); break;
            case Color::Red:    qc = QColor(0xe7, 0x4c, 0x3c); break;
        }

        QPixmap pix(22, 22);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);

        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(16);
        p.setFont(font);
        p.setPen(qc);
        p.drawText(pix.rect(), Qt::AlignCenter, "4");
        p.end();

        m_tray->setIcon(QIcon(pix));
    }

    QString m_iface;
    QString m_targetIp;
    QString m_label;
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_statusAction = nullptr;
    int m_sock = -1;
    uint16_t m_pid = 0;
    uint16_t m_seq = 0;
    int m_failStreak = 0;
    Color m_currentColor = Color::Yellow;
};

#include "conwatch-tray.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <interface> <target-ip> [label]\n", argv[0]);
        return 1;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        fprintf(stderr, "No system tray available\n");
        return 1;
    }

    QString iface = argv[1];
    QString target = argv[2];
    QString label = argc >= 4 ? argv[3] : iface;

    PingMonitor monitor(iface, target, label);

    return app.exec();
}
