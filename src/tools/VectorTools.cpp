#include "tools/VectorTools.h"

#include <QLabel>
#include <QVBoxLayout>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "vector/VectorRender.h"

namespace gx {

LineworkOptions& lineworkOptions() {
    static LineworkOptions o;
    return o;
}

double VectorEditTool::eraserSize = 16;

static QWidget* lineworkOptionsWidget(QWidget* parent, const QString& hintText) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 6, 6, 6);
    LineworkOptions& o = lineworkOptions();
    auto bar = [&](const QString& label, double min, double max, int dec, double* field, double scale, const QString& suffix, bool log) {
        auto* b = new ValueBar(label, min, max, dec, w);
        b->setDisplayScale(scale);
        b->setSuffix(suffix);
        b->setLogarithmic(log);
        b->setValue(*field);
        QObject::connect(b, &ValueBar::valueChanged, w, [field](double x) { *field = x; });
        v->addWidget(b);
    };
    bar(T("Width"), 0.5, 500, 1, &o.width, 1, " px", true);
    bar(T("Min width"), 0, 1, 2, &o.minWidth, 100, "%", false);
    bar(T("Hardness"), 0, 1, 2, &o.hardness, 100, "%", false);
    bar(T("Opacity"), 0, 1, 2, &o.opacity, 100, "%", false);
    bar(T("Simplify"), 0, 5, 1, &o.simplify, 1, " px", false);
    auto* hint = new QLabel(hintText, w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    v->addStretch();
    return w;
}

VectorLayer* VectorToolBase::vectorLayer(CanvasView* v, bool report) const {
    if (!v) return nullptr;
    auto* l = dynamic_cast<VectorLayer*>(v->doc()->currentLayer());
    if (!l && report) app()->showStatus(T("Linework tools work on linework layers (Layer > New linework layer)."));
    if (l && l->isEffectivelyLocked()) {
        if (report) app()->showStatus(T("The layer is locked."));
        return nullptr;
    }
    return l;
}

void VectorToolBase::rerender(CanvasView* v, VectorLayer* l, const QRectF& r) {
    QRect rr = r.toAlignedRect().adjusted(-2, -2, 2, 2);
    renderVectorLayer(*l, rr);
    v->doc()->pixelsChanged(rr, l);
}

// ============================================================================ pen

void VectorPenTool::press(const PointerEvent& e) {
    VectorLayer* l = vectorLayer(e.view);
    if (!l) return;
    m_view = e.view;
    Document* d = e.view->doc();
    d->pushUndo(T("Linework stroke"));
    l = static_cast<VectorLayer*>(d->currentLayer());
    VectorStroke s;
    s.id = l->nextStrokeId++;
    const LineworkOptions& o = lineworkOptions();
    s.width = float(o.width);
    s.minWidth = float(o.minWidth);
    s.hardness = float(o.hardness);
    s.opacity = float(o.opacity);
    s.color = app()->foreground();
    l->strokes.push_back(s);
    m_strokeId = s.id;
    m_raw.clear();
    m_lastBounds = QRectF();
    m_active = true;
    m_stab.configure(Stabilizer::Mode(app()->stabilizerMode()), app()->stabilizerLevel(), app()->pressureSmoothing());
    InputSample in;
    in.pos = e.pos;
    in.pressure = e.pressure;
    in.timeMs = e.timeMs;
    for (const auto& x : m_stab.begin(in)) addSample(x);
}

void VectorPenTool::addSample(const InputSample& s) {
    if (!m_view) return;
    auto* l = dynamic_cast<VectorLayer*>(m_view->doc()->currentLayer());
    if (!l) return;
    VectorStroke* st = l->strokeById(m_strokeId);
    if (!st) return;
    float pr = app()->pressureCurve().map(float(s.pressure));
    if (!m_raw.isEmpty() && QLineF(m_raw.back().pos, s.pos).length() < 0.4) return;
    m_raw.push_back({s.pos, pr});
    st->points = m_raw;
    QRectF b = st->bounds();
    QRectF tail;
    int n = int(m_raw.size());
    for (int i = std::max(0, n - 4); i < n; ++i) tail |= QRectF(m_raw[i].pos, QSizeF(0, 0));
    tail.adjust(-st->width - 2, -st->width - 2, st->width + 2, st->width + 2);
    rerender(m_view, l, tail);
    m_lastBounds = b;
}

