#pragma once

#include "netlink.hpp"
#include "process_manager.hpp"

#include <string>
#include <unordered_map>

#include "config.hpp"

struct IfaceState {
    std::string name;
    bool wasUp = false;
};

// Owns the daemon's netlink/signal poll loop: reacts to interface
// up/down transitions by starting/stopping conwatch-tray children, and
// to SIGCHLD/SIGTERM/SIGINT via a signalfd read inside the same loop
// (not an async-signal-handler context, so plain member state is safe
// in place of a volatile sig_atomic_t global).
class Watcher
{
public:
    Watcher(Config cfg, std::string configPath);

    int run();

private:
    // Blocks the signals we want to receive via signalfd, so the
    // default disposition never races with our poll() loop.
    bool openSignalFd();

    void poll();
    void handleSignals();

    NetlinkWatcher::LinkChangeCallback onChangeCallback();
    NetlinkWatcher::LinkRemovedCallback onRemovedCallback();

    void onChange(int ifindex, const std::string &name, bool isUp);
    void startIfEligible(int ifindex, const std::string &name);
    void onRemoved(int ifindex);

    Config m_cfg;
    std::string m_configPath;
    NetlinkWatcher m_netlink;
    ProcessManager m_processes;
    std::unordered_map<int, IfaceState> m_ifaces;
    int m_sigFd = -1;
    bool m_shouldExit = false;
};
