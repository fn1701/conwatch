#include "../src/watcher/config.hpp"

#include <gtest/gtest.h>

TEST(Resolve, TargetAndLabelFallBackToDefaults)
{
    Config cfg;
    cfg.defaultTarget = "1.1.1.1";
    InterfaceOverride ov;
    ov.label = "VPN";
    ov.target = "10.10.0.1";
    cfg.interfaces["wg0"] = ov;

    EXPECT_EQ(resolveTarget(cfg, "wg0"), "10.10.0.1");
    EXPECT_EQ(resolveLabel(cfg, "wg0"), "VPN");
    EXPECT_EQ(resolveTarget(cfg, "wlan0"), "1.1.1.1");
    EXPECT_EQ(resolveLabel(cfg, "wlan0"), "wlan0");
}

TEST(Resolve, GatewayIpOverrideFallsBackToEmptyWhenUnset)
{
    Config cfg;
    InterfaceOverride ov;
    ov.label = "VPN";
    ov.gatewayIpOverride4 = "10.0.100.1";
    cfg.interfaces["wg0"] = ov;

    EXPECT_EQ(resolveGatewayIpOverride4(cfg, "wg0"), "10.0.100.1");
    EXPECT_EQ(resolveGatewayIpOverride6(cfg, "wg0"), "");
    EXPECT_EQ(resolveGatewayIpOverride4(cfg, "wlan0"), "");
}

TEST(Resolve, Target6FallsBackToDefaultTarget6OrEmpty)
{
    Config cfg;
    cfg.defaultTarget6 = "2606:4700:4700::1111";
    InterfaceOverride ov;
    ov.label = "VPN";
    ov.target6 = "fd00::1";
    cfg.interfaces["wg0"] = ov;

    EXPECT_EQ(resolveTarget6(cfg, "wg0"), "fd00::1");
    EXPECT_EQ(resolveTarget6(cfg, "wlan0"), "2606:4700:4700::1111");

    Config noDefault;
    EXPECT_EQ(resolveTarget6(noDefault, "wlan0"), "");
}
