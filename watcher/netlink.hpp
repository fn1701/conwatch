#pragma once

#include <functional>
#include <string>

// Thin wrapper around a NETLINK_ROUTE socket subscribed to RTMGRP_LINK.
// Reports operational up/down transitions per interface, including an
// initial dump of already-up interfaces at startup.
class NetlinkWatcher {
public:
    // ifindex, name, isOperationallyUp
    using LinkChangeCallback = std::function<void(int, const std::string &, bool)>;
    // ifindex - interface removed entirely (RTM_DELLINK)
    using LinkRemovedCallback = std::function<void(int)>;

    NetlinkWatcher();
    ~NetlinkWatcher();

    NetlinkWatcher(const NetlinkWatcher &) = delete;
    NetlinkWatcher &operator=(const NetlinkWatcher &) = delete;

    // Opens the netlink socket, binds to RTMGRP_LINK, and requests an
    // initial RTM_GETLINK dump. Returns false on failure.
    bool open();

    int fd() const { return m_fd; }

    // Processes all currently-available messages on the socket
    // (dump replies and/or live notifications), invoking the
    // callbacks for each. Call when poll() reports POLLIN on fd().
    void processPendingMessages(const LinkChangeCallback &onChange,
                                 const LinkRemovedCallback &onRemoved);

private:
    void requestDump();
    void handleMessage(const void *data, size_t len,
                        const LinkChangeCallback &onChange,
                        const LinkRemovedCallback &onRemoved);

    int m_fd = -1;
};
