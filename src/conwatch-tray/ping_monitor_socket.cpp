#include "ping_monitor.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <unistd.h>

uint16_t PingMonitor::checksum(void *buf, int len)
{
    uint16_t *p = static_cast<uint16_t *>(buf);
    uint32_t sum = 0;
    for (; len > 1; len -= 2)
        sum += *p++;
    if (len == 1)
        sum += *reinterpret_cast<uint8_t *>(p);
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

// True if `iface` currently has at least one non-link-local address of
// `family` (AF_INET or AF_INET6) assigned. Link-local IPv6 (fe80::/10) is
// excluded since it can't reach an external ping target.
bool PingMonitor::matchesIfaceAddress(const struct ifaddrs *a, const std::string &iface, int family)
{
    if (!a->ifa_addr || a->ifa_addr->sa_family != family)
        return false;
    if (iface != a->ifa_name)
        return false;

    if (family == AF_INET6) {
        auto *sin6 = reinterpret_cast<struct sockaddr_in6 *>(a->ifa_addr);
        if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
            return false;
    }
    return true;
}

bool PingMonitor::ifaceHasAddress(const std::string &iface, int family)
{
    struct ifaddrs *addrs = nullptr;
    if (getifaddrs(&addrs) != 0)
        return false;

    bool found = false;
    for (struct ifaddrs *a = addrs; a != nullptr; a = a->ifa_next) {
        if (matchesIfaceAddress(a, iface, family)) {
            found = true;
            break;
        }
    }
    freeifaddrs(addrs);
    return found;
}

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
