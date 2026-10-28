#include "tools/NavTools.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

#include "app/App.h"
#include "app/BrushLibrary.h"
#include "app/I18n.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/Pixel.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"

namespace gx {

// ---------------------------------------------------------------- hand
void HandTool::press(const PointerEvent& e) {
    m_last = e.widgetPos;
    m_drag = true;
    if (e.view) e.view->setCursor(Qt::ClosedHandCursor);
}
void HandTool::move(const PointerEvent& e) {
    if (!m_drag || !e.view) return;
    e.view->panBy(e.widgetPos - m_last);
    m_last = e.widgetPos;
}
void HandTool::release(const PointerEvent& e) {
    m_drag = false;
    if (e.view) e.view->refreshCursor();
}

// ---------------------------------------------------------------- zoom
void ZoomTool::press(const PointerEvent& e) {
    m_start = e.widgetPos;
    m_startZoom = e.view ? e.view->zoom() : 1;
    m_moved = false;
}
void ZoomTool::move(const PointerEvent& e) {
    if (!e.view) return;
    double dx = e.widgetPos.x() - m_start.x();
    if (std::fabs(dx) > 3) m_moved = true;
    if (m_moved) e.view->setZoom(m_startZoom * std::exp(dx * 0.01), m_start);
}
void ZoomTool::release(const PointerEvent& e) {
    if (!e.view || m_moved) return;
    if (e.mods & Qt::AltModifier) e.view->zoomBy(1 / 1.5, e.widgetPos);
    else e.view->zoomBy(1.5, e.widgetPos);
}

// ---------------------------------------------------------------- rotate
void RotateTool::press(const PointerEvent& e) {
    if (!e.view) return;
    QPointF c(e.view->width() / 2.0, e.view->height() / 2.0);
    QPointF d = e.widgetPos - c;
    m_startAngle = qRadiansToDegrees(std::atan2(d.y(), d.x()));
    m_startRot = e.view->rotation();
    m_drag = true;
}
void RotateTool::move(const PointerEvent& e) {
    if (!m_drag || !e.view) return;
    QPointF c(e.view->width() / 2.0, e.view->height() / 2.0);
    QPointF d = e.widgetPos - c;
    if (std::hypot(d.x(), d.y()) < 4) return;
    double a = qRadiansToDegrees(std::atan2(d.y(), d.x()));
    double r = m_startRot + (a - m_startAngle);
    if (e.mods & Qt::ShiftModifier) r = std::round(r / 15.0) * 15.0;
    e.view->setRotation(r);
}
void RotateTool::release(const PointerEvent&) { m_drag = false; }
void RotateTool::doubleClick(const PointerEvent& e) {
    if (e.view) e.view->setRotation(0);
}
void RotateTool::drawOverlay(QPainter& p, CanvasView* v) {
    if (!m_drag) return;
    QPointF c(v->width() / 2.0, v->height() / 2.0);
    p.save();
    p.setPen(QPen(QColor(255, 255, 255, 200), 1.5));
    p.drawEllipse(c, 40, 40);
    double a = qDegreesToRadians(v->rotation() - 90);
    p.setPen(QPen(Theme::colors().accent, 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c, c + QPointF(std::cos(a), std::sin(a)) * 40);
    p.setPen(Qt::white);
    p.drawText(QRectF(c.x() - 40, c.y() + 44, 80, 20), Qt::AlignCenter, QString::number(v->rotation(), 'f', 1) + QChar(0xB0));
    p.restore();
}

// ---------------------------------------------------------------- brush size
void BrushSizeTool::press(const PointerEvent& e) {
    m_start = e.widgetPos;
    m_center = e.widgetPos;
    m_startSize = app()->brushes()->current().size;
    m_drag = true;
}
void BrushSizeTool::move(const PointerEvent& e) {
    if (!m_drag) return;
    double dx = e.widgetPos.x() - m_start.x();
    BrushSettings s = app()->brushes()->current();
    s.size = std::clamp(m_startSize * std::exp(dx * 0.01), 0.5, 5000.0);
    app()->brushes()->update(app()->brushes()->currentIndex(), s);
    Q_EMIT overlayChanged();
}
void BrushSizeTool::drawOverlay(QPainter& p, CanvasView* v) {
    double r = app()->brushes()->current().size * 0.5 * v->zoom();
    QPointF c = m_drag ? m_center : v->toWidget(v->cursorCanvasPos());
    p.save();
    p.setPen(QPen(QColor(0, 0, 0, 160), 1.6));
    p.drawEllipse(c, r, r);
    p.setPen(QPen(Qt::white, 0.8));
    p.drawEllipse(c, r, r);
    p.setPen(Qt::white);
    p.drawText(QRectF(c.x() - 60, c.y() - 9, 120, 18), Qt::AlignCenter,
               QString::number(app()->brushes()->current().size, 'f', 1) + " px");
    p.restore();
}

// ---------------------------------------------------------------- eyedropper
QColor EyedropperTool::sample(CanvasView* v, const QPointF& pos) const {
    Document* d = v->doc();
    int r = average / 2;
    QRect rr(int(std::floor(pos.x())) - r, int(std::floor(pos.y())) - r, average, average);
    rr &= d->bounds();
    if (rr.isEmpty()) return QColor();
    std::vector<float> buf(size_t(rr.width()) * rr.height() * 4);
    if (sampleMerged) {
        Compositor::prepare(d->root(), rr);
        Compositor::composite(d->root(), rr, buf.data());
    } else {
        Layer* l = d->currentLayer();
        if (!l) return QColor();
        CompositeOptions o;
        o.only = l;
        Compositor::prepare(d->root(), rr);
        Compositor::composite(d->root(), rr, buf.data(), o);
    }
    double s[4] = {0, 0, 0, 0};
    size_t n = buf.size() / 4;
    for (size_t i = 0; i < n; ++i)
        for (int k = 0; k < 4; ++k) s[k] += buf[i * 4 + size_t(k)];
    float px[4] = {float(s[0] / n), float(s[1] / n), float(s[2] / n), float(s[3] / n)};
    if (px[3] < 1e-4f) return QColor();  // transparent: keep the color
    return QColor::fromRgbF(clamp01(px[0] / px[3]), clamp01(px[1] / px[3]), clamp01(px[2] / px[3]));
}

void EyedropperTool::press(const PointerEvent& e) {
    if (!e.view) return;
    m_drag = true;
    m_background = (e.mods & Qt::ShiftModifier) != 0;
    m_prev = m_background ? app()->background() : app()->foreground();
    move(e);
}

void EyedropperTool::move(const PointerEvent& e) {
    if (!m_drag || !e.view) return;
    m_pos = e.widgetPos;
    QColor c = sample(e.view, e.pos);
    if (!c.isValid()) return;
    m_now = c;
    if (m_background) app()->setBackground(c);
    else app()->setForeground(c);
    Q_EMIT overlayChanged();
}

void EyedropperTool::release(const PointerEvent&) {
    m_drag = false;
    if (m_now.isValid()) app()->pushColorHistory(m_now);
    Q_EMIT overlayChanged();
}

void EyedropperTool::drawOverlay(QPainter& p, CanvasView*) {
    if (!m_drag || !m_now.isValid()) return;
    // Two-colour ring like CSP: new colour on top, previous below.
    QRectF r(m_pos.x() - 34, m_pos.y() - 34, 68, 68);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath ring;
    ring.addEllipse(r);
    ring.addEllipse(r.adjusted(14, 14, -14, -14));
    ring.setFillRule(Qt::OddEvenFill);
    p.setClipPath(ring);
    p.fillRect(QRectF(r.left(), r.top(), r.width(), r.height() / 2), m_now);
    p.fillRect(QRectF(r.left(), r.center().y(), r.width(), r.height() / 2), m_prev.isValid() ? m_prev : m_now);
    p.setClipping(false);
    p.setPen(QPen(QColor(0, 0, 0, 120), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(r);
    p.drawEllipse(r.adjusted(14, 14, -14, -14));
    p.restore();
}

QCursor EyedropperTool::cursor(CanvasView*) const {
    static QCursor c;
    static QString theme;
    if (theme != Theme::current() || c.pixmap().isNull()) {
        theme = Theme::current();
        QPixmap pm(26, 26);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.drawPixmap(1, 1, Theme::iconPixmap("eyedropper", 24, Qt::black));
        p.drawPixmap(0, 0, Theme::iconPixmap("eyedropper", 24, Qt::white));
        c = QCursor(pm, 4, 21);
    }
    return c;
}

QWidget* EyedropperTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* f = new QFormLayout(w);
    f->setContentsMargins(6, 6, 6, 6);
    auto* src = new QComboBox(w);
    src->addItem(T("All visible layers"));
    src->addItem(T("Current layer"));
    src->setCurrentIndex(sampleMerged ? 0 : 1);
    connect(src, &QComboBox::currentIndexChanged, this, [this](int i) { sampleMerged = i == 0; });
    f->addRow(T("Sample"), src);
    auto* avg = new QComboBox(w);
    for (int v : {1, 3, 5, 11}) avg->addItem(v == 1 ? T("Point") : QStringLiteral("%1 x %1").arg(v), v);
    avg->setCurrentIndex(std::max(0, avg->findData(average)));
    connect(avg, &QComboBox::currentIndexChanged, this, [this, avg](int i) { average = avg->itemData(i).toInt(); });
    f->addRow(T("Average"), avg);
    auto* hint = new QLabel(T("Shift + click picks the background color."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    f->addRow(hint);
    return w;
}

}  // namespace gx
