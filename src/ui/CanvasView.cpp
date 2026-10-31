#include "ui/CanvasView.h"

#include <QApplication>
#include <QPaintEvent>
#include <QPainter>
#include <QTabletEvent>

#include "app/TabletInput.h"
#include <QtConcurrent/QtConcurrentMap>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/App.h"
#include "app/BrushLibrary.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"
#include "tools/SelectTools.h"
#include "tools/ToolManager.h"

namespace gx {

bool CanvasView::s_recordingPaint = false;

namespace {
constexpr double kMinZoom = 0.002, kMaxZoom = 64.0;

inline quint32 div255(quint32 v) { return (v + 128 + ((v + 128) >> 8)) >> 8; }

inline quint32 overOpaque(quint32 src, quint32 bg) {
    quint32 a = src >> 24;
    if (a == 255) return src;
    if (a == 0) return bg | 0xff000000u;
    quint32 ia = 255 - a;
    quint32 r = ((src >> 16) & 255) + div255(((bg >> 16) & 255) * ia);
    quint32 g = ((src >> 8) & 255) + div255(((bg >> 8) & 255) * ia);
    quint32 b = (src & 255) + div255((bg & 255) * ia);
    return 0xff000000u | (std::min(r, 255u) << 16) | (std::min(g, 255u) << 8) | std::min(b, 255u);
}

inline quint32 lerpPx(quint32 a, quint32 b, quint32 w) {  // w in 0..256
    quint32 iw = 256 - w;
    quint32 rb = (((a & 0x00ff00ffu) * iw + (b & 0x00ff00ffu) * w) >> 8) & 0x00ff00ffu;
    quint32 ag = ((((a >> 8) & 0x00ff00ffu) * iw + ((b >> 8) & 0x00ff00ffu) * w) >> 8) & 0x00ff00ffu;
    return rb | (ag << 8);
}

struct TileLookup {
    const Projection* proj;
    int level;
    int tx = INT_MIN, ty = INT_MIN;
    const Tile<quint32>* t = nullptr;
    inline quint32 fetch(int x, int y) {
        int ntx = x >> TILE_SHIFT, nty = y >> TILE_SHIFT;
        if (ntx != tx || nty != ty) {
            tx = ntx;
            ty = nty;
            const Projection::Entry* e = proj->entry(level, tileKey(tx, ty));
            t = (e && e->tile) ? e->tile.get() : nullptr;
        }
        if (!t) return 0;
        return t->px ? t->px[((y & (TILE - 1)) << TILE_SHIFT) | (x & (TILE - 1))] : t->uniform;
    }
};
}  // namespace

CanvasView::CanvasView(std::shared_ptr<Document> doc, QWidget* parent, bool scratchpad)
    : QWidget(parent), m_doc(std::move(doc)), m_scratch(scratchpad) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TabletTracking);
    setMouseTracking(true);
    setTabletTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(64, 64);
    m_focus = QPointF(m_doc->size().width() / 2.0, m_doc->size().height() / 2.0);
    m_sinceTablet.start();

    connect(m_doc.get(), &Document::imageChanged, this, &CanvasView::markCanvasDirty);
    connect(m_doc.get(), &Document::sizeChanged, this, [this] { invalidateAll(); });
    connect(m_doc.get(), &Document::selectionChanged, this, [this] {
        m_selDirty = true;
        m_selPainting = false;
        m_selTintValid = false;
        update();
    });
    connect(m_doc.get(), &Document::selectionPainting, this, [this](const QRect& r) {
        m_selPainting = true;
        if (m_selTintValid) updateSelectionTint(r);
        update();
    });
    connect(m_doc.get(), &Document::sizeChanged, this, [this] { m_selTintValid = false; });
    connect(m_doc.get(), &Document::guidesChanged, this, [this] { update(); });
    connect(m_doc.get(), &Document::layersChanged, this, [this] { update(); });
    connect(app()->tools(), &ToolManager::overlayChanged, this, [this] { update(); });
    connect(app()->tools(), &ToolManager::activeChanged, this, [this] { refreshCursor(); update(); });
    connect(app(), &App::prefsChanged, this, [this] { invalidateAll(); refreshCursor(); });
    connect(app(), &App::viewOptionsChanged, this, [this] { invalidateAll(); });
    connect(app()->brushes(), &BrushLibrary::brushChanged, this, [this] { if (m_inside) update(); });
    connect(app()->brushes(), &BrushLibrary::currentChanged, this, [this] { refreshCursor(); if (m_inside) update(); });

    m_ants.setInterval(150);
    connect(&m_ants, &QTimer::timeout, this, [this] {
        if (!m_doc->hasSelection() || !m_doc->showSelection || !isVisible()) return;
        m_antsPhase = (m_antsPhase + 1) % 8;
        update();
    });
    m_ants.start();
}

CanvasView::~CanvasView() = default;

QTransform CanvasView::canvasToWidget() const {
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.rotate(m_rot);
    t.scale(m_flip ? -m_zoom : m_zoom, m_zoom);
    t.translate(-m_focus.x(), -m_focus.y());
    return t;
}

