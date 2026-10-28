#include "tools/MoveTool.h"

#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "ui/CanvasView.h"
#include "vector/VectorRender.h"

namespace gx {

namespace {
void collectTargets(Layer* l, QVector<quint32>& out) {
    out << l->id;
    if (l->isGroup())
        for (auto& c : static_cast<GroupLayer*>(l)->children) collectTargets(c.get(), out);
}
}  // namespace

void MoveTool::translateLayer(Layer* l, const QPoint& d) {
    if (d.isNull()) return;
    if (l->hasMask) l->mask = l->mask.translated(d.x(), d.y());
    switch (l->type()) {
    case LayerType::Raster: {
        auto* r = static_cast<RasterLayer*>(l);
        r->pixels = r->pixels.translated(d.x(), d.y());
        break;
    }
    case LayerType::Vector: {
        auto* v = static_cast<VectorLayer*>(l);
        for (auto& s : v->strokes)
            for (auto& p : s.points) p.pos += d;
        v->cache = v->cache.translated(d.x(), d.y());
        break;
    }
    case LayerType::Text: {
        auto* t = static_cast<TextLayer*>(l);
        t->text.origin += d;
        t->cache = t->cache.translated(d.x(), d.y());
        break;
    }
    default: break;
    }
}

QRect MoveTool::targetsBounds() const {
    QRect b;
    if (!m_view) return b;
    for (quint32 id : m_targets)
        if (Layer* l = m_view->doc()->layer(id)) b |= m_view->doc()->layerBounds(l);
    return b;
}

void MoveTool::setOffsets(const QPoint& d) {
    if (!m_view) return;
    QRect before = targetsBounds();
    for (quint32 id : m_targets)
        if (Layer* l = m_view->doc()->layer(id)) l->previewOffset = d;
    QRect after = targetsBounds();
    m_view->doc()->pixelsChanged(before | after);
}

void MoveTool::rebuildSelectionPreview(const QPoint& d) {
    Document* doc = m_view->doc();
    auto* rl = static_cast<RasterLayer*>(doc->currentLayer());
    if (!rl || !rl->preview) return;
    RasterSurface moved = m_float.translated(d.x(), d.y());
    RasterSurface preview = m_base;
    QRect r = m_selRect.translated(d) & doc->bounds();
    if (!r.isEmpty()) {
        std::vector<float> a(size_t(r.width()) * r.height() * 4), b(a.size());
        readFloat(preview, r, a.data());
        readFloat(moved, r, b.data());
        blendRow(BlendMode::Normal, a.data(), b.data(), r.width() * r.height());
        writeFloat(preview, r, a.data());
    }
    *rl->preview = std::move(preview);
    QRect changed = m_lastPreview | r | m_selRect;
    m_lastPreview = r;
    doc->pixelsChanged(changed, rl);
}

void MoveTool::press(const PointerEvent& e) {
    if (!e.view) return;
    m_view = e.view;
    Document* doc = e.view->doc();
    Layer* l = doc->currentLayer();
    if (!l) return;
    if (l->isEffectivelyLocked()) {
        app()->showStatus(T("The layer is locked."));
        return;
    }
    m_start = e.pos;
    m_delta = QPoint();
    m_active = true;
    m_selMode = doc->hasSelection() && l->isRaster();
    m_targets.clear();
    if (m_selMode) {
        doc->pushUndo(T("Move selection"));
        auto* rl = static_cast<RasterLayer*>(l);
        m_selRect = doc->selectionBounds();
        m_selOrig = doc->selection();
        m_float = RasterSurface();
        m_base = rl->pixels;
        const QRect r = m_selRect;
        if (!r.isEmpty()) {
            const size_t n = size_t(r.width()) * r.height();
            std::vector<float> px(n * 4), sel(n), fl(n * 4), bs(n * 4);
            readFloat(rl->pixels, r, px.data());
            readMask(doc->selection(), r, sel.data());
            for (size_t i = 0; i < n; ++i)
                for (int k = 0; k < 4; ++k) {
                    fl[i * 4 + size_t(k)] = px[i * 4 + size_t(k)] * sel[i];
                    bs[i * 4 + size_t(k)] = px[i * 4 + size_t(k)] * (1.f - sel[i]);
                }
            writeFloat(m_float, r, fl.data());
            m_float.optimizeRect(r);
            writeFloat(m_base, r, bs.data());
            m_base.optimizeRect(r);
        }
        rl->preview = std::make_shared<RasterSurface>(rl->pixels);
        m_lastPreview = QRect();
    } else {
        doc->pushUndo(T("Move layer"));
        collectTargets(l, m_targets);
    }
}

void MoveTool::move(const PointerEvent& e) {
    if (!m_active || !m_view) return;
    QPointF d = e.pos - m_start;
    if (e.mods & Qt::ShiftModifier) {
        if (std::fabs(d.x()) > std::fabs(d.y())) d.setY(0);
        else d.setX(0);
    }
    QPoint id(int(std::lround(d.x())), int(std::lround(d.y())));
    if (id == m_delta) return;
    m_delta = id;
    if (m_selMode) rebuildSelectionPreview(id);
    else setOffsets(id);
}

void MoveTool::release(const PointerEvent&) {
    if (!m_active || !m_view) return;
    m_active = false;
    Document* doc = m_view->doc();
    if (m_selMode) {
        auto* rl = static_cast<RasterLayer*>(doc->currentLayer());
        if (m_delta.isNull()) {
            if (rl) rl->preview.reset();
            doc->revertLastUndo();
            return;
        }
        if (rl && rl->preview) {
            rl->pixels = std::move(*rl->preview);
            rl->preview.reset();
            rl->pixels.optimize();
        }
        doc->selection() = m_selOrig.translated(m_delta.x(), m_delta.y());
        doc->selection().optimize();
        doc->selectionModified();
        doc->pixelsChanged(m_selRect | m_selRect.translated(m_delta), rl);
        m_float = RasterSurface();
        m_base = RasterSurface();
        return;
    }
    QRect before = targetsBounds();
    for (quint32 id : m_targets)
        if (Layer* l = doc->layer(id)) l->previewOffset = QPoint();
    if (m_delta.isNull()) {
        doc->revertLastUndo();
        return;
    }
    for (quint32 id : m_targets)
        if (Layer* l = doc->layer(id)) {
            translateLayer(l, m_delta);
            doc->pixelsChanged(doc->layerBounds(l), l);
        }
    doc->pixelsChanged(before | targetsBounds());
}

bool MoveTool::keyPress(QKeyEvent* e, CanvasView* v) {
    if (!v || m_active) return false;
    QPoint d;
    int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    switch (e->key()) {
    case Qt::Key_Left: d = QPoint(-step, 0); break;
    case Qt::Key_Right: d = QPoint(step, 0); break;
    case Qt::Key_Up: d = QPoint(0, -step); break;
    case Qt::Key_Down: d = QPoint(0, step); break;
    default: return false;
    }
    Document* doc = v->doc();
    Layer* l = doc->currentLayer();
    if (!l || l->isEffectivelyLocked()) return true;
    doc->pushUndo(T("Nudge layer"), "nudge");
    QVector<quint32> ids;
    collectTargets(l, ids);
    QRect before;
    for (quint32 id : ids)
        if (Layer* t = doc->layer(id)) before |= doc->layerBounds(t);
    for (quint32 id : ids)
        if (Layer* t = doc->layer(id)) translateLayer(t, d);
    QRect after;
    for (quint32 id : ids)
        if (Layer* t = doc->layer(id)) {
            after |= doc->layerBounds(t);
            t->version++;
        }
    doc->pixelsChanged(before | after, l);
    return true;
}

}  // namespace gx
