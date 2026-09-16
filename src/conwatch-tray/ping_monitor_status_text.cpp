#include "ping_monitor.hpp"

#include <QAction>
#include <QSystemTrayIcon>

void PingMonitor::renderTray()
{
    ProtoState v4State = stateOf(m_v4);
    ProtoState v6State = stateOf(m_v6);
    Severity v4Severity = severityOf(m_v4);
    Severity v6Severity = severityOf(m_v6);

    QString glyph = glyphFor(v4State, v6State, v4Severity, v6Severity);
    Severity color = worstSeverityOf(v4State, v6State, v4Severity, v6Severity);
    setIcon(color, glyph);

    applyStatus(statusTextFor(v4State, v6State));
}

// Reflects which protocol(s) are CURRENTLY succeeding (healthy or blue), not
// just configured -- shrinks/grows as protocols fail/recover.
QString PingMonitor::glyphFor(ProtoState v4State, ProtoState v6State, Severity v4Severity, Severity v6Severity)
{
    QString glyph;
    if (v4State == ProtoState::Healthy || v4Severity == Severity::Blue)
        glyph += "4";
    if (v6State == ProtoState::Healthy || v6Severity == Severity::Blue) {
        if (!glyph.isEmpty())
            glyph += "/";
        glyph += "6";
    }
    return glyph;
}

// No worst-of/best-of ranking between protocols: each active protocol's
// severity is fully independent. Green/blue take priority display-wise over
// yellow only in the sense that a protocol reaching its target or gateway is
// good news on its own -- but red is reserved for total failure, so it's
// only shown when every active protocol has independently gone red (target
// down AND gateway unreachable on all of them).
PingMonitor::Severity PingMonitor::worstSeverityOf(ProtoState v4State, ProtoState v6State, Severity v4Severity, Severity v6Severity)
{
    bool anyHealthy = v4State == ProtoState::Healthy || v6State == ProtoState::Healthy;
    bool anyBlue = v4Severity == Severity::Blue || v6Severity == Severity::Blue;
    bool anyYellow = (v4State == ProtoState::Failing && v4Severity == Severity::Yellow) || (v6State == ProtoState::Failing && v6Severity == Severity::Yellow);

    if (anyHealthy)
        return Severity::Green;
    if (anyBlue)
        return Severity::Blue;
    if (anyYellow)
        return Severity::Yellow;
    if (v4State == ProtoState::Failing || v6State == ProtoState::Failing)
        return Severity::Red;
    return Severity::Green;
}

QString PingMonitor::protoStatusText(const QString &tag, ProtoState state, const ProtoTrack &t)
{
    if (state == ProtoState::Healthy)
        return QString("%1 connected").arg(tag);
    QString text = QString("%1: %2 consecutive losses").arg(tag).arg(t.failStreak);
    if (t.gatewayReachable)
        text += " (gateway reachable)";
    return text;
}

QString PingMonitor::statusTextFor(ProtoState v4State, ProtoState v6State) const
{
    QStringList parts;
    if (v4State != ProtoState::NoLocalAddress)
        parts << protoStatusText("v4", v4State, m_v4);
    if (v6State != ProtoState::NoLocalAddress)
        parts << protoStatusText("v6", v6State, m_v6);

    if (parts.isEmpty())
        return QString("%1: no target resolved").arg(m_label);
    return QString("%1: %2").arg(m_label, parts.join(", "));
}

void PingMonitor::applyStatus(const QString &status)
{
    m_statusAction->setText(status);
    // Only touch the tooltip when its text actually changes -- calling
    // setToolTip() every tick (even with identical text) makes some tray
    // hosts (e.g. Plasma) tear down and re-show an already-open tooltip,
    // which looks like it vanishing mid-hover.
    if (status != m_currentTooltip) {
        m_currentTooltip = status;
        m_tray->setToolTip(status);
    }
}
