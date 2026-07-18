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
    // Spawns `conwatch-tray <iface> <target> <target6> <label>` if not
    // already running for `iface`. `target6` may be empty (not provided).
    // No-op if already tracked.
    void start(const std::string &iface, const std::string &target, const std::string &target6, const std::string &label);

    // Sends SIGTERM to the tracked child for `iface` and stops
    // tracking it immediately (actual exit is reaped asynchronously
    // via reapExited()). No-op if not tracked.
    void stop(const std::string &iface);

    // Reaps any exited children (waitpid(..., WNOHANG) loop) and
    // clears their tracking entries if not already cleared by stop().
    // Call in response to SIGCHLD.
    void reapExited();

    // Sends SIGTERM to every tracked child, then blocks (with a short
    // timeout) waiting for them all to exit. Call on watcher shutdown.
    void stopAll();

    bool isRunning(const std::string &iface) const;

private:
    std::unordered_map<std::string, pid_t> m_byIface;
    std::unordered_map<pid_t, std::string> m_byPid;
};
