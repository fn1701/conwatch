#include "watcher.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <poll.h>
#include <sys/signalfd.h>
#include <unistd.h>

Watcher::Watcher(Config cfg, std::string configPath)
    : m_cfg(std::move(cfg))
    , m_configPath(std::move(configPath))
{
}

int Watcher::run()
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

bool Watcher::openSignalFd()
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

void Watcher::poll()
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

void Watcher::handleSignals()
{
    struct signalfd_siginfo si;
    while (read(m_sigFd, &si, sizeof(si)) == sizeof(si)) {
        if (si.ssi_signo == SIGCHLD)
            m_processes.reapExited();
        else
            m_shouldExit = true;
    }
}
