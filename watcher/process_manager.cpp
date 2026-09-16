#include "process_manager.hpp"

#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

void ProcessManager::start(int ifindex,
                           const std::string &iface,
                           const std::string &target,
                           const std::string &target6,
                           const std::string &label,
                           const std::string &gatewayIpOverride4,
                           const std::string &gatewayIpOverride6)
{
    if (isRunning(ifindex)) {
        return;
    }

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

        execlp("/usr/local/bin/conwatch-tray",
               "conwatch-tray",
               iface.c_str(),
               target.c_str(),
               target6.c_str(),
               label.c_str(),
               gatewayIpOverride4.c_str(),
               gatewayIpOverride6.c_str(),
               static_cast<char *>(nullptr));
        _exit(127); // exec failed
    }

    fprintf(stderr, "conwatch: started conwatch-tray for %s (pid %d)\n", iface.c_str(), pid);
    m_byIfindex[ifindex] = {pid, iface};
    m_ifindexByPid[pid] = ifindex;
}

void ProcessManager::stop(int ifindex)
{
    auto it = m_byIfindex.find(ifindex);
    if (it == m_byIfindex.end()) {
        return;
    }

    pid_t pid = it->second.pid;
    kill(pid, SIGTERM);
    fprintf(stderr, "conwatch: stopping conwatch-tray for %s (pid %d)\n", it->second.iface.c_str(), pid);

    m_ifindexByPid.erase(pid);
    m_byIfindex.erase(it);
}

void ProcessManager::reapExited()
{
    int status = 0;
    pid_t pid = 0;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        auto it = m_ifindexByPid.find(pid);
        if (it != m_ifindexByPid.end()) {
            m_byIfindex.erase(it->second);
            m_ifindexByPid.erase(it);
        }
    }
}

void ProcessManager::stopAll()
{
    for (const auto &[pid, ifindex] : m_ifindexByPid) {
        kill(pid, SIGTERM);
    }

    // Blocking wait, bounded by iteration count rather than wall clock
    // to avoid pulling in a timer for a shutdown path executed once.
    for (int i = 0; i < 200 && !m_ifindexByPid.empty(); ++i) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid > 0) {
            auto it = m_ifindexByPid.find(pid);
            if (it != m_ifindexByPid.end()) {
                m_byIfindex.erase(it->second);
                m_ifindexByPid.erase(it);
            }
        } else {
            break; // no more children (ECHILD) or an error
        }
    }
}

bool ProcessManager::isRunning(int ifindex) const
{
    return m_byIfindex.find(ifindex) != m_byIfindex.end();
}
