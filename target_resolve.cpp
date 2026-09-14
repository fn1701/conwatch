#include "target_resolve.hpp"

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>

namespace
{

// Resolves one target string (literal IPv4/IPv6 address, or hostname) into
// zero, one, or two addresses to ping.
class TargetResolver
{
public:
    explicit TargetResolver(const std::string &target)
        : m_target(target)
    {
    }

    ResolvedTargets resolve()
    {
        if (tryLiteral())
            return m_result;
        resolveViaDns();
        return m_result;
    }

private:
    bool tryLiteral()
    {
        struct in_addr v4addr;
        if (inet_pton(AF_INET, m_target.c_str(), &v4addr) == 1) {
            m_result.v4 = m_target;
            return true;
        }

        struct in6_addr v6addr;
        if (inet_pton(AF_INET6, m_target.c_str(), &v6addr) == 1) {
            m_result.v6 = m_target;
            return true;
        }

        return false;
    }

    void resolveViaDns()
    {
        struct addrinfo hints {
        };
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_RAW;

        struct addrinfo *addrs = nullptr;
        int rc = getaddrinfo(m_target.c_str(), nullptr, &hints, &addrs);
        if (rc != 0) {
            fprintf(stderr, "resolveTargets: getaddrinfo(%s) failed: %s\n", m_target.c_str(), gai_strerror(rc));
            return;
        }

        collectAddresses(addrs);
        freeaddrinfo(addrs);
    }

    void collectAddresses(struct addrinfo *addrs)
    {
        char buf[INET6_ADDRSTRLEN];
        for (struct addrinfo *ai = addrs; ai != nullptr; ai = ai->ai_next) {
            if (ai->ai_family == AF_INET && !m_result.v4) {
                auto *sin = reinterpret_cast<struct sockaddr_in *>(ai->ai_addr);
                if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
                    m_result.v4 = std::string(buf);
                }
            } else if (ai->ai_family == AF_INET6 && !m_result.v6) {
                auto *sin6 = reinterpret_cast<struct sockaddr_in6 *>(ai->ai_addr);
                if (inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf))) {
                    m_result.v6 = std::string(buf);
                }
            }
            if (m_result.v4 && m_result.v6)
                break;
        }
    }

    std::string m_target;
    ResolvedTargets m_result;
};

} // namespace

// Free function kept as the public API contract declared in
// target_resolve.hpp; the real implementation is TargetResolver above.
ResolvedTargets resolveTargets(const std::string &target)
{
    return TargetResolver(target).resolve();
}