void VectorPenTool::move(const PointerEvent& e) {
    if (!m_active) return;
    InputSample in;
    in.pos = e.pos;
    in.pressure = e.pressure;
    in.timeMs = e.timeMs;
    for (const auto& x : m_stab.feed(in)) addSample(x);
}

void VectorPenTool::release(const PointerEvent&) {
    if (!m_active || !m_view) return;
    m_active = false;
    for (const auto& x : m_stab.finish()) addSample(x);
    auto* l = dynamic_cast<VectorLayer*>(m_view->doc()->currentLayer());
    if (!l) return;
    VectorStroke* st = l->strokeById(m_strokeId);
    if (!st) return;
    if (st->points.isEmpty()) {
        m_view->doc()->revertLastUndo();
        return;
    }
    QRectF before = st->bounds();
    st->points = simplifyPoints(m_raw, lineworkOptions().simplify);
    rerender(m_view, l, before | st->bounds());
}

QWidget* VectorPenTool::createOptions(QWidget* parent) {
    return lineworkOptionsWidget(parent, T("Strokes stay editable: change points, pressure, width and color with the other linework tools. "
                                           "Uses the stabilizer from the top bar."));
}

// ============================================================================ curve

void CurveTool::press(const PointerEvent& e) {
    if (!vectorLayer(e.view)) return;
    if (m_view && m_view != e.view) m_pts.clear();
    m_view = e.view;
    m_pts.push_back({e.pos, 1.f});
    Q_EMIT overlayChanged();
}

void CurveTool::hover(const PointerEvent& e) {
    m_hover = e.pos;
    if (!m_pts.isEmpty()) Q_EMIT overlayChanged();
}

void CurveTool::doubleClick(const PointerEvent&) { finish(); }

bool CurveTool::keyPress(QKeyEvent* e, CanvasView*) {
    if (m_pts.isEmpty()) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { finish(); return true; }
    if (e->key() == Qt::Key_Escape) { m_pts.clear(); Q_EMIT overlayChanged(); return true; }
    if (e->key() == Qt::Key_Backspace) { m_pts.removeLast(); Q_EMIT overlayChanged(); return true; }
    return false;
}

void CurveTool::finish() {
    QVector<VectorPoint> pts = m_pts;
    m_pts.clear();
    Q_EMIT overlayChanged();
    // drop the duplicate point created by the double-click
    while (pts.size() >= 2 && QLineF(pts[pts.size() - 1].pos, pts[pts.size() - 2].pos).length() < 1) pts.removeLast();
    if (!m_view || pts.size() < 2) return;
    VectorLayer* l = vectorLayer(m_view);
    if (!l) return;
    Document* d = m_view->doc();
    d->pushUndo(T("Curve"));
    l = static_cast<VectorLayer*>(d->currentLayer());
    VectorStroke s;
    s.id = l->nextStrokeId++;
    const LineworkOptions& o = lineworkOptions();
    s.width = float(o.width);
    s.minWidth = float(o.minWidth);
    s.hardness = float(o.hardness);
    s.opacity = float(o.opacity);
    s.color = app()->foreground();
    s.points = pts;
    l->strokes.push_back(s);
    rerender(m_view, l, s.bounds());
}

void CurveTool::drawOverlay(QPainter& p, CanvasView* v) {
    if (m_pts.isEmpty() || v != m_view) return;
    VectorStroke tmp;
    tmp.points = m_pts;
    tmp.points.push_back({m_hover, 1.f});
    QVector<VectorPoint> dense = sampleStroke(tmp, 2.0);
    QPainterPath path;
    for (int i = 0; i < dense.size(); ++i) {
        QPointF w = v->toWidget(dense[i].pos);
        if (i == 0) path.moveTo(w);
        else path.lineTo(w);
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::colors().accent, 1.5));
    p.drawPath(path);
    for (const auto& pt : m_pts) {
        p.setBrush(Qt::white);
        p.setPen(QPen(Qt::black, 1));
        p.drawRect(QRectF(v->toWidget(pt.pos) - QPointF(3, 3), QSizeF(6, 6)));
    }
    p.restore();
}

QWidget* CurveTool::createOptions(QWidget* parent) {
    return lineworkOptionsWidget(parent, T("Click to place points, double-click or Enter to finish, Backspace removes the last point."));
}

// ============================================================================ edit tools

