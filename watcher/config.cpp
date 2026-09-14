#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{

constexpr const char *DefaultConfigYaml = R"YAML(# conwatch configuration
# Auto-generated with default values on first run. Edit and save --
# the watcher re-reads this file on next start (no live-reload).

# Default ping target for any monitored interface without its own
# "target" under interfaces: below.
default_target: "1.1.1.1"

# Optional default IPv6 ping target, used alongside default_target so
# interfaces without their own target/target6 override get both an
# IPv4 and IPv6 check. Leave unset to run IPv4-only by default (a
# hostname default_target that resolves to both A/AAAA records also
# enables both protocols, without needing this key).
# default_target6: "2606:4700:4700::1111"

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
#     target6: "fd00::1"
interfaces: {}
)YAML";

// Locates and creates the on-disk config file.
class ConfigFile
{
public:
    static std::string resolvePath()
    {
        const char *xdgConfig = std::getenv("XDG_CONFIG_HOME");
        fs::path base = (xdgConfig && *xdgConfig) ? fs::path(xdgConfig) : homeConfigDir();
        return (base / "conwatch" / "config.yaml").string();
    }

    // Writes the default config to `path` if it doesn't already exist
    // (temp-file-then-rename to avoid partial writes). Returns true if
    // a new file was created.
    static bool ensureExists(const std::string &path)
    {
        if (fs::exists(path))
            return false;

        fs::path p(path);
        fs::create_directories(p.parent_path());
        writeDefaultConfig(p);

        fprintf(stderr, "conwatch: created default config at %s\n", path.c_str());
        return true;
    }

private:
    static fs::path homeConfigDir()
    {
        const char *home = std::getenv("HOME");
        return fs::path(home ? home : ".") / ".config";
    }

    static void writeDefaultConfig(const fs::path &p)
    {
        fs::path tmp = p;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << DefaultConfigYaml;
        }
        fs::rename(tmp, p);
    }
};

} // namespace

// Free function kept as the public API contract declared in
// config.hpp; the real implementation is ConfigFile above.
std::string resolveConfigPath()
{
    return ConfigFile::resolvePath();
}

// Free function kept as the public API contract declared in
// config.hpp; the real implementation is ConfigFile above.
bool ensureConfigExists(const std::string &path)
{
    return ConfigFile::ensureExists(path);
}
