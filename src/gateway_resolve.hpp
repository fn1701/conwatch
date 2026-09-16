#pragma once

#include <optional>
#include <string>

// Resolves the default gateway address for `iface` from the kernel routing
// table, for the given address family (AF_INET or AF_INET6). Returns
// unset if there is no default route via that interface for that family.
std::optional<std::string> resolveGateway(const std::string &iface, int family);
