#include "target_resolve.hpp"

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>

ResolvedTargets resolveTargets(const std::string &target)
{
    ResolvedTargets result;

    struct in_addr v4addr;
    if (inet_pton(AF_INET, target.c_str(), &v4addr) == 1) {
        result.v4 = target;
        return result;
    }

    struct in6_addr v6addr;
    if (inet_pton(AF_INET6, target.c_str(), &v6addr) == 1) {
        result.v6 = target;
        return result;
    }

    struct addrinfo hints {
    };
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_RAW;

    struct addrinfo *addrs = nullptr;
    int rc = getaddrinfo(target.c_str(), nullptr, &hints, &addrs);
    if (rc != 0) {
        fprintf(stderr, "resolveTargets: getaddrinfo(%s) failed: %s\n", target.c_str(), gai_strerror(rc));
        return result;
    }

    char buf[INET6_ADDRSTRLEN];
    for (struct addrinfo *ai = addrs; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET && !result.v4) {
            auto *sin = reinterpret_cast<struct sockaddr_in *>(ai->ai_addr);
            if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
                result.v4 = std::string(buf);
            }
        } else if (ai->ai_family == AF_INET6 && !result.v6) {
            auto *sin6 = reinterpret_cast<struct sockaddr_in6 *>(ai->ai_addr);
            if (inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf))) {
                result.v6 = std::string(buf);
            }
        }
        if (result.v4 && result.v6)
            break;
    }

    freeaddrinfo(addrs);
    return result;
}
