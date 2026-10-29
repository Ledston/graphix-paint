#include "ui/BrushPreview.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTabletEvent>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "app/TabletInput.h"
#include "brush/BrushStroke.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/Layer.h"
#include "ui/Theme.h"

namespace gx {

namespace {
// Colors to mix with: three soft stripes on the right half of the sheet.
void paintStripes(RasterLayer* l, const QRect& area) {
    const quint32 cols[] = {0xffe8828au, 0xff74a9e8u, 0xfff0d070u};
    const int x0 = area.left() + area.width() * 55 / 100;
    const int w = (area.right() - x0) / 3 + 1;
    for (int i = 0; i < 3; ++i) l->pixels.fillRect(QRect(x0 + i * w, area.top(), w, area.height()), cols[i]);
}

// The brush made to fit a sheet of the given height.
BrushSettings fitted(BrushSettings b, double height) {
    const double maxDia = std::max(2.0, height * 0.55);
    if (b.size > maxDia) {
        const double k = maxDia / b.size;
        b.size = maxDia;
        b.taperIn *= k;
        b.taperOut *= k;
    }
    b.airbrush = false;  // no time passes in a preview
    return b;
}

std::shared_ptr<RasterLayer> makeSheet(Document& doc) {
    auto paper = std::make_shared<RasterLayer>();
    paper->id = doc.newLayerId();
    paper->pixels.fillRect(doc.bounds(), 0xffffffffu);
    doc.root()->insert(0, paper);
    auto layer = std::make_shared<RasterLayer>();
    layer->id = doc.newLayerId();
    doc.root()->insert(1, layer);
    return layer;
}
}  // namespace

QImage renderBrushPreview(const BrushSettings& brush, const QSize& size, const QColor& fg, const QColor& bg, bool colors, double dpr) {
    const QSize px(std::max(8, int(size.width() * dpr)), std::max(8, int(size.height() * dpr)));
    Document doc(px);
    auto layer = makeSheet(doc);
    BrushSettings b = fitted(brush, px.height());
    if (b.engine == BrushEngine::Erase) layer->pixels.fillRect(doc.bounds(), 0xff9aa3b5u);  // something to erase
    else if (colors || b.engine != BrushEngine::Paint) paintStripes(layer.get(), doc.bounds());
    StrokeContext ctx;
    ctx.doc = &doc;
    ctx.layer = layer.get();
    ctx.brush = b;
    ctx.color = fg;
    ctx.subColor = bg;
    PressureCurve curve;
    ctx.pressureCurve = &curve;
    {
        BrushStroke st(ctx);
        const int N = 160;
        const double w = px.width(), h = px.height();
        for (int k = 0; k <= N; ++k) {
            const double t = double(k) / N;
            InputSample s;
            s.pos = QPointF(w * (0.06 + 0.88 * t), h * 0.5 + std::sin(t * 2 * M_PI) * h * 0.2);
            s.pressure = std::sin(t * M_PI);
            s.timeMs = k * 6.0;
            if (k == 0) st.begin(s);
            else st.add(s);
        }
        st.end();
    }
    QImage img = Compositor::renderImage(doc.root(), doc.bounds()).convertToFormat(QImage::Format_RGB32);
    img.setDevicePixelRatio(dpr);
    return img;
}

// ============================================================================ preview widget

BrushPreviewWidget::BrushPreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(80);
    m_timer.setSingleShot(true);
    m_timer.setInterval(60);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_img = renderBrushPreview(m_brush, size(), app()->foreground(), app()->background(), true, devicePixelRatioF());
        update();
    });
    setToolTip(T("A sample stroke: light, full, then light pressure. Big brushes are shown smaller."));
}

void BrushPreviewWidget::setBrush(const BrushSettings& b) {
    m_brush = b;
    m_timer.start();
}

void BrushPreviewWidget::resizeEvent(QResizeEvent*) { m_timer.start(); }

void BrushPreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (!m_img.isNull()) p.drawImage(QPointF(0, 0), m_img);
    p.setPen(Theme::colors().border);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

// ============================================================================ test pad

BrushTestPad::BrushTestPad(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 140);
    setAttribute(Qt::WA_TabletTracking);
    setCursor(Qt::CrossCursor);
    setToolTip(T("Try the brush here with the pen or the mouse."));
}

BrushTestPad::~BrushTestPad() {
    if (m_stroke) m_stroke->cancel();
}

