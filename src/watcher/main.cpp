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
#include "netlink.hpp"
#include "process_manager.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <poll.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <unordered_map>

namespace
{

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
    Watcher(Config cfg, std::string configPath)
        : m_cfg(std::move(cfg))
        , m_configPath(std::move(configPath))
    {
    }

    int run()
    {
        if (!m_netlink.open()) {
            fprintf(stderr, "conwatch: failed to open netlink socket\n");
            return 1;
        }
        if (!openSignalFd())
            return 1;

        fprintf(stderr, "conwatch: running (config: %s)\n", m_configPath.c_str());
        poll();

        fprintf(stderr, "conwatch: shutting down\n");
        m_processes.stopAll();
        close(m_sigFd);
        return 0;
    }

private:
    // Blocks the signals we want to receive via signalfd, so the
    // default disposition never races with our poll() loop.
    bool openSignalFd()
    {
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
            perror("sigprocmask");
            return false;
        }

        m_sigFd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
        if (m_sigFd < 0) {
            perror("signalfd");
            return false;
        }
        return true;
    }

    void poll()
    {
        struct pollfd fds[2];
        fds[0] = {m_netlink.fd(), POLLIN, 0};
        fds[1] = {m_sigFd, POLLIN, 0};

        while (!m_shouldExit) {
            int ready = ::poll(fds, 2, -1);
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                perror("poll");
                break;
            }

            if (fds[0].revents & POLLIN)
                m_netlink.processPendingMessages(onChangeCallback(), onRemovedCallback());
            if (fds[1].revents & POLLIN)
                handleSignals();
        }
    }

    void handleSignals()
    {
        struct signalfd_siginfo si;
        while (read(m_sigFd, &si, sizeof(si)) == sizeof(si)) {
            if (si.ssi_signo == SIGCHLD)
                m_processes.reapExited();
            else
                m_shouldExit = true;
        }
    }

    NetlinkWatcher::LinkChangeCallback onChangeCallback()
    {
        return [this](int ifindex, const std::string &name, bool isUp) {
            onChange(ifindex, name, isUp);
        };
    }

    NetlinkWatcher::LinkRemovedCallback onRemovedCallback()
    {
        return [this](int ifindex) {
            onRemoved(ifindex);
        };
    }

    void onChange(int ifindex, const std::string &name, bool isUp)
    {
        IfaceState &state = m_ifaces[ifindex];
        bool renamed = !state.name.empty() && state.name != name;
        bool wasUp = state.wasUp;
        state.name = name;
        state.wasUp = isUp;

        // A rename-while-up carries the same ifindex and isUp=true
        // throughout, so it would otherwise be swallowed by the
        // isUp-transition check below. Stop the process tracked under
        // the old name and restart it under the new one so it never
        // keeps binding to a device name the kernel has already
        // dropped.
        if (renamed && wasUp) {
            m_processes.stop(ifindex);
            if (isUp)
                startIfEligible(ifindex, name);
            return;
        }

        if (isUp == wasUp)
            return;

        if (isUp)
            startIfEligible(ifindex, name);
        else
            m_processes.stop(ifindex);
    }

    void startIfEligible(int ifindex, const std::string &name)
    {
        if (!isEligible(m_cfg, name))
            return;
        m_processes.start(ifindex,
                          name,
                          resolveTarget(m_cfg, name),
                          resolveTarget6(m_cfg, name),
                          resolveLabel(m_cfg, name),
                          resolveGatewayIpOverride4(m_cfg, name),
                          resolveGatewayIpOverride6(m_cfg, name));
    }

    void onRemoved(int ifindex)
    {
        auto it = m_ifaces.find(ifindex);
        if (it == m_ifaces.end())
            return;
        if (it->second.wasUp)
            m_processes.stop(ifindex);
        m_ifaces.erase(it);
    }

    Config m_cfg;
    std::string m_configPath;
    NetlinkWatcher m_netlink;
    ProcessManager m_processes;
    std::unordered_map<int, IfaceState> m_ifaces;
    int m_sigFd = -1;
    bool m_shouldExit = false;
};

} // namespace

int main()
{
    std::string configPath = resolveConfigPath();
    ensureConfigExists(configPath);

    Watcher watcher(loadConfig(configPath), configPath);
    return watcher.run();
}
