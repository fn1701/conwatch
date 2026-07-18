// conwatch: netlink-driven auto-discovery of interfaces to monitor.
// Replaces per-interface systemd units and the NetworkManager
// dispatcher hook with a single daemon that reacts to kernel link
// state directly, so it works regardless of which (if any) network
// manager is running.
//
// Spawns/kills `conwatch-tray <iface> <target> <target6> <label>`
// child processes as interfaces matching the config's include/exclude
// rules go operationally up/down. See ../README.md for the config
// schema.

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

volatile sig_atomic_t g_shouldExit = 0;

} // namespace

int main()
{
    std::string configPath = resolveConfigPath();
    ensureConfigExists(configPath);
    Config cfg = loadConfig(configPath);

    NetlinkWatcher netlink;
    if (!netlink.open()) {
        fprintf(stderr, "conwatch: failed to open netlink socket\n");
        return 1;
    }

    ProcessManager processes;
    std::unordered_map<int, IfaceState> ifaces; // by ifindex

    // Block the signals we want to receive via signalfd, so the
    // default disposition never races with our poll() loop.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        perror("sigprocmask");
        return 1;
    }

    int sigFd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sigFd < 0) {
        perror("signalfd");
        return 1;
    }

    auto onChange = [&](int ifindex, const std::string &name, bool isUp) {
        IfaceState &state = ifaces[ifindex];
        state.name = name;

        if (isUp == state.wasUp)
            return;
        state.wasUp = isUp;

        if (isUp) {
            if (!isEligible(cfg, name))
                return;
            processes.start(name, resolveTarget(cfg, name), resolveTarget6(cfg, name), resolveLabel(cfg, name));
        } else {
            processes.stop(name);
        }
    };

    auto onRemoved = [&](int ifindex) {
        auto it = ifaces.find(ifindex);
        if (it == ifaces.end())
            return;
        if (it->second.wasUp)
            processes.stop(it->second.name);
        ifaces.erase(it);
    };

    fprintf(stderr, "conwatch: running (config: %s)\n", configPath.c_str());

    struct pollfd fds[2];
    fds[0] = {netlink.fd(), POLLIN, 0};
    fds[1] = {sigFd, POLLIN, 0};

    while (!g_shouldExit) {
        int ready = poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            netlink.processPendingMessages(onChange, onRemoved);
        }

        if (fds[1].revents & POLLIN) {
            struct signalfd_siginfo si;
            while (read(sigFd, &si, sizeof(si)) == sizeof(si)) {
                if (si.ssi_signo == SIGCHLD) {
                    processes.reapExited();
                } else {
                    g_shouldExit = 1;
                }
            }
        }
    }

    fprintf(stderr, "conwatch: shutting down\n");
    processes.stopAll();
    close(sigFd);
    return 0;
}
