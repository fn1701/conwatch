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

NetlinkWatcher::LinkChangeCallback Watcher::onChangeCallback()
{
    return [this](int ifindex, const std::string &name, bool isUp) {
        onChange(ifindex, name, isUp);
    };
}

NetlinkWatcher::LinkRemovedCallback Watcher::onRemovedCallback()
{
    return [this](int ifindex) {
        onRemoved(ifindex);
    };
}

void Watcher::onChange(int ifindex, const std::string &name, bool isUp)
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

void Watcher::startIfEligible(int ifindex, const std::string &name)
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

void Watcher::onRemoved(int ifindex)
{
    auto it = m_ifaces.find(ifindex);
    if (it == m_ifaces.end())
        return;
    if (it->second.wasUp)
        m_processes.stop(ifindex);
    m_ifaces.erase(it);
}
