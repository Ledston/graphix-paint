#include "ui/NavigatorPanel.h"
#include <QWheelEvent>

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

NavigatorView::NavigatorView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(90);
    setCursor(Qt::PointingHandCursor);
    m_timer.setSingleShot(true);
    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, &NavigatorView::refresh);
}

void NavigatorView::setView(CanvasView* v) {
    if (m_view == v) return;
    if (m_view) {
        disconnect(m_view, nullptr, this, nullptr);
        if (m_view->doc()) disconnect(m_view->doc(), nullptr, this, nullptr);
    }
    m_view = v;
    m_thumb = QImage();
    if (v) {
        connect(v, &CanvasView::viewChanged, this, [this] { update(); });
        connect(v->doc(), &Document::imageChanged, this, [this] { if (!m_timer.isActive()) m_timer.start(); });
        connect(v->doc(), &Document::sizeChanged, this, [this] { m_timer.start(0); });
        connect(v, &QObject::destroyed, this, [this] { m_thumb = QImage(); update(); });
    }
    m_timer.start(0);
}

void NavigatorView::refresh() {
    if (!m_view || !isVisible()) return;
    QImage img;
    bool done = m_view->doc()->projection()->thumbnail(std::max(width(), height()) * 2, 8.0, img);
    m_thumb = img;
    if (!done) m_timer.start(30);
    update();
}

QRectF NavigatorView::imageRect() const {
    if (!m_view) return {};
    QSizeF cs = m_view->doc()->size();
    double s = std::min((width() - 8) / cs.width(), (height() - 8) / cs.height());
    QSizeF is = cs * s;
    return QRectF((width() - is.width()) / 2, (height() - is.height()) / 2, is.width(), is.height());
}

void NavigatorView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const ThemeColors& c = Theme::colors();
    p.fillRect(rect(), c.base);
    if (!m_view) return;
    QRectF r = imageRect();
    p.fillRect(r, checkerBrush(5));
    if (!m_thumb.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(r, m_thumb);
    }
    p.setPen(c.border);
    p.drawRect(r);
    QSizeF cs = m_view->doc()->size();
    QTransform t = QTransform::fromScale(r.width() / cs.width(), r.height() / cs.height()) * QTransform::fromTranslate(r.left(), r.top());
    QPolygonF vis = t.map(m_view->visibleCanvas());
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 160), 2.5));
    p.drawPolygon(vis);
    p.setPen(QPen(QColor(255, 80, 80), 1.2));
    p.drawPolygon(vis);
}

void NavigatorView::jump(const QPointF& w) {
    if (!m_view) return;
    QRectF r = imageRect();
    QSizeF cs = m_view->doc()->size();
    m_view->centerOn(QPointF((w.x() - r.left()) / r.width() * cs.width(), (w.y() - r.top()) / r.height() * cs.height()));
}

void NavigatorView::mousePressEvent(QMouseEvent* e) { jump(e->position()); }
void NavigatorView::wheelEvent(QWheelEvent* e) {
    // the wheel over the navigator zooms the canvas
    if (!m_view) return;
    if (e->angleDelta().y() > 0) m_view->zoomIn();
    else if (e->angleDelta().y() < 0) m_view->zoomOut();
    e->accept();
}
void NavigatorView::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons()) jump(e->position());
}

NavigatorPanel::NavigatorPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(3);
    m_nav = new NavigatorView(this);
    v->addWidget(m_nav, 1);
    m_zoom = new ValueBar(T("Zoom"), 1, 6400, 1, this);
    m_zoom->setLogarithmic(true);
    m_zoom->setSuffix("%");
    m_rot = new ValueBar(T("Angle"), -180, 180, 1, this);
    m_rot->setSuffix(QString(QChar(0xB0)));
    auto* row = new QHBoxLayout;
    row->setSpacing(2);
    auto button = [&](const QString& icon, const QString& tip, std::function<void()> fn) {
        auto* b = new QToolButton(this);
        b->setIcon(Theme::icon(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [this, fn] { if (m_view) fn(); });
        row->addWidget(b);
        return b;
    };
    button("minus", T("Zoom out"), [this] { m_view->zoomOut(); });
    button("plus", T("Zoom in"), [this] { m_view->zoomIn(); });
    button("zoom", T("Fit to window"), [this] { m_view->fitToWindow(); });
    button("image", T("Actual pixels (100%)"), [this] { m_view->actualPixels(); });
    button("rotate", T("Reset rotation"), [this] { m_view->setRotation(0); });
    button("flip", T("Flip view horizontally"), [this] { m_view->setFlipped(!m_view->isFlipped()); });
    row->addStretch();
    v->addWidget(m_zoom);
    v->addWidget(m_rot);
    v->addLayout(row);
    connect(m_zoom, &ValueBar::valueChanged, this, [this](double z) { if (m_view && !m_updating) m_view->setZoom(z / 100.0); });
    connect(m_rot, &ValueBar::valueChanged, this, [this](double r) { if (m_view && !m_updating) m_view->setRotation(r); });
    connect(app(), &App::activeViewChanged, this, &NavigatorPanel::setView);
    setView(app()->activeView());
}

void NavigatorPanel::setView(CanvasView* v) {
    if (m_view) disconnect(m_view, nullptr, this, nullptr);
    m_view = v;
    m_nav->setView(v);
    if (v) connect(v, &CanvasView::viewChanged, this, &NavigatorPanel::sync);
    sync();
}

void NavigatorPanel::sync() {
    m_updating = true;
    if (m_view) {
        m_zoom->setValue(m_view->zoom() * 100.0);
        m_rot->setValue(m_view->rotation());
    }
    m_updating = false;
}

}  // namespace gx
