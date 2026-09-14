#include "config.hpp"

namespace
{

// Resolves per-interface effective target/label/target6, applying any
// per-interface override over the config defaults.
class ConfigResolver
{
public:
    static std::string target(const Config &cfg, const std::string &iface)
    {
        auto it = cfg.interfaces.find(iface);
        if (it != cfg.interfaces.end() && it->second.target)
            return *it->second.target;
        return cfg.defaultTarget;
    }

    static std::string label(const Config &cfg, const std::string &iface)
    {
        auto it = cfg.interfaces.find(iface);
        if (it != cfg.interfaces.end() && !it->second.label.empty())
            return it->second.label;
        return iface;
    }

    static std::string target6(const Config &cfg, const std::string &iface)
    {
        auto it = cfg.interfaces.find(iface);
        if (it != cfg.interfaces.end() && it->second.target6)
            return *it->second.target6;
        return cfg.defaultTarget6.value_or("");
    }
};

} // namespace

// Free function kept as the public API contract declared in
// config.hpp; the real implementation is ConfigResolver above.
std::string resolveTarget(const Config &cfg, const std::string &iface)
{
    return ConfigResolver::target(cfg, iface);
}

// Free function kept as the public API contract declared in
// config.hpp; the real implementation is ConfigResolver above.
std::string resolveLabel(const Config &cfg, const std::string &iface)
{
    return ConfigResolver::label(cfg, iface);
}

// Free function kept as the public API contract declared in
// config.hpp; the real implementation is ConfigResolver above.
std::string resolveTarget6(const Config &cfg, const std::string &iface)
{
    return ConfigResolver::target6(cfg, iface);
}
