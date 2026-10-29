#include "tools/BrushTool.h"

#include <cmath>

#include "app/App.h"
#include "app/BrushLibrary.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"

namespace gx {

bool BrushTool::editMask = false;

BrushTool::BrushTool(QObject* p) : Tool("brush", "Brush", "brush", p) {
    connect(&m_air, &QTimer::timeout, this, [this] {
        if (m_stroke) m_stroke->airbrushTick();
    });
}

double BrushTool::outlineDiameter() const {
    BrushLibrary* b = app()->brushes();
    if ((m_hoverEraser || m_usingEraser) && b->eraserIndex() >= 0) return b->at(b->eraserIndex()).size;
    return b->current().size;
}

InputSample BrushTool::toSample(const PointerEvent& e) const {
    InputSample s;
    s.pos = e.pos;
    s.pressure = e.pressure;
    s.tiltX = e.tiltX;
    s.tiltY = e.tiltY;
    s.rotation = e.rotation;
    s.timeMs = e.timeMs;
    return s;
}

void BrushTool::hover(const PointerEvent& e) {
    if (e.tablet && m_hoverEraser != e.eraser) {
        m_hoverEraser = e.eraser;
        Q_EMIT overlayChanged();
    }
}

void BrushTool::press(const PointerEvent& e) {
    if (!e.view) return;
    Document* doc = e.view->doc();
    Layer* l = doc->currentLayer();
    m_view = e.view;
    if (!l) return;
    if (l->type() == LayerType::Vector && !editMask) {
        m_forward = app()->tools()->tool("vpen");
        if (m_forward) m_forward->press(e);
        return;
    }
    const bool maskTarget = editMask && l->hasMask;
    if (!maskTarget) {
        if (l->isGroup()) { app()->showStatus(T("Select a layer to paint on (folders can't be painted).")); return; }
        if (l->type() == LayerType::Adjustment) { app()->showStatus(T("Adjustment layers have no pixels. Paint on their mask instead.")); return; }
        if (l->type() == LayerType::Text) { app()->showStatus(T("Text layers can't be painted. Use Layer > Rasterize first.")); return; }
    }
    if (l->isEffectivelyLocked()) { app()->showStatus(T("The layer is locked.")); return; }
    if (!l->isEffectivelyVisible()) app()->showStatus(T("Painting on a hidden layer."));

    BrushLibrary* lib = app()->brushes();
    m_usingEraser = e.eraser && app()->prefs().eraserEndUsesEraser && lib->eraserIndex() >= 0;
    StrokeContext ctx;
    ctx.doc = doc;
    ctx.layer = l;
    ctx.targetMask = maskTarget;
    ctx.brush = m_usingEraser ? lib->at(lib->eraserIndex()) : lib->current();
    ctx.color = app()->foreground();
    ctx.subColor = app()->background();
    ctx.transparentColor = app()->transparentColor();
    ctx.pressureCurve = &app()->pressureCurve();
    if (doc->symmetry.isOn()) ctx.symmetry = doc->symmetry.transforms();

    m_stab.configure(Stabilizer::Mode(app()->stabilizerMode()), app()->stabilizerLevel(), app()->pressureSmoothing());
    doc->pushUndo(m_usingEraser || ctx.brush.engine == BrushEngine::Erase ? T("Erase") : T("Brush stroke"));
    m_stroke = std::make_unique<BrushStroke>(ctx);

    InputSample s = toSample(e);
    m_snap = doc->perspective.enabled && doc->perspective.snap;
    m_snapTarget = -1;
    m_snapStart = s.pos;
    m_snapHold.clear();

    if ((e.mods & Qt::ShiftModifier) && m_hasLastEnd && m_lastDoc == doc) {
        // Straight line from the end of the previous stroke
        InputSample a = s;
        a.pos = m_lastEnd;
        a.pressure = m_lastPressure;
        m_stroke->begin(a);
        m_stroke->add(s);
        m_stab.begin(s);
    } else {
        feed(m_stab.begin(s));
    }
    if (ctx.brush.airbrush) m_air.start(std::max(5, int(1000.0 / ctx.brush.airbrushRate)));
}

bool BrushTool::constrain(InputSample& s) {
    if (!m_snap || !m_view) return true;
    const Perspective& ps = m_view->doc()->perspective;
    if (m_snapTarget < 0) {
        double d = std::hypot(s.pos.x() - m_snapStart.x(), s.pos.y() - m_snapStart.y());
        if (d * m_view->zoom() < 8.0) return false;  // wait to know the direction
        m_snapTarget = ps.bestTarget(m_snapStart, s.pos);
    }
    s.pos = ps.constrain(m_snapStart, s.pos, m_snapTarget);
    return true;
}

void BrushTool::feed(const std::vector<InputSample>& v) {
    if (!m_stroke) return;
    for (const auto& s0 : v) {
        InputSample s = s0;
        m_stroke->add(s);
    }
}

void BrushTool::move(const PointerEvent& e) {
    if (m_forward) { m_forward->move(e); return; }
    if (!m_stroke) return;
    InputSample s = toSample(e);
    if (!constrain(s)) return;
    feed(m_stab.feed(s));
}

void BrushTool::release(const PointerEvent& e) {
    if (m_forward) {
        m_forward->release(e);
        m_forward = nullptr;
        return;
    }
    m_air.stop();
    if (!m_stroke) return;
    feed(m_stab.finish());
    m_stroke->end();
    if (m_view) {
        m_lastEnd = m_view->toCanvas(e.widgetPos);
        if (m_snap && m_snapTarget >= 0) {
            InputSample s = toSample(e);
            constrain(s);
            m_lastEnd = s.pos;
        }
        m_lastDoc = m_view->doc();
        m_hasLastEnd = true;
    }
    m_lastPressure = std::max(0.3, e.pressure);
    m_stroke.reset();
    m_usingEraser = false;
}

bool BrushTool::keyPress(QKeyEvent* e, CanvasView* v) {
    if (e->key() == Qt::Key_Escape && m_stroke) {
        m_air.stop();
        m_stroke->cancel();
        m_stroke.reset();
        if (v) v->doc()->revertLastUndo();
        return true;
    }
    return false;
}

}  // namespace gx
