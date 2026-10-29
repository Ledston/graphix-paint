#include "tools/FillTools.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "app/Materials.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"
#include "ui/CanvasView.h"
#include "ui/Dialogs.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

namespace {

}  // namespace

// Composites a color (or the current pattern) onto a raster layer through a coverage mask.
void paintThroughMask(Document* d, RasterLayer* l, const MaskSurface& mask, const QColor& color, double opacity, bool erase,
                      const QString& undoName, bool pattern, bool clipToSelection) {
    QRect r = mask.exactBounds() & d->bounds();
    if (r.isEmpty()) return;
    Materials& mat = Materials::instance();
    QImage tile = pattern ? mat.pattern(mat.currentPattern()) : QImage();
    if (pattern && tile.isNull()) {
        app()->showStatus(T("Choose a pattern first: Window > Materials > Patterns."), 5000);
        return;
    }
    d->pushUndo(undoName);
    l = static_cast<RasterLayer*>(d->layer(l->id));
    const size_t n = size_t(r.width()) * r.height();
    std::vector<float> px(n * 4), m(n), sel;
    readFloat(l->pixels, r, px.data());
    readMask(mask, r, m.data());
    if (clipToSelection && d->hasSelection()) {
        sel.resize(n);
        readMask(d->selection(), r, sel.data());
    }
    float c[3] = {float(color.redF()), float(color.greenF()), float(color.blueF())};
    const double scale = std::max(0.05, mat.patternScale);
    const bool tint = mat.patternTint;
    for (size_t i = 0; i < n; ++i) {
        float w = m[i] * float(opacity) * (sel.empty() ? 1.f : sel[i]);
        if (w <= 0) continue;
        float* p = &px[i * 4];
        if (erase) {
            for (int k = 0; k < 4; ++k) p[k] *= 1.f - w;
            continue;
        }
        float s[4];
        if (!tile.isNull()) {
            // the pattern tiles from the corner of the picture, so fills next to each other match
            const int x = r.left() + int(i % size_t(r.width())), y = r.top() + int(i / size_t(r.width()));
            int tx = int(std::floor(x / scale)) % tile.width(), ty = int(std::floor(y / scale)) % tile.height();
            if (tx < 0) tx += tile.width();
            if (ty < 0) ty += tile.height();
            const QRgb t = reinterpret_cast<const QRgb*>(tile.constScanLine(ty))[tx];
            const float a = qAlpha(t) / 255.f * w;
            if (tint) s[0] = c[0] * a, s[1] = c[1] * a, s[2] = c[2] * a;
            else s[0] = qRed(t) / 255.f * a, s[1] = qGreen(t) / 255.f * a, s[2] = qBlue(t) / 255.f * a;
            s[3] = a;
        } else {
            s[0] = c[0] * w;
            s[1] = c[1] * w;
            s[2] = c[2] * w;
            s[3] = w;
        }
        if (l->alphaLock) blendRowAtop(BlendMode::Normal, p, s, 1);
        else blendRow(BlendMode::Normal, p, s, 1);
    }
    writeFloat(l->pixels, r, px.data(), true);
    l->pixels.optimizeRect(r);
    d->pixelsChanged(r, l);
}

namespace {

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
    paintThroughMask(d, l, m, app()->foreground(), opacity, erase, T("Fill"), usePattern);
}

