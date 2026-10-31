#include "tools/TransformTool.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerOps.h"
#include "core/Pixel.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

// ============================================================================ FloatingPixels

RasterLayer* FloatingPixels::layer() const { return doc ? dynamic_cast<RasterLayer*>(doc->layer(layerId)) : nullptr; }

bool FloatingPixels::begin(Document* d, RasterLayer* l, const QString& undoName, bool wholeCanvasIfEmpty, QString* error) {
    if (!d || !l) return false;
    if (l->isEffectivelyLocked()) {
        if (error) *error = "The layer is locked.";
        return false;
    }
    hadSelection = d->hasSelection();
    QRect r = hadSelection ? d->selectionBounds() : (l->pixels.exactBounds() & d->bounds().adjusted(-4096, -4096, 4096, 4096));
    if (wholeCanvasIfEmpty && !hadSelection) r = d->bounds();
    if (r.isEmpty()) {
        if (error) *error = hadSelection ? "The selection is empty." : "The layer is empty.";
        return false;
    }
    d->pushUndo(undoName);
    l = static_cast<RasterLayer*>(d->layer(l->id));
    doc = d;
    layerId = l->id;
    srcRect = r;
    selection = d->selection();
    src = surfaceToImage(l->pixels, r);
    if (src.isNull()) {
        if (error) *error = "Not enough memory.";
        d->revertLastUndo();
        return false;
    }
    base = l->pixels;
    if (hadSelection) {
        const size_t n = size_t(r.width()) * r.height();
        std::vector<float> px(n * 4), m(n);
        readFloat(l->pixels, r, px.data());
        readMask(selection, r, m.data());
        std::vector<float> keep(n * 4);
        for (size_t i = 0; i < n; ++i)
            for (int k = 0; k < 4; ++k) {
                keep[i * 4 + size_t(k)] = px[i * 4 + size_t(k)] * (1.f - m[i]);
                px[i * 4 + size_t(k)] *= m[i];
            }
        writeFloat(base, r, keep.data());
        base.optimizeRect(r);
        for (int y = 0; y < r.height(); ++y) {
            quint32* row = reinterpret_cast<quint32*>(src.scanLine(y));
            for (int x = 0; x < r.width(); ++x) row[x] = pack(&px[(size_t(y) * r.width() + x) * 4]);
        }
    } else {
        base.fillRect(r, 0);
    }
    l->preview = std::make_shared<RasterSurface>(l->pixels);
    lastPreview = QRect();
    active = true;
    return true;
}

void FloatingPixels::showImage(const QImage& img, const QPoint& at) {
    RasterLayer* l = layer();
    if (!l || !l->preview) return;
    RasterSurface preview = base;
    QRect r(at, img.size());
    if (!img.isNull()) {
        RasterSurface over;
        imageToSurface(img, over, at);
        for (auto it = over.tiles().cbegin(); it != over.tiles().cend(); ++it) {
            QRect tr = tileRectForKey(it.key()) & r;
            if (!preview.tile(keyTx(it.key()), keyTy(it.key()))) {
                preview.setTile(it.key(), it.value());
                continue;
            }
            std::vector<float> a(size_t(tr.width()) * tr.height() * 4), b(a.size());
            readFloat(preview, tr, a.data());
            readFloat(over, tr, b.data());
            blendRow(BlendMode::Normal, a.data(), b.data(), tr.width() * tr.height());
            writeFloat(preview, tr, a.data());
        }
    }
    *l->preview = std::move(preview);
    QRect changed = lastPreview | r | srcRect;
    lastPreview = r;
    doc->pixelsChanged(changed, l);
}

void FloatingPixels::commit() {
    RasterLayer* l = layer();
    active = false;
    if (!l || !l->preview) return;
    l->pixels = std::move(*l->preview);
    l->preview.reset();
    l->pixels.optimize();
    doc->pixelsChanged(lastPreview | srcRect, l);
    src = QImage();
    base = RasterSurface();
}

void FloatingPixels::cancel() {
    if (!active) return;
    active = false;
    if (RasterLayer* l = layer()) l->preview.reset();
    if (doc) doc->revertLastUndo();
    src = QImage();
    base = RasterSurface();
}

// ============================================================================ TransformTool

TransformTool::TransformTool(QObject* p) : Tool("transform", "Transform", "transform", p) {
    m_throttle.setSingleShot(true);
    m_throttle.setInterval(16);
    connect(&m_throttle, &QTimer::timeout, this, [this] { updatePreview(false); });
}

bool TransformTool::start() {
    CanvasView* v = app()->activeView();
    if (!v) return false;
    Document* d = v->doc();
    Layer* l = d->currentLayer();
    if (!l) return false;
    if (l->isEffectivelyLocked()) {
        app()->showStatus(T("The layer is locked."));
        return false;
    }
    if (l->type() == LayerType::Adjustment) {
        app()->showStatus(T("Adjustment layers can't be transformed."));
        return false;
    }
    m_view = v;
    m_doc = d;
    m_layerId = l->id;
    m_originals.clear();
    auto contentBounds = [d](Layer* x) {
        QRect b = d->layerBounds(x);
        if (x->type() == LayerType::Text) b = x->displaySurface()->exactBounds();
        if (x->type() == LayerType::Vector) {
            b = QRect();
            for (const auto& s : static_cast<VectorLayer*>(x)->strokes) b |= s.bounds().toAlignedRect();
        }
        if (x->isGroup()) {
            b = QRect();
            forEachLayer(static_cast<GroupLayer*>(x), [&](Layer* c) {
                if (const RasterSurface* s = c->displaySurface()) b |= s->exactBounds();
                return true;
            });
        }
        return b;
    };
    const QList<quint32> many = d->topLevelSelectedIds();
    if (many.size() > 1) {
        // several selected layers: all of them get the same transformation
        QRect b;
        for (quint32 id : many)
            if (Layer* x = d->layer(id); x && x->type() != LayerType::Adjustment && !x->isEffectivelyLocked()) b |= contentBounds(x);
        if (b.isEmpty()) {
            app()->showStatus(T("The layer is empty."));
            return false;
        }
        d->pushUndo(T("Transform"));
        for (quint32 id : many)
            if (Layer* x = d->layer(id); x && x->type() != LayerType::Adjustment && !x->isEffectivelyLocked())
                m_originals.emplace_back(id, x->clone());
        m_rasterMode = false;
        m_src = QRectF(b);
    } else if (l->isRaster()) {
        QString err;
        if (!m_float.begin(d, static_cast<RasterLayer*>(l), T("Transform"), false, &err)) {
            app()->showStatus(T(err));
            return false;
        }
        m_rasterMode = true;
        m_src = QRectF(m_float.srcRect);
    } else {
        QRect b = contentBounds(l);
        if (b.isEmpty()) {
            app()->showStatus(T("The layer is empty."));
            return false;
        }
        d->pushUndo(T("Transform"));
        l = d->layer(m_layerId);
        m_originals.emplace_back(m_layerId, l->clone());
        m_rasterMode = false;
        m_src = QRectF(b);
    }
    m_quad = QPolygonF({m_src.topLeft(), m_src.topRight(), m_src.bottomRight(), m_src.bottomLeft()});
    m_active = true;
    Q_EMIT overlayChanged();
    return true;
}

