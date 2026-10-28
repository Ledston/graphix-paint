#include "tools/SelectTools.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "brush/Dab.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerOps.h"
#include "core/Pixel.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

SelectionOptions& selectionOptions() {
    static SelectionOptions o;
    return o;
}

ops::SelectMode selectModeFor(Qt::KeyboardModifiers m) {
    bool sh = m & Qt::ShiftModifier, alt = m & Qt::AltModifier;
    if (sh && alt) return ops::SelectMode::Intersect;
    if (sh) return ops::SelectMode::Add;
    if (alt) return ops::SelectMode::Subtract;
    return ops::SelectMode(selectionOptions().mode);
}

QWidget* createSelectionOptions(QWidget* parent, bool showAA) {
    auto* w = new QWidget(parent);
    auto* f = new QFormLayout(w);
    f->setContentsMargins(6, 6, 6, 6);
    auto* mode = new QComboBox(w);
    mode->addItems({T("New selection"), T("Add (Shift)"), T("Subtract (Alt)"), T("Intersect (Shift+Alt)")});
    mode->setCurrentIndex(selectionOptions().mode);
    QObject::connect(mode, &QComboBox::currentIndexChanged, w, [](int i) { selectionOptions().mode = i; });
    f->addRow(T("Mode"), mode);
    if (showAA) {
        auto* aa = new QCheckBox(T("Anti-aliased edges"), w);
        aa->setChecked(selectionOptions().antialias);
        QObject::connect(aa, &QCheckBox::toggled, w, [](bool on) { selectionOptions().antialias = on; });
        f->addRow(aa);
    }
    auto* fe = new ValueBar(T("Feather"), 0, 200, 0, w);
    fe->setSuffix(" px");
    fe->setValue(selectionOptions().feather);
    QObject::connect(fe, &ValueBar::valueChanged, w, [](double v) { selectionOptions().feather = v; });
    f->addRow(fe);
    auto* hint = new QLabel(T("Shift adds, Alt subtracts, Shift+Alt intersects."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    f->addRow(hint);
    return w;
}

static void finishSelection(Document* d) {
    if (selectionOptions().feather > 0 && d->hasSelection()) ops::featherSelection(*d, selectionOptions().feather);
}

// ============================================================================ rect / ellipse

QRectF ShapeSelectTool::rectNow() const {
    QPointF a = m_a, b = m_b;
    QPointF d = b - a;
    if (m_mods & Qt::ShiftModifier && !(m_mods & Qt::AltModifier)) {
        // square / circle while adding is ambiguous; use Ctrl for constraining instead
    }
    if (m_mods & Qt::ControlModifier) {
        double s = std::max(std::fabs(d.x()), std::fabs(d.y()));
        d = QPointF(d.x() < 0 ? -s : s, d.y() < 0 ? -s : s);
        b = a + d;
    }
    QRectF r = QRectF(a, b).normalized();
    if (!m_ellipse) r = QRectF(QPointF(std::round(r.left()), std::round(r.top())), QPointF(std::round(r.right()), std::round(r.bottom())));
    return r;
}

void ShapeSelectTool::press(const PointerEvent& e) {
    m_view = e.view;
    m_a = m_b = e.pos;
    m_mods = e.mods;
    m_drag = true;
}

void ShapeSelectTool::move(const PointerEvent& e) {
    if (!m_drag) return;
    m_b = e.pos;
    m_mods = (m_mods & (Qt::ShiftModifier | Qt::AltModifier)) | (e.mods & Qt::ControlModifier);
    Q_EMIT overlayChanged();
}

void ShapeSelectTool::release(const PointerEvent& e) {
    if (!m_drag || !m_view) return;
    m_drag = false;
    m_b = e.pos;
    Document* d = m_view->doc();
    QRectF r = rectNow();
    if (r.width() < 1 || r.height() < 1) {
        if (selectModeFor(m_mods) == ops::SelectMode::Replace) ops::deselect(*d);
        Q_EMIT overlayChanged();
        return;
    }
    QPainterPath p;
    if (m_ellipse) p.addEllipse(r);
    else p.addRect(r);
    ops::selectPath(*d, p, selectModeFor(m_mods), selectionOptions().antialias && m_ellipse, T(name()));
    finishSelection(d);
    Q_EMIT overlayChanged();
}

void ShapeSelectTool::drawOverlay(QPainter& p, CanvasView* v) {
    if (!m_drag || v != m_view) return;
    QRectF r = rectNow();
    p.save();
    p.setTransform(v->canvasToWidget(), true);
    QPen pen(Theme::colors().accent, 0, Qt::DashLine);
    pen.setCosmetic(true);
    p.setPen(pen);
    QColor fill = Theme::colors().accent;
    fill.setAlpha(40);
    p.setBrush(fill);
    if (m_ellipse) p.drawEllipse(r);
    else p.drawRect(r);
    p.restore();
    p.setPen(Qt::white);
    QPointF tl = v->toWidget(r.bottomRight()) + QPointF(8, 14);
    p.drawText(tl, QStringLiteral("%1 x %2").arg(int(r.width())).arg(int(r.height())));
}

// ============================================================================ lasso

void LassoSelectTool::press(const PointerEvent& e) {
    m_view = e.view;
    if (m_poly.isEmpty()) m_mods = e.mods;
    m_drag = true;
    m_dragged = false;
    m_poly << e.pos;
    Q_EMIT overlayChanged();
}

void LassoSelectTool::move(const PointerEvent& e) {
    if (!m_drag) return;
    if (!m_poly.isEmpty() && QLineF(m_poly.back(), e.pos).length() * (m_view ? m_view->zoom() : 1) < 2) return;
    m_poly << e.pos;
    m_dragged = true;
    Q_EMIT overlayChanged();
}

void LassoSelectTool::release(const PointerEvent&) {
    m_drag = false;
    if (m_dragged) finish();  // freehand: close on release
}

void LassoSelectTool::hover(const PointerEvent& e) {
    m_hover = e.pos;
    if (!m_poly.isEmpty()) Q_EMIT overlayChanged();
}

void LassoSelectTool::doubleClick(const PointerEvent&) { finish(); }

bool LassoSelectTool::keyPress(QKeyEvent* e, CanvasView*) {
    if (m_poly.isEmpty()) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { finish(); return true; }
    if (e->key() == Qt::Key_Escape) { m_poly.clear(); Q_EMIT overlayChanged(); return true; }
    if (e->key() == Qt::Key_Backspace) { m_poly.removeLast(); Q_EMIT overlayChanged(); return true; }
    return false;
}

void LassoSelectTool::finish() {
    QPolygonF poly = m_poly;
    m_poly.clear();
    Q_EMIT overlayChanged();
    if (!m_view || poly.size() < 3) return;
    apply(m_view, poly, m_mods);
}

void LassoSelectTool::apply(CanvasView* v, const QPolygonF& poly, Qt::KeyboardModifiers mods) {
    QPainterPath p;
    p.addPolygon(poly);
    p.closeSubpath();
    ops::selectPath(*v->doc(), p, selectModeFor(mods), selectionOptions().antialias, T(name()));
    finishSelection(v->doc());
}

void LassoSelectTool::drawOverlay(QPainter& p, CanvasView* v) {
    if (m_poly.isEmpty() || v != m_view) return;
    p.save();
    p.setTransform(v->canvasToWidget(), true);
    QPen pen(Theme::colors().accent, 0);
    pen.setCosmetic(true);
    p.setPen(pen);
    QColor fill = Theme::colors().accent;
    fill.setAlpha(40);
    p.setBrush(fill);
    QPolygonF poly = m_poly;
    if (!m_drag) poly << m_hover;
    p.drawPolygon(poly);
    p.restore();
}

// ============================================================================ flood fill

MaskSurface floodFill(Document* doc, const QPoint& seed, int tol, bool merged, bool contiguous, const Layer* layer, QString* error) {
    return floodFillEx(doc, seed, tol, merged ? FillSource::AllVisible : FillSource::CurrentLayer, contiguous, layer, 0, error);
}

MaskSurface floodFillEx(Document* doc, const QPoint& seed, int tol, FillSource source, bool contiguous, const Layer* layer, int gapClose,
                        QString* error) {
    const bool merged = source == FillSource::AllVisible;
    MaskSurface out(0);
    QRect area = doc->hasSelection() ? doc->selectionBounds() : doc->bounds();
    if (!area.contains(seed)) return out;
    const qint64 W = area.width(), H = area.height();
    if (W * H > 400ll * 1000 * 1000) {
        if (error) *error = "The area is too large to fill at once. Select a smaller area first.";
        return out;
    }
    std::vector<quint32> px(size_t(W * H));
    if (merged) {
        // Use the projection (the image exactly as shown)
        Projection* pr = doc->projection();
        TileRange tr = tileRangeFor(area);
        QVector<qint64> keys;
        for (int ty = tr.y0; ty <= tr.y1; ++ty)
            for (int tx = tr.x0; tx <= tr.x1; ++tx) keys << tileKey(tx, ty);
        for (int guard = 0; guard < 1000 && !pr->update(0, keys, 1e9); ++guard) {}
        for (qint64 k : keys) {
            const Projection::Entry* e = pr->entry(0, k);
            QRect r = tileRectForKey(k) & area;
            for (int y = r.top(); y <= r.bottom(); ++y)
                for (int x = r.left(); x <= r.right(); ++x)
                    px[size_t(y - area.top()) * W + (x - area.left())] =
                        (e && e->tile) ? e->tile->at(((y & (TILE - 1)) << TILE_SHIFT) | (x & (TILE - 1))) : 0u;
        }
    } else if (source == FillSource::ReferenceLayers) {
        // only the layers marked as reference (line art), shown or not
        std::vector<LayerPtr> refs;
        forEachLayer(doc->root(), [&](Layer* l) {
            if (l->reference && !l->isGroup()) {
                LayerPtr c = l->clone();
                c->visible = true;
                c->clipping = false;
                refs.push_back(c);
            }
            return true;
        });
        if (refs.empty()) {
            if (error) *error = "No reference layer yet: right-click the line art layer and choose \"Reference layer\".";
            return out;
        }
        RasterSurface rs = ops::renderLayers(*doc, refs, area);
        rs.readRect(area, px.data(), int(W));
    } else if (layer && layer->displaySurface()) {
        layer->displaySurface()->readRect(area.translated(-layer->previewOffset), px.data(), int(W));
    }
    const quint32 ref = px[size_t(seed.y() - area.top()) * W + (seed.x() - area.left())];
    auto match = [&](quint32 c) {
        if (c == ref) return true;
        for (int s = 0; s < 32; s += 8) {
            int a = int((c >> s) & 255u), b = int((ref >> s) & 255u);
            if (std::abs(a - b) > tol) return false;
        }
        return true;
    };
    std::vector<uint8_t> fill(size_t(W * H), 0);
    // Closing gaps: the borders are made thicker so small breaks close, the fill
    // happens, and then it grows back by the same amount under the borders.
    std::vector<uint8_t> wall;
    if (gapClose > 0 && contiguous) {
        std::vector<uint8_t> border(size_t(W * H));
        for (size_t i = 0; i < border.size(); ++i) border[i] = match(px[i]) ? 0 : 1;
        std::vector<float> d2;
        distanceTransform(border, int(W), int(H), d2);
        const float r2 = float(gapClose) * float(gapClose) * 0.25f;  // gap width -> half on each side
        wall.resize(border.size());
        for (size_t i = 0; i < wall.size(); ++i) wall[i] = d2[i] <= r2 ? 1 : 0;
        if (wall[size_t(seed.y() - area.top()) * W + (seed.x() - area.left())]) wall.clear();  // clicked right on a border
    }
    auto open = [&](size_t i) { return wall.empty() ? match(px[i]) : !wall[i]; };
    if (!contiguous) {
        for (size_t i = 0; i < px.size(); ++i) fill[i] = match(px[i]) ? 1 : 0;
    } else {
        std::vector<std::pair<int, int>> stack;
        stack.push_back({seed.x() - area.left(), seed.y() - area.top()});
        while (!stack.empty()) {
            auto [x, y] = stack.back();
            stack.pop_back();
            size_t row = size_t(y) * W;
            if (fill[row + x] || !open(row + x)) continue;
            int l = x, r = x;
            while (l > 0 && !fill[row + l - 1] && open(row + l - 1)) --l;
            while (r < W - 1 && !fill[row + r + 1] && open(row + r + 1)) ++r;
            for (int i = l; i <= r; ++i) fill[row + i] = 1;
            for (int ny : {y - 1, y + 1}) {
                if (ny < 0 || ny >= H) continue;
                size_t nrow = size_t(ny) * W;
                bool in = false;
                for (int i = l; i <= r; ++i) {
                    bool ok = !fill[nrow + i] && open(nrow + i);
                    if (ok && !in) { stack.push_back({i, ny}); in = true; }
                    else if (!ok) in = false;
                }
            }
        }
    }
    if (!wall.empty()) {
        // grow back into the thickened border, but only over open pixels
        std::vector<uint8_t> grown = fill;
        std::vector<float> d2;
        distanceTransform(fill, int(W), int(H), d2);
        const float r2 = (float(gapClose) * 0.5f + 1.f) * (float(gapClose) * 0.5f + 1.f);
        for (size_t i = 0; i < fill.size(); ++i)
            if (!fill[i] && d2[i] <= r2 && match(px[i])) grown[i] = 1;
        fill.swap(grown);
    }
    std::vector<quint8> m(fill.size());
    for (size_t i = 0; i < fill.size(); ++i) m[i] = fill[i] ? 255 : 0;
    out.writeRect(area, m.data(), int(W));
    out.optimize();
    return out;
}

// ============================================================================ wand

void MagicWandTool::press(const PointerEvent& e) {
    if (!e.view) return;
    Document* d = e.view->doc();
    QPoint seed(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())));
    if (!d->bounds().contains(seed)) return;
    QString err;
    bool hadSel = d->hasSelection();
    // the wand ignores the current selection as a limit when replacing
    MaskSurface keep = d->selection();
    if (selectModeFor(e.mods) == ops::SelectMode::Replace && hadSel) d->setHasSelection(false);
    MaskSurface m = floodFill(d, seed, tolerance, sampleMerged, contiguous, d->currentLayer(), &err);
    if (selectModeFor(e.mods) == ops::SelectMode::Replace && hadSel) {
        d->selection() = keep;
        d->setHasSelection(true);
    }
    if (!err.isEmpty()) {
        app()->showStatus(T(err));
        return;
    }
    ops::combineSelection(*d, m, selectModeFor(e.mods), T("Magic wand"));
    finishSelection(d);
}

