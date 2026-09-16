#include "ping_monitor.hpp"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QSystemTrayIcon>

void PingMonitor::paintIconBase(QPainter &p, int size, const QColor &qc)
{
    p.setBrush(qc);
    p.setPen(QColor(0, 0, 0, 60));
    p.drawEllipse(size / 16, size / 16, size - size / 8, size - size / 8);

    QFont ifaceFont = p.font();
    ifaceFont.setBold(true);
    ifaceFont.setPixelSize(size * 26 / 64);
    p.setFont(ifaceFont);
    p.setPen(Qt::black);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, m_ifaceTag);
}

void PingMonitor::paintIconGlyph(QPainter &p, int size, const QString &glyph)
{
    if (glyph.isEmpty())
        return;

    QFont protoFont = p.font();
    protoFont.setBold(true);
    protoFont.setPixelSize(size * 22 / 64);
    p.setFont(protoFont);
    p.setPen(Qt::white);
    QFontMetrics protoMetrics(protoFont);
    QRect protoInk = protoMetrics.tightBoundingRect(glyph);
    // Shift so the glyph's actual ink (not its font-metrics box) is
    // flush against the top-right pixel corner of the canvas.
    int protoX = size - protoInk.width() - protoInk.left();
    int protoY = -protoInk.top();
    p.drawText(protoX, protoY, glyph);
}

QPixmap PingMonitor::renderIcon(int size, const QColor &qc, const QString &glyph)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    paintIconBase(p, size, qc);
    paintIconGlyph(p, size, glyph);
    p.end();

    return pix;
}

QColor PingMonitor::qColorFor(Severity worst)
{
    switch (worst) {
    case Severity::Green:
        return QColor(0x2e, 0xcc, 0x71);
    case Severity::Blue:
        return QColor(0x34, 0x98, 0xdb);
    case Severity::Yellow:
        return QColor(0xf1, 0xc4, 0x0f);
    case Severity::Red:
        return QColor(0xe7, 0x4c, 0x3c);
    }
    return QColor();
}

// Icons only vary by (severity, glyph) -- a small fixed set (3 severities x
// a handful of glyph strings) for a given process's fixed m_ifaceTag.
// Caching by that key means QPainter only runs once per distinct combo ever
// seen, not once per tick/transition -- a flapping connection re-uses
// cached QIcons instead of repainting 7 pixmap sizes on every bounce.
QIcon &PingMonitor::iconFor(Severity worst, const QString &glyph)
{
    auto key = std::make_pair(static_cast<int>(worst), glyph);
    auto it = m_iconCache.find(key);
    if (it != m_iconCache.end())
        return it->second;

    QColor qc = qColorFor(worst);

    // Render at several standard tray/panel sizes so the desktop shell can
    // pick the closest match instead of scaling one fixed bitmap.
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64, 128}) {
        icon.addPixmap(renderIcon(size, qc, glyph));
    }

    return m_iconCache.emplace(key, std::move(icon)).first->second;
}

void PingMonitor::setIcon(Severity worst, const QString &glyph)
{
    if (worst == m_currentSeverity && glyph == m_currentGlyph)
        return;
    m_currentSeverity = worst;
    m_currentGlyph = glyph;
    m_tray->setIcon(iconFor(worst, glyph));
}
