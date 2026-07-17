#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace {

constexpr const char *kDefaultConfigYaml = R"YAML(# conwatch configuration
# Auto-generated with default values on first run. Edit and save --
# the watcher re-reads this file on next start (no live-reload).

# Default ping target for any monitored interface without its own
# "target" under interfaces: below.
default_target: "1.1.1.1"

# Exactly one of include / exclude should be populated; leave the
# other empty ([]). Populating both with real patterns is a startup
# error (see README) rather than a silently-guessed precedence.

# If non-empty, ONLY interfaces matching one of these glob patterns
# are monitored (allow-list mode).
include: []

# Interfaces matching any of these glob patterns are never monitored.
# Defaults cover loopback and common virtual/container interfaces.
exclude:
  - "lo"
  - "docker*"
  - "podman*"
  - "virbr*"
  - "veth*"
  - "br-*"

# Per-interface overrides, keyed by exact interface name. Optional.
# Example:
# interfaces:
#   wlan0:
#     label: "WiFi"
#   wg0:
#     label: "VPN"
#     target: "10.10.0.1"
interfaces: {}
)YAML";

bool isNoopPattern(const std::vector<std::string> &patterns) {
    return patterns.size() == 1 && patterns[0] == "*";
}

} // namespace

std::string resolveConfigPath() {
    const char *xdgConfig = std::getenv("XDG_CONFIG_HOME");
    fs::path base = (xdgConfig && *xdgConfig)
        ? fs::path(xdgConfig)
        : fs::path(std::getenv("HOME") ? std::getenv("HOME") : ".") / ".config";
    return (base / "conwatch" / "config.yaml").string();
}

bool ensureConfigExists(const std::string &path) {
    if (fs::exists(path)) return false;

    fs::path p(path);
    fs::create_directories(p.parent_path());

    fs::path tmp = p;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::trunc);
        out << kDefaultConfigYaml;
    }
    fs::rename(tmp, p);

    fprintf(stderr, "conwatch: created default config at %s\n", path.c_str());
    return true;
}

Config loadConfig(const std::string &path) {
    Config cfg;

    YAML::Node root = YAML::LoadFile(path);

    if (root["default_target"]) {
        cfg.defaultTarget = root["default_target"].as<std::string>();
    }
    if (root["include"]) {
        for (auto n : root["include"]) cfg.include.push_back(n.as<std::string>());
    }
    if (root["exclude"]) {
        for (auto n : root["exclude"]) cfg.exclude.push_back(n.as<std::string>());
    }
    if (root["interfaces"]) {
        for (auto it : root["interfaces"]) {
            InterfaceOverride ov;
            std::string ifaceName = it.first.as<std::string>();
            YAML::Node node = it.second;
            ov.label = node["label"] ? node["label"].as<std::string>() : ifaceName;
            if (node["target"]) ov.target = node["target"].as<std::string>();
            cfg.interfaces[ifaceName] = ov;
        }
    }

    bool includeReal = !cfg.include.empty() && !isNoopPattern(cfg.include);
    bool excludeReal = !cfg.exclude.empty() && !isNoopPattern(cfg.exclude);
    if (includeReal && excludeReal) {
        fprintf(stderr,
                "conwatch: config error: both include and exclude are "
                "populated with real patterns in %s. Exactly one must be used "
                "-- leave the other empty ([]) or set it to [\"*\"].\n",
                path.c_str());
        exit(1);
    }

    return cfg;
}

bool isEligible(const Config &cfg, const std::string &name) {
    auto matchesAny = [&name](const std::vector<std::string> &patterns) {
        for (const auto &p : patterns) {
            if (fnmatch(p.c_str(), name.c_str(), 0) == 0) return true;
        }
        return false;
    };

    bool includeReal = !cfg.include.empty() && !isNoopPattern(cfg.include);
    if (includeReal) return matchesAny(cfg.include);

    if (!cfg.exclude.empty()) return !matchesAny(cfg.exclude);

    return true;
}

std::string resolveTarget(const Config &cfg, const std::string &iface) {
    auto it = cfg.interfaces.find(iface);
    if (it != cfg.interfaces.end() && it->second.target) return *it->second.target;
    return cfg.defaultTarget;
}

std::string resolveLabel(const Config &cfg, const std::string &iface) {
    auto it = cfg.interfaces.find(iface);
    if (it != cfg.interfaces.end() && !it->second.label.empty()) return it->second.label;
    return iface;
}