QWidget* MagicWandTool::createOptions(QWidget* parent) {
    auto* w = createSelectionOptions(parent, false);
    auto* f = static_cast<QFormLayout*>(w->layout());
    auto* tol = new ValueBar(T("Tolerance"), 0, 255, 0, w);
    tol->setValue(tolerance);
    connect(tol, &ValueBar::valueChanged, this, [this](double v) { tolerance = int(v); });
    f->insertRow(0, tol);
    auto* src = new QComboBox(w);
    src->addItems({T("All visible layers"), T("Current layer")});
    src->setCurrentIndex(sampleMerged ? 0 : 1);
    connect(src, &QComboBox::currentIndexChanged, this, [this](int i) { sampleMerged = i == 0; });
    f->insertRow(1, T("Sample"), src);
    auto* cont = new QCheckBox(T("Contiguous"), w);
    cont->setChecked(contiguous);
    connect(cont, &QCheckBox::toggled, this, [this](bool on) { contiguous = on; });
    f->insertRow(2, cont);
    return w;
}

// ============================================================================ selection pen

void SelectionBrushTool::press(const PointerEvent& e) {
    if (!e.view) return;
    m_view = e.view;
    Document* d = e.view->doc();
    m_erase = (e.mods & Qt::AltModifier) || e.eraser;
    d->pushUndo(m_erase ? T("Erase selection") : T("Paint selection"));
    if (!d->hasSelection()) {
        d->selection().clear();
        d->setHasSelection(true);
    }
    m_drag = true;
    m_last = e.pos;
    m_residual = 0;
    m_dirty = QRect();
    dab(e.pos, e.pressure);
}

