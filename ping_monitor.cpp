#include "ping_monitor.hpp"

#include "gateway_resolve.hpp"
#include "target_resolve.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTimer>

#include <unistd.h>

PingMonitor::PingMonitor(QString iface, QString target, QString target6, QString label)
    : m_iface(std::move(iface))
    , m_label(std::move(label))
{
    m_pid = static_cast<uint16_t>(getpid() & 0xffff);
    m_ifaceTag = shortIfaceTag(m_iface);

    resolveInitialTargets(target, target6);
    setupTray();

    if (m_v4.configured)
        openSocket4();
    if (m_v6.configured)
        openSocket6();

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PingMonitor::tick);
    timer->start(1000);
}

void PingMonitor::resolveInitialTargets(const QString &target, const QString &target6)
{
    ResolvedTargets resolved = resolveTargets(target.toStdString());
    if (resolved.v4) {
        m_v4.targetIp = QString::fromStdString(*resolved.v4);
        m_v4.configured = true;
    }
    if (resolved.v6) {
        m_v6.targetIp = QString::fromStdString(*resolved.v6);
        m_v6.configured = true;
    }

    // target6, if provided, is merged in additively -- it never overrides a
    // v6 target already resolved from `target` above (a literal-v6 or
    // hostname-with-AAAA `target` takes precedence).
    if (!m_v6.configured && !target6.isEmpty()) {
        ResolvedTargets resolved6 = resolveTargets(target6.toStdString());
        if (resolved6.v6) {
            m_v6.targetIp = QString::fromStdString(*resolved6.v6);
            m_v6.configured = true;
        }
    }
}

void PingMonitor::setupTray()
{
    m_tray = new QSystemTrayIcon(this);
    auto *menu = new QMenu();
    m_statusAction = menu->addAction(QString("%1: starting...").arg(m_label));
    m_statusAction->setEnabled(false);
    menu->addSeparator();
    menu->addAction("Quit", qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(m_label);

    renderTray();
    m_tray->show();
}

// Short (<=3 char) tag for an interface name, for display on a small icon:
// leading letters (up to 2) plus the trailing digit run, e.g. "wlan0" ->
// "wl0", "enp196s0f4u1u4" -> "en4".
QString PingMonitor::shortIfaceTag(const QString &iface)
{
    QString letters;
    for (QChar c : iface) {
        if (!c.isLetter())
            break;
        letters += c;
    }
    letters = letters.left(2);

    QString trailingDigits;
    int i = iface.size() - 1;
    while (i >= 0 && iface[i].isDigit()) {
        trailingDigits.prepend(iface[i]);
        --i;
    }

    return letters + trailingDigits.right(1);
}