void TransformTool::activate() {
    if (!m_active) start();
}

void TransformTool::deactivate() {
    if (m_active) apply();
}

QTransform TransformTool::currentTransform() const {
    QTransform t;
    QPolygonF src({m_src.topLeft(), m_src.topRight(), m_src.bottomRight(), m_src.bottomLeft()});
    if (!QTransform::quadToQuad(src, m_quad, t)) return QTransform();
    return t;
}

QPointF TransformTool::center() const {
    QPointF c(0, 0);
    for (const QPointF& p : m_quad) c += p;
    return c / 4.0;
}

void TransformTool::updatePreview(bool final) {
    if (!m_active || !m_doc) return;
    QTransform t = currentTransform();
    Document* d = m_doc;
    if (m_rasterMode) {
        QRect limit = d->bounds().adjusted(-d->size().width(), -d->size().height(), d->size().width(), d->size().height());
        QRect dr = t.mapRect(m_src).toAlignedRect().adjusted(-1, -1, 1, 1) & limit;
        if (dr.isEmpty()) {
            m_float.showImage(QImage(), QPoint());
            return;
        }
        QImage dst(dr.size(), QImage::Format_ARGB32_Premultiplied);
        if (dst.isNull()) return;
        dst.fill(0);
        {
            QPainter p(&dst);
            bool sm = smooth && (final || qint64(m_float.src.width()) * m_float.src.height() < 3000000);
            p.setRenderHint(QPainter::SmoothPixmapTransform, sm);
            p.setRenderHint(QPainter::Antialiasing, sm);
            p.setTransform(t * QTransform::fromTranslate(-dr.left(), -dr.top()));
            p.drawImage(m_src.topLeft(), m_float.src);
        }
        m_float.showImage(dst, dr.topLeft());
    } else {
        for (const auto& [id, original] : m_originals) {
            Layer* cur = d->layer(id);
            if (!cur || !original || !cur->parent) continue;
            QRect before = d->layerBounds(cur);
            LayerPtr fresh = original->clone();
            ops::transformLayerContent(*fresh, t, smooth);
            GroupLayer* parent = cur->parent;
            int idx = parent->indexOf(cur);
            parent->take(idx);
            parent->insert(idx, fresh);
            d->pixelsChanged(before | d->layerBounds(fresh.get()), fresh.get());
        }
    }
    Q_EMIT overlayChanged();
}

int TransformTool::hitTest(const QPointF& w, CanvasView* v) const {
    if (!m_active || !v) return 0;
    QPolygonF q = v->canvasToWidget().map(m_quad);
    for (int i = 0; i < 4; ++i)
        if (QLineF(q[i], w).length() < 9) return 1 + i;
    for (int i = 0; i < 4; ++i)
        if (QLineF((q[i] + q[(i + 1) % 4]) / 2, w).length() < 9) return 5 + i;
    if (q.containsPoint(w, Qt::OddEvenFill)) return 9;
    return 10;
}

void TransformTool::hover(const PointerEvent& e) {
    int h = hitTest(e.widgetPos, e.view);
    if (h != m_hover) {
        m_hover = h;
        if (e.view) e.view->refreshCursor();
    }
}

QCursor TransformTool::cursor(CanvasView*) const {
    switch (m_hover) {
    case 1: case 3: return Qt::SizeFDiagCursor;
    case 2: case 4: return Qt::SizeBDiagCursor;
    case 5: case 7: return Qt::SizeVerCursor;
    case 6: case 8: return Qt::SizeHorCursor;
    case 9: return Qt::SizeAllCursor;
    case 10: return Qt::CrossCursor;
    default: return Qt::ArrowCursor;
    }
}

void TransformTool::press(const PointerEvent& e) {
    if (!m_active) {
        if (!start()) return;
    }
    if (e.view && e.view->doc() != m_doc) return;
    m_view = e.view;
    m_dragMode = hitTest(e.widgetPos, e.view);
    m_pressPos = e.pos;
    m_startQuad = m_quad;
    m_mods = e.mods;
}

