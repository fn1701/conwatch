#include "../gateway_resolve.hpp"

#include <gtest/gtest.h>
#include <sys/socket.h>

TEST(ResolveGateway, NonexistentInterfaceReturnsUnset)
{
    auto gw = resolveGateway("this-iface-should-not-exist0", AF_INET);
    EXPECT_FALSE(gw.has_value());
}

TEST(ResolveGateway, LoopbackHasNoDefaultRouteV4)
{
    auto gw = resolveGateway("lo", AF_INET);
    EXPECT_FALSE(gw.has_value());
}

TEST(ResolveGateway, LoopbackHasNoDefaultRouteV6)
{
    auto gw = resolveGateway("lo", AF_INET6);
    EXPECT_FALSE(gw.has_value());
}

// A test asserting an actual gateway address is intentionally omitted --
// that depends on the live routing table of whatever machine/container
// runs this suite (which interface has a default route, and what its
// gateway is), the same environment-dependency this repo's other tests
// (config_test.cpp, target_resolve_test.cpp) avoid.