QTransform CanvasView::physicalTransform() const {
    const double dpr = devicePixelRatioF();
    return canvasToWidget() * QTransform::fromScale(dpr, dpr);
}

void CanvasView::viewTransformChanged() {
    update();
    refreshCursor();
    Q_EMIT viewChanged();
}

void CanvasView::setZoom(double z, const QPointF& anchor) {
    z = std::clamp(z, kMinZoom, kMaxZoom);
    QPointF c = toCanvas(anchor);
    m_zoom = z;
    QPointF c2 = toCanvas(anchor);
    m_focus += c - c2;
    viewTransformChanged();
}

void CanvasView::zoomBy(double f, const QPointF& anchor) { setZoom(m_zoom * f, anchor); }

static const double kZoomSteps[] = {0.01, 0.02, 0.03, 0.05, 0.0625, 0.083, 0.1, 0.125, 0.167, 0.2, 0.25, 0.333, 0.5,
                                    0.667, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0, 48.0, 64.0};

void CanvasView::zoomIn() {
    for (double s : kZoomSteps)
        if (s > m_zoom * 1.001) { setZoom(s); return; }
}

void CanvasView::zoomOut() {
    for (int i = int(std::size(kZoomSteps)) - 1; i >= 0; --i)
        if (kZoomSteps[i] < m_zoom * 0.999) { setZoom(kZoomSteps[i]); return; }
}

void CanvasView::setRotation(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg > 180) deg -= 360;
    if (deg <= -180) deg += 360;
    m_rot = deg;
    viewTransformChanged();
}

void CanvasView::setFlipped(bool f) {
    m_flip = f;
    viewTransformChanged();
}

void CanvasView::fitToWindow() {
    QSize s = m_doc->size();
    if (width() < 10 || height() < 10) return;
    m_rot = 0;
    double z = std::min((width() - 24.0) / s.width(), (height() - 24.0) / s.height());
    m_zoom = std::clamp(z, kMinZoom, kMaxZoom);
    m_focus = QPointF(s.width() / 2.0, s.height() / 2.0);
    viewTransformChanged();
}

void CanvasView::actualPixels() { setZoom(1.0); }

void CanvasView::resetView() {
    m_flip = false;
    fitToWindow();
}

void CanvasView::panBy(const QPointF& d) {
    QPointF a = toCanvas(QPointF(0, 0)), b = toCanvas(d);
    m_focus -= (b - a);
    viewTransformChanged();
}

void CanvasView::centerOn(const QPointF& c) {
    m_focus = c;
    viewTransformChanged();
}

QPolygonF CanvasView::visibleCanvas() const {
    return widgetToCanvas().map(QPolygonF(QRectF(0, 0, width(), height())));
}

void CanvasView::invalidateAll() {
    m_main.valid = m_rec.valid = false;
    m_selDirty = true;
    update();
}

void CanvasView::markCanvasDirty(const QRect& r) {
    if (m_rec.valid) {
        QRect rr = m_rec.xf.mapRect(QRectF(r)).toAlignedRect().adjusted(-2, -2, 2, 2) & m_rec.img.rect();
        m_rec.dirty |= rr;
    }
    if (!m_main.valid) { update(); return; }
    QRect pr = m_main.xf.mapRect(QRectF(r)).toAlignedRect().adjusted(-2, -2, 2, 2) & m_main.img.rect();
    if (pr.isEmpty()) return;
    m_main.dirty |= pr;
    const double dpr = devicePixelRatioF();
    update(QRectF(pr.x() / dpr, pr.y() / dpr, pr.width() / dpr, pr.height() / dpr).toAlignedRect().adjusted(-2, -2, 2, 2));
}

void CanvasView::resizeEvent(QResizeEvent*) { m_main.valid = m_rec.valid = false; }

