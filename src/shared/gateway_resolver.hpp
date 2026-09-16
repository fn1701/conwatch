#pragma once

#include <linux/rtnetlink.h>
#include <optional>
#include <string>

// Implementation detail of resolveGateway() (see gateway_resolve.hpp).
// Not part of the public API -- only gateway_resolve.hpp should include
// this header.
namespace gateway_resolver
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
    GatewayResolver(const std::string &iface, int family);

    std::optional<std::string> resolve();

private:
    std::optional<std::string> sendRequestAndDrain(int fd);
    RouteRequest buildRequest() const;
    std::optional<std::string> drain(int fd);

    // Scans one recv()'d batch of netlink messages for a default-route
    // gateway. Sets `done` once the dump ends (NLMSG_DONE) or errors
    // (NLMSG_ERROR); returns the last matching gateway found, if any.
    std::optional<std::string> scanBatch(char *buf, ssize_t n, bool &done);

    // A default route has no RTA_DST (dst prefix length 0) and carries
    // RTA_GATEWAY (next hop) plus RTA_OIF (outgoing interface index).
    std::optional<std::string> parseDefaultRouteGateway(const struct nlmsghdr *nlh) const;
    std::optional<std::string> formatGateway(const void *gateway, int gatewayLen) const;

    int m_family;
    int m_ifindex;
};

} // namespace gateway_resolver
