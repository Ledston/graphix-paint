#include "ui/ReferenceOverlay.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QToolButton>
#include <QWindow>

#include "app/App.h"
#include "app/I18n.h"
#include "ui/Theme.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace gx {

namespace {
constexpr int kEdge = 8;  // how close to the border a press resizes the window
}

ReferenceOverlay::ReferenceOverlay(QWidget* owner)
    : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint) {
    setObjectName("referenceOverlay");
    setWindowTitle(T("Reference over everything"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);  // the canvas keeps the keyboard
    setMouseTracking(true);
    setMinimumSize(80, 60);

    QSettings& s = app()->settings();
    m_opacity = std::clamp(s.value("overlay/opacity", 0.5).toDouble(), 0.05, 1.0);
    m_gray = s.value("overlay/gray", false).toBool();
    m_flip = s.value("overlay/flip", false).toBool();
    m_hideCapture = s.value("overlay/hideFromCapture", true).toBool();
    const QRect g = s.value("overlay/geometry").toRect();
    if (g.isValid() && QGuiApplication::screenAt(g.center())) setGeometry(g);
    else resize(480, 360);

    // A small bar that appears under the mouse (not while drawing through).
    m_bar = new QWidget(this);
    m_bar->setObjectName("overlayBar");
    m_bar->setAutoFillBackground(true);
    auto* h = new QHBoxLayout(m_bar);
    h->setContentsMargins(4, 2, 4, 2);
    h->setSpacing(2);
    auto btn = [&](const QString& icon, const QString& tip, bool check) {
        auto* b = new QToolButton(m_bar);
        b->setIcon(Theme::icon(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setCheckable(check);
        h->addWidget(b);
        return b;
    };
    m_opSlider = new QSlider(Qt::Horizontal, m_bar);
    m_opSlider->setRange(5, 100);
    m_opSlider->setValue(int(std::lround(m_opacity * 100)));
    m_opSlider->setToolTip(T("Opacity (Ctrl + wheel)"));
    m_opSlider->setMinimumWidth(50);
    h->addWidget(m_opSlider, 1);
    connect(m_opSlider, &QSlider::valueChanged, this, [this](int v) { setOpacityValue(v / 100.0); });
    m_grayBtn = btn("adjustment", T("Grayscale (check values)"), true);
    m_grayBtn->setChecked(m_gray);
    connect(m_grayBtn, &QToolButton::toggled, this, &ReferenceOverlay::setGray);
    m_flipBtn = btn("flip", T("Flip horizontally"), true);
    m_flipBtn->setChecked(m_flip);
    connect(m_flipBtn, &QToolButton::toggled, this, &ReferenceOverlay::setFlip);
    m_throughBtn = btn("cursor", T("Draw through the picture (the pen goes to the canvas below). Turn it off in the Reference panel or with the shortcut in View."), true);
    connect(m_throughBtn, &QToolButton::toggled, this, &ReferenceOverlay::setClickThrough);
    QToolButton* close = btn("close", T("Close"), false);
    connect(close, &QToolButton::clicked, this, &QWidget::hide);
    m_bar->hide();
}

ReferenceOverlay::~ReferenceOverlay() { saveState(); }

void ReferenceOverlay::saveState() const {
    QSettings& s = app()->settings();
    s.setValue("overlay/opacity", m_opacity);
    s.setValue("overlay/gray", m_gray);
    s.setValue("overlay/flip", m_flip);
    s.setValue("overlay/hideFromCapture", m_hideCapture);
    s.setValue("overlay/geometry", geometry());
}

void ReferenceOverlay::setImage(const QImage& img) {
    const bool first = m_img.isNull();
    m_img = img;
    m_grayImg = img.isNull() ? QImage() : img.convertToFormat(QImage::Format_Grayscale8);
    if (first && !img.isNull() && !app()->settings().contains("overlay/geometry")) {
        // first time: the picture's own shape, at most about a third of the screen
        QSize avail = (screen() ? screen()->availableSize() : QSize(1600, 900)) / 2;
        resize(img.size().scaled(avail, Qt::KeepAspectRatio).boundedTo(img.size()).expandedTo(minimumSize()));
    }
    if (img.isNull()) hide();
    update();
}

void ReferenceOverlay::setOpacityValue(double o) {
    o = std::clamp(o, 0.05, 1.0);
    if (std::abs(o - m_opacity) < 1e-4) return;
    m_opacity = o;
    const QSignalBlocker b(m_opSlider);
    m_opSlider->setValue(int(std::lround(o * 100)));
    update();
    Q_EMIT stateChanged();
}

void ReferenceOverlay::setGray(bool on) {
    m_gray = on;
    const QSignalBlocker b(m_grayBtn);
    m_grayBtn->setChecked(on);
    update();
}

void ReferenceOverlay::setFlip(bool on) {
    m_flip = on;
    const QSignalBlocker b(m_flipBtn);
    m_flipBtn->setChecked(on);
    update();
}

void ReferenceOverlay::setClickThrough(bool on) {
    if (on == m_through) return;
    m_through = on;
    {
        const QSignalBlocker b(m_throughBtn);
        m_throughBtn->setChecked(on);
    }
    // Changing window flags hides the window (and may make a new native window),
    // so show it again; the capture hiding is set again in showEvent.
    const bool vis = isVisible();
    const QRect g = geometry();
    setWindowFlag(Qt::WindowTransparentForInput, on);
    setGeometry(g);
    if (vis) show();
    updateBar();
    update();
    Q_EMIT stateChanged();
}

void ReferenceOverlay::setHideFromCapture(bool on) {
    m_hideCapture = on;
    applyCaptureHiding();
    Q_EMIT stateChanged();
}

void ReferenceOverlay::applyCaptureHiding() {
#ifdef Q_OS_WIN
    if (!testAttribute(Qt::WA_WState_Created) || !isWindow()) return;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!m_hideCapture) {
        SetWindowDisplayAffinity(hwnd, WDA_NONE);
        m_captureState = CaptureState::Off;
    } else if (SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) {
        m_captureState = CaptureState::Hidden;  // Windows 10 2004 and newer
    } else if (SetWindowDisplayAffinity(hwnd, WDA_MONITOR)) {
        m_captureState = CaptureState::BlackBox;  // older Windows: a black box instead of the picture
    } else {
        m_captureState = CaptureState::Failed;
    }
#else
    m_captureState = m_hideCapture ? CaptureState::NotSupported : CaptureState::Off;
#endif
}

QString ReferenceOverlay::describe(CaptureState s) {
    switch (s) {
    case CaptureState::Hidden: return T("Not shown in screen recordings, streams or screen sharing.");
    case CaptureState::BlackBox: return T("This Windows version shows a black box instead of the picture in recordings. Windows 10 (2004) or newer hides it completely.");
    case CaptureState::Off: return T("Screen recordings and screen sharing show this picture.");
    case CaptureState::Failed: return T("Could not hide the picture from screen recordings on this computer.");
    case CaptureState::NotSupported: break;
    }
    return T("Hiding from screen recordings works on Windows 10 (2004) and newer.");
}

bool ReferenceOverlay::event(QEvent* e) {
    if (e->type() == QEvent::WinIdChange) applyCaptureHiding();
    return QWidget::event(e);
}

void ReferenceOverlay::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    applyCaptureHiding();
    Q_EMIT stateChanged();
}

void ReferenceOverlay::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    saveState();
    m_bar->hide();
    // Only a real close counts; the hide while the window flags change is followed by a show.
    QMetaObject::invokeMethod(this, [this] { Q_EMIT stateChanged(); }, Qt::QueuedConnection);
}