void CanvasView::ensureCache(ViewCache& vc, Projection* proj) {
    const double dpr = devicePixelRatioF();
    QSize phys(std::max(1, int(std::ceil(width() * dpr))), std::max(1, int(std::ceil(height() * dpr))));
    if (vc.img.size() != phys) {
        vc.img = QImage(phys, QImage::Format_RGB32);
        vc.valid = false;
    }
    QTransform xf = physicalTransform();
    if (!vc.valid) {
        vc.dirty = vc.img.rect();
    } else if (xf != vc.xf) {
        bool sameLinear = qFuzzyCompare(xf.m11(), vc.xf.m11()) && qFuzzyCompare(xf.m22() + 10, vc.xf.m22() + 10) &&
                          qFuzzyCompare(xf.m12() + 10, vc.xf.m12() + 10) && qFuzzyCompare(xf.m21() + 10, vc.xf.m21() + 10);
        double ddx = xf.dx() - vc.xf.dx(), ddy = xf.dy() - vc.xf.dy();
        int ix = int(std::lround(ddx)), iy = int(std::lround(ddy));
        if (sameLinear && std::fabs(ddx - ix) < 1e-6 && std::fabs(ddy - iy) < 1e-6 && std::abs(ix) < phys.width() &&
            std::abs(iy) < phys.height()) {
            // Pure scroll: move the pixels, redraw only the uncovered strips.
            const int W = phys.width(), H = phys.height();
            const qsizetype bpl = vc.img.bytesPerLine();
            uchar* bits = vc.img.bits();
            auto copyRow = [&](int dstY, int srcY) {
                quint32* d = reinterpret_cast<quint32*>(bits + bpl * dstY);
                const quint32* s = reinterpret_cast<const quint32*>(bits + bpl * srcY);
                int x0 = std::max(0, ix), x1 = std::min(W, W + ix);
                std::memmove(d + x0, s + (x0 - ix), sizeof(quint32) * size_t(std::max(0, x1 - x0)));
            };
            if (iy > 0)
                for (int y = H - 1; y >= iy; --y) copyRow(y, y - iy);
            else
                for (int y = 0; y < H + iy; ++y) copyRow(y, y - iy);
            QRect moved = vc.dirty.translated(ix, iy);
            vc.dirty = moved & vc.img.rect();
            if (ix > 0) vc.dirty |= QRect(0, 0, ix, H);
            if (ix < 0) vc.dirty |= QRect(W + ix, 0, -ix, H);
            if (iy > 0) vc.dirty |= QRect(0, 0, W, iy);
            if (iy < 0) vc.dirty |= QRect(0, H + iy, W, -iy);
        } else {
            vc.dirty = vc.img.rect();
        }
    }
    vc.xf = xf;
    vc.valid = true;
    if (vc.dirty.isEmpty()) {
        vc.complete = true;
        return;
    }

    // Which projection level to sample
    double s = std::sqrt(std::fabs(xf.determinant()));
    int maxL = proj->maxLevel();
    int L = 0;
    if (s < 1.0) L = std::clamp(int(std::floor(std::log2(1.0 / s))), 0, maxL);

    QRect dirty = vc.dirty;
    QRect canvasArea = xf.inverted().mapRect(QRectF(dirty)).toAlignedRect().adjusted(-2, -2, 2, 2) & m_doc->bounds();
    bool complete = true;
    if (!canvasArea.isEmpty()) {
        int sh = TILE_SHIFT + L;
        int x0 = canvasArea.left() >> sh, y0 = canvasArea.top() >> sh, x1 = canvasArea.right() >> sh, y1 = canvasArea.bottom() >> sh;
        QVector<qint64> keys;
        QPointF center = xf.inverted().map(QPointF(phys.width() / 2.0, phys.height() / 2.0));
        std::vector<std::pair<double, qint64>> order;
        for (int ty = y0; ty <= y1; ++ty)
            for (int tx = x0; tx <= x1; ++tx) {
                QRect tr = Projection::levelTileRect(L, tx, ty);
                double d = std::hypot(tr.center().x() - center.x(), tr.center().y() - center.y());
                order.push_back({d, tileKey(tx, ty)});
            }
        std::sort(order.begin(), order.end());
        for (auto& o : order) keys << o.second;
        complete = proj->update(L, keys, 14.0);
        for (qint64 k : keys) proj->touch(L, k);
    }
    renderRect(vc, proj, dirty, L);
    vc.dirty = complete ? QRect() : dirty;
    if (&vc == &m_main) m_moreWork = !complete;
    vc.complete = complete;
    if (++m_frame % 90 == 0) {
        proj->nextFrame();
        proj->trim(app()->prefs().projectionCacheMB * 16);
    }
}

