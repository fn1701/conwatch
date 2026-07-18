#include "gateway_resolve.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

struct RouteRequest {
    struct nlmsghdr hdr;
    struct rtmsg rtm;
};

// A default route has no RTA_DST (dst prefix length 0) and carries
// RTA_GATEWAY (next hop) plus RTA_OIF (outgoing interface index).
std::optional<std::string> parseDefaultRouteGateway(const struct nlmsghdr *nlh, int family, int ifindex)
{
    auto *rtm = static_cast<const struct rtmsg *>(NLMSG_DATA(nlh));
    if (rtm->rtm_family != family || rtm->rtm_dst_len != 0)
        return std::nullopt;

    int oif = -1;
    const void *gateway = nullptr;
    int gatewayLen = 0;

    int attrLen = static_cast<int>(NLMSG_PAYLOAD(nlh, sizeof(struct rtmsg)));
    for (auto *rta = reinterpret_cast<const struct rtattr *>(reinterpret_cast<const char *>(rtm) + NLMSG_ALIGN(sizeof(struct rtmsg))); RTA_OK(rta, attrLen);
         rta = RTA_NEXT(rta, attrLen)) {
        if (rta->rta_type == RTA_OIF) {
            oif = *static_cast<const int *>(RTA_DATA(rta));
        } else if (rta->rta_type == RTA_GATEWAY) {
            gateway = RTA_DATA(rta);
            gatewayLen = static_cast<int>(RTA_PAYLOAD(rta));
        }
    }

    if (oif != ifindex || gateway == nullptr)
        return std::nullopt;

    char buf[INET6_ADDRSTRLEN];
    if (family == AF_INET && gatewayLen == sizeof(struct in_addr)) {
        if (inet_ntop(AF_INET, gateway, buf, sizeof(buf)))
            return std::string(buf);
    } else if (family == AF_INET6 && gatewayLen == sizeof(struct in6_addr)) {
        if (inet_ntop(AF_INET6, gateway, buf, sizeof(buf)))
            return std::string(buf);
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> resolveGateway(const std::string &iface, int family)
{
    int ifindex = static_cast<int>(if_nametoindex(iface.c_str()));
    if (ifindex == 0)
        return std::nullopt;

    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0)
        return std::nullopt;

    RouteRequest req{};
    req.hdr.nlmsg_len = sizeof(req);
    req.hdr.nlmsg_type = RTM_GETROUTE;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.hdr.nlmsg_seq = 1;
    req.rtm.rtm_family = static_cast<unsigned char>(family);

    if (send(fd, &req, sizeof(req), 0) != static_cast<ssize_t>(sizeof(req))) {
        close(fd);
        return std::nullopt;
    }

    std::optional<std::string> result;
    char buf[8192];
    bool done = false;
    while (!done) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
            break;

        for (auto *nlh = reinterpret_cast<struct nlmsghdr *>(buf); NLMSG_OK(nlh, n); nlh = NLMSG_NEXT(nlh, n)) {
            if (nlh->nlmsg_type == NLMSG_DONE) {
                done = true;
                break;
            }
            if (nlh->nlmsg_type == NLMSG_ERROR) {
                done = true;
                break;
            }
            if (nlh->nlmsg_type != RTM_NEWROUTE)
                continue;

            if (auto gw = parseDefaultRouteGateway(nlh, family, ifindex)) {
                result = gw;
                // Keep draining so the socket doesn't leave unread
                // messages behind, but we already have our answer.
            }
        }
    }

    close(fd);
    return result;
}