void BrushTestPad::ensureDoc() {
    const double dpr = devicePixelRatioF();
    const QSize px(std::max(8, int(width() * dpr)), std::max(8, int(height() * dpr)));
    if (m_doc && m_doc->size() == px) return;
    m_doc = std::make_shared<Document>(px);
    m_layer = makeSheet(*m_doc);
    paintStripes(m_layer.get(), m_doc->bounds());
    m_view = Compositor::renderImage(m_doc->root(), m_doc->bounds()).convertToFormat(QImage::Format_RGB32);
    m_dirty = {};
    connect(m_doc.get(), &Document::imageChanged, this, [this](const QRect& r) {
        m_dirty |= r;
        update();
    });
}

void BrushTestPad::clear() {
    if (m_stroke) {
        m_stroke->cancel();
        m_stroke.reset();
    }
    m_doc.reset();
    ensureDoc();
    update();
}

void BrushTestPad::resizeEvent(QResizeEvent*) {
    if (!m_stroke) {
        m_doc.reset();
        update();
    }
}

QPointF BrushTestPad::toDoc(const QPointF& w) const { return w * devicePixelRatioF(); }

void BrushTestPad::begin(const QPointF& p, double pressure, double tx, double ty) {
    ensureDoc();
    StrokeContext ctx;
    ctx.doc = m_doc.get();
    ctx.layer = m_layer.get();
    ctx.brush = m_brush;
    ctx.brush.size *= devicePixelRatioF();  // the same size on screen as on a 100% canvas
    ctx.color = app()->foreground();
    ctx.subColor = app()->background();
    ctx.transparentColor = app()->transparentColor();
    ctx.pressureCurve = &app()->pressureCurve();
    m_stroke = std::make_unique<BrushStroke>(ctx);
    m_time = 0;
    InputSample s;
    s.pos = toDoc(p);
    s.pressure = pressure;
    s.tiltX = tx;
    s.tiltY = ty;
    m_stroke->begin(s);
}

void BrushTestPad::add(const QPointF& p, double pressure, double tx, double ty) {
    if (!m_stroke) return;
    InputSample s;
    s.pos = toDoc(p);
    s.pressure = pressure;
    s.tiltX = tx;
    s.tiltY = ty;
    s.timeMs = (m_time += 8.0);
    m_stroke->add(s);
}

void BrushTestPad::finish() {
    if (!m_stroke) return;
    m_stroke->end();
    m_stroke.reset();
}

void BrushTestPad::mousePressEvent(QMouseEvent* e) {
    if (m_tablet || e->button() != Qt::LeftButton) return;
    begin(e->position(), 0.7, 0, 0);
}

void BrushTestPad::mouseMoveEvent(QMouseEvent* e) {
    if (m_tablet) return;
    add(e->position(), 0.7, 0, 0);
}

void BrushTestPad::mouseReleaseEvent(QMouseEvent*) {
    if (m_tablet) return;
    finish();
}

void BrushTestPad::tabletEvent(QTabletEvent* e) {
    if (TabletInput::shouldIgnore(e)) return;
    e->accept();
    switch (e->type()) {
    case QEvent::TabletPress:
        m_tablet = true;
        begin(e->position(), e->pressure(), e->xTilt(), e->yTilt());
        break;
    case QEvent::TabletMove:
        if (m_tablet) add(e->position(), e->pressure(), e->xTilt(), e->yTilt());
        break;
    case QEvent::TabletRelease:
        finish();
        m_tablet = false;
        break;
    default:
        break;
    }
}

void BrushTestPad::paintEvent(QPaintEvent*) {
    ensureDoc();
    if (!m_dirty.isEmpty()) {
        const QRect r = m_dirty & m_doc->bounds();
        m_dirty = {};
        if (!r.isEmpty()) {
            QImage part = Compositor::renderImage(m_doc->root(), r);
            m_view.setDevicePixelRatio(1.0);  // paint in sheet pixels
            QPainter pp(&m_view);
            pp.setCompositionMode(QPainter::CompositionMode_Source);
            pp.drawImage(r.topLeft(), part);
        }
    }
    QPainter p(this);
    m_view.setDevicePixelRatio(devicePixelRatioF());
    p.drawImage(QPointF(0, 0), m_view);
    p.setPen(Theme::colors().border);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

}  // namespace gx
