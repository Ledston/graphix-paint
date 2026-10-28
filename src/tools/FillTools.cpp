#include "tools/FillTools.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"
#include "ui/CanvasView.h"
#include "ui/Dialogs.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

namespace {

// Composites a color onto a raster layer through a coverage mask (0..255 surface).
void paintThroughMask(Document* d, RasterLayer* l, const MaskSurface& mask, const QColor& color, double opacity, bool erase,
                      const QString& undoName) {
    QRect r = mask.exactBounds() & d->bounds();
    if (r.isEmpty()) return;
    d->pushUndo(undoName);
    l = static_cast<RasterLayer*>(d->layer(l->id));
    const size_t n = size_t(r.width()) * r.height();
    std::vector<float> px(n * 4), m(n), sel;
    readFloat(l->pixels, r, px.data());
    readMask(mask, r, m.data());
    if (d->hasSelection()) {
        sel.resize(n);
        readMask(d->selection(), r, sel.data());
    }
    float c[3] = {float(color.redF()), float(color.greenF()), float(color.blueF())};
    for (size_t i = 0; i < n; ++i) {
        float w = m[i] * float(opacity) * (sel.empty() ? 1.f : sel[i]);
        if (w <= 0) continue;
        float* p = &px[i * 4];
        if (erase) {
            for (int k = 0; k < 4; ++k) p[k] *= 1.f - w;
            continue;
        }
        float oldA = p[3];
        float s[4] = {c[0] * w, c[1] * w, c[2] * w, w};
        if (l->alphaLock) blendRowAtop(BlendMode::Normal, p, s, 1);
        else blendRow(BlendMode::Normal, p, s, 1);
        (void)oldA;
    }
    writeFloat(l->pixels, r, px.data(), true);
    l->pixels.optimizeRect(r);
    d->pixelsChanged(r, l);
}

RasterLayer* paintableLayer(Document* d) {
    Layer* l = d->currentLayer();
    if (!l) return nullptr;
    if (!l->isRaster()) {
        app()->showStatus(T("Select a pixel layer to fill."));
        return nullptr;
    }
    if (l->isEffectivelyLocked()) {
        app()->showStatus(T("The layer is locked."));
        return nullptr;
    }
    return static_cast<RasterLayer*>(l);
}

}  // namespace

// ============================================================================ fill

void FillTool::press(const PointerEvent& e) {
    if (!e.view) return;
    Document* d = e.view->doc();
    RasterLayer* l = paintableLayer(d);
    if (!l) return;
    QPoint seed(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())));
    if (!d->bounds().contains(seed)) return;
    QString err;
    MaskSurface m = floodFillEx(d, seed, tolerance, FillSource(source), contiguous, l, gapClose, &err);
    if (!err.isEmpty()) {
        app()->showStatus(T(err));
        return;
    }
    QRect b = m.exactBounds();
    if (b.isEmpty()) return;
    if (expand > 0 || antialias) {
        int margin = expand + 2;
        QRect r = b.adjusted(-margin, -margin, margin, margin) & d->bounds();
        const int W = r.width(), H = r.height();
        std::vector<float> v(size_t(W) * H);
        readMask(m, r, v.data());
        if (expand > 0) {
            std::vector<uint8_t> in(v.size());
            for (size_t i = 0; i < v.size(); ++i) in[i] = v[i] >= 0.5f;
            std::vector<float> d2;
            distanceTransform(in, W, H, d2);
            for (size_t i = 0; i < v.size(); ++i) v[i] = clamp01(float(expand) + 0.5f - std::sqrt(d2[i]));
        }
        if (antialias) gaussianBlur(v.data(), W, H, 1, 0.6f);
        std::vector<quint8> u(v.size());
        for (size_t i = 0; i < v.size(); ++i) u[i] = quint8(std::clamp(int(v[i] * 255.f + 0.5f), 0, 255));
        m.writeRect(r, u.data(), W);
    }
    bool erase = app()->transparentColor();
    paintThroughMask(d, l, m, app()->foreground(), opacity, erase, T("Fill"));
}

