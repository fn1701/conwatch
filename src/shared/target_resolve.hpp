#pragma once

#include <optional>
#include <string>

struct ResolvedTargets {
    std::optional<std::string> v4; // dotted-quad, ready for inet_pton(AF_INET, ...)
    std::optional<std::string> v6; // ready for inet_pton(AF_INET6, ...)
};

// Resolves `target` (a literal IPv4 address, a literal IPv6 address, or a
// hostname) into zero, one, or two addresses to ping. Literal addresses are
// detected via inet_pton and returned as-is, with no DNS lookup. Hostnames
// are resolved via getaddrinfo(AF_UNSPEC); the first A and first AAAA record
// found (if any) become v4/v6. If resolution fails entirely, both stay unset.
ResolvedTargets resolveTargets(const std::string &target);
