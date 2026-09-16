#include "gateway_override.hpp"

#include <algorithm>
#include <cctype>

namespace
{
std::string toLower(const std::string &s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return out;
}
} // namespace

GatewayOverride::GatewayOverride(const std::string &configValue)
{
    if (configValue.empty()) {
        m_mode = Mode::Auto;
    } else if (toLower(configValue) == DisabledSentinel) {
        m_mode = Mode::Disabled;
    } else {
        m_mode = Mode::Fixed;
        m_fixedIp = configValue;
    }
}

GatewayOverride::Mode GatewayOverride::mode() const
{
    return m_mode;
}

const std::string &GatewayOverride::fixedIp() const
{
    return m_fixedIp;
}
