#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct InterfaceOverride {
    std::string label;
    std::optional<std::string> target;
    std::optional<std::string> target6;
};

struct Config {
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    std::string defaultTarget = "1.1.1.1";
    std::optional<std::string> defaultTarget6;
    std::unordered_map<std::string, InterfaceOverride> interfaces;
};

// Implemented across config.cpp (path/default-file creation),
// config_load.cpp (loadConfig/isEligible), and config_resolve.cpp
// (per-interface target/label resolution).

// Resolves ~/.config/nettray/config.yaml (respecting $XDG_CONFIG_HOME).
std::string resolveConfigPath();

// Writes the documented default config to `path` if it does not
// already exist (temp-file-then-rename to avoid partial writes).
// Returns true if a new file was created, false if one already existed.
bool ensureConfigExists(const std::string &path);

// Loads and validates the config at `path`. Exits the process with a
// clear error message if include/exclude are both populated with real
// (non-"*") patterns, per the documented precedence rule.
Config loadConfig(const std::string &path);

// True if `name` should be monitored under `cfg`'s include/exclude rules.
bool isEligible(const Config &cfg, const std::string &name);

// Resolves the effective ping target and label for a given interface,
// applying any per-interface override.
std::string resolveTarget(const Config &cfg, const std::string &iface);
std::string resolveLabel(const Config &cfg, const std::string &iface);

// Resolves the effective IPv6 default-target override for a given
// interface, if any (per-interface target6, falling back to
// default_target6). Empty string if neither is set -- conwatch-tray
// treats an empty target6 argv as "not provided".
std::string resolveTarget6(const Config &cfg, const std::string &iface);