void SelectionBrushTool::dab(const QPointF& p, double pressure) {
    Document* d = m_view->doc();
    DabParams dp;
    dp.cx = p.x();
    dp.cy = p.y();
    dp.diameter = size * (0.3 + 0.7 * app()->pressureCurve().map(float(pressure)));
    dp.hardness = hardness;
    QRect box = dabBounds(dp) & d->bounds();
    if (box.isEmpty()) return;
    std::vector<float> cov(size_t(box.width()) * box.height());
    if (!rasterizeDab(dp, box, cov.data())) return;
    std::vector<quint8> m(cov.size());
    d->selection().readRect(box, m.data(), box.width());
    for (size_t i = 0; i < m.size(); ++i) {
        float v = m[i] / 255.f, c = cov[i];
        v = m_erase ? v * (1.f - c) : std::max(v, c);
        m[i] = quint8(std::clamp(int(v * 255.f + 0.5f), 0, 255));
    }
    d->selection().writeRect(box, m.data(), box.width());
    m_dirty |= box;
}

void SelectionBrushTool::move(const PointerEvent& e) {
    if (!m_drag || !m_view) return;
    double step = std::max(1.0, size * 0.15);
    QPointF a = m_last, b = e.pos;
    double len = QLineF(a, b).length();
    double pos = step - m_residual;
    while (pos <= len) {
        dab(a + (b - a) * (pos / len), e.pressure);
        pos += step;
    }
    m_residual = len - (pos - step);
    m_last = b;
    m_view->doc()->selectionModified();
}

void SelectionBrushTool::release(const PointerEvent&) {
    if (!m_drag || !m_view) return;
    m_drag = false;
    Document* d = m_view->doc();
    d->selection().optimize();
    d->setHasSelection(!d->selection().isEmpty());
    d->selectionModified();
}

QWidget* SelectionBrushTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 6, 6, 6);
    auto* s = new ValueBar(T("Size"), 1, 2000, 0, w);
    s->setLogarithmic(true);
    s->setSuffix(" px");
    s->setValue(size);
    connect(s, &ValueBar::valueChanged, this, [this](double x) { size = x; });
    auto* h = new ValueBar(T("Hardness"), 0, 1, 2, w);
    h->setDisplayScale(100);
    h->setSuffix("%");
    h->setValue(hardness);
    connect(h, &ValueBar::valueChanged, this, [this](double x) { hardness = x; });
    v->addWidget(s);
    v->addWidget(h);
    auto* hint = new QLabel(T("Paint to add to the selection, hold Alt (or use the pen eraser) to remove."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    v->addStretch();
    return w;
}

}  // namespace gx
