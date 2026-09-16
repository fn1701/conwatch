#include "ping_monitor.hpp"

#include <ifaddrs.h>
#include <netinet/in.h>

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
