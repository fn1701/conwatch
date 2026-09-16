// conwatch: netlink-driven auto-discovery of interfaces to monitor.
// Replaces per-interface systemd units and the NetworkManager
// dispatcher hook with a single daemon that reacts to kernel link
// state directly, so it works regardless of which (if any) network
// manager is running.
//
// Spawns/kills `conwatch-tray <iface> <target> <target6> <label>
// <gateway_ip_override4> <gateway_ip_override6>` child processes as
// interfaces matching the config's include/exclude rules go operationally
// up/down. See ../README.md for the config schema.

#include "config.hpp"
#include "watcher.hpp"

int main()
{
    std::string configPath = resolveConfigPath();
    ensureConfigExists(configPath);

    Watcher watcher(loadConfig(configPath), configPath);
    return watcher.run();
}