bool CanvasView::renderRect(ViewCache& vc, const Projection* proj, const QRect& pr, int L) {
    const QTransform inv = vc.xf.inverted();
    const double m11 = inv.m11(), m12 = inv.m12(), m21 = inv.m21(), m22 = inv.m22(), dx = inv.dx(), dy = inv.dy();
    const Preferences& prefs = app()->prefs();
    const QString theme = prefs.theme;
    const QColor ws = theme == "light" ? prefs.workspaceLight : theme == "gray" ? prefs.workspaceGray : prefs.workspaceDark;
    const quint32 wsc = 0xff000000u | (quint32(ws.red()) << 16) | (quint32(ws.green()) << 8) | quint32(ws.blue());
    const bool checker = prefs.checkerboard;
    auto rgb = [](const QColor& c) { return (quint32(c.red()) << 16) | (quint32(c.green()) << 8) | quint32(c.blue()); };
    const quint32 c1 = rgb(prefs.checker1), c2 = rgb(prefs.checker2), paper = rgb(prefs.paperColor);
    const int cs = std::max(2, int(prefs.checkerSize * devicePixelRatioF()));
    const double scale = std::sqrt(std::fabs(vc.xf.determinant()));
    const double lvScale = scale * double(1 << L);  // screen px per level px
    const bool rotated = std::fabs(vc.xf.m12()) > 1e-9 || std::fabs(vc.xf.m21()) > 1e-9;
    const bool nearest = (lvScale >= 1.999 && prefs.pixelatedZoomIn) || (!rotated && std::fabs(lvScale - 1.0) < 1e-6);
    const bool gray = app()->grayscaleView() && !m_scratch;
    const int CW = m_doc->size().width(), CH = m_doc->size().height();
    const int LW = (CW + (1 << L) - 1) >> L, LH = (CH + (1 << L) - 1) >> L;
    const double inv2L = 1.0 / double(1 << L);
    uchar* bits = vc.img.bits();
    const qsizetype bpl = vc.img.bytesPerLine();

    const int bandH = 16;
    std::vector<int> bands;
    for (int y = pr.top(); y <= pr.bottom(); y += bandH) bands.push_back(y);
    auto work = [&](int& y0) {
        TileLookup lk{proj, L};
        const int yEnd = std::min(pr.bottom(), y0 + bandH - 1);
        for (int y = y0; y <= yEnd; ++y) {
            quint32* row = reinterpret_cast<quint32*>(bits + bpl * y);
            double px = pr.left() + 0.5, py = y + 0.5;
            double cx = m11 * px + m21 * py + dx, cy = m12 * px + m22 * py + dy;
            const int checkRow = (y / cs) & 1;
            for (int x = pr.left(); x <= pr.right(); ++x, cx += m11, cy += m12) {
                if (cx < 0 || cy < 0 || cx >= CW || cy >= CH) {
                    row[x] = wsc;
                    continue;
                }
                quint32 bg = checker ? ((((x / cs) & 1) ^ checkRow) ? c1 : c2) : paper;
                quint32 src;
                double lx = cx * inv2L, ly = cy * inv2L;
                if (nearest) {
                    int ix = std::min(LW - 1, int(lx)), iy = std::min(LH - 1, int(ly));
                    src = lk.fetch(ix, iy);
                } else {
                    double fx0 = lx - 0.5, fy0 = ly - 0.5;
                    int ix = int(std::floor(fx0)), iy = int(std::floor(fy0));
                    quint32 wx = quint32((fx0 - ix) * 256.0), wy = quint32((fy0 - iy) * 256.0);
                    int ix0 = std::clamp(ix, 0, LW - 1), ix1 = std::clamp(ix + 1, 0, LW - 1);
                    int iy0 = std::clamp(iy, 0, LH - 1), iy1 = std::clamp(iy + 1, 0, LH - 1);
                    quint32 a = lk.fetch(ix0, iy0), b = lk.fetch(ix1, iy0);
                    quint32 c = lk.fetch(ix0, iy1), d = lk.fetch(ix1, iy1);
                    src = lerpPx(lerpPx(a, b, wx), lerpPx(c, d, wx), wy);
                }
                row[x] = overOpaque(src, bg);
                if (gray) {
                    const quint32 c = row[x];
                    const quint32 l = (((c >> 16) & 255u) * 77u + ((c >> 8) & 255u) * 150u + (c & 255u) * 29u) >> 8;
                    row[x] = 0xff000000u | (l << 16) | (l << 8) | l;
                }
            }
        }
    };
    if (bands.size() <= 1) {
        for (int& b : bands) work(b);
    } else {
        parallelFor(int(bands.size()), [&](int i) { work(bands[size_t(i)]); });
    }
    return true;
}

void CanvasView::paintEvent(QPaintEvent* ev) {
    if (s_recordingPaint) {
        // A frame for a screen recording (MainWindow::grab): the picture without the layers
        // hidden from recordings, drawn completely (no half-computed tiles in the video).
        Projection* proj = m_doc->recordingProjection();
        for (int i = 0; i < 40; ++i) {
            ensureCache(m_rec, proj);
            if (m_rec.complete) break;
        }
        QPainter p(this);
        m_rec.img.setDevicePixelRatio(devicePixelRatioF());
        p.drawImage(QPointF(0, 0), m_rec.img);
        paintOverlays(p);
        return;
    }
    ensureCache(m_main, m_doc->projection());
    QPainter p(this);
    m_main.img.setDevicePixelRatio(devicePixelRatioF());
    p.setClipRect(ev->rect());
    p.drawImage(QPointF(0, 0), m_main.img);
    paintOverlays(p);
    if (m_moreWork) QTimer::singleShot(0, this, [this] { update(); });
}

