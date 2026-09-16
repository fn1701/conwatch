#include "watcher.hpp"

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
