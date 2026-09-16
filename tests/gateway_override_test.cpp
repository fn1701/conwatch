#include "../gateway_override.hpp"

#include <gtest/gtest.h>

TEST(GatewayOverride, EmptyValueIsAuto)
{
    GatewayOverride ov("");
    EXPECT_EQ(ov.mode(), GatewayOverride::Mode::Auto);
    EXPECT_EQ(ov.fixedIp(), "");
}

TEST(GatewayOverride, NoneSentinelIsDisabled)
{
    GatewayOverride ov("none");
    EXPECT_EQ(ov.mode(), GatewayOverride::Mode::Disabled);
}

TEST(GatewayOverride, NoneSentinelIsCaseInsensitive)
{
    GatewayOverride ov("None");
    EXPECT_EQ(ov.mode(), GatewayOverride::Mode::Disabled);
}

TEST(GatewayOverride, LiteralIpIsFixed)
{
    GatewayOverride ov("10.0.100.1");
    EXPECT_EQ(ov.mode(), GatewayOverride::Mode::Fixed);
    EXPECT_EQ(ov.fixedIp(), "10.0.100.1");
}

TEST(GatewayOverride, LiteralIpv6IsFixed)
{
    GatewayOverride ov("fd00::1");
    EXPECT_EQ(ov.mode(), GatewayOverride::Mode::Fixed);
    EXPECT_EQ(ov.fixedIp(), "fd00::1");
}