// "Fill with: color / pattern", the pattern, its size and color (shared by every fill)
QWidget* patternOptions(QWidget* parent, bool* use) {
    auto* box = new QWidget(parent);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    auto* with = new QComboBox(box);
    with->addItem(T("Fill with the main color"), 0);
    with->addItem(T("Fill with a pattern"), 1);
    with->setCurrentIndex(*use ? 1 : 0);
    v->addWidget(with);
    auto* more = new QWidget(box);
    auto* mv = new QVBoxLayout(more);
    mv->setContentsMargins(0, 0, 0, 0);
    mv->setSpacing(3);
    auto* pat = new QComboBox(more);
    pat->setIconSize(QSize(24, 24));
    auto fillList = [pat] {
        const QSignalBlocker b(pat);
        pat->clear();
        Materials& m = Materials::instance();
        for (const QString& n : m.patterns()) {
            QImage img = m.pattern(n);
            QImage icon(24, 24, QImage::Format_ARGB32_Premultiplied);
            icon.fill(Qt::white);
            {
                QPainter ip(&icon);
                ip.fillRect(icon.rect(), QBrush(img.scaled(std::max(4, img.width() / 2), std::max(4, img.height() / 2), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
            pat->addItem(QIcon(QPixmap::fromImage(icon)), T(n), n);
        }
        pat->setCurrentIndex(std::max(0, pat->findData(m.currentPattern())));
    };
    fillList();
    QObject::connect(&Materials::instance(), &Materials::changed, pat, fillList);
    QObject::connect(&Materials::instance(), &Materials::currentPatternChanged, pat, [pat] {
        const QSignalBlocker b(pat);
        pat->setCurrentIndex(std::max(0, pat->findData(Materials::instance().currentPattern())));
    });
    QObject::connect(pat, &QComboBox::activated, pat, [pat](int i) { Materials::instance().setCurrentPattern(pat->itemData(i).toString()); });
    mv->addWidget(pat);
    auto* sc = new ValueBar(T("Pattern size"), 0.1, 10, 2, more);
    sc->setDisplayScale(100);
    sc->setSuffix("%");
    sc->setLogarithmic(true);
    sc->setValue(Materials::instance().patternScale);
    QObject::connect(sc, &ValueBar::valueChanged, sc, [](double x) {
        Materials& m = Materials::instance();
        m.patternScale = x;
        m.setCurrentPattern(m.currentPattern());  // remembers the size
    });
    mv->addWidget(sc);
    auto* tint = new QCheckBox(T("In the main color"), more);
    tint->setToolTip(T("Paint the pattern in the main color. Off: the pattern keeps its own colors."));
    tint->setChecked(Materials::instance().patternTint);
    QObject::connect(tint, &QCheckBox::toggled, tint, [](bool on) {
        Materials& m = Materials::instance();
        m.patternTint = on;
        m.setCurrentPattern(m.currentPattern());
    });
    mv->addWidget(tint);
    v->addWidget(more);
    more->setVisible(*use);
    QObject::connect(with, &QComboBox::activated, with, [with, more, use](int i) {
        *use = with->itemData(i).toInt() == 1;
        more->setVisible(*use);
    });
    return box;
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
    f->addWidget(patternOptions(w, &usePattern));
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

// "Enclose and fill": the closed areas of the line art that lie (mostly) inside the shape are
// filled whole, also where they reach a little outside it, and the fill goes under the lines.
static MaskSurface enclosedAreas(Document* d, const QPolygonF& poly, FillSource source, const Layer* layer, int gapClose, int under, QString* error) {
    MaskSurface out(0);
    const QRect pb = poly.boundingRect().toAlignedRect();
    const int margin = std::max(16, std::max(pb.width(), pb.height()) / 4);  // to see how far an area reaches outside
    QRect area = pb.adjusted(-margin, -margin, margin, margin) & d->bounds();
    if (d->hasSelection()) area &= d->selectionBounds().adjusted(-margin, -margin, margin, margin);
    if (area.isEmpty()) return out;
    const int W = area.width(), H = area.height();
    std::vector<quint32> px;
    if (!readFillSource(d, area, source, layer, px, error)) return out;
    const size_t N = size_t(W) * size_t(H);
    // the lines: opaque pixels of line layers; dark pixels when looking at the whole picture
    std::vector<uint8_t> line(N);
    for (size_t i = 0; i < N; ++i) {
        const quint32 c = px[i];
        const int a = int(c >> 24);
        if (source == FillSource::AllVisible) {
            // over white paper: luminance of the shown color
            const int r = int((c >> 16) & 255) + (255 - a), g = int((c >> 8) & 255) + (255 - a), b = int(c & 255) + (255 - a);
            line[i] = (r * 3 + g * 6 + b) / 10 < 110 ? 1 : 0;
        } else {
            line[i] = a > 110 ? 1 : 0;
        }
    }
    // gaps up to gapClose px count as closed
    std::vector<uint8_t> wall = line;
    if (gapClose > 0) {
        std::vector<float> d2;
        distanceTransform(line, W, H, d2);
        const float r2 = float(gapClose) * float(gapClose) * 0.25f;
        for (size_t i = 0; i < N; ++i) wall[i] = d2[i] <= r2 ? 1 : 0;
    }
    // the shape itself
    QImage shape(W, H, QImage::Format_Grayscale8);
    shape.fill(0);
    {
        QPainter sp(&shape);
        sp.setPen(Qt::NoPen);
        sp.setBrush(Qt::white);
        sp.drawPolygon(poly.translated(-area.left(), -area.top()));
    }
    // the open areas, one by one
    std::vector<int> label(N, -1);
    std::vector<uint8_t> fill(N, 0);
    std::vector<int> stack;
    std::vector<int> members;
    for (size_t start = 0; start < N; ++start) {
        if (wall[start] || label[start] >= 0) continue;
        members.clear();
        stack.clear();
        stack.push_back(int(start));
        label[start] = 1;
        qint64 total = 0, inside = 0;
        bool touchesEdge = false;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            members.push_back(i);
            const int x = i % W, y = i / W;
            ++total;
            if (shape.constScanLine(y)[x]) ++inside;
            if (x == 0 || y == 0 || x == W - 1 || y == H - 1) touchesEdge = true;
            const int nb[4] = {x > 0 ? i - 1 : -1, x < W - 1 ? i + 1 : -1, y > 0 ? i - W : -1, y < H - 1 ? i + W : -1};
            for (int j : nb)
                if (j >= 0 && !wall[size_t(j)] && label[size_t(j)] < 0) {
                    label[size_t(j)] = 1;
                    stack.push_back(j);
                }
        }
        // mostly inside the shape (an area running off the searched area is the background)
        const bool keep = inside > 0 && inside * 10 >= total * 6 && !(touchesEdge && inside * 10 < total * 9);
        if (keep)
            for (int i : members) fill[size_t(i)] = 1;
    }
    // grow back into the thickened lines and under the lines
    const int grow = (gapClose + 1) / 2 + std::max(0, under);
    if (grow > 0) {
        std::vector<float> d2;
        distanceTransform(fill, W, H, d2);
        const float r2 = float(grow) * float(grow);
        for (size_t i = 0; i < N; ++i)
            if (!fill[i] && wall[i] && d2[i] <= r2) fill[i] = 1;
    }
    std::vector<quint8> m(N);
    for (size_t i = 0; i < N; ++i) m[i] = fill[i] ? 255 : 0;
    out.writeRect(area, m.data(), W);
    out.optimize();
    return out;
}

void LassoFillTool::apply(CanvasView* v, const QPolygonF& poly, Qt::KeyboardModifiers mods) {
    Document* d = v->doc();
    RasterLayer* l = paintableLayer(d);
    if (!l) return;
    MaskSurface m(0);
    if (mode == 1) {
        QString err;
        m = enclosedAreas(d, poly, FillSource(source), l, gapClose, under, &err);
        if (!err.isEmpty()) {
            app()->showStatus(T(err), 5000);
            return;
        }
        if (m.exactBounds().isEmpty()) {
            app()->showStatus(T("No closed area inside the shape. Enclose the area more fully or raise \"Close gaps\"."), 5000);
            return;
        }
    } else {
        QPainterPath p;
        p.addPolygon(poly);
        p.closeSubpath();
        m = ops::rasterizePath(p, d->bounds(), antialias);
    }
    bool erase = eraseMode || (mods & Qt::AltModifier) || app()->transparentColor();
    paintThroughMask(d, l, m, app()->foreground(), opacity, erase, erase ? T("Lasso erase") : T("Lasso fill"), usePattern && !erase);
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
    auto* modeBox = new QComboBox(w);
    modeBox->addItem(T("Fill the whole shape"), 0);
    modeBox->addItem(T("Fill the closed areas inside"), 1);
    modeBox->setCurrentIndex(mode);
    modeBox->setToolTip(T("Closed areas: draw roughly around the parts to color; every area of the line art that is mostly inside is filled up to the lines, without gaps under them."));
    f->addWidget(modeBox);
    f->addWidget(patternOptions(w, &usePattern));
    auto* enclosed = new QWidget(w);
    auto* ef = new QVBoxLayout(enclosed);
    ef->setContentsMargins(0, 0, 0, 0);
    auto* src = new QComboBox(enclosed);
    src->addItem(T("Lines: reference layers"), int(FillSource::ReferenceLayers));
    src->addItem(T("Lines: current layer"), int(FillSource::CurrentLayer));
    src->addItem(T("Lines: all visible layers"), int(FillSource::AllVisible));
    src->setCurrentIndex(std::max(0, src->findData(source)));
    connect(src, &QComboBox::activated, this, [this, src](int i) { source = src->itemData(i).toInt(); });
    ef->addWidget(src);
    auto* gap = new ValueBar(T("Close gaps"), 0, 30, 0, enclosed);
    gap->setSuffix(" px");
    gap->setValue(gapClose);
    connect(gap, &ValueBar::valueChanged, this, [this](double v) { gapClose = int(v); });
    ef->addWidget(gap);
    auto* und = new ValueBar(T("Go under the lines"), 0, 10, 0, enclosed);
    und->setSuffix(" px");
    und->setValue(under);
    connect(und, &ValueBar::valueChanged, this, [this](double v) { under = int(v); });
    ef->addWidget(und);
    enclosed->setVisible(mode == 1);
    connect(modeBox, &QComboBox::activated, this, [this, modeBox, enclosed, aa](int i) {
        mode = modeBox->itemData(i).toInt();
        enclosed->setVisible(mode == 1);
        aa->setVisible(mode == 0);
    });
    aa->setVisible(mode == 0);
    f->addWidget(enclosed);
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
