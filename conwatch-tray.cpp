// conwatch-tray entry point. See ping_monitor.hpp for the monitor's design.
//
// Usage: conwatch-tray <interface> <target> <target6> [label]

#include "ping_monitor.hpp"

#include <QApplication>
#include <QSystemTrayIcon>

#include <cstdio>

namespace
{

// Free function since it runs before QApplication exists to own it --
// there is no PingMonitor/QObject instance yet to attach argv validation to.
bool argsValid(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <interface> <target> [target6] [label]\n", argv[0]);
        return false;
    }
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        fprintf(stderr, "No system tray available\n");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    if (!argsValid(argc, argv))
        return 1;

    QString iface = argv[1];
    QString target = argv[2];
    QString target6 = argc >= 4 ? argv[3] : QString();
    QString label = argc >= 5 ? argv[4] : iface;

    PingMonitor monitor(iface, target, target6, label);
    return app.exec();
}