QWidget* FillTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* f = new QVBoxLayout(w);
    f->setContentsMargins(6, 6, 6, 6);
    auto* tol = new ValueBar(T("Tolerance"), 0, 255, 0, w);
    tol->setValue(tolerance);
    connect(tol, &ValueBar::valueChanged, this, [this](double v) { tolerance = int(v); });
    auto* ex = new ValueBar(T("Expand"), 0, 50, 0, w);
    ex->setSuffix(" px");
    ex->setValue(expand);
    connect(ex, &ValueBar::valueChanged, this, [this](double v) { expand = int(v); });
    auto* op = new ValueBar(T("Opacity"), 0, 1, 2, w);
    op->setDisplayScale(100);
    op->setSuffix("%");
    op->setValue(opacity);
    connect(op, &ValueBar::valueChanged, this, [this](double v) { opacity = v; });
    auto* src = new QComboBox(w);
    src->addItems({T("Borders from all visible layers"), T("Borders from the current layer"), T("Borders from reference layers (line art)")});
    src->setCurrentIndex(source);
    src->setToolTip(T("Reference layers: right-click the line art layer and choose \"Reference layer\"."));
    connect(src, &QComboBox::currentIndexChanged, this, [this](int i) { source = i; });
    auto* gap = new ValueBar(T("Close gaps"), 0, 30, 0, w);
    gap->setSuffix(" px");
    gap->setValue(gapClose);
    gap->setToolTip(T("Small breaks in the lines up to this size count as closed"));
    connect(gap, &ValueBar::valueChanged, this, [this](double v) { gapClose = int(v); });
    auto* cont = new QCheckBox(T("Contiguous"), w);
    cont->setChecked(contiguous);
    connect(cont, &QCheckBox::toggled, this, [this](bool on) { contiguous = on; });
    auto* aa = new QCheckBox(T("Soft edges"), w);
    aa->setChecked(antialias);
    connect(aa, &QCheckBox::toggled, this, [this](bool on) { antialias = on; });
    ex->setLabel(T("Go under the lines"));
    for (QWidget* x : {static_cast<QWidget*>(src), static_cast<QWidget*>(tol), static_cast<QWidget*>(gap), static_cast<QWidget*>(ex),
                       static_cast<QWidget*>(op), static_cast<QWidget*>(cont), static_cast<QWidget*>(aa)})
        f->addWidget(x);
    auto* hint = new QLabel(T("Coloring line art: mark the line art as reference layer, choose \"Borders from reference layers\" and fill on a layer under the lines."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    f->addWidget(hint);
    f->addStretch();
    return w;
}

// ============================================================================ lasso fill

LassoFillTool::LassoFillTool(QObject* p) : LassoSelectTool(p) {
    m_id = "lasso-fill";
    m_name = "Lasso fill";
    m_icon = "lasso-fill";
}

void LassoFillTool::apply(CanvasView* v, const QPolygonF& poly, Qt::KeyboardModifiers mods) {
    Document* d = v->doc();
    RasterLayer* l = paintableLayer(d);
    if (!l) return;
    QPainterPath p;
    p.addPolygon(poly);
    p.closeSubpath();
    MaskSurface m = ops::rasterizePath(p, d->bounds(), antialias);
    bool erase = eraseMode || (mods & Qt::AltModifier) || app()->transparentColor();
    paintThroughMask(d, l, m, app()->foreground(), opacity, erase, erase ? T("Lasso erase") : T("Lasso fill"));
}

QWidget* LassoFillTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* f = new QVBoxLayout(w);
    f->setContentsMargins(6, 6, 6, 6);
    auto* op = new ValueBar(T("Opacity"), 0, 1, 2, w);
    op->setDisplayScale(100);
    op->setSuffix("%");
    op->setValue(opacity);
    connect(op, &ValueBar::valueChanged, this, [this](double v) { opacity = v; });
    auto* aa = new QCheckBox(T("Anti-aliased edges"), w);
    aa->setChecked(antialias);
    connect(aa, &QCheckBox::toggled, this, [this](bool on) { antialias = on; });
    auto* er = new QCheckBox(T("Erase instead of filling"), w);
    er->setChecked(eraseMode);
    connect(er, &QCheckBox::toggled, this, [this](bool on) { eraseMode = on; });
    f->addWidget(op);
    f->addWidget(aa);
    f->addWidget(er);
    auto* hint = new QLabel(T("Draw a closed shape: it is filled with the main color. Hold Alt to erase. Click points for straight edges, double-click to finish."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    f->addWidget(hint);
    f->addStretch();
    return w;
}

// ============================================================================ gradient

GradientTool::GradientTool(QObject* p) : Tool("gradient", "Gradient", "gradient", p) {
    m_throttle.setSingleShot(true);
    m_throttle.setInterval(30);
    connect(&m_throttle, &QTimer::timeout, this, [this] {
        if (m_pending) {
            m_pending = false;
            render();
        }
    });
    custom = Gradient::twoColor(Qt::black, Qt::white);
}

Gradient GradientTool::currentGradient() const {
    if (useCustom) return custom;
    QVector<Gradient> p = Gradient::presets(app()->foreground(), app()->background());
    return p[std::clamp(preset, 0, int(p.size()) - 1)];
}

void GradientTool::press(const PointerEvent& e) {
    if (!e.view) return;
    Document* d = e.view->doc();
    RasterLayer* l = paintableLayer(d);
    if (!l) return;
    m_view = e.view;
    d->pushUndo(T("Gradient"));
    l = static_cast<RasterLayer*>(d->currentLayer());
    l->preview = std::make_shared<RasterSurface>(l->pixels);
    m_a = m_b = e.pos;
    m_drag = true;
}

void GradientTool::move(const PointerEvent& e) {
    if (!m_drag) return;
    m_b = e.pos;
    if (e.mods & Qt::ShiftModifier) {
        QPointF d = m_b - m_a;
        double ang = std::atan2(d.y(), d.x());
        ang = std::round(ang / (M_PI / 4)) * (M_PI / 4);
        double len = std::hypot(d.x(), d.y());
        m_b = m_a + QPointF(std::cos(ang), std::sin(ang)) * len;
    }
    m_pending = true;
    if (!m_throttle.isActive()) m_throttle.start();
    Q_EMIT overlayChanged();
}

void GradientTool::render() {
    if (!m_view) return;
    Document* d = m_view->doc();
    auto* l = dynamic_cast<RasterLayer*>(d->currentLayer());
    if (!l || !l->preview) return;
    QRect area = d->hasSelection() ? d->selectionBounds() : d->bounds();
    if (area.isEmpty()) return;
    QPointF dir = m_b - m_a;
    double len2 = dir.x() * dir.x() + dir.y() * dir.y();
    if (len2 < 1) return;
    double len = std::sqrt(len2);
    std::vector<float> lut = currentGradient().buildLut(1024, reverse);
    TileRange tr = tileRangeFor(area);
    QVector<qint64> keys;
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) keys << tileKey(tx, ty);
    const bool sel = d->hasSelection();
    const float op = float(opacity);
    const BlendMode bm = mode;
    const int sh = shape, rep = repeat;
    const QPointF a = m_a;
    const bool lock = l->alphaLock;
    std::vector<RasterSurface::Ptr> out(size_t(keys.size()));
    parallelFor(int(keys.size()), [&](int i) {
        QRect r = tileRectForKey(keys[i]) & area;
        const size_t n = size_t(r.width()) * r.height();
        std::vector<float> dst(n * 4), src(n * 4), m;
        readFloat(l->pixels, r, dst.data());
        if (sel) {
            m.resize(n);
            readMask(d->selection(), r, m.data());
        }
        for (int y = 0; y < r.height(); ++y)
            for (int x = 0; x < r.width(); ++x) {
                double px = r.left() + x + 0.5 - a.x(), py = r.top() + y + 0.5 - a.y();
                double t;
                switch (sh) {
                case Radial: t = std::hypot(px, py) / len; break;
                case Reflected: t = std::fabs((px * dir.x() + py * dir.y()) / len2); break;
                case Diamond: {
                    double u = (px * dir.x() + py * dir.y()) / len, v = (-px * dir.y() + py * dir.x()) / len;
                    t = (std::fabs(u) + std::fabs(v)) / len;
                    break;
                }
                case Angle: {
                    double base = std::atan2(dir.y(), dir.x());
                    double ang = std::atan2(py, px) - base;
                    t = ang / (2 * M_PI);
                    t -= std::floor(t);
                    break;
                }
                default: t = (px * dir.x() + py * dir.y()) / len2; break;
                }
                if (rep == 1) t -= std::floor(t);
                else if (rep == 2) {
                    t = std::fmod(std::fabs(t), 2.0);
                    if (t > 1) t = 2 - t;
                } else t = std::clamp(t, 0.0, 1.0);
                const float* g = &lut[size_t(std::clamp(int(t * 1023 + 0.5), 0, 1023)) * 4];
                size_t idx = size_t(y) * r.width() + x;
                float w = op * (sel ? m[idx] : 1.f);
                for (int k = 0; k < 4; ++k) src[idx * 4 + size_t(k)] = g[k] * w;
            }
        if (lock) blendRowAtop(bm, dst.data(), src.data(), int(n));
        else blendRow(bm, dst.data(), src.data(), int(n));
        RasterSurface tmp;
        writeFloat(tmp, r, dst.data(), true);
        // move the single tile into the result (r lies within one tile)
        out[size_t(i)] = tmp.tilePtr(keys[i]);
    });
    RasterSurface preview = l->pixels;
    for (int i = 0; i < keys.size(); ++i) {
        if (!out[size_t(i)]) {
            // fully transparent result
            QRect r = tileRectForKey(keys[i]) & area;
            if (r == tileRectForKey(keys[i])) preview.removeTile(keys[i]);
            else preview.fillRect(r, 0);
            continue;
        }
        QRect r = tileRectForKey(keys[i]) & area;
        if (r == tileRectForKey(keys[i])) preview.setTile(keys[i], out[size_t(i)]);
        else {
            std::vector<quint32> buf(size_t(r.width()) * r.height());
            RasterSurface one;
            one.setTile(keys[i], out[size_t(i)]);
            one.readRect(r, buf.data(), r.width());
            preview.writeRect(r, buf.data(), r.width());
        }
    }
    *l->preview = std::move(preview);
    d->pixelsChanged(area, l);
}

void GradientTool::release(const PointerEvent&) {
    if (!m_drag || !m_view) return;
    m_drag = false;
    m_throttle.stop();
    render();
    Document* d = m_view->doc();
    auto* l = dynamic_cast<RasterLayer*>(d->currentLayer());
    if (!l || !l->preview) return;
    if (QLineF(m_a, m_b).length() < 1) {
        l->preview.reset();
        d->revertLastUndo();
        return;
    }
    l->pixels = std::move(*l->preview);
    l->preview.reset();
    l->pixels.optimize();
    d->pixelsChanged(d->bounds(), l);
    Q_EMIT overlayChanged();
}

bool GradientTool::keyPress(QKeyEvent* e, CanvasView* v) {
    if (m_drag && e->key() == Qt::Key_Escape && v) {
        m_drag = false;
        if (auto* l = dynamic_cast<RasterLayer*>(v->doc()->currentLayer())) l->preview.reset();
        v->doc()->revertLastUndo();
        Q_EMIT overlayChanged();
        return true;
    }
    return false;
}

void GradientTool::drawOverlay(QPainter& p, CanvasView* v) {
    if (!m_drag || v != m_view) return;
    QPointF a = v->toWidget(m_a), b = v->toWidget(m_b);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 150), 3));
    p.drawLine(a, b);
    p.setPen(QPen(Qt::white, 1.4));
    p.drawLine(a, b);
    p.setBrush(Theme::colors().accent);
    p.drawEllipse(a, 4, 4);
    p.drawEllipse(b, 4, 4);
    p.restore();
}