void TransformTool::move(const PointerEvent& e) {
    if (!m_dragMode) {
        hover(e);
        return;
    }
    QPointF delta = e.pos - m_pressPos;
    const QPolygonF& s = m_startQuad;
    const bool ctrl = e.mods & Qt::ControlModifier;
    const bool shift = e.mods & Qt::ShiftModifier;
    if (m_dragMode == 9) {
        for (int i = 0; i < 4; ++i) m_quad[i] = s[i] + delta;
    } else if (m_dragMode == 10) {
        QPointF c(0, 0);
        for (const QPointF& p : s) c += p;
        c /= 4.0;
        double a0 = std::atan2(m_pressPos.y() - c.y(), m_pressPos.x() - c.x());
        double a1 = std::atan2(e.pos.y() - c.y(), e.pos.x() - c.x());
        double da = a1 - a0;
        if (shift) da = std::round(da / (M_PI / 12)) * (M_PI / 12);
        QTransform r;
        r.translate(c.x(), c.y());
        r.rotateRadians(da);
        r.translate(-c.x(), -c.y());
        for (int i = 0; i < 4; ++i) m_quad[i] = r.map(s[i]);
    } else if (m_dragMode >= 1 && m_dragMode <= 4) {
        int k = m_dragMode - 1;
        if (ctrl || mode == Distort) {
            m_quad[k] = s[k] + delta;
        } else if (mode == Perspective || mode == Skew) {
            // the corner moves along one of its edges; in perspective its neighbour on that edge
            // moves the other way (a trapezoid), in skew the neighbour moves the same way
            const int nh = k ^ 1, nv = 3 - k;  // neighbours on the top / bottom edge and on the side edge
            QPointF eh = s[nh] - s[k], ev = s[nv] - s[k];
            const double lh = std::hypot(eh.x(), eh.y()), lv = std::hypot(ev.x(), ev.y());
            if (lh > 1e-9 && lv > 1e-9) {
                eh /= lh;
                ev /= lv;
                const double ah = delta.x() * eh.x() + delta.y() * eh.y(), av = delta.x() * ev.x() + delta.y() * ev.y();
                const bool alongH = std::fabs(ah) >= std::fabs(av);
                const QPointF dir = alongH ? eh : ev;
                const double a = alongH ? ah : av;
                const int n = alongH ? nh : nv;
                for (int i = 0; i < 4; ++i) m_quad[i] = s[i];
                m_quad[k] = s[k] + dir * a;
                m_quad[n] = s[n] + dir * (mode == Perspective ? -a : a);
            }
        } else {
            QPointF O = s[(k + 2) % 4];
            QPointF u = s[(k + 1) % 4] - O, v = s[(k + 3) % 4] - O;
            QPointF P = s[k] + delta - O;
            double det = u.x() * v.y() - u.y() * v.x();
            if (std::fabs(det) > 1e-9) {
                double a = (P.x() * v.y() - P.y() * v.x()) / det;
                double b = (u.x() * P.y() - u.y() * P.x()) / det;
                if (shift) {
                    double m = (std::fabs(a) + std::fabs(b)) / 2;
                    a = a < 0 ? -m : m;
                    b = b < 0 ? -m : m;
                }
                m_quad[(k + 2) % 4] = O;
                m_quad[(k + 1) % 4] = O + u * a;
                m_quad[(k + 3) % 4] = O + v * b;
                m_quad[k] = O + u * a + v * b;
            }
        }
    } else if (m_dragMode >= 5 && m_dragMode <= 8) {
        int ed = m_dragMode - 5;
        int c0 = ed, c1 = (ed + 1) % 4, p0 = (ed + 3) % 4, p1 = (ed + 2) % 4;
        if (ctrl) {
            QPointF dir = s[c1] - s[c0];
            double len = std::hypot(dir.x(), dir.y());
            if (len > 1e-9) {
                dir /= len;
                QPointF d = dir * (delta.x() * dir.x() + delta.y() * dir.y());
                m_quad[c0] = s[c0] + d;
                m_quad[c1] = s[c1] + d;
            }
        } else {
            QPointF mo = (s[p0] + s[p1]) / 2, mt = (s[c0] + s[c1]) / 2;
            QPointF w = mt - mo;
            double l2 = w.x() * w.x() + w.y() * w.y();
            if (l2 > 1e-9) {
                QPointF P = mt + delta - mo;
                double f = (P.x() * w.x() + P.y() * w.y()) / l2;
                m_quad[c0] = s[p0] + (s[c0] - s[p0]) * f;
                m_quad[c1] = s[p1] + (s[c1] - s[p1]) * f;
            }
        }
    }
    if (!m_throttle.isActive()) m_throttle.start();
    Q_EMIT overlayChanged();
}

void TransformTool::release(const PointerEvent&) {
    if (!m_dragMode) return;
    m_dragMode = 0;
    m_throttle.stop();
    updatePreview(true);
}

bool TransformTool::keyPress(QKeyEvent* e, CanvasView*) {
    if (!m_active) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { apply(); return true; }
    if (e->key() == Qt::Key_Escape) { cancel(); return true; }
    return false;
}

void TransformTool::flip(bool horizontal) {
    if (!m_active && !start()) return;
    if (horizontal) m_quad = QPolygonF({m_quad[1], m_quad[0], m_quad[3], m_quad[2]});
    else m_quad = QPolygonF({m_quad[3], m_quad[2], m_quad[1], m_quad[0]});
    updatePreview(true);
}

void TransformTool::rotate90(bool cw) {
    if (!m_active && !start()) return;
    QPointF c = center();
    QTransform r;
    r.translate(c.x(), c.y());
    r.rotate(cw ? 90 : -90);
    r.translate(-c.x(), -c.y());
    for (int i = 0; i < 4; ++i) m_quad[i] = r.map(m_quad[i]);
    updatePreview(true);
}

void TransformTool::reset() {
    if (!m_active) return;
    m_quad = QPolygonF({m_src.topLeft(), m_src.topRight(), m_src.bottomRight(), m_src.bottomLeft()});
    updatePreview(true);
}

void TransformTool::apply() {
    if (!m_active) return;
    m_throttle.stop();
    updatePreview(true);
    m_active = false;
    if (m_rasterMode) {
        QTransform t = currentTransform();
        bool hadSel = m_float.hadSelection;
        MaskSurface sel = m_float.selection;
        m_float.commit();
        if (hadSel && m_doc) {
            ops::transformMask(sel, t, smooth);
            m_doc->selection() = sel;
            m_doc->setHasSelection(!sel.isEmpty());
            m_doc->selectionModified();
        }
    } else {
        m_originals.clear();
    }
    Q_EMIT overlayChanged();
}

void TransformTool::cancel() {
    if (!m_active) return;
    m_throttle.stop();
    m_active = false;
    if (m_rasterMode) m_float.cancel();
    else if (m_doc) {
        m_originals.clear();
        m_doc->revertLastUndo();
    }
    Q_EMIT overlayChanged();
}

void TransformTool::drawOverlay(QPainter& p, CanvasView* v) {
    if (!m_active || !m_doc || v->doc() != m_doc) return;
    QPolygonF q = v->canvasToWidget().map(m_quad);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 140), 3));
    p.drawPolygon(q);
    p.setPen(QPen(Theme::colors().accent, 1.4));
    p.drawPolygon(q);
    auto handle = [&](const QPointF& c) {
        p.setPen(QPen(QColor(0, 0, 0, 160), 1));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
    };
    for (int i = 0; i < 4; ++i) {
        handle(q[i]);
        handle((q[i] + q[(i + 1) % 4]) / 2);
    }
    QPointF c = v->toWidget(center());
    p.setPen(QPen(Theme::colors().accent, 1.2));
    p.drawLine(c - QPointF(5, 0), c + QPointF(5, 0));
    p.drawLine(c - QPointF(0, 5), c + QPointF(0, 5));
    p.restore();
}

