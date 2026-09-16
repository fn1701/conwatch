#include "ping_monitor.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

void PingMonitor::openSocket6()
{
    if (m_v6.sock >= 0)
        close(m_v6.sock);
    m_v6.sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    if (m_v6.sock < 0) {
        perror("socket(AF_INET6)");
        return;
    }
    if (setsockopt(m_v6.sock, SOL_SOCKET, SO_BINDTODEVICE, m_iface.toUtf8().constData(), m_iface.toUtf8().size()) < 0) {
        perror("SO_BINDTODEVICE (v6)");
    }
    struct timeval tv {
        0, 800 * 1000
    };
    setsockopt(m_v6.sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

bool PingMonitor::sendPing6(const QString &destIp)
{
    if (m_v6.sock < 0)
        return false;

    struct sockaddr_in6 dst {
    };
    dst.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, destIp.toUtf8().constData(), &dst.sin6_addr) != 1) {
        return false;
    }

    struct {
        struct icmp6_hdr hdr;
        char payload[16];
    } packet{};
    packet.hdr.icmp6_type = ICMP6_ECHO_REQUEST;
    packet.hdr.icmp6_code = 0;
    packet.hdr.icmp6_id = m_pid;
    packet.hdr.icmp6_seq = m_seq6++;
    std::memset(packet.payload, 0x42, sizeof(packet.payload));
    // Leave icmp6_cksum as 0 -- the kernel computes/fills the ICMPv6
    // checksum in-kernel via the pseudo-header for raw AF_INET6 sockets.

    ssize_t n = sendto(m_v6.sock, &packet, sizeof(packet), 0, reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
    return n == sizeof(packet);
}

bool PingMonitor::recvReply6()
{
    if (m_v6.sock < 0)
        return false;

    char buf[512];
    struct sockaddr_in6 from {
    };
    socklen_t fromLen = sizeof(from);

    // Unlike IPv4 raw sockets, an ICMPv6 raw socket's payload does not
    // include the IPv6 header -- it starts directly at icmp6_hdr.
    while (true) {
        ssize_t n = recvfrom(m_v6.sock, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr *>(&from), &fromLen);
        if (n <= 0)
            return false;
        if (isMatchingReply6(buf, n, m_pid))
            return true;
    }
}

bool PingMonitor::isMatchingReply6(const char *buf, ssize_t n, uint16_t pid)
{
    if (n < static_cast<ssize_t>(sizeof(struct icmp6_hdr)))
        return false;

    auto *icmp6 = reinterpret_cast<const struct icmp6_hdr *>(buf);
    return icmp6->icmp6_type == ICMP6_ECHO_REPLY && icmp6->icmp6_id == pid;
}
