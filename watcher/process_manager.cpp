#include "process_manager.hpp"

#include <cstdio>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

void ProcessManager::start(const std::string &iface, const std::string &target,
                             const std::string &label) {
    if (isRunning(iface)) return;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0) {
        // The watcher blocks SIGCHLD/SIGTERM/SIGINT via sigprocmask so it
        // can receive them through its signalfd; that blocked mask is
        // otherwise inherited across fork()+exec(), which would leave
        // the child unable to be terminated by SIGTERM. Unblock
        // everything before exec so conwatch-tray gets normal signal defaults.
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);

        execlp("/usr/local/bin/conwatch-tray", "conwatch-tray", iface.c_str(), target.c_str(),
               label.c_str(), static_cast<char *>(nullptr));
        _exit(127); // exec failed
    }

    fprintf(stderr, "conwatch: started conwatch-tray for %s (pid %d)\n", iface.c_str(), pid);
    m_byIface[iface] = pid;
    m_byPid[pid] = iface;
}

void ProcessManager::stop(const std::string &iface) {
    auto it = m_byIface.find(iface);
    if (it == m_byIface.end()) return;

    pid_t pid = it->second;
    kill(pid, SIGTERM);
    fprintf(stderr, "conwatch: stopping conwatch-tray for %s (pid %d)\n", iface.c_str(), pid);

    m_byIface.erase(it);
    m_byPid.erase(pid);
}

void ProcessManager::reapExited() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        auto it = m_byPid.find(pid);
        if (it != m_byPid.end()) {
            m_byIface.erase(it->second);
            m_byPid.erase(it);
        }
    }
}

void ProcessManager::stopAll() {
    for (const auto &[pid, iface] : m_byPid) {
        kill(pid, SIGTERM);
    }

    // Blocking wait, bounded by iteration count rather than wall clock
    // to avoid pulling in a timer for a shutdown path executed once.
    for (int i = 0; i < 200 && !m_byPid.empty(); ++i) {
        int status;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid > 0) {
            auto it = m_byPid.find(pid);
            if (it != m_byPid.end()) {
                m_byIface.erase(it->second);
                m_byPid.erase(it);
            }
        } else {
            break; // no more children (ECHILD) or an error
        }
    }
}

bool ProcessManager::isRunning(const std::string &iface) const {
    return m_byIface.find(iface) != m_byIface.end();
}
