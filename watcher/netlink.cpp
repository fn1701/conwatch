#include "netlink.hpp"

#include <cstdio>
#include <cstring>
#include <linux/if.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// Determine "operationally up with carrier": administratively enabled,
// kernel-reported running, and driver-reported link/carrier present.
// This is what distinguishes an admin-up ethernet port with an
// unplugged cable (or a WiFi radio on but not associated) from an
// interface actually usable for pinging.
bool isOperationallyUp(unsigned int flags) {
    return (flags & IFF_UP) && (flags & IFF_RUNNING) && (flags & IFF_LOWER_UP);
}

} // namespace

NetlinkWatcher::NetlinkWatcher() = default;

NetlinkWatcher::~NetlinkWatcher() {
    if (m_fd >= 0) close(m_fd);
}

bool NetlinkWatcher::open() {
    m_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (m_fd < 0) {
        perror("socket(NETLINK_ROUTE)");
        return false;
    }

    struct sockaddr_nl addr {};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK;

    if (bind(m_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        perror("bind(NETLINK_ROUTE)");
        close(m_fd);
        m_fd = -1;
        return false;
    }

    requestDump();
    return true;
}

void NetlinkWatcher::requestDump() {
    struct {
        struct nlmsghdr hdr;
        struct ifinfomsg ifi;
    } req {};

    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifi));
    req.hdr.nlmsg_type = RTM_GETLINK;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.hdr.nlmsg_seq = 1;
    req.ifi.ifi_family = AF_UNSPEC;

    struct sockaddr_nl dst {};
    dst.nl_family = AF_NETLINK;

    if (sendto(m_fd, &req, req.hdr.nlmsg_len, 0,
               reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst)) < 0) {
        perror("sendto(RTM_GETLINK dump)");
    }
}

void NetlinkWatcher::handleMessage(const void *data, size_t len,
                                    const LinkChangeCallback &onChange,
                                    const LinkRemovedCallback &onRemoved) {
    for (const struct nlmsghdr *nlh = static_cast<const struct nlmsghdr *>(data);
         NLMSG_OK(nlh, len);
         nlh = NLMSG_NEXT(nlh, len)) {

        if (nlh->nlmsg_type == NLMSG_DONE || nlh->nlmsg_type == NLMSG_ERROR) {
            continue;
        }
        if (nlh->nlmsg_type != RTM_NEWLINK && nlh->nlmsg_type != RTM_DELLINK) {
            continue;
        }

        auto *ifi = static_cast<const struct ifinfomsg *>(NLMSG_DATA(nlh));

        std::string name;
        int attrLen = IFLA_PAYLOAD(nlh);
        for (const struct rtattr *rta = IFLA_RTA(ifi);
             RTA_OK(rta, attrLen);
             rta = RTA_NEXT(rta, attrLen)) {
            if (rta->rta_type == IFLA_IFNAME) {
                name.assign(static_cast<const char *>(RTA_DATA(rta)),
                            RTA_PAYLOAD(rta) - 1); // drop trailing NUL
                break;
            }
        }

        if (nlh->nlmsg_type == RTM_DELLINK) {
            onRemoved(ifi->ifi_index);
            continue;
        }

        if (name.empty()) continue; // shouldn't happen, but don't crash on it
        onChange(ifi->ifi_index, name, isOperationallyUp(ifi->ifi_flags));
    }
}

void NetlinkWatcher::processPendingMessages(const LinkChangeCallback &onChange,
                                              const LinkRemovedCallback &onRemoved) {
    char buf[8192];
    while (true) {
        ssize_t n = recv(m_fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n < 0) break; // EAGAIN/EWOULDBLOCK: no more messages right now
        if (n == 0) break;
        handleMessage(buf, static_cast<size_t>(n), onChange, onRemoved);
    }
}
