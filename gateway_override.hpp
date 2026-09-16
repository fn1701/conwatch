#pragma once

#include <cstdint>
#include <string>

// GatewayOverride: parses the optional gateway_ip_override4/6 config value
// into how PingMonitor should source a protocol's blue-check gateway.
//
//   unset/empty -> Auto:     resolve the kernel default route, as before
//   a literal IP -> Fixed:   pin the gateway to check, skip route lookup
//   "none"       -> Disabled: skip the gateway check entirely (severity
//                    goes straight from yellow to red at the loss
//                    threshold, never blue)
//
// Kept as its own class/file (rather than inline in PingMonitor) since it is
// self-contained parsing logic with no dependency on PingMonitor's instance
// state, mirroring the existing gateway_resolve.hpp/target_resolve.hpp split.
// Uses std::string (not QString) so it can be unit tested without linking
// Qt, same as those two.
class GatewayOverride
{
public:
    enum class Mode : std::uint8_t {
        Auto,
        Fixed,
        Disabled
    };

    explicit GatewayOverride(const std::string &configValue);

    [[nodiscard]] Mode mode() const;
    [[nodiscard]] const std::string &fixedIp() const;

private:
    static constexpr auto DisabledSentinel = "none";

    Mode m_mode = Mode::Auto;
    std::string m_fixedIp;
};
