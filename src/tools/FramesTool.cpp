#include "tools/FramesTool.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerOps.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

// ---------------------------------------------------------------------------- the frames

QJsonObject FrameSet::toJson() const {
    QJsonArray fr;
    for (const QPolygonF& p : frames) {
        QJsonArray pts;
        for (const QPointF& q : p) pts << q.x() << q.y();
        fr << pts;
    }
    return {{"type", "frames"}, {"width", width}, {"color", color.name(QColor::HexArgb)}, {"frames", fr}};
}

FrameSet FrameSet::fromJson(const QJsonObject& o) {
    FrameSet f;
    f.width = std::clamp(o.value("width").toDouble(6), 0.5, 200.0);
    f.color = QColor(o.value("color").toString("#ff000000"));
    for (const QJsonValue& v : o.value("frames").toArray()) {
        const QJsonArray a = v.toArray();
        QPolygonF p;
        for (int i = 0; i + 1 < a.size(); i += 2) p << QPointF(a[i].toDouble(), a[i + 1].toDouble());
        if (p.size() >= 3) f.frames << p;
    }
    return f;
}

void FrameSet::renderInto(Document& doc, RasterLayer& layer) const {
    const QRect b = doc.bounds();
    QImage img(b.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.setBrush(Qt::NoBrush);
        for (const QPolygonF& f : frames) p.drawPolygon(f);
    }
    const QRect old = layer.pixels.exactBounds();
    layer.pixels = RasterSurface();
    imageToSurface(img, layer.pixels, b.topLeft());
    layer.pixels.optimizeRect(b);
    layer.generator = toJson();
    doc.pixelsChanged((old | b) & b, &layer);
    layer.generatorVersion = layer.version;
}

namespace {
// The part of a convex polygon on one side of a line (side * distance >= offset).
QPolygonF clipHalf(const QPolygonF& poly, QPointF a, QPointF n, double offset) {
    QPolygonF out;
    auto dist = [&](QPointF p) { return (p.x() - a.x()) * n.x() + (p.y() - a.y()) * n.y() - offset; };
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF p = poly[i], q = poly[(i + 1) % poly.size()];
        const double dp = dist(p), dq = dist(q);
        if (dp >= 0) out << p;
        if ((dp >= 0) != (dq >= 0)) out << p + (q - p) * (dp / (dp - dq));
    }
    return out;
}
}  // namespace

int FrameSet::cut(QPointF a, QPointF b, double gutter) {
    const QPointF d = b - a;
    const double len = std::hypot(d.x(), d.y());
    if (len < 2) return 0;
    const QPointF n(-d.y() / len, d.x() / len);
    QVector<QPolygonF> out;
    int count = 0;
    for (const QPolygonF& f : frames) {
        // only frames the line really crosses (the drag goes over the frame, not just its line)
        bool pos = false, neg = false;
        for (const QPointF& p : f) {
            const double s = (p.x() - a.x()) * n.x() + (p.y() - a.y()) * n.y();
            pos |= s > gutter / 2;
            neg |= s < -gutter / 2;
        }
        const QPolygonF along = QPolygonF({a, b});
        const bool crosses = pos && neg && (f.containsPoint((a + b) / 2, Qt::OddEvenFill) || f.boundingRect().intersects(along.boundingRect()));
        if (!crosses) {
            out << f;
            continue;
        }
        const QPolygonF p1 = clipHalf(f, a, n, gutter / 2), p2 = clipHalf(f, a, -n, gutter / 2);
        if (p1.size() >= 3) out << p1;
        if (p2.size() >= 3) out << p2;
        ++count;
    }
    frames = out;
    return count;
}

FrameSet FrameSet::page(const QRectF& area, const QVector<int>& columns, double gutter) {
    FrameSet f;
    const int rows = std::max(1, int(columns.size()));
    const double rowH = (area.height() - gutter * (rows - 1)) / rows;
    for (int r = 0; r < rows; ++r) {
        const int cols = std::max(1, columns[r]);
        const double colW = (area.width() - gutter * (cols - 1)) / cols;
        for (int c = 0; c < cols; ++c)
            f.frames << QPolygonF(QRectF(area.left() + c * (colW + gutter), area.top() + r * (rowH + gutter), colW, rowH));
    }
    return f;
}

// ---------------------------------------------------------------------------- the tool

RasterLayer* FramesTool::framesLayer(Document* d) const {
    auto* l = d ? dynamic_cast<RasterLayer*>(d->currentLayer()) : nullptr;
    return l && l->generatorEditable() && FrameSet::isFrames(l->generator) ? l : nullptr;
}

void FramesTool::commit(Document* d, const FrameSet& fs0, const QString& undo) {
    FrameSet fs = fs0;
    fs.width = width;
    if (RasterLayer* l = framesLayer(d)) {
        const quint32 id = l->id;
        d->pushUndo(undo);
        if (auto* nl = dynamic_cast<RasterLayer*>(d->layer(id))) fs.renderInto(*d, *nl);
        return;
    }
    // a new frames layer; the fill tools take their borders from it
    auto nl = std::make_shared<RasterLayer>();
    nl->name = d->uniqueLayerName(T("Frames"));
    nl->reference = true;
    auto* added = static_cast<RasterLayer*>(ops::insertLayer(*d, nl, nullptr, true, undo));
    fs.renderInto(*d, *added);
}

