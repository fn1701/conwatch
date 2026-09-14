#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <fnmatch.h>
#include <yaml-cpp/yaml.h>

namespace
{

// Loads a Config from YAML and answers include/exclude eligibility
// questions against it.
class ConfigLoader
{
public:
    static Config load(const std::string &path)
    {
        Config cfg;
        YAML::Node root = YAML::LoadFile(path);

        applyScalars(root, cfg);
        applyPatternLists(root, cfg);
        applyInterfaceOverrides(root, cfg);
        validatePrecedence(cfg, path);

        return cfg;
    }

    static bool isEligible(const Config &cfg, const std::string &name)
    {
        bool includeReal = !cfg.include.empty() && !isNoopPattern(cfg.include);
        if (includeReal)
            return matchesAny(cfg.include, name);

        if (!cfg.exclude.empty())
            return !matchesAny(cfg.exclude, name);

        return true;
    }

private:
    static void applyScalars(const YAML::Node &root, Config &cfg)
    {
        if (root["default_target"])
            cfg.defaultTarget = root["default_target"].as<std::string>();
        if (root["default_target6"])
            cfg.defaultTarget6 = root["default_target6"].as<std::string>();
    }

    static void applyPatternLists(const YAML::Node &root, Config &cfg)
    {
        if (root["include"]) {
            for (auto n : root["include"])
                cfg.include.push_back(n.as<std::string>());
        }
        if (root["exclude"]) {
            for (auto n : root["exclude"])
                cfg.exclude.push_back(n.as<std::string>());
        }
    }

    static void applyInterfaceOverrides(const YAML::Node &root, Config &cfg)
    {
        if (!root["interfaces"])
            return;

        for (auto it : root["interfaces"])
            cfg.interfaces[it.first.as<std::string>()] = parseOverride(it);
    }

    static InterfaceOverride parseOverride(const YAML::const_iterator::value_type &entry)
    {
        std::string ifaceName = entry.first.as<std::string>();
        YAML::Node node = entry.second;

        InterfaceOverride ov;
        ov.label = node["label"] ? node["label"].as<std::string>() : ifaceName;
        if (node["target"])
            ov.target = node["target"].as<std::string>();
        if (node["target6"])
            ov.target6 = node["target6"].as<std::string>();
        return ov;
    }

    static void validatePrecedence(const Config &cfg, const std::string &path)
    {
        bool includeReal = !cfg.include.empty() && !isNoopPattern(cfg.include);
        bool excludeReal = !cfg.exclude.empty() && !isNoopPattern(cfg.exclude);
        if (!includeReal || !excludeReal)
            return;

        fprintf(stderr,
                "conwatch: config error: both include and exclude are "
                "populated with real patterns in %s. Exactly one must be used "
                "-- leave the other empty ([]) or set it to [\"*\"].\n",
                path.c_str());
        exit(1);
    }

    static bool isNoopPattern(const std::vector<std::string> &patterns)
    {
        return patterns.size() == 1 && patterns[0] == "*";
    }

    static bool matchesAny(const std::vector<std::string> &patterns, const std::string &name)
    {
        for (const auto &p : patterns) {
            if (fnmatch(p.c_str(), name.c_str(), 0) == 0)
                return true;
        }
        return false;
    }
};

} // namespace

// Free function kept as the public API contract declared in
// config.hpp; the real implementation is ConfigLoader above.
Config loadConfig(const std::string &path)
{
    return ConfigLoader::load(path);
}

// Free function kept as the public API contract declared in
// config.hpp; the real implementation is ConfigLoader above.
bool isEligible(const Config &cfg, const std::string &name)
{
    return ConfigLoader::isEligible(cfg, name);
}
