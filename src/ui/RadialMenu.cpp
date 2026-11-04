#include "ui/RadialMenu.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <algorithm>
#include <cmath>

#include "ui/Theme.h"

namespace gx {

RadialMenu::RadialMenu(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    const int side = int(kOuter * 2 + 24);
    resize(side, side);
    hide();
}

void RadialMenu::setItems(const QList<Item>& items) {
    m_items = items;
    m_hi = -1;
    update();
}

void RadialMenu::popup(const QPointF& center) {
    QPointF tl = center - QPointF(width() / 2.0, height() / 2.0);
    if (QWidget* p = parentWidget()) {  // stays inside the canvas
        tl.setX(std::clamp(tl.x(), 0.0, std::max(0.0, double(p->width() - width()))));
        tl.setY(std::clamp(tl.y(), 0.0, std::max(0.0, double(p->height() - height()))));
    }
    move(tl.toPoint());
    show();
    raise();
    setFocus();
    pointAt(mapFromGlobal(QCursor::pos()));
}

QPointF RadialMenu::itemCenter(int i) const {
    const int n = std::max(1, int(m_items.size()));
    const double a = -M_PI / 2 + 2 * M_PI * i / n;  // the first item on top, clockwise
    const double r = (kOuter + kInner) / 2;
    return QPointF(width() / 2.0 + std::cos(a) * r, height() / 2.0 + std::sin(a) * r);
}

int RadialMenu::itemAt(const QPointF& p) const {
    const QPointF c(width() / 2.0, height() / 2.0);
    const QPointF d = p - c;
    const double dist = std::hypot(d.x(), d.y());
    if (dist < kInner || m_items.isEmpty()) return -1;
    if (dist > kOuter + 60) return -1;  // far away: nothing
    const int n = int(m_items.size());
    double a = std::atan2(d.y(), d.x()) + M_PI / 2 + M_PI / n;  // 0 = the top item's left edge
    a = std::fmod(a + 4 * M_PI, 2 * M_PI);
    return std::clamp(int(a / (2 * M_PI / n)), 0, n - 1);
}

void RadialMenu::pointAt(const QPointF& p) {
    const int h = itemAt(p);
    if (h != m_hi) {
        m_hi = h;
        update();
    }
}

void RadialMenu::choose(int i) {
    if (i < 0 || i >= m_items.size()) {
        close();
        return;
    }
    auto run = m_items[i].run;
    close();
    if (run) QTimer::singleShot(0, run);
}

void RadialMenu::chooseHighlighted() { choose(m_hi); }

void RadialMenu::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const ThemeColors& c = Theme::colors();
    const QPointF ctr(width() / 2.0, height() / 2.0);
    const int n = int(m_items.size());
    // the ring
    QPainterPath ring;
    ring.addEllipse(ctr, kOuter, kOuter);
    QPainterPath hole;
    hole.addEllipse(ctr, kInner, kInner);
    ring = ring.subtracted(hole);
    QColor bg = c.window;
    bg.setAlpha(238);
    p.setPen(QPen(c.border, 1.2));
    p.setBrush(bg);
    p.drawPath(ring);
    QColor mid = c.panel;
    mid.setAlpha(245);
    p.setBrush(mid);
    p.drawEllipse(ctr, kInner, kInner);
    if (n == 0) return;
    const double step = 360.0 / n;
    // the highlighted slice
    if (m_hi >= 0) {
        QPainterPath slice;
        const QRectF outer(ctr - QPointF(kOuter, kOuter), QSizeF(2 * kOuter, 2 * kOuter));
        const double start = 90 - (m_hi * step) + step / 2;  // Qt angles: counter-clockwise from 3 o'clock
        slice.moveTo(ctr);
        slice.arcTo(outer, start, -step);
        slice.closeSubpath();
        slice = slice.intersected(ring);
        QColor hi = c.accent;
        hi.setAlpha(200);
        p.setPen(Qt::NoPen);
        p.setBrush(hi);
        p.drawPath(slice);
    }
    // dividers
    p.setPen(QPen(c.border, 1));
    for (int i = 0; i < n; ++i) {
        const double a = -M_PI / 2 + 2 * M_PI * (i + 0.5) / n;
        p.drawLine(ctr + QPointF(std::cos(a), std::sin(a)) * kInner, ctr + QPointF(std::cos(a), std::sin(a)) * kOuter);
    }
    // icons and names
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.5));
    p.setFont(f);
    const QFontMetrics fm(f);
    const double cellW = std::max(2 * M_PI * (kOuter + kInner) / 2 / n + 10, 60.0);
    for (int i = 0; i < n; ++i) {
        const QPointF ic = itemCenter(i);
        const bool on = i == m_hi;
        const QPixmap pm = m_items[i].icon.pixmap(QSize(24, 24), devicePixelRatioF());
        p.drawPixmap(QPointF(ic.x() - 12, ic.y() - 20), pm);
        p.setPen(on ? c.accentText : c.text);
        const QString label = fm.elidedText(m_items[i].label, Qt::ElideRight, int(std::min(cellW, 76.0)));
        p.drawText(QRectF(ic.x() - 40, ic.y() + 5, 80, fm.height() + 2), Qt::AlignHCenter | Qt::AlignTop, label);
    }
    // the middle: the name of the item pointed at
    p.setPen(m_hi >= 0 ? c.text : c.dim);
    const QRectF in(ctr.x() - kInner + 6, ctr.y() - kInner + 8, 2 * kInner - 12, 2 * kInner - 16);
    p.drawText(in, Qt::AlignCenter | Qt::TextWordWrap, m_hi >= 0 ? m_items[m_hi].label : QStringLiteral("Esc"));
}

void RadialMenu::mouseMoveEvent(QMouseEvent* e) { pointAt(e->position()); }

void RadialMenu::mousePressEvent(QMouseEvent* e) {
    pointAt(e->position());
    e->accept();
}

void RadialMenu::mouseReleaseEvent(QMouseEvent* e) {
    const int i = itemAt(e->position());
    if (i >= 0) choose(i);
    else close();
}

void RadialMenu::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

void RadialMenu::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    Q_EMIT closed();
}

}  // namespace gx
