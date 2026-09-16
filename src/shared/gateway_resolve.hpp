#pragma once

#include "gateway_resolver.hpp"

#include <optional>
#include <string>

// Resolves the default gateway address for `iface` from the kernel routing
// table, for the given address family (AF_INET or AF_INET6). Returns
// unset if there is no default route via that interface for that family.
inline std::optional<std::string> resolveGateway(const std::string &iface, int family)
{
    return gateway_resolver::GatewayResolver(iface, family).resolve();
}