void ReferenceOverlay::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    m_bar->setGeometry(0, 0, width(), m_bar->sizeHint().height());
}

void ReferenceOverlay::updateBar() {
    const bool show = !m_through && underMouse();
    m_bar->setGeometry(0, 0, width(), m_bar->sizeHint().height());
    m_bar->setVisible(show);
    if (show) m_bar->raise();
}

void ReferenceOverlay::enterEvent(QEnterEvent* e) {
    QWidget::enterEvent(e);
    updateBar();
    update();
}

void ReferenceOverlay::leaveEvent(QEvent* e) {
    QWidget::leaveEvent(e);
    if (!m_moving && !m_resizing) m_bar->hide();
    update();
}

QRectF ReferenceOverlay::imageRect() const {
    if (m_img.isNull()) return rect();
    QSizeF s = QSizeF(m_img.size()).scaled(size(), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

void ReferenceOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    // a nearly invisible fill keeps the whole frame grabbable (fully clear pixels let clicks through)
    if (!m_through) p.fillRect(rect(), QColor(0, 0, 0, 2));
    if (!m_img.isNull()) {
        const QRectF r = imageRect();
        p.save();
        p.setOpacity(m_opacity);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_flip) {
            p.translate(r.center().x() * 2, 0);
            p.scale(-1, 1);
        }
        p.drawImage(r, m_gray ? m_grayImg : m_img);
        p.restore();
    }
    // the frame: thin while drawing through, clearer under the mouse
    const ThemeColors& c = Theme::colors();
    QPen pen(m_through ? c.accent : (underMouse() ? c.accent : c.dim));
    pen.setWidth(1);
    if (m_through) pen.setStyle(Qt::DashLine);
    QColor col = pen.color();
    col.setAlphaF(m_through ? 0.6 : (underMouse() ? 0.9 : 0.35));
    pen.setColor(col);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    if (!m_through && underMouse()) {
        // resize grip in the corner
        p.setPen(QPen(c.accent, 1));
        for (int i = 4; i <= 12; i += 4) p.drawLine(width() - i, height() - 2, width() - 2, height() - i);
    }
}

