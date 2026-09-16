#include "../src/watcher/config.hpp"
#include "temp_config_file.hpp"

#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

TEST(Config, LoadsDefaultsForMissingKeys)
{
    TempConfigFile f("{}\n");
    Config cfg = loadConfig(f.path());
    EXPECT_EQ(cfg.defaultTarget, "1.1.1.1");
    EXPECT_TRUE(cfg.include.empty());
    EXPECT_TRUE(cfg.exclude.empty());
    EXPECT_TRUE(cfg.interfaces.empty());
}

TEST(Config, ParsesIncludeExcludeAndOverrides)
{
    TempConfigFile f(R"YAML(
default_target: "9.9.9.9"
include: []
exclude: ["lo", "docker*"]
interfaces:
  wg0:
    label: "VPN"
    target: "10.0.0.1"
)YAML");
    Config cfg = loadConfig(f.path());
    EXPECT_EQ(cfg.defaultTarget, "9.9.9.9");
    EXPECT_TRUE(cfg.include.empty());
    ASSERT_EQ(cfg.exclude.size(), 2u);
    EXPECT_EQ(cfg.exclude[0], "lo");
    ASSERT_TRUE(cfg.interfaces.count("wg0"));
    EXPECT_EQ(cfg.interfaces.at("wg0").label, "VPN");
    ASSERT_TRUE(cfg.interfaces.at("wg0").target.has_value());
    EXPECT_EQ(*cfg.interfaces.at("wg0").target, "10.0.0.1");
}

TEST(Config, ParsesGatewayIpOverrides)
{
    TempConfigFile f(R"YAML(
interfaces:
  wg0:
    label: "VPN"
    gateway_ip_override4: "10.0.100.1"
    gateway_ip_override6: "none"
)YAML");
    Config cfg = loadConfig(f.path());
    ASSERT_TRUE(cfg.interfaces.count("wg0"));
    ASSERT_TRUE(cfg.interfaces.at("wg0").gatewayIpOverride4.has_value());
    EXPECT_EQ(*cfg.interfaces.at("wg0").gatewayIpOverride4, "10.0.100.1");
    ASSERT_TRUE(cfg.interfaces.at("wg0").gatewayIpOverride6.has_value());
    EXPECT_EQ(*cfg.interfaces.at("wg0").gatewayIpOverride6, "none");
}

TEST(Config, BothIncludeAndExcludePopulatedIsAFatalError)
{
    TempConfigFile f(R"YAML(
include: ["wg*"]
exclude: ["docker*"]
)YAML");
    EXPECT_EXIT(loadConfig(f.path()), ::testing::ExitedWithCode(1), "config error");
}

TEST(EnsureConfigExists, WritesDefaultFileOnlyWhenMissing)
{
    fs::path dir = fs::temp_directory_path() / "conwatch-test-ensure";
    fs::remove_all(dir);
    std::string path = (dir / "config.yaml").string();

    EXPECT_TRUE(ensureConfigExists(path));
    EXPECT_TRUE(fs::exists(path));
    EXPECT_FALSE(ensureConfigExists(path)); // second call is a no-op

    Config cfg = loadConfig(path); // must be parseable
    EXPECT_EQ(cfg.defaultTarget, "1.1.1.1");

    fs::remove_all(dir);
}