QWidget* TransformTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 6, 6, 6);
    auto* modeBox = new QComboBox(w);
    modeBox->addItems({T("Corners scale"), T("Corners distort freely"), T("Perspective"), T("Skew")});
    modeBox->setCurrentIndex(mode);
    modeBox->setToolTip(T("What dragging a corner does. Perspective: the corner and its neighbour move toward or away from each other."));
    connect(modeBox, &QComboBox::currentIndexChanged, this, [this](int i) { mode = i; });
    auto* mrow = new QHBoxLayout;
    mrow->addWidget(new QLabel(T("Mode"), w));
    mrow->addWidget(modeBox, 1);
    v->addLayout(mrow);
    auto* sm = new QCheckBox(T("Smooth resampling"), w);
    sm->setChecked(smooth);
    connect(sm, &QCheckBox::toggled, this, [this](bool on) { smooth = on; updatePreview(true); });
    v->addWidget(sm);
    auto* g = new QGridLayout;
    auto button = [&](const QString& icon, const QString& text, int r, int c, std::function<void()> fn) {
        auto* b = new QPushButton(Theme::icon(icon), text, w);
        connect(b, &QPushButton::clicked, this, fn);
        g->addWidget(b, r, c);
    };
    button("flip", T("Flip H"), 0, 0, [this] { flip(true); });
    button("flip", T("Flip V"), 0, 1, [this] { flip(false); });
    button("rotate", T("90° left"), 1, 0, [this] { rotate90(false); });
    button("rotate", T("90° right"), 1, 1, [this] { rotate90(true); });
    button("reset", T("Reset"), 2, 0, [this] { reset(); });
    v->addLayout(g);
    auto* row = new QHBoxLayout;
    auto* ok = new QPushButton(Theme::icon("check"), T("Apply"), w);
    ok->setToolTip("Enter");
    auto* cancelB = new QPushButton(T("Cancel"), w);
    cancelB->setToolTip("Esc");
    connect(ok, &QPushButton::clicked, this, [this] { apply(); });
    connect(cancelB, &QPushButton::clicked, this, [this] { cancel(); });
    row->addWidget(ok);
    row->addWidget(cancelB);
    v->addLayout(row);
    auto* hint = new QLabel(T("Drag inside to move, outside to rotate (Shift: 15° steps). Corners do what the mode says (Shift keeps "
                              "proportions when scaling). Ctrl + corner always distorts freely, Ctrl + edge skews. Click the canvas to start again."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    v->addStretch();
    return w;
}

// ============================================================================ WarpTool

WarpTool::WarpTool(QObject* p) : Tool("warp", "Mesh warp", "warp", p) {
    m_throttle.setSingleShot(true);
    m_throttle.setInterval(20);
    connect(&m_throttle, &QTimer::timeout, this, [this] { render(false); });
}

bool WarpTool::start() {
    CanvasView* v = app()->activeView();
    if (!v) return false;
    Document* d = v->doc();
    auto* l = dynamic_cast<RasterLayer*>(d->currentLayer());
    if (!l) {
        app()->showStatus(T("Mesh warp works on pixel layers."));
        return false;
    }
    QString err;
    if (!m_float.begin(d, l, T("Mesh warp"), false, &err)) {
        app()->showStatus(T(err));
        return false;
    }
    m_view = v;
    m_active = true;
    resetGrid();
    return true;
}

void WarpTool::resetGrid() {
    m_nu = m_nv = cells + 1;
    m_us.resize(size_t(m_nu));
    m_vs.resize(size_t(m_nv));
    for (int i = 0; i < m_nu; ++i) m_us[size_t(i)] = double(i) / cells;
    for (int j = 0; j < m_nv; ++j) m_vs[size_t(j)] = double(j) / cells;
    m_colAge.assign(size_t(m_nu), 0);
    m_rowAge.assign(size_t(m_nv), 0);
    m_pts.resize(size_t(m_nu) * m_nv);
    QRectF r(m_float.srcRect);
    for (int j = 0; j < m_nv; ++j)
        for (int i = 0; i < m_nu; ++i)
            m_pts[size_t(j) * m_nu + i] = QPointF(r.left() + r.width() * m_us[size_t(i)], r.top() + r.height() * m_vs[size_t(j)]);
    if (m_active) render(true);
}

int WarpTool::addColumn(double u, int onRow) {
    // the column between the two around u; its points sit on the surface as it is now
    int ci = 0;
    while (ci + 1 < m_nu - 1 && m_us[size_t(ci + 1)] <= u) ++ci;
    const double span = m_us[size_t(ci + 1)] - m_us[size_t(ci)];
    const double t = span > 1e-9 ? (u - m_us[size_t(ci)]) / span : 0.5;
    if (t < 0.02 || t > 0.98) return -1;  // too close to a line that is there
    std::vector<QPointF> col(static_cast<size_t>(m_nv));
    for (int j = 0; j < m_nv; ++j) col[size_t(j)] = gridPoint(ci + t, j);
    std::vector<QPointF> pts;
    pts.reserve(size_t(m_nu + 1) * m_nv);
    for (int j = 0; j < m_nv; ++j)
        for (int i = 0; i < m_nu; ++i) {
            pts.push_back(m_pts[size_t(j) * m_nu + i]);
            if (i == ci) pts.push_back(col[size_t(j)]);
        }
    m_pts.swap(pts);
    m_us.insert(m_us.begin() + ci + 1, u);
    m_colAge.insert(m_colAge.begin() + ci + 1, 1 + *std::max_element(m_colAge.begin(), m_colAge.end()) +
                                                    *std::max_element(m_rowAge.begin(), m_rowAge.end()));
    ++m_nu;
    render(true);
    return std::clamp(onRow, 0, m_nv - 1) * m_nu + ci + 1;
}

int WarpTool::addRow(double v, int onColumn) {
    int cj = 0;
    while (cj + 1 < m_nv - 1 && m_vs[size_t(cj + 1)] <= v) ++cj;
    const double span = m_vs[size_t(cj + 1)] - m_vs[size_t(cj)];
    const double t = span > 1e-9 ? (v - m_vs[size_t(cj)]) / span : 0.5;
    if (t < 0.02 || t > 0.98) return -1;
    std::vector<QPointF> row(static_cast<size_t>(m_nu));
    for (int i = 0; i < m_nu; ++i) row[size_t(i)] = gridPoint(i, cj + t);
    m_pts.insert(m_pts.begin() + size_t(cj + 1) * m_nu, row.begin(), row.end());
    m_vs.insert(m_vs.begin() + cj + 1, v);
    m_rowAge.insert(m_rowAge.begin() + cj + 1, 1 + *std::max_element(m_colAge.begin(), m_colAge.end()) +
                                                    *std::max_element(m_rowAge.begin(), m_rowAge.end()));
    ++m_nv;
    render(true);
    return (cj + 1) * m_nu + std::clamp(onColumn, 0, m_nu - 1);
}

bool WarpTool::removeLineAt(int point) {
    if (point < 0 || point >= int(m_pts.size())) return false;
    const int i = point % m_nu, j = point / m_nu;
    const bool colInner = i > 0 && i < m_nu - 1 && m_nu > 2, rowInner = j > 0 && j < m_nv - 1 && m_nv > 2;
    if (!colInner && !rowInner) return false;  // a point of the outer edge stays
    // the line that was added last goes first
    const bool dropColumn = colInner && (!rowInner || m_colAge[size_t(i)] >= m_rowAge[size_t(j)]);
    if (dropColumn) {
        std::vector<QPointF> pts;
        pts.reserve(size_t(m_nu - 1) * m_nv);
        for (int jj = 0; jj < m_nv; ++jj)
            for (int ii = 0; ii < m_nu; ++ii)
                if (ii != i) pts.push_back(m_pts[size_t(jj) * m_nu + ii]);
        m_pts.swap(pts);
        m_us.erase(m_us.begin() + i);
        m_colAge.erase(m_colAge.begin() + i);
        --m_nu;
    } else {
        m_pts.erase(m_pts.begin() + size_t(j) * m_nu, m_pts.begin() + size_t(j + 1) * m_nu);
        m_vs.erase(m_vs.begin() + j);
        m_rowAge.erase(m_rowAge.begin() + j);
        --m_nv;
    }
    render(true);
    return true;
}

QPointF WarpTool::sourceAt(double u, double v) const {
    auto at = [](const std::vector<double>& p, double x) {
        const int n = int(p.size());
        const int k = std::clamp(int(std::floor(x)), 0, n - 2);
        const double t = x - k;
        return p[size_t(k)] + (p[size_t(k + 1)] - p[size_t(k)]) * t;
    };
    const QRectF sr(m_float.srcRect);
    return QPointF(sr.width() * at(m_us, u), sr.height() * at(m_vs, v));
}

bool WarpTool::hitLine(CanvasView* view, QPointF wp, bool* horizontal, double* u, double* v) const {
    const QTransform t = view->canvasToWidget();
    const int S = 24;
    double best = 7;  // px
    bool found = false;
    for (int j = 0; j < m_nv; ++j)
        for (int k = 0; k <= (m_nu - 1) * S; ++k) {
            const double uu = double(k) / S;
            const double d = QLineF(t.map(gridPoint(uu, j)), wp).length();
            if (d < best) best = d, found = true, *horizontal = true, *u = uu, *v = j;
        }
    for (int i = 0; i < m_nu; ++i)
        for (int k = 0; k <= (m_nv - 1) * S; ++k) {
            const double vv = double(k) / S;
            const double d = QLineF(t.map(gridPoint(i, vv)), wp).length();
            if (d < best) best = d, found = true, *horizontal = false, *u = i, *v = vv;
        }
    return found;
}

void WarpTool::activate() {
    if (!m_active) start();
}

void WarpTool::deactivate() {
    if (m_active) apply();
}

QPointF WarpTool::gridPoint(double u, double v) const {
    auto P = [&](int i, int j) {
        i = std::clamp(i, 0, m_nu - 1);
        j = std::clamp(j, 0, m_nv - 1);
        return m_pts[size_t(j) * m_nu + i];
    };
    auto cr = [](const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, double t) {
        double t2 = t * t, t3 = t2 * t;
        return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
    };
    int ci = std::clamp(int(std::floor(u)), 0, m_nu - 2), cj = std::clamp(int(std::floor(v)), 0, m_nv - 2);
    double tu = u - ci, tv = v - cj;
    QPointF rows[4];
    for (int k = -1; k <= 2; ++k) {
        // extrapolate beyond the border so the edges stay straight when untouched
        auto Q = [&](int i, int j) {
            if (i < 0) return 2 * P(0, j) - P(1, j);
            if (i > m_nu - 1) return 2 * P(m_nu - 1, j) - P(m_nu - 2, j);
            return P(i, j);
        };
        int j = cj + k;
        auto R = [&](int i) {
            if (j < 0) return 2 * Q(i, 0) - Q(i, 1);
            if (j > m_nv - 1) return 2 * Q(i, m_nv - 1) - Q(i, m_nv - 2);
            return Q(i, j);
        };
        rows[k + 1] = cr(R(ci - 1), R(ci), R(ci + 1), R(ci + 2), tu);
    }
    return cr(rows[0], rows[1], rows[2], rows[3], tv);
}

void WarpTool::render(bool final) {
    if (!m_active) return;
    Document* d = m_float.doc;
    if (!d) return;
    const int S = final ? 12 : 6;
    const int NU = (m_nu - 1) * S + 1, NV = (m_nv - 1) * S + 1;
    std::vector<QPointF> pos(size_t(NU) * NV), uv(size_t(NU) * NV);
    // Bounding box of the mesh. (QRectF::united ignores empty rects, so track min/max by hand.)
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (int j = 0; j < NV; ++j)
        for (int i = 0; i < NU; ++i) {
            QPointF p = gridPoint(double(i) / S, double(j) / S);
            pos[size_t(j) * NU + i] = p;
            uv[size_t(j) * NU + i] = sourceAt(double(i) / S, double(j) / S);
            minX = std::min(minX, p.x());
            minY = std::min(minY, p.y());
            maxX = std::max(maxX, p.x());
            maxY = std::max(maxY, p.y());
        }
    const QRectF bb(QPointF(minX, minY), QPointF(maxX, maxY));
    QRect limit = d->bounds().adjusted(-d->size().width(), -d->size().height(), d->size().width(), d->size().height());
    QRect dr = bb.toAlignedRect().adjusted(-1, -1, 1, 1) & limit;
    if (dr.isEmpty()) return;
    QImage dst(dr.size(), QImage::Format_ARGB32_Premultiplied);
    if (dst.isNull()) return;
    dst.fill(0);
    const QImage& src = m_float.src;
    const int bandH = 32;
    std::vector<int> bands;
    for (int y = 0; y < dr.height(); y += bandH) bands.push_back(y);
    parallelFor(int(bands.size()), [&](int bi) {
        int y0 = bands[size_t(bi)], y1 = std::min(dr.height() - 1, y0 + bandH - 1);
        for (int j = 0; j + 1 < NV; ++j)
            for (int i = 0; i + 1 < NU; ++i) {
                size_t a = size_t(j) * NU + i, b = a + 1, c = a + NU, e = c + 1;
                for (int tri = 0; tri < 2; ++tri) {
                    size_t v0 = a, v1 = tri ? e : b, v2 = tri ? c : e;
                    QPointF p0 = pos[v0] - dr.topLeft(), p1 = pos[v1] - dr.topLeft(), p2 = pos[v2] - dr.topLeft();
                    double minY = std::min({p0.y(), p1.y(), p2.y()}), maxY = std::max({p0.y(), p1.y(), p2.y()});
                    if (maxY < y0 || minY > y1 + 1) continue;
                    double minX = std::min({p0.x(), p1.x(), p2.x()}), maxX = std::max({p0.x(), p1.x(), p2.x()});
                    double area = (p1.x() - p0.x()) * (p2.y() - p0.y()) - (p2.x() - p0.x()) * (p1.y() - p0.y());
                    if (std::fabs(area) < 1e-9) continue;
                    int ys = std::max(y0, int(std::floor(minY))), ye = std::min(y1, int(std::ceil(maxY)));
                    int xs = std::max(0, int(std::floor(minX))), xe = std::min(dr.width() - 1, int(std::ceil(maxX)));
                    for (int y = ys; y <= ye; ++y) {
                        quint32* row = reinterpret_cast<quint32*>(dst.scanLine(y));
                        double py = y + 0.5;
                        for (int x = xs; x <= xe; ++x) {
                            double px = x + 0.5;
                            double w0 = ((p1.x() - px) * (p2.y() - py) - (p2.x() - px) * (p1.y() - py)) / area;
                            double w1 = ((p2.x() - px) * (p0.y() - py) - (p0.x() - px) * (p2.y() - py)) / area;
                            double w2 = 1 - w0 - w1;
                            if (w0 < -1e-6 || w1 < -1e-6 || w2 < -1e-6) continue;
                            double su = uv[v0].x() * w0 + uv[v1].x() * w1 + uv[v2].x() * w2;
                            double sv = uv[v0].y() * w0 + uv[v1].y() * w1 + uv[v2].y() * w2;
                            float out[4];
                            sampleBilinear(src, float(su), float(sv), out);
                            if (out[3] <= 0) continue;
                            row[x] = pack(out);
                        }
                    }
                }
            }
    });
    m_float.showImage(dst, dr.topLeft());
    Q_EMIT overlayChanged();
}

void WarpTool::press(const PointerEvent& e) {
    if (!m_active && !start()) return;
    if (!e.view || e.view->doc() != m_float.doc) return;
    m_view = e.view;
    m_drag = -1;
    double best = 12;
    for (size_t i = 0; i < m_pts.size(); ++i) {
        double d = QLineF(e.view->toWidget(m_pts[i]), e.widgetPos).length();
        if (d < best) { best = d; m_drag = int(i); }
    }
    if (e.mods & Qt::ControlModifier) {
        // Ctrl + click on a point: its line goes away
        if (m_drag >= 0 && !removeLineAt(m_drag)) app()->showStatus(T("The points of the outer edge stay."), 2500);
        m_drag = -1;
        Q_EMIT overlayChanged();
        return;
    }
    if (m_drag < 0) {
        // a click on a line adds a line through that place, with a point to drag right away
        bool horizontal = false;
        double u = 0, v = 0;
        if (hitLine(e.view, e.widgetPos, &horizontal, &u, &v)) {
            const QPointF src = sourceAt(u, v);
            const QRectF sr(m_float.srcRect);
            if (horizontal) m_drag = addColumn(sr.width() > 0 ? src.x() / sr.width() : 0.5, int(std::lround(v)));
            else m_drag = addRow(sr.height() > 0 ? src.y() / sr.height() : 0.5, int(std::lround(u)));
        }
    }
    m_pressPos = e.pos;
    m_moveAll = m_drag < 0;
    if (m_moveAll) m_drag = int(m_pts.size());  // marker: whole mesh
    else m_startPt = m_pts[size_t(m_drag)];
    m_startPts = m_pts;
}

void WarpTool::move(const PointerEvent& e) {
    if (m_drag < 0) return;
    QPointF delta = e.pos - m_pressPos;
    if (m_moveAll) {
        for (size_t i = 0; i < m_pts.size(); ++i) m_pts[i] = m_startPts[i] + delta;
    } else {
        m_pts[size_t(m_drag)] = m_startPt + delta;
    }
    if (!m_throttle.isActive()) m_throttle.start();
    Q_EMIT overlayChanged();
}

void WarpTool::release(const PointerEvent&) {
    if (m_drag < 0) return;
    m_drag = -1;
    m_throttle.stop();
    render(true);
}

bool WarpTool::keyPress(QKeyEvent* e, CanvasView*) {
    if (!m_active) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { apply(); return true; }
    if (e->key() == Qt::Key_Escape) { cancel(); return true; }
    return false;
}

void WarpTool::apply() {
    if (!m_active) return;
    m_throttle.stop();
    render(true);
    m_active = false;
    bool hadSel = m_float.hadSelection;
    Document* d = m_float.doc;
    m_float.commit();
    if (hadSel && d) {
        // the selection follows the warped pixels' bounds
        ops::deselect(*d);
    }
    Q_EMIT overlayChanged();
}

void WarpTool::cancel() {
    if (!m_active) return;
    m_throttle.stop();
    m_active = false;
    m_float.cancel();
    Q_EMIT overlayChanged();
}

void WarpTool::drawOverlay(QPainter& p, CanvasView* v) {
    if (!m_active || v->doc() != m_float.doc) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QTransform t = v->canvasToWidget();
    QPen pen(Theme::colors().accent, 1);
    p.setPen(pen);
    const int S = 8;
    for (int j = 0; j < m_nv; ++j) {
        QPainterPath path;
        for (int k = 0; k <= (m_nu - 1) * S; ++k) {
            QPointF q = t.map(gridPoint(double(k) / S, j));
            if (k == 0) path.moveTo(q);
            else path.lineTo(q);
        }
        p.drawPath(path);
    }
    for (int i = 0; i < m_nu; ++i) {
        QPainterPath path;
        for (int k = 0; k <= (m_nv - 1) * S; ++k) {
            QPointF q = t.map(gridPoint(i, double(k) / S));
            if (k == 0) path.moveTo(q);
            else path.lineTo(q);
        }
        p.drawPath(path);
    }
    for (const QPointF& c : m_pts) {
        QPointF w = t.map(c);
        p.setPen(QPen(QColor(0, 0, 0, 170), 1));
        p.setBrush(Qt::white);
        p.drawEllipse(w, 4.5, 4.5);
    }
    p.restore();
}

QWidget* WarpTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 6, 6, 6);
    auto* g = new ValueBar(T("Grid"), 1, 10, 0, w);
    g->setValue(cells);
    connect(g, &ValueBar::valueChanged, this, [this](double x) {
        cells = int(x);
        if (m_active) resetGrid();
    });
    v->addWidget(g);
    auto* row = new QHBoxLayout;
    auto* reset = new QPushButton(Theme::icon("reset"), T("Reset"), w);
    auto* ok = new QPushButton(Theme::icon("check"), T("Apply"), w);
    ok->setToolTip("Enter");
    auto* cancelB = new QPushButton(T("Cancel"), w);
    cancelB->setToolTip("Esc");
    connect(reset, &QPushButton::clicked, this, [this] { if (m_active) resetGrid(); });
    connect(ok, &QPushButton::clicked, this, [this] { apply(); });
    connect(cancelB, &QPushButton::clicked, this, [this] { cancel(); });
    v->addWidget(reset);
    row->addWidget(ok);
    row->addWidget(cancelB);
    v->addLayout(row);
    auto* hint = new QLabel(T("Drag the points to bend the image smoothly. Click a line to add a point there (and a line through it); "
                              "Ctrl + click a point to remove its line. Drag elsewhere to move the whole mesh. "
                              "Works on the selection, or on the whole layer."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    v->addStretch();
    return w;
}

// ============================================================================ LiquifyTool

LiquifyTool::LiquifyTool(QObject* p) : Tool("liquify", "Liquify", "liquify", p) {}

bool LiquifyTool::start() {
    CanvasView* v = app()->activeView();
    if (!v) return false;
    Document* d = v->doc();
    auto* l = dynamic_cast<RasterLayer*>(d->currentLayer());
    if (!l) {
        app()->showStatus(T("Liquify works on pixel layers."));
        return false;
    }
    QString err;
    if (!m_float.begin(d, l, T("Liquify"), true, &err)) {
        app()->showStatus(T(err));
        return false;
    }
    m_view = v;
    m_region = m_float.srcRect;
    qint64 area = qint64(m_region.width()) * m_region.height();
    m_scale = area > 24000000 ? 4 : area > 6000000 ? 2 : 1;
    m_fw = (m_region.width() + m_scale - 1) / m_scale + 1;
    m_fh = (m_region.height() + m_scale - 1) / m_scale + 1;
    m_dx.assign(size_t(m_fw) * m_fh, 0.f);
    m_dy.assign(size_t(m_fw) * m_fh, 0.f);
    m_out = m_float.src.copy();
    m_active = true;
    m_float.showImage(m_out, m_region.topLeft());
    return true;
}

void LiquifyTool::activate() {
    if (!m_active) start();
}

void LiquifyTool::deactivate() {
    if (m_active) apply();
}

void LiquifyTool::renderRect(const QRect& cr0) {
    QRect cr = cr0 & m_region;
    if (cr.isEmpty()) return;
    const QImage& src = m_float.src;
    const int s = m_scale;
    auto field = [&](const std::vector<float>& f, double fx, double fy) {
        fx = std::clamp(fx, 0.0, double(m_fw - 1));
        fy = std::clamp(fy, 0.0, double(m_fh - 1));
        int x0 = std::min(int(fx), m_fw - 2 < 0 ? 0 : m_fw - 2), y0 = std::min(int(fy), m_fh - 2 < 0 ? 0 : m_fh - 2);
        double tx = fx - x0, ty = fy - y0;
        auto at = [&](int x, int y) { return f[size_t(std::min(y, m_fh - 1)) * m_fw + std::min(x, m_fw - 1)]; };
        return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
    };
    parallelFor(cr.height(), [&](int yy) {
        int y = cr.top() + yy;
        quint32* row = reinterpret_cast<quint32*>(m_out.scanLine(y - m_region.top()));
        for (int x = cr.left(); x <= cr.right(); ++x) {
            double lx = x - m_region.left() + 0.5, ly = y - m_region.top() + 0.5;
            double dx = field(m_dx, lx / s, ly / s), dy = field(m_dy, lx / s, ly / s);
            float o[4];
            sampleBilinear(src, float(lx + dx), float(ly + dy), o);
            row[x - m_region.left()] = pack(o);
        }
    });
}

void LiquifyTool::dab(const QPointF& p, const QPointF& delta, double pressure) {
    const double R = size / 2;
    const double str = strength * std::clamp(pressure, 0.05, 1.0);
    const int s = m_scale;
    QPointF lp = p - m_region.topLeft();
    int fx0 = std::max(0, int((lp.x() - R) / s) - 1), fx1 = std::min(m_fw - 1, int((lp.x() + R) / s) + 1);
    int fy0 = std::max(0, int((lp.y() - R) / s) - 1), fy1 = std::min(m_fh - 1, int((lp.y() + R) / s) + 1);
    if (fx0 > fx1 || fy0 > fy1) return;
    const int W = fx1 - fx0 + 1, H = fy1 - fy0 + 1;
    std::vector<float> ndx(size_t(W) * H), ndy(size_t(W) * H);
    auto sampleOld = [&](const std::vector<float>& f, double cx, double cy) {
        double fx = std::clamp(cx / s, 0.0, double(m_fw - 1)), fy = std::clamp(cy / s, 0.0, double(m_fh - 1));
        int x0 = std::min(int(fx), std::max(0, m_fw - 2)), y0 = std::min(int(fy), std::max(0, m_fh - 2));
        double tx = fx - x0, ty = fy - y0;
        auto at = [&](int x, int y) { return f[size_t(std::min(y, m_fh - 1)) * m_fw + std::min(x, m_fw - 1)]; };
        return float((at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty);
    };
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            int fx = fx0 + i, fy = fy0 + j;
            size_t fi = size_t(fy) * m_fw + fx;
            double cx = fx * s, cy = fy * s;
            double d = std::hypot(cx - lp.x(), cy - lp.y());
            float odx = m_dx[fi], ody = m_dy[fi];
            ndx[size_t(j) * W + i] = odx;
            ndy[size_t(j) * W + i] = ody;
            if (d >= R) continue;
            double f = 1 - (d / R) * (d / R);
            double w = f * f * str;
            double qx = cx, qy = cy;
            switch (mode) {
            case Push:
                qx = cx - delta.x() * w;
                qy = cy - delta.y() * w;
                break;
            case TwirlCW:
            case TwirlCCW: {
                double a = (mode == TwirlCW ? -1 : 1) * w * 0.12;
                double rx = cx - lp.x(), ry = cy - lp.y();
                qx = lp.x() + rx * std::cos(a) - ry * std::sin(a);
                qy = lp.y() + rx * std::sin(a) + ry * std::cos(a);
                break;
            }
            case Pinch:
                qx = lp.x() + (cx - lp.x()) * (1 + 0.08 * w);
                qy = lp.y() + (cy - lp.y()) * (1 + 0.08 * w);
                break;
            case Bloat:
                qx = lp.x() + (cx - lp.x()) * (1 - 0.08 * w);
                qy = lp.y() + (cy - lp.y()) * (1 - 0.08 * w);
                break;
            case Restore:
                ndx[size_t(j) * W + i] = float(odx * (1 - w * 0.6));
                ndy[size_t(j) * W + i] = float(ody * (1 - w * 0.6));
                continue;
            case Smooth: {
                float ax = 0, ay = 0;
                int cnt = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox) {
                        int xx = std::clamp(fx + ox, 0, m_fw - 1), yy = std::clamp(fy + oy, 0, m_fh - 1);
                        ax += m_dx[size_t(yy) * m_fw + xx];
                        ay += m_dy[size_t(yy) * m_fw + xx];
                        ++cnt;
                    }
                ndx[size_t(j) * W + i] = float(odx + (ax / cnt - odx) * w);
                ndy[size_t(j) * W + i] = float(ody + (ay / cnt - ody) * w);
                continue;
            }
            }
            ndx[size_t(j) * W + i] = float(qx - cx) + sampleOld(m_dx, qx, qy);
            ndy[size_t(j) * W + i] = float(qy - cy) + sampleOld(m_dy, qx, qy);
        }
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            size_t fi = size_t(fy0 + j) * m_fw + fx0 + i;
            m_dx[fi] = ndx[size_t(j) * W + i];
            m_dy[fi] = ndy[size_t(j) * W + i];
        }
    QRect cr = QRect(m_region.left() + fx0 * s, m_region.top() + fy0 * s, W * s, H * s).adjusted(-2, -2, 2, 2);
    renderRect(cr);
    // update the preview only around the dab
    RasterLayer* l = m_float.layer();
    if (!l || !l->preview) return;
    QRect ur = cr & m_region;
    const size_t n = size_t(ur.width()) * ur.height();
    std::vector<float> a(n * 4), b(n * 4);
    readFloat(m_float.base, ur, a.data());
    for (int y = 0; y < ur.height(); ++y) {
        const quint32* row = reinterpret_cast<const quint32*>(m_out.constScanLine(ur.top() - m_region.top() + y));
        for (int x = 0; x < ur.width(); ++x) unpack(row[ur.left() - m_region.left() + x], &b[(size_t(y) * ur.width() + x) * 4]);
    }
    blendRow(BlendMode::Normal, a.data(), b.data(), int(n));
    writeFloat(*l->preview, ur, a.data());
    m_float.doc->pixelsChanged(ur, l);
}