VectorEditTool::VectorEditTool(Kind k, QObject* p) : VectorToolBase("vedit", "Edit linework", "vector-edit", p), m_kind(k) {
    switch (k) {
    case Pressure: m_id = "vpressure"; m_name = "Linework pressure"; m_icon = "vector-pressure"; break;
    case Weight: m_id = "vweight"; m_name = "Linework width"; m_icon = "vector-weight"; break;
    case Color: m_id = "vcolor"; m_name = "Linework color"; m_icon = "vector-color"; break;
    case Eraser: m_id = "veraser"; m_name = "Linework eraser"; m_icon = "vector-eraser"; break;
    default: break;
    }
}

void VectorEditTool::eraseAt(CanvasView* v, const QPointF& p) {
    auto* l = dynamic_cast<VectorLayer*>(v->doc()->currentLayer());
    if (!l) return;
    int idx = hitTestStroke(*l, p, eraserSize / 2);
    if (idx < 0) return;
    QRectF b = l->strokes[idx].bounds();
    l->strokes.removeAt(idx);
    m_changed = true;
    rerender(v, l, b);
}

void VectorEditTool::press(const PointerEvent& e) {
    VectorLayer* l = vectorLayer(e.view);
    if (!l) return;
    m_view = e.view;
    Document* d = e.view->doc();
    const double tol = 8.0 / e.view->zoom();
    m_press = e.pos;
    m_changed = false;
    switch (m_kind) {
    case Eraser:
        d->pushUndo(T("Erase linework"));
        m_drag = true;
        eraseAt(e.view, e.pos);
        return;
    case Color: {
        int s = hitTestStroke(*l, e.pos, tol);
        if (s < 0) return;
        d->pushUndo(T("Linework color"));
        l = static_cast<VectorLayer*>(d->currentLayer());
        l->strokes[s].color = app()->foreground();
        rerender(e.view, l, l->strokes[s].bounds());
        return;
    }
    case Weight: {
        int s = hitTestStroke(*l, e.pos, tol);
        if (s < 0) return;
        d->pushUndo(T("Linework width"));
        l = static_cast<VectorLayer*>(d->currentLayer());
        m_stroke = s;
        m_orig = l->strokes[s];
        m_drag = true;
        return;
    }
    default: break;
    }
    int pi = -1;
    int s = hitTestControlPoint(*l, e.pos, tol, &pi);
    if (s >= 0 && m_kind == Edit && (e.mods & Qt::AltModifier)) {
        d->pushUndo(T("Delete point"));
        l = static_cast<VectorLayer*>(d->currentLayer());
        QRectF b = l->strokes[s].bounds();
        l->strokes[s].points.removeAt(pi);
        if (l->strokes[s].points.isEmpty()) l->strokes.removeAt(s);
        rerender(e.view, l, b);
        return;
    }
    if (s < 0 && m_kind == Edit && (e.mods & Qt::ControlModifier)) {
        s = hitTestStroke(*l, e.pos, tol);
        pi = -1;  // whole stroke
    }
    if (s < 0) return;
    d->pushUndo(m_kind == Pressure ? T("Linework pressure") : T("Edit linework"));
    l = static_cast<VectorLayer*>(d->currentLayer());
    m_stroke = s;
    m_point = pi;
    m_orig = l->strokes[s];
    m_drag = true;
}

void VectorEditTool::move(const PointerEvent& e) {
    if (!m_drag || !m_view) return;
    if (m_kind == Eraser) {
        eraseAt(m_view, e.pos);
        return;
    }
    auto* l = dynamic_cast<VectorLayer*>(m_view->doc()->currentLayer());
    if (!l || m_stroke < 0 || m_stroke >= l->strokes.size()) return;
    VectorStroke& s = l->strokes[m_stroke];
    QRectF before = s.bounds();
    QPointF d = e.pos - m_press;
    if (m_kind == Edit) {
        if (m_point < 0) {
            for (int i = 0; i < s.points.size(); ++i) s.points[i].pos = m_orig.points[i].pos + d;
        } else if (m_point < s.points.size()) {
            s.points[m_point].pos = m_orig.points[m_point].pos + d;
        }
    } else if (m_kind == Pressure && m_point >= 0 && m_point < s.points.size()) {
        double dy = (e.widgetPos.y() - m_view->toWidget(m_press).y()) / 120.0;
        s.points[m_point].pressure = float(std::clamp(m_orig.points[m_point].pressure - dy, 0.0, 1.0));
    } else if (m_kind == Weight) {
        double dx = e.widgetPos.x() - m_view->toWidget(m_press).x();
        s.width = float(std::clamp(m_orig.width * std::exp(dx * 0.01), 0.3, 1000.0));
    }
    m_changed = true;
    rerender(m_view, l, before | s.bounds());
}

