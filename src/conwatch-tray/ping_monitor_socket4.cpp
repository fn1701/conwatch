#include "ping_monitor.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <unistd.h>

void PingMonitor::openSocket4()
{
    if (m_v4.sock >= 0)
        close(m_v4.sock);
    m_v4.sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (m_v4.sock < 0) {
        perror("socket(AF_INET)");
        return;
    }
    if (setsockopt(m_v4.sock, SOL_SOCKET, SO_BINDTODEVICE, m_iface.toUtf8().constData(), m_iface.toUtf8().size()) < 0) {
        perror("SO_BINDTODEVICE (v4)");
    }
    struct timeval tv {
        0, 800 * 1000
    }; // 800ms recv timeout
    setsockopt(m_v4.sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

bool PingMonitor::sendPing4(const QString &destIp)
{
    if (m_v4.sock < 0)
        return false;

    struct sockaddr_in dst {
    };
    dst.sin_family = AF_INET;
    if (inet_pton(AF_INET, destIp.toUtf8().constData(), &dst.sin_addr) != 1) {
        return false;
    }

    struct {
        struct icmphdr hdr;
        char payload[16];
    } packet{};
    packet.hdr.type = ICMP_ECHO;
    packet.hdr.code = 0;
    packet.hdr.un.echo.id = m_pid;
    packet.hdr.un.echo.sequence = m_seq4++;
    std::memset(packet.payload, 0x42, sizeof(packet.payload));
    packet.hdr.checksum = 0;
    packet.hdr.checksum = checksum(&packet, sizeof(packet));

    ssize_t n = sendto(m_v4.sock, &packet, sizeof(packet), 0, reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
    return n == sizeof(packet);
}

bool PingMonitor::isMatchingReply4(const char *buf, ssize_t n, uint16_t pid)
{
    auto *ip = reinterpret_cast<const struct iphdr *>(buf);
    int ipHeaderLen = ip->ihl * 4;
    if (n < ipHeaderLen + static_cast<ssize_t>(sizeof(struct icmphdr)))
        return false;

    auto *icmp = reinterpret_cast<const struct icmphdr *>(buf + ipHeaderLen);
    return icmp->type == ICMP_ECHOREPLY && icmp->un.echo.id == pid;
}

bool PingMonitor::recvReply4()
{
    if (m_v4.sock < 0)
        return false;

    char buf[512];
    struct sockaddr_in from {
    };
    socklen_t fromLen = sizeof(from);

    // Drain any replies waiting; accept if any matches our pid.
    while (true) {
        ssize_t n = recvfrom(m_v4.sock, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr *>(&from), &fromLen);
        if (n <= 0)
            return false;
        if (isMatchingReply4(buf, n, m_pid))
            return true;
    }
}