QWidget* GradientTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* f = new QFormLayout(w);
    f->setContentsMargins(6, 6, 6, 6);
    auto* sh = new QComboBox(w);
    sh->addItems({T("Linear"), T("Radial"), T("Reflected"), T("Diamond"), T("Angle")});
    sh->setCurrentIndex(shape);
    connect(sh, &QComboBox::currentIndexChanged, this, [this](int i) { shape = i; });
    f->addRow(T("Shape"), sh);
    auto* pr = new QComboBox(w);
    for (const Gradient& g : Gradient::presets(Qt::black, Qt::white)) pr->addItem(T(g.name));
    pr->addItem(T("Custom..."));
    pr->setCurrentIndex(useCustom ? pr->count() - 1 : preset);
    connect(pr, &QComboBox::activated, this, [this, pr, w](int i) {
        if (i == pr->count() - 1) {
            Gradient g = custom;
            if (Dialogs::editGradient(w, g)) {
                custom = g;
                useCustom = true;
            }
        } else {
            preset = i;
            useCustom = false;
        }
    });
    f->addRow(T("Colors"), pr);
    auto* rp = new QComboBox(w);
    rp->addItems({T("No repeat"), T("Repeat"), T("Mirror")});
    rp->setCurrentIndex(repeat);
    connect(rp, &QComboBox::currentIndexChanged, this, [this](int i) { repeat = i; });
    f->addRow(T("Repeat"), rp);
    auto* md = new QComboBox(w);
    for (BlendMode bm : blendModeUiOrder()) md->addItem(T(blendModeName(bm)), int(bm));
    connect(md, &QComboBox::activated, this, [this, md](int i) { mode = BlendMode(md->itemData(i).toInt()); });
    f->addRow(T("Mode"), md);
    auto* op = new ValueBar(T("Opacity"), 0, 1, 2, w);
    op->setDisplayScale(100);
    op->setSuffix("%");
    op->setValue(opacity);
    connect(op, &ValueBar::valueChanged, this, [this](double v) { opacity = v; });
    f->addRow(op);
    auto* rv = new QCheckBox(T("Reverse"), w);
    rv->setChecked(reverse);
    connect(rv, &QCheckBox::toggled, this, [this](bool on) { reverse = on; });
    f->addRow(rv);
    auto* hint = new QLabel(T("Drag on the canvas. Shift snaps the angle. Works inside the selection if there is one."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    f->addRow(hint);
    return w;
}

}  // namespace gx
