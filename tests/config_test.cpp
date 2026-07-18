#include "../watcher/config.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

// Writes `contents` to a fresh temp file and returns its path; the
// file is removed when the returned guard goes out of scope.
class TempConfigFile {
public:
    explicit TempConfigFile(const std::string &contents) {
        m_path = (fs::temp_directory_path() /
                   ("conwatch-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                    "-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".yaml"))
                      .string();
        std::ofstream out(m_path, std::ios::trunc);
        out << contents;
    }
    ~TempConfigFile() { fs::remove(m_path); }

    const std::string &path() const { return m_path; }

private:
    std::string m_path;
};

} // namespace

TEST(Config, LoadsDefaultsForMissingKeys) {
    TempConfigFile f("{}\n");
    Config cfg = loadConfig(f.path());
    EXPECT_EQ(cfg.defaultTarget, "1.1.1.1");
    EXPECT_TRUE(cfg.include.empty());
    EXPECT_TRUE(cfg.exclude.empty());
    EXPECT_TRUE(cfg.interfaces.empty());
}

TEST(Config, ParsesIncludeExcludeAndOverrides) {
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

TEST(Eligibility, EmptyIncludeExcludeAllowsEverythingExceptNothing) {
    Config cfg;
    EXPECT_TRUE(isEligible(cfg, "wlan0"));
    EXPECT_TRUE(isEligible(cfg, "lo"));
}

TEST(Eligibility, ExcludeGlobBlocksMatches) {
    Config cfg;
    cfg.exclude = {"lo", "docker*", "veth*"};
    EXPECT_FALSE(isEligible(cfg, "lo"));
    EXPECT_FALSE(isEligible(cfg, "docker0"));
    EXPECT_FALSE(isEligible(cfg, "veth1234"));
    EXPECT_TRUE(isEligible(cfg, "wlan0"));
}

TEST(Eligibility, IncludeGlobIsAllowList) {
    Config cfg;
    cfg.include = {"wg*", "wlan0"};
    EXPECT_TRUE(isEligible(cfg, "wg0"));
    EXPECT_TRUE(isEligible(cfg, "wlan0"));
    EXPECT_FALSE(isEligible(cfg, "eth0"));
}

TEST(Eligibility, IncludeTakesPrecedenceOverExcludeWhenBothSetButExcludeIsNoop) {
    Config cfg;
    cfg.include = {"wg*"};
    cfg.exclude = {"*"}; // documented no-op pattern
    EXPECT_TRUE(isEligible(cfg, "wg0"));
    EXPECT_FALSE(isEligible(cfg, "eth0"));
}

TEST(Eligibility, WildcardIncludeIsNoop) {
    Config cfg;
    cfg.include = {"*"};
    cfg.exclude = {"docker*"};
    EXPECT_FALSE(isEligible(cfg, "docker0"));
    EXPECT_TRUE(isEligible(cfg, "wlan0"));
}

TEST(Resolve, TargetAndLabelFallBackToDefaults) {
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

TEST(Resolve, Target6FallsBackToDefaultTarget6OrEmpty) {
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

TEST(Config, BothIncludeAndExcludePopulatedIsAFatalError) {
    TempConfigFile f(R"YAML(
include: ["wg*"]
exclude: ["docker*"]
)YAML");
    EXPECT_EXIT(loadConfig(f.path()), ::testing::ExitedWithCode(1), "config error");
}

TEST(EnsureConfigExists, WritesDefaultFileOnlyWhenMissing) {
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