void LiquifyTool::press(const PointerEvent& e) {
    if (!m_active && !start()) return;
    if (!e.view || e.view->doc() != m_float.doc) return;
    m_view = e.view;
    m_drag = true;
    m_last = e.pos;
    if (mode != Push) dab(e.pos, QPointF(), e.pressure);
}

void LiquifyTool::move(const PointerEvent& e) {
    if (!m_drag) return;
    QPointF d = e.pos - m_last;
    double len = std::hypot(d.x(), d.y());
    double step = std::max(1.0, size * 0.08);
    if (mode == Push) {
        if (len < step * 0.5) return;
        dab(m_last, d, e.pressure);
        m_last = e.pos;
        return;
    }
    if (len < step) return;
    int n = int(len / step);
    for (int i = 1; i <= n; ++i) dab(m_last + d * (double(i) / n), QPointF(), e.pressure);
    m_last = e.pos;
}

void LiquifyTool::release(const PointerEvent&) { m_drag = false; }

bool LiquifyTool::keyPress(QKeyEvent* e, CanvasView*) {
    if (!m_active) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { apply(); return true; }
    if (e->key() == Qt::Key_Escape) { cancel(); return true; }
    return false;
}

void LiquifyTool::apply() {
    if (!m_active) return;
    m_active = false;
    m_float.commit();
    m_dx.clear();
    m_dy.clear();
    m_out = QImage();
}