void FramesTool::addFrame(Document* d, const QRectF& r) {
    if (!d || r.width() < 4 || r.height() < 4) return;
    FrameSet fs;
    if (RasterLayer* l = framesLayer(d)) fs = FrameSet::fromJson(l->generator);
    else fs.color = app()->foreground();
    fs.frames << QPolygonF(r);
    commit(d, fs, "Panel frame");
}

void FramesTool::cutFrames(Document* d, QPointF a, QPointF b) {
    RasterLayer* l = framesLayer(d);
    if (!l) {
        app()->showStatus(T("Select a frames layer first (made by this tool)."), 4000);
        return;
    }
    FrameSet fs = FrameSet::fromJson(l->generator);
    if (fs.cut(a, b, gutter) == 0) {
        app()->showStatus(T("The line does not cross a frame."), 3000);
        return;
    }
    commit(d, fs, "Cut frames");
}

void FramesTool::makePage(Document* d, const QVector<int>& columns) {
    if (!d) return;
    const QRectF b(d->bounds());
    const double m = std::min(b.width(), b.height()) * margin;
    FrameSet fs = FrameSet::page(b.adjusted(m, m, -m, -m), columns, gutter);
    if (RasterLayer* l = framesLayer(d)) fs.color = FrameSet::fromJson(l->generator).color;
    else fs.color = app()->foreground();
    commit(d, fs, "Page of frames");
}

void FramesTool::press(const PointerEvent& e) {
    if (!e.view) return;
    m_view = e.view;
    m_dragging = true;
    m_a = m_b = e.pos;
}

void FramesTool::move(const PointerEvent& e) {
    if (!m_dragging) return;
    m_b = e.pos;
    if (mode == 0 && (e.mods & Qt::ShiftModifier)) {  // a square frame
        const QPointF d = m_b - m_a;
        const double s = std::max(std::abs(d.x()), std::abs(d.y()));
        m_b = m_a + QPointF(d.x() < 0 ? -s : s, d.y() < 0 ? -s : s);
    }
    Q_EMIT overlayChanged();
}

void FramesTool::release(const PointerEvent&) {
    if (!m_dragging) return;
    m_dragging = false;
    Document* d = m_view ? m_view->doc() : nullptr;
    if (mode == 0) addFrame(d, QRectF(m_a, m_b).normalized());
    else cutFrames(d, m_a, m_b);
    Q_EMIT overlayChanged();
}

void FramesTool::drawOverlay(QPainter& p, CanvasView* v) {
    if (!m_dragging || v != m_view) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(Theme::colors().accent, 1.5, Qt::DashLine);
    p.setPen(pen);
    if (mode == 0) p.drawPolygon(v->canvasToWidget().map(QPolygonF(QRectF(m_a, m_b).normalized())));
    else p.drawLine(v->toWidget(m_a), v->toWidget(m_b));
    p.restore();
}

QWidget* FramesTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 6, 6, 6);
    auto* m = new QComboBox(w);
    m->addItems({T("Draw a frame (drag a rectangle)"), T("Cut frames (drag a line across)")});
    m->setCurrentIndex(mode);
    connect(m, &QComboBox::currentIndexChanged, this, [this](int i) { mode = i; });
    v->addWidget(m);
    auto* bw = new ValueBar(T("Border"), 0.5, 60, 1, w);
    bw->setSuffix(" px");
    bw->setValue(width);
    connect(bw, &ValueBar::valueChanged, this, [this](double x) {
        width = x;
        // the frames layer that is selected follows at once
        Document* d = app()->activeDocument();
        if (RasterLayer* l = framesLayer(d)) commit(d, FrameSet::fromJson(l->generator), "Panel frame");
    });
    v->addWidget(bw);
    auto* gt = new ValueBar(T("Gutter"), 0, 300, 0, w);
    gt->setSuffix(" px");
    gt->setValue(gutter);
    gt->setToolTip(T("The space between frames when cutting or making a page."));
    connect(gt, &ValueBar::valueChanged, this, [this](double x) { gutter = x; });
    v->addWidget(gt);
    auto* mg = new ValueBar(T("Page margins"), 0, 0.3, 2, w);
    mg->setDisplayScale(100);
    mg->setSuffix("%");
    mg->setValue(margin);
    connect(mg, &ValueBar::valueChanged, this, [this](double x) { margin = x; });
    v->addWidget(mg);
    auto* t = new QLabel(T("A page of frames"), w);
    t->setObjectName("sectionTitle");
    v->addWidget(t);
    auto* g = new QGridLayout;
    const QList<QPair<QString, QVector<int>>> pages = {
        {T("1 frame"), {1}}, {T("2 rows"), {1, 1}}, {T("3 rows"), {1, 1, 1}}, {T("3 rows, middle split"), {1, 2, 1}},
        {T("2 x 2"), {2, 2}}, {T("4 rows"), {1, 2, 1, 2}}};
    for (int i = 0; i < pages.size(); ++i) {
        auto* b = new QPushButton(pages[i].first, w);
        const QVector<int> cols = pages[i].second;
        connect(b, &QPushButton::clicked, this, [this, cols] { makePage(app()->activeDocument(), cols); });
        g->addWidget(b, i / 2, i % 2);
    }
    v->addLayout(g);
    auto* hint = new QLabel(T("Frames are drawn with the main color on a frames layer, which stays editable while you do not paint on it. "
                              "Select it to add or cut more frames; select another layer to start a new set. The fill tools stop at frames."),
                            w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    v->addStretch();
    return w;
}

}  // namespace gx
