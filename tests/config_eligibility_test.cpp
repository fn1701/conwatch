#include "../src/watcher/config.hpp"

#include <gtest/gtest.h>

TEST(Eligibility, EmptyIncludeExcludeAllowsEverythingExceptNothing)
{
    Config cfg;
    EXPECT_TRUE(isEligible(cfg, "wlan0"));
    EXPECT_TRUE(isEligible(cfg, "lo"));
}

TEST(Eligibility, ExcludeGlobBlocksMatches)
{
    Config cfg;
    cfg.exclude = {"lo", "docker*", "veth*"};
    EXPECT_FALSE(isEligible(cfg, "lo"));
    EXPECT_FALSE(isEligible(cfg, "docker0"));
    EXPECT_FALSE(isEligible(cfg, "veth1234"));
    EXPECT_TRUE(isEligible(cfg, "wlan0"));
}

TEST(Eligibility, IncludeGlobIsAllowList)
{
    Config cfg;
    cfg.include = {"wg*", "wlan0"};
    EXPECT_TRUE(isEligible(cfg, "wg0"));
    EXPECT_TRUE(isEligible(cfg, "wlan0"));
    EXPECT_FALSE(isEligible(cfg, "eth0"));
}

TEST(Eligibility, IncludeTakesPrecedenceOverExcludeWhenBothSetButExcludeIsNoop)
{
    Config cfg;
    cfg.include = {"wg*"};
    cfg.exclude = {"*"}; // documented no-op pattern
    EXPECT_TRUE(isEligible(cfg, "wg0"));
    EXPECT_FALSE(isEligible(cfg, "eth0"));
}

TEST(Eligibility, WildcardIncludeIsNoop)
{
    Config cfg;
    cfg.include = {"*"};
    cfg.exclude = {"docker*"};
    EXPECT_FALSE(isEligible(cfg, "docker0"));
    EXPECT_TRUE(isEligible(cfg, "wlan0"));
}
