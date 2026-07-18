#include "../target_resolve.hpp"

#include <gtest/gtest.h>

TEST(ResolveTargets, LiteralIPv4NoDnsLookup)
{
    ResolvedTargets t = resolveTargets("1.1.1.1");
    ASSERT_TRUE(t.v4.has_value());
    EXPECT_EQ(*t.v4, "1.1.1.1");
    EXPECT_FALSE(t.v6.has_value());
}

TEST(ResolveTargets, LiteralIPv6)
{
    ResolvedTargets t = resolveTargets("2606:4700:4700::1111");
    ASSERT_TRUE(t.v6.has_value());
    EXPECT_EQ(*t.v6, "2606:4700:4700::1111");
    EXPECT_FALSE(t.v4.has_value());
}

TEST(ResolveTargets, UnresolvableHostnameLeavesBothUnset)
{
    ResolvedTargets t = resolveTargets("this-hostname-should-not-exist.invalid");
    EXPECT_FALSE(t.v4.has_value());
    EXPECT_FALSE(t.v6.has_value());
}

// Hostname-resolution tests that depend on live DNS/network are
// intentionally omitted -- this repo's other tests (config_test.cpp)
// avoid any environment/filesystem dependency beyond a private temp
// file, and a real DNS lookup would make this test flaky/offline-unsafe.