Qt::Edges ReferenceOverlay::edgesAt(const QPoint& pt) const {
    Qt::Edges e;
    if (pt.x() < kEdge) e |= Qt::LeftEdge;
    if (pt.x() >= width() - kEdge) e |= Qt::RightEdge;
    if (pt.y() >= height() - kEdge) e |= Qt::BottomEdge;
    if (pt.y() < kEdge && !m_bar->isVisible()) e |= Qt::TopEdge;
    return e;
}

static Qt::CursorShape cursorFor(Qt::Edges e) {
    if (e == (Qt::LeftEdge | Qt::TopEdge) || e == (Qt::RightEdge | Qt::BottomEdge)) return Qt::SizeFDiagCursor;
    if (e == (Qt::RightEdge | Qt::TopEdge) || e == (Qt::LeftEdge | Qt::BottomEdge)) return Qt::SizeBDiagCursor;
    if (e & (Qt::LeftEdge | Qt::RightEdge)) return Qt::SizeHorCursor;
    if (e & (Qt::TopEdge | Qt::BottomEdge)) return Qt::SizeVerCursor;
    return Qt::SizeAllCursor;
}

void ReferenceOverlay::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const Qt::Edges edges = edgesAt(e->position().toPoint());
    m_pressGlobal = e->globalPosition().toPoint();
    m_pressGeom = geometry();
    // the system move / resize feels native and snaps to screen edges; fall back to doing it by hand
    if (windowHandle()) {
        if (edges ? windowHandle()->startSystemResize(edges) : windowHandle()->startSystemMove()) return;
    }
    if (edges) m_resizing = edges;
    else m_moving = true;
}

void ReferenceOverlay::mouseMoveEvent(QMouseEvent* e) {
    const QPoint d = e->globalPosition().toPoint() - m_pressGlobal;
    if (m_moving) {
        move(m_pressGeom.topLeft() + d);
        return;
    }
    if (m_resizing) {
        QRect g = m_pressGeom;
        if (m_resizing & Qt::LeftEdge) g.setLeft(std::min(g.left() + d.x(), g.right() - minimumWidth()));
        if (m_resizing & Qt::RightEdge) g.setRight(std::max(g.right() + d.x(), g.left() + minimumWidth()));
        if (m_resizing & Qt::TopEdge) g.setTop(std::min(g.top() + d.y(), g.bottom() - minimumHeight()));
        if (m_resizing & Qt::BottomEdge) g.setBottom(std::max(g.bottom() + d.y(), g.top() + minimumHeight()));
        setGeometry(g);
        return;
    }
    setCursor(cursorFor(edgesAt(e->position().toPoint())));
    if (!m_bar->isVisible() && !m_through) updateBar();
}

void ReferenceOverlay::mouseReleaseEvent(QMouseEvent*) {
    m_moving = false;
    m_resizing = {};
    saveState();
}

void ReferenceOverlay::wheelEvent(QWheelEvent* e) {
    const double steps = e->angleDelta().y() / 120.0;
    if (e->modifiers() & Qt::ControlModifier) {
        setOpacityValue(m_opacity + steps * 0.05);
        return;
    }
    // zoom the whole window around the mouse
    const double f = std::pow(1.1, steps);
    const QPointF at = e->position();
    QSize ns = (QSizeF(size()) * f).toSize().expandedTo(minimumSize());
    if (screen()) ns = ns.boundedTo(screen()->availableSize() * 2);
    const QPointF k(double(ns.width()) / width(), double(ns.height()) / height());
    const QPoint tl = pos() + (at - QPointF(at.x() * k.x(), at.y() * k.y())).toPoint();
    setGeometry(QRect(tl, ns));
    saveState();
}

}  // namespace gx
