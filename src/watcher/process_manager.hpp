#pragma once

#include <string>
#include <sys/types.h>
#include <unordered_map>

// Tracks and manages conwatch-tray child processes, one per monitored
// interface. Uses fork()+execlp() (not QProcess/posix_spawn) so this
// watcher can stay Qt-free and lightweight while idle.
class ProcessManager
{
public:
    // Spawns `conwatch-tray <iface> <target> <target6> <label>
    // <gatewayIpOverride4> <gatewayIpOverride6>` if not already running for
    // `ifindex`. `target6`, `gatewayIpOverride4`, and `gatewayIpOverride6`
    // may be empty (not provided). No-op if already tracked. Tracking is
    // keyed by ifindex rather than `iface` so a kernel rename-while-up
    // (same interface, new name) is recognized as the same tracked
    // process instead of spawning a duplicate under the new name.
    void start(int ifindex,
               const std::string &iface,
               const std::string &target,
               const std::string &target6,
               const std::string &label,
               const std::string &gatewayIpOverride4,
               const std::string &gatewayIpOverride6);

    // Sends SIGTERM to the tracked child for `ifindex` and stops
    // tracking it immediately (actual exit is reaped asynchronously
    // via reapExited()). No-op if not tracked.
    void stop(int ifindex);

    // Reaps any exited children (waitpid(..., WNOHANG) loop) and
    // clears their tracking entries if not already cleared by stop().
    // Call in response to SIGCHLD.
    void reapExited();

    // Sends SIGTERM to every tracked child, then blocks (with a short
    // timeout) waiting for them all to exit. Call on watcher shutdown.
    void stopAll();

    bool isRunning(int ifindex) const;

private:
    struct TrackedProcess {
        pid_t pid;
        std::string iface;
    };

    std::unordered_map<int, TrackedProcess> m_byIfindex;
    std::unordered_map<pid_t, int> m_ifindexByPid;
};
