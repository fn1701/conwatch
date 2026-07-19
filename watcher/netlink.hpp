#pragma once

#include <functional>
#include <linux/rtnetlink.h>
#include <string>

// Thin wrapper around a NETLINK_ROUTE socket subscribed to RTMGRP_LINK.
// Reports operational up/down transitions per interface, including an
// initial dump of already-up interfaces at startup.
class NetlinkWatcher
{
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

    int fd() const;

    // Processes all currently-available messages on the socket
    // (dump replies and/or live notifications), invoking the
    // callbacks for each. Call when poll() reports POLLIN on fd().
    void processPendingMessages(const LinkChangeCallback &onChange, const LinkRemovedCallback &onRemoved);

private:
    void requestDump();
    void handleMessage(const void *data, size_t len, const LinkChangeCallback &onChange, const LinkRemovedCallback &onRemoved);
    static std::string extractIfName(const struct nlmsghdr *nlh, const struct ifinfomsg *ifi);

    // "Operationally up with carrier": administratively enabled,
    // kernel-reported running, and driver-reported link/carrier
    // present. Distinguishes an admin-up ethernet port with an
    // unplugged cable from one actually usable for pinging.
    static bool isOperationallyUp(unsigned int flags);

    int m_fd = -1;
};
