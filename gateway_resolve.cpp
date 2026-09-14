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

// Resolves the default-route gateway for one interface/family pair via a
// single RTM_GETROUTE netlink dump conversation.
class GatewayResolver
{
public:
    GatewayResolver(const std::string &iface, int family)
        : m_family(family)
        , m_ifindex(static_cast<int>(if_nametoindex(iface.c_str())))
    {
    }

    std::optional<std::string> resolve()
    {
        if (m_ifindex == 0)
            return std::nullopt;

        int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (fd < 0)
            return std::nullopt;

        std::optional<std::string> result = sendRequestAndDrain(fd);
        close(fd);
        return result;
    }

private:
    std::optional<std::string> sendRequestAndDrain(int fd)
    {
        RouteRequest req = buildRequest();
        if (send(fd, &req, sizeof(req), 0) != static_cast<ssize_t>(sizeof(req)))
            return std::nullopt;
        return drain(fd);
    }

    RouteRequest buildRequest() const
    {
        RouteRequest req{};
        req.hdr.nlmsg_len = sizeof(req);
        req.hdr.nlmsg_type = RTM_GETROUTE;
        req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
        req.hdr.nlmsg_seq = 1;
        req.rtm.rtm_family = static_cast<unsigned char>(m_family);
        return req;
    }

    std::optional<std::string> drain(int fd)
    {
        std::optional<std::string> result;
        char buf[8192];
        bool done = false;
        while (!done) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            if (auto gw = scanBatch(buf, n, done))
                result = gw;
        }
        return result;
    }

    // Scans one recv()'d batch of netlink messages for a default-route
    // gateway. Sets `done` once the dump ends (NLMSG_DONE) or errors
    // (NLMSG_ERROR); returns the last matching gateway found, if any.
    std::optional<std::string> scanBatch(char *buf, ssize_t n, bool &done)
    {
        std::optional<std::string> result;
        for (auto *nlh = reinterpret_cast<struct nlmsghdr *>(buf); NLMSG_OK(nlh, n); nlh = NLMSG_NEXT(nlh, n)) {
            if (nlh->nlmsg_type == NLMSG_DONE || nlh->nlmsg_type == NLMSG_ERROR) {
                done = true;
                break;
            }
            if (nlh->nlmsg_type != RTM_NEWROUTE)
                continue;

            if (auto gw = parseDefaultRouteGateway(nlh)) {
                // Keep draining so the socket doesn't leave unread
                // messages behind, but we already have our answer.
                result = gw;
            }
        }
        return result;
    }

    // A default route has no RTA_DST (dst prefix length 0) and carries
    // RTA_GATEWAY (next hop) plus RTA_OIF (outgoing interface index).
    std::optional<std::string> parseDefaultRouteGateway(const struct nlmsghdr *nlh) const
    {
        auto *rtm = static_cast<const struct rtmsg *>(NLMSG_DATA(nlh));
        if (rtm->rtm_family != m_family || rtm->rtm_dst_len != 0)
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

        if (oif != m_ifindex || gateway == nullptr)
            return std::nullopt;

        return formatGateway(gateway, gatewayLen);
    }

    std::optional<std::string> formatGateway(const void *gateway, int gatewayLen) const
    {
        char buf[INET6_ADDRSTRLEN];
        if (m_family == AF_INET && gatewayLen == sizeof(struct in_addr)) {
            if (inet_ntop(AF_INET, gateway, buf, sizeof(buf)))
                return std::string(buf);
        } else if (m_family == AF_INET6 && gatewayLen == sizeof(struct in6_addr)) {
            if (inet_ntop(AF_INET6, gateway, buf, sizeof(buf)))
                return std::string(buf);
        }
        return std::nullopt;
    }

    int m_family;
    int m_ifindex;
};

} // namespace

// Free function kept as the public API contract declared in
// gateway_resolve.hpp; the real implementation is GatewayResolver above.
std::optional<std::string> resolveGateway(const std::string &iface, int family)
{
    return GatewayResolver(iface, family).resolve();
}