void VectorEditTool::release(const PointerEvent&) {
    if (!m_drag) return;
    m_drag = false;
    if (!m_changed && m_view) m_view->doc()->revertLastUndo();
    m_stroke = m_point = -1;
}

void VectorEditTool::hover(const PointerEvent& e) {
    auto* l = e.view ? dynamic_cast<VectorLayer*>(e.view->doc()->currentLayer()) : nullptr;
    int h = l ? hitTestStroke(*l, e.pos, 8.0 / e.view->zoom()) : -1;
    m_view = e.view;
    if (h != m_hoverStroke) {
        m_hoverStroke = h;
        Q_EMIT overlayChanged();
    }
}

void VectorEditTool::doubleClick(const PointerEvent& e) {
    if (m_kind != Edit) return;
    VectorLayer* l = vectorLayer(e.view);
    if (!l) return;
    int s = hitTestStroke(*l, e.pos, 8.0 / e.view->zoom());
    if (s < 0) return;
    // insert a control point at the nearest segment
    const auto& pts = l->strokes[s].points;
    int best = -1;
    double bd = 1e18;
    for (int i = 0; i + 1 < pts.size(); ++i) {
        QPointF m = (pts[i].pos + pts[i + 1].pos) / 2;
        double d = QLineF(m, e.pos).length();
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return;
    e.view->doc()->pushUndo(T("Add point"));
    l = static_cast<VectorLayer*>(e.view->doc()->currentLayer());
    auto& p2 = l->strokes[s].points;
    p2.insert(best + 1, {e.pos, (p2[best].pressure + p2[best + 1].pressure) / 2});
    rerender(e.view, l, l->strokes[s].bounds());
}

void VectorEditTool::drawOverlay(QPainter& p, CanvasView* v) {
    auto* l = dynamic_cast<VectorLayer*>(v->doc()->currentLayer());
    if (!l || m_kind == Eraser || m_kind == Color) {
        if (l && m_hoverStroke >= 0 && m_hoverStroke < l->strokes.size() && m_kind == Color) {
            QPolygonF q = v->canvasToWidget().map(QPolygonF(l->strokes[m_hoverStroke].bounds()));
            p.setPen(QPen(Theme::colors().accent, 1, Qt::DashLine));
            p.drawPolygon(q);
        }
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QTransform t = v->canvasToWidget();
    for (int si = 0; si < l->strokes.size(); ++si) {
        const auto& s = l->strokes[si];
        QRectF vb = t.mapRect(s.bounds());
        if (!vb.intersects(QRectF(v->rect()))) continue;
        bool hot = si == m_hoverStroke || si == m_stroke;
        QPainterPath path;
        for (int i = 0; i < s.points.size(); ++i) {
            QPointF w = t.map(s.points[i].pos);
            if (i == 0) path.moveTo(w);
            else path.lineTo(w);
        }
        p.setPen(QPen(hot ? Theme::colors().accent : QColor(70, 140, 255, 120), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        if (!hot && l->strokes.size() > 200) continue;
        for (const auto& pt : s.points) {
            QPointF w = t.map(pt.pos);
            double r = m_kind == Pressure ? 2 + pt.pressure * 4 : 3;
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(hot ? Theme::colors().accent : QColor(255, 255, 255));
            p.drawRect(QRectF(w.x() - r, w.y() - r, r * 2, r * 2));
        }
    }
    p.restore();
}

QWidget* VectorEditTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 6, 6, 6);
    QString text;
    switch (m_kind) {
    case Edit: text = T("Drag points to move them. Ctrl + drag a stroke moves it. Alt + click deletes a point, double-click adds one."); break;
    case Pressure: text = T("Drag a point up or down to change the pressure (thickness) there."); break;
    case Weight: text = T("Drag a stroke left or right to make it thinner or thicker."); break;
    case Color: text = T("Click a stroke to give it the main color."); break;
    case Eraser: {
        text = T("Drag over strokes to delete them.");
        auto* b = new ValueBar(T("Size"), 1, 200, 0, w);
        b->setSuffix(" px");
        b->setValue(eraserSize);
        connect(b, &ValueBar::valueChanged, this, [](double x) { eraserSize = x; });
        v->addWidget(b);
        break;
    }
    }
    auto* hint = new QLabel(text, w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    v->addStretch();
    return w;
}

}  // namespace gx