void LiquifyTool::cancel() {
    if (!m_active) return;
    m_active = false;
    m_float.cancel();
    m_dx.clear();
    m_dy.clear();
    m_out = QImage();
}

QWidget* LiquifyTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 6, 6, 6);
    auto* m = new QComboBox(w);
    m->addItem(Theme::icon("liquify"), T("Push"));
    m->addItem(T("Twirl clockwise"));
    m->addItem(T("Twirl counter-clockwise"));
    m->addItem(T("Pinch"));
    m->addItem(T("Bloat"));
    m->addItem(T("Restore"));
    m->addItem(T("Smooth"));
    m->setCurrentIndex(mode);
    connect(m, &QComboBox::currentIndexChanged, this, [this](int i) { mode = i; });
    v->addWidget(m);
    auto* s = new ValueBar(T("Size"), 4, 3000, 0, w);
    s->setLogarithmic(true);
    s->setSuffix(" px");
    s->setValue(size);
    connect(s, &ValueBar::valueChanged, this, [this](double x) { size = x; Q_EMIT overlayChanged(); });
    v->addWidget(s);
    auto* st = new ValueBar(T("Strength"), 0.01, 1, 2, w);
    st->setDisplayScale(100);
    st->setSuffix("%");
    st->setValue(strength);
    connect(st, &ValueBar::valueChanged, this, [this](double x) { strength = x; });
    v->addWidget(st);
    auto* row = new QHBoxLayout;
    auto* ok = new QPushButton(Theme::icon("check"), T("Apply"), w);
    ok->setToolTip("Enter");
    auto* cancelB = new QPushButton(T("Cancel"), w);
    cancelB->setToolTip("Esc");
    connect(ok, &QPushButton::clicked, this, [this] { apply(); });
    connect(cancelB, &QPushButton::clicked, this, [this] { cancel(); });
    row->addWidget(ok);
    row->addWidget(cancelB);
    v->addLayout(row);
    auto* hint = new QLabel(T("Paint to push, twirl, pinch or bloat the pixels. Changes stay editable until you apply."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    v->addStretch();
    return w;
}

}  // namespace gx