void CanvasView::paintOverlays(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing, true);
    // canvas border
    QPolygonF border = canvasToWidget().map(QPolygonF(QRectF(m_doc->bounds())));
    p.setPen(QPen(QColor(0, 0, 0, 90), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(border);

    if (app()->prefs().showPixelGrid && m_zoom >= 8.0) {
        QRectF vis = visibleCanvas().boundingRect() & QRectF(m_doc->bounds());
        int x0 = int(std::floor(vis.left())), x1 = int(std::ceil(vis.right()));
        int y0 = int(std::floor(vis.top())), y1 = int(std::ceil(vis.bottom()));
        if ((x1 - x0) + (y1 - y0) < 1200) {
            p.save();
            p.setTransform(canvasToWidget(), true);
            QPen pen(QColor(128, 128, 128, 90), 0);
            pen.setCosmetic(true);
            p.setPen(pen);
            for (int x = x0; x <= x1; ++x) p.drawLine(QPointF(x, y0), QPointF(x, y1));
            for (int y = y0; y <= y1; ++y) p.drawLine(QPointF(x0, y), QPointF(x1, y));
            p.restore();
        }
    }
    drawCompositionGrid(p);
    drawGuides(p);
    drawSelection(p);
    app()->tools()->drawOverlay(p, this);
    drawBrushOutline(p);
}

// View > Grid and guides: a drawing grid and composition helpers. Only shown, never painted.
void CanvasView::drawCompositionGrid(QPainter& p) {
    const Preferences& pr = app()->prefs();
    if (!pr.showGrid && !pr.showThirds && !pr.showGoldenRatio && !pr.showCenterLines) return;
    const QRectF b(m_doc->bounds());
    QRectF vis = visibleCanvas().boundingRect() & b;
    if (vis.isEmpty()) return;
    p.save();
    p.setTransform(canvasToWidget(), true);
    auto pen = [](QColor c, int alpha, qreal w = 1.0) {
        c.setAlpha(alpha);
        QPen q(c, w);
        q.setCosmetic(true);
        return q;
    };
    if (pr.showGrid && pr.gridSize > 0) {
        const double step = pr.gridSize;
        const double sub = step / std::max(1, pr.gridSubdivisions);
        // skip the fine lines when they would be closer than 6 screen pixels
        auto lines = [&](double st, const QPen& q) {
            if (st * m_zoom < 6) return;
            p.setPen(q);
            for (double x = std::ceil(vis.left() / st) * st; x <= vis.right(); x += st) p.drawLine(QPointF(x, vis.top()), QPointF(x, vis.bottom()));
            for (double y = std::ceil(vis.top() / st) * st; y <= vis.bottom(); y += st) p.drawLine(QPointF(vis.left(), y), QPointF(vis.right(), y));
        };
        if (pr.gridSubdivisions > 1) lines(sub, pen(pr.gridColor, 45));
        lines(step, pen(pr.gridColor, 130));
    }
    auto vline = [&](double f) { p.drawLine(QPointF(b.left() + b.width() * f, b.top()), QPointF(b.left() + b.width() * f, b.bottom())); };
    auto hline = [&](double f) { p.drawLine(QPointF(b.left(), b.top() + b.height() * f), QPointF(b.right(), b.top() + b.height() * f)); };
    if (pr.showThirds) {
        p.setPen(pen(QColor(255, 120, 60), 170, 1.2));
        for (double f : {1.0 / 3, 2.0 / 3}) { vline(f); hline(f); }
    }
    if (pr.showGoldenRatio) {
        p.setPen(pen(QColor(240, 200, 40), 170, 1.2));
        for (double f : {0.381966, 0.618034}) { vline(f); hline(f); }
    }
    if (pr.showCenterLines) {
        p.setPen(pen(QColor(60, 200, 120), 170, 1.2));
        vline(0.5);
        hline(0.5);
    }
    p.restore();
}

void CanvasView::drawGuides(QPainter& p) {
    const Perspective& ps = m_doc->perspective;
    QRectF vis = visibleCanvas().boundingRect();
    if (ps.enabled) {
        p.save();
        p.setTransform(canvasToWidget(), true);
        QColor c = ps.color;
        c.setAlpha(110);
        QPen pen(c, 0);
        pen.setCosmetic(true);
        p.setPen(pen);
        double reach = std::hypot(vis.width(), vis.height()) * 2 + 1e4;
        for (int i = 0; i < ps.points; ++i) {
            QPointF vp = ps.vp[i];
            int n = std::max(4, ps.gridLines);
            for (int k = 0; k < n; ++k) {
                double a = 2 * M_PI * k / n;
                p.drawLine(vp, vp + QPointF(std::cos(a), std::sin(a)) * reach);
            }
        }
        if (ps.points >= 2) {
            QPen hp(QColor(ps.color.red(), ps.color.green(), ps.color.blue(), 200), 0);
            hp.setCosmetic(true);
            p.setPen(hp);
            p.drawLine(ps.horizon());
        }
        p.restore();
    }
    const Ruler& ru = m_doc->ruler;
    if (ru.isOn()) {
        p.save();
        p.setTransform(canvasToWidget(), true);
        p.setBrush(Qt::NoBrush);
        QColor c = ru.color;
        c.setAlpha(ru.kind == Ruler::Concentric || ru.kind == Ruler::Parallel || ru.kind == Ruler::Fan ? 90 : 120);
        QPen helper(c, 0, Qt::DashLine);
        helper.setCosmetic(true);
        p.setPen(helper);
        p.drawPath(ru.guidePath(vis));
        QPen main(QColor(ru.color.red(), ru.color.green(), ru.color.blue(), ru.snap ? 230 : 140), 2);
        main.setCosmetic(true);
        p.setPen(main);
        p.drawPath(ru.mainPath());
        p.restore();
    }
    const Symmetry& sy = m_doc->symmetry;
    if (sy.isOn()) {
        p.save();
        p.setTransform(canvasToWidget(), true);
        QPen pen(QColor(255, 80, 160, 170), 0, Qt::DashLine);
        pen.setCosmetic(true);
        p.setPen(pen);
        double reach = std::hypot(vis.width(), vis.height()) + 1e4;
        auto axis = [&](double deg) {
            double a = qDegreesToRadians(deg);
            QPointF d(std::cos(a), std::sin(a));
            p.drawLine(sy.center - d * reach, sy.center + d * reach);
        };
        if (sy.mode == Symmetry::Vertical || sy.mode == Symmetry::Both) axis(sy.angle + 90);
        if (sy.mode == Symmetry::Horizontal || sy.mode == Symmetry::Both) axis(sy.angle);
        if (sy.mode == Symmetry::Radial) {
            int n = std::max(2, sy.radialCount);
            for (int i = 0; i < n; ++i) {
                double a = qDegreesToRadians(sy.angle + 90 + 360.0 * i / n);
                p.drawLine(sy.center, sy.center + QPointF(std::cos(a), std::sin(a)) * reach);
            }
        }
        p.restore();
    }
}

void CanvasView::rebuildSelectionPath() {
    m_selDirty = false;
    m_selPath = QPainterPath();
    if (!m_doc->hasSelection()) return;
    QRect b = m_doc->selectionBounds();
    if (b.isEmpty()) return;
    int sc = 1;
    while (qint64(b.width() / sc) * (b.height() / sc) > 4000000) sc *= 2;
    const int W = (b.width() + sc - 1) / sc + 2, H = (b.height() + sc - 1) / sc + 2;
    std::vector<uint8_t> in(size_t(W) * H, 0);
    std::vector<quint8> row(size_t(b.width()));
    for (int j = 1; j < H - 1; ++j) {
        int cy = b.top() + (j - 1) * sc + sc / 2;
        if (cy > b.bottom()) cy = b.bottom();
        m_doc->selection().readRect(QRect(b.left(), cy, b.width(), 1), row.data(), b.width());
        for (int i = 1; i < W - 1; ++i) {
            int cx = (i - 1) * sc + sc / 2;
            if (cx >= b.width()) cx = b.width() - 1;
            in[size_t(j) * W + i] = row[size_t(cx)] >= 128;
        }
    }
    auto X = [&](int i) { return double(b.left() + (i - 1) * sc); };
    auto Y = [&](int j) { return double(b.top() + (j - 1) * sc); };
    // horizontal edges
    for (int j = 1; j < H; ++j) {
        int start = -1;
        for (int i = 0; i <= W; ++i) {
            bool edge = i < W && in[size_t(j - 1) * W + i] != in[size_t(j) * W + i];
            if (edge && start < 0) start = i;
            if (!edge && start >= 0) {
                m_selPath.moveTo(X(start), Y(j));
                m_selPath.lineTo(X(i), Y(j));
                start = -1;
            }
        }
    }
    for (int i = 1; i < W; ++i) {
        int start = -1;
        for (int j = 0; j <= H; ++j) {
            bool edge = j < H && in[size_t(j) * W + i - 1] != in[size_t(j) * W + i];
            if (edge && start < 0) start = j;
            if (!edge && start >= 0) {
                m_selPath.moveTo(X(i), Y(start));
                m_selPath.lineTo(X(i), Y(j));
                start = -1;
            }
        }
    }
}

bool CanvasView::wantsSelectionTint() const {
    auto* pen = qobject_cast<SelectionBrushTool*>(app()->tools()->active());
    return pen && pen->showTint;
}

void CanvasView::updateSelectionTint(const QRect& cr) {
    const double s = m_selTintScale;
    const QRect tr = QRect(QPoint(int(std::floor(cr.left() * s)) - 1, int(std::floor(cr.top() * s)) - 1),
                           QPoint(int(std::ceil((cr.right() + 1) * s)), int(std::ceil((cr.bottom() + 1) * s)))) &
                     m_selTint.rect();
    if (tr.isEmpty()) return;
    const QSize ds = m_doc->size();
    std::vector<quint8> row(size_t(ds.width()));
    const QColor c(110, 90, 255);
    for (int y = tr.top(); y <= tr.bottom(); ++y) {
        const int cy = std::clamp(int((y + 0.5) / s), 0, ds.height() - 1);
        const int x0 = std::clamp(int((tr.left() + 0.5) / s), 0, ds.width() - 1), x1 = std::clamp(int((tr.right() + 0.5) / s), 0, ds.width() - 1);
        m_doc->selection().readRect(QRect(x0, cy, x1 - x0 + 1, 1), row.data(), x1 - x0 + 1);
        QRgb* out = reinterpret_cast<QRgb*>(m_selTint.scanLine(y));
        for (int x = tr.left(); x <= tr.right(); ++x) {
            const int cx = std::clamp(int((x + 0.5) / s), x0, x1);
            const int a = row[size_t(cx - x0)] * 100 / 255;  // up to ~40% strong
            out[x] = qRgba(c.red() * a / 255, c.green() * a / 255, c.blue() * a / 255, a);
        }
    }
}

void CanvasView::drawSelection(QPainter& p) {
    if (!m_doc->hasSelection() || !m_doc->showSelection) return;
    if (wantsSelectionTint()) {
        if (!m_selTintValid) {
            const QSize ds = m_doc->size();
            m_selTintScale = std::min(1.0, 2048.0 / std::max(1, std::max(ds.width(), ds.height())));
            m_selTint = QImage(std::max(1, int(std::ceil(ds.width() * m_selTintScale))), std::max(1, int(std::ceil(ds.height() * m_selTintScale))),
                               QImage::Format_ARGB32_Premultiplied);
            m_selTint.fill(Qt::transparent);
            m_selTintValid = true;
            updateSelectionTint(m_doc->selectionBounds());
        }
        p.save();
        p.setTransform(canvasToWidget(), true);
        p.scale(1.0 / m_selTintScale, 1.0 / m_selTintScale);
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom * m_selTintScale < 1.0);
        p.drawImage(0, 0, m_selTint);
        p.restore();
    }
    if (m_selPainting) return;  // the outline is rebuilt when the stroke ends
    if (m_selDirty) rebuildSelectionPath();
    if (m_selPath.isEmpty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setTransform(canvasToWidget(), true);
    QPen white(Qt::white, 0);
    white.setCosmetic(true);
    p.setPen(white);
    p.drawPath(m_selPath);
    QPen black(Qt::black, 0);
    black.setCosmetic(true);
    black.setDashPattern({4, 4});
    black.setDashOffset(m_antsPhase);
    p.setPen(black);
    p.drawPath(m_selPath);
    p.restore();
}

void CanvasView::drawBrushOutline(QPainter& p) {
    if (!m_inside) return;
    Tool* t = app()->tools()->active();
    if (!t || !t->showsBrushOutline() || !app()->prefs().showBrushOutline) return;
    if (app()->prefs().hideCursorWhilePainting && app()->tools()->isDragging() && (m_tabletDown || m_mouseDown)) return;
    double r = t->outlineDiameter() * 0.5 * m_zoom;
    QPointF c = m_hover;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    if (r >= 3) {
        p.setPen(QPen(QColor(0, 0, 0, 160), 1.6));
        p.drawEllipse(c, r, r);
        p.setPen(QPen(QColor(255, 255, 255, 220), 0.8));
        p.drawEllipse(c, r, r);
    }
    if (app()->prefs().crosshair || r < 3) {
        p.setPen(QPen(QColor(0, 0, 0, 180), 1.5));
        p.drawLine(c - QPointF(4, 0), c + QPointF(4, 0));
        p.drawLine(c - QPointF(0, 4), c + QPointF(0, 4));
        p.setPen(QPen(QColor(255, 255, 255, 220), 0.7));
        p.drawLine(c - QPointF(4, 0), c + QPointF(4, 0));
        p.drawLine(c - QPointF(0, 4), c + QPointF(0, 4));
    }
    p.restore();
}

void CanvasView::refreshCursor() {
    Tool* t = app()->tools()->active();
    if (t && t->showsBrushOutline() && app()->prefs().showBrushOutline) setCursor(Qt::BlankCursor);
    else setCursor(t ? t->cursor(this) : QCursor(Qt::ArrowCursor));
}

PointerEvent CanvasView::makeEvent(const QPointF& wpos, double pressure, Qt::MouseButton b, Qt::MouseButtons bs,
                                   Qt::KeyboardModifiers m, double timeMs, bool tablet) const {
    PointerEvent e;
    e.widgetPos = wpos;
    e.pos = toCanvas(wpos);
    e.pressure = pressure;
    e.button = b;
    e.buttons = bs;
    e.mods = m;
    e.timeMs = timeMs;
    e.tablet = tablet;
    e.view = const_cast<CanvasView*>(this);
    return e;
}

void CanvasView::tabletEvent(QTabletEvent* ev) {
    ev->accept();
    if (TabletInput::shouldIgnore(ev)) return;  // same pen reported twice (Wintab + Windows Ink)
    m_sinceTablet.restart();
    double pressure = ev->pressure();
    if (const double th = app()->prefs().pressureThreshold; th > 0) pressure = std::max(0.0, (pressure - th) / (1.0 - th));
    PointerEvent e = makeEvent(ev->position(), pressure, ev->button(), ev->buttons(), ev->modifiers(),
                               double(ev->timestamp()), true);
    e.tiltX = ev->xTilt();
    e.tiltY = ev->yTilt();
    e.rotation = ev->rotation();
    e.eraser = ev->pointerType() == QPointingDevice::PointerType::Eraser;
    m_hover = ev->position();
    switch (ev->type()) {
    case QEvent::TabletPress:
        if (m_tabletDown) break;
        m_tabletDown = true;
        m_tabletButton = ev->button();
        if (!m_scratch) app()->setActiveView(this);
        Q_EMIT activated(this);
        setFocus();
        app()->tools()->press(e);
        break;
    case QEvent::TabletMove:
        if (m_tabletDown) {
            e.button = m_tabletButton;
            app()->tools()->move(e);
        } else {
            app()->tools()->hover(e);
        }
        Q_EMIT cursorMoved(e.pos);
        if (m_inside) update();
        break;
    case QEvent::TabletRelease:
        if (!m_tabletDown) break;
        m_tabletDown = false;
        e.button = m_tabletButton;
        e.pressure = 0;
        app()->tools()->release(e);
        break;
    default: break;
    }
}

void CanvasView::mousePressEvent(QMouseEvent* ev) {
    if (m_tabletDown || m_sinceTablet.elapsed() < 200) { ev->accept(); return; }
    m_mouseDown = true;
    if (!m_scratch) app()->setActiveView(this);
    Q_EMIT activated(this);
    setFocus();
    PointerEvent e = makeEvent(ev->position(), app()->prefs().mousePressure, ev->button(), ev->buttons(), ev->modifiers(),
                               double(ev->timestamp()), false);
    app()->tools()->press(e);
}

void CanvasView::mouseMoveEvent(QMouseEvent* ev) {
    m_hover = ev->position();
    if (m_tabletDown || m_sinceTablet.elapsed() < 200) {
        if (m_inside) update();
        return;
    }
    PointerEvent e = makeEvent(ev->position(), app()->prefs().mousePressure, Qt::NoButton, ev->buttons(), ev->modifiers(),
                               double(ev->timestamp()), false);
    if (m_mouseDown) app()->tools()->move(e);
    else app()->tools()->hover(e);
    Q_EMIT cursorMoved(e.pos);
    update();
}

void CanvasView::mouseReleaseEvent(QMouseEvent* ev) {
    if (!m_mouseDown) return;
    if (ev->buttons() != Qt::NoButton) return;  // wait until all buttons are up
    m_mouseDown = false;
    PointerEvent e = makeEvent(ev->position(), 0, ev->button(), ev->buttons(), ev->modifiers(), double(ev->timestamp()), false);
    app()->tools()->release(e);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* ev) {
    PointerEvent e = makeEvent(ev->position(), 1, ev->button(), ev->buttons(), ev->modifiers(), double(ev->timestamp()), false);
    app()->tools()->doubleClick(e);
}

void CanvasView::wheelEvent(QWheelEvent* ev) {
    QPoint ad = ev->angleDelta();
    double steps = (ad.y() != 0 ? ad.y() : ad.x()) / 120.0;
    if (app()->prefs().invertWheel) steps = -steps;
    if (steps == 0) return;
    const auto mods = ev->modifiers();
    if ((mods & Qt::ControlModifier) && app()->prefs().ctrlWheelBrushSize) {
        BrushLibrary* b = app()->brushes();
        BrushSettings s = b->current();
        s.size = std::clamp(s.size * std::pow(1.12, steps), 0.5, 5000.0);
        b->update(b->currentIndex(), s);
        update();
    } else if (mods & Qt::AltModifier) {
        rotateBy(app()->prefs().rotateStepDeg * (steps > 0 ? 1 : -1));
    } else if (mods & Qt::ShiftModifier) {
        panBy(QPointF(steps * 60, 0));
    } else if (app()->prefs().wheelZooms) {
        zoomBy(std::pow(1.0 + app()->prefs().zoomStepPercent / 100.0, steps), ev->position());
    } else {
        panBy(QPointF(0, steps * 60));
    }
    ev->accept();
}

void CanvasView::keyPressEvent(QKeyEvent* e) {
    if (app()->tools()->keyPress(e, this)) {
        e->accept();
        refreshCursor();
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}

void CanvasView::keyReleaseEvent(QKeyEvent* e) {
    if (app()->tools()->keyRelease(e, this)) {
        e->accept();
        refreshCursor();
        update();
        return;
    }
    refreshCursor();
    QWidget::keyReleaseEvent(e);
}

bool CanvasView::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride) {
        // Let held keys (Space, Alt...) reach the canvas instead of menu shortcuts.
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Space && !(k->modifiers() & ~(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier))) {
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}

void CanvasView::enterEvent(QEnterEvent*) {
    m_inside = true;
    QWidget* f = QApplication::focusWidget();
    // Do not steal focus from text fields
    if (!f || !(f->inherits("QLineEdit") || f->inherits("QTextEdit") || f->inherits("QPlainTextEdit") || f->inherits("QAbstractSpinBox")))
        setFocus();
    refreshCursor();
    update();
}

void CanvasView::leaveEvent(QEvent*) {
    m_inside = false;
    update();
}

void CanvasView::focusOutEvent(QFocusEvent*) { app()->tools()->clearKeys(); }

void CanvasView::focusInEvent(QFocusEvent*) {
    if (!m_scratch) app()->setActiveView(this);
}

}  // namespace gx
