#include "brush/BrushStroke.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"

namespace gx {

namespace {
thread_local std::vector<float> tlCov, tlSel, tlDst, tlMask, tlAux;
thread_local std::vector<quint8> tlMaskU8;

double dist(const QPointF& a, const QPointF& b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

InputSample lerpSample(const InputSample& a, const InputSample& b, double t) {
    InputSample o = b;
    o.pos = a.pos + (b.pos - a.pos) * t;
    o.pressure = a.pressure + (b.pressure - a.pressure) * t;
    o.tiltX = a.tiltX + (b.tiltX - a.tiltX) * t;
    o.tiltY = a.tiltY + (b.tiltY - a.tiltY) * t;
    o.rotation = a.rotation + (b.rotation - a.rotation) * t;
    o.timeMs = a.timeMs + (b.timeMs - a.timeMs) * t;
    return o;
}

PressureCurve& localCurveFor(const BrushSettings& b) {
    thread_local PressureCurve c;
    thread_local QVector<QPointF> last;
    if (last != b.pressureCurve) {
        c.set(b.pressureCurve);
        last = b.pressureCurve;
    }
    return c;
}
}  // namespace

BrushStroke::BrushStroke(const StrokeContext& ctx) : m_ctx(ctx), m_rng(std::random_device{}()) {
    m_engine = ctx.brush.engine;
    if (ctx.transparentColor && m_engine == BrushEngine::Paint) m_engine = BrushEngine::Erase;
    if (ctx.targetMask && (m_engine == BrushEngine::Mix || m_engine == BrushEngine::Smudge)) m_engine = BrushEngine::Paint;

    m_emitters.push_back(Emitter{});
    for (const QTransform& t : ctx.symmetry) {
        Emitter e;
        e.xf = t;
        m_emitters.push_back(e);
    }
    auto& lib = TextureLibrary::instance();
    if (ctx.brush.textureStrength > 0 && !ctx.brush.texture.isEmpty()) m_texture = lib.findAny(ctx.brush.texture);
    if (!ctx.brush.shape.isEmpty()) m_shape = lib.get(TextureKind::Shape, ctx.brush.shape);
    if (!ctx.brush.stampSet.isEmpty()) m_stamps = lib.stampSet(ctx.brush.stampSet);
    if (!ctx.brush.ribbon.isEmpty()) {
        m_ribbon = lib.get(TextureKind::Ribbon, ctx.brush.ribbon);
        // the pattern keeps its proportions at the full brush size (pressure only makes it narrower)
        if (m_ribbon) m_ribbonRepeat = std::max(1.0, m_ribbon->w * (ctx.brush.size / std::max(1, m_ribbon->h)) * ctx.brush.ribbonLength);
    }

    m_color[0] = float(ctx.color.redF());
    m_color[1] = float(ctx.color.greenF());
    m_color[2] = float(ctx.color.blueF());
    jitterColor(m_color);
    m_sub[0] = float(ctx.subColor.redF());
    m_sub[1] = float(ctx.subColor.greenF());
    m_sub[2] = float(ctx.subColor.blueF());

    // Hatching: the lines of the bundle get their place, width and start once per stroke,
    // so every stroke looks a little different, like drawn by hand.
    const BrushSettings& b = ctx.brush;
    if (b.bundleLines > 1) {
        const int N = b.bundleLines;
        m_lines.resize(size_t(N));
        for (int i = 0; i < N; ++i) {
            Line& L = m_lines[size_t(i)];
            L.offset = (i + 0.5) / N - 0.5 + (m_uni(m_rng) - 0.5) * 0.3 / N;
            L.widthK = 1.0 - b.bundleThicknessJitter * 0.8 * m_uni(m_rng);
            L.startPx = m_uni(m_rng) * b.bundleLengthJitter * b.size * 2.5;
            L.phase = m_uni(m_rng) * 2 * M_PI;
            L.freq = 2 * M_PI / std::max(4.0, b.size * (2.0 + m_uni(m_rng) * 4.0));
        }
        // exactly this share of the lines gets the second color (picked at random)
        int subs = int(std::lround(N * b.subColorShare));
        if (b.subColorShare > 0.02 && subs == 0) subs = 1;
        std::vector<int> idx(static_cast<size_t>(N), 0);
        for (int i = 0; i < N; ++i) idx[size_t(i)] = i;
        std::shuffle(idx.begin(), idx.end(), m_rng);
        for (int k = 0; k < std::min(subs, N); ++k) m_lines[size_t(idx[size_t(k)])].sub = true;
        for (auto& e : m_emitters) e.lineCarry.resize(size_t(N));
    }

    m_useSelection = ctx.doc->hasSelection();
    m_alphaLock = !ctx.targetMask && (ctx.layer->alphaLock || ctx.brush.keepOpacity);

    // The mixing brush picks colors up mostly from the canvas as it was when the stroke
    // started, otherwise every dab would only see the paint of the previous dab (which
    // overlaps it by ~95%) and the brush would never mix with what is underneath.
    if (m_engine == BrushEngine::Mix && !ctx.targetMask && ctx.layer->isRaster())
        m_origin = std::make_unique<RasterSurface>(static_cast<RasterLayer*>(ctx.layer)->pixels);

    const bool buffered = m_engine == BrushEngine::Paint || m_engine == BrushEngine::Erase;
    if (buffered) {
        if (ctx.targetMask) ctx.layer->previewMask = std::make_shared<MaskSurface>(ctx.layer->mask);
        else if (ctx.layer->isRaster()) {
            auto* r = static_cast<RasterLayer*>(ctx.layer);
            r->preview = std::make_shared<RasterSurface>(r->pixels);
        }
    }
}

BrushStroke::~BrushStroke() {
    if (!m_ended) cancel();
}

float BrushStroke::mapPressure(double p) const {
    if (m_ctx.brush.customPressureCurve) return localCurveFor(m_ctx.brush).map(float(p));
    if (m_ctx.pressureCurve) return m_ctx.pressureCurve->map(float(p));
    return clamp01(float(p));
}

// SAI's "Prs. Hard <-> Soft": 100 leaves the pressure as it is, 200 = a light touch is enough,
// 0 = it takes a hard press.
static double soften(double pr, double softness) {
    pr = std::clamp(pr, 0.0, 1.0);
    if (std::abs(softness - 100.0) < 0.5) return pr;
    return std::pow(pr, std::pow(2.0, (100.0 - softness) / 50.0));
}

double BrushStroke::diameterFor(double pr, double taper) const {
    const BrushSettings& b = m_ctx.brush;
    double f = b.sizePressure ? b.minSize + (1.0 - b.minSize) * soften(pr, b.sizeSoftness) : 1.0;
    return std::max(0.0, b.size * f * taper);
}

void BrushStroke::jitterColor(float* rgb) {
    const BrushSettings& b = m_ctx.brush;
    if (b.hueJitter <= 0 && b.saturationJitter <= 0 && b.valueJitter <= 0) return;
    QColor c = QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    if (h < 0) h = 0;
    h += float((m_uni(m_rng) - 0.5) * b.hueJitter);
    h -= std::floor(h);
    s = clamp01(s + float((m_uni(m_rng) - 0.5) * b.saturationJitter));
    v = clamp01(v + float((m_uni(m_rng) - 0.5) * b.valueJitter));
    c = QColor::fromHsvF(h, s, v);
    rgb[0] = float(c.redF());
    rgb[1] = float(c.greenF());
    rgb[2] = float(c.blueF());
}

float BrushStroke::textureValue(float v) const {
    const BrushSettings& b = m_ctx.brush;
    if (b.textureInvert) v = 1.f - v;
    // density: lower = only the high grain takes paint, higher = the grain fills in
    v = clamp01(v + (float(b.textureDensity) - 0.5f) * 2.f);
    float s = float(b.textureStrength);
    return 1.f - s + s * v;
}

float BrushStroke::textureAt(int x, int y) const {
    if (!m_texture) return 1.f;
    float sc = float(std::max(0.05, m_ctx.brush.textureScale));
    return textureValue(m_texture->sample((x + 0.5f) / sc, (y + 0.5f) / sc));
}

float BrushStroke::ribbonAt(int x, int y, const TexFrame& tf, double diameter, float* dist) const {
    if (!m_ribbon) return 1.f;
    const double X = x + 0.5, Y = y + 0.5;
    double u, across, w = diameter;
    if (tf.hasPrev) {
        // the pixel's place on the center line between the previous dab and this one:
        // neighbouring dabs agree there, so the pattern bends smoothly with the stroke
        const double sx = tf.cx - tf.px, sy = tf.cy - tf.py;
        const double L2 = sx * sx + sy * sy;
        const double t = L2 > 1e-9 ? std::clamp(((X - tf.px) * sx + (Y - tf.py) * sy) / L2, 0.0, 1.0) : 1.0;
        const double qx = tf.px + sx * t, qy = tf.py + sy * t;
        const double L = std::sqrt(std::max(L2, 1e-18));
        const double dx = L2 > 1e-9 ? sx / L : tf.dx, dy = L2 > 1e-9 ? sy / L : tf.dy;
        u = tf.palong + (tf.along - tf.palong) * t;
        const double side = -(X - tf.px) * dy + (Y - tf.py) * dx;
        across = std::copysign(std::hypot(X - qx, Y - qy), side);
        w = tf.pdiam + (diameter - tf.pdiam) * t;
        if (dist) *dist = float(std::hypot(X - qx, Y - qy) + (t <= 0.0 || t >= 1.0 ? 0.25 : 0.0));
    } else {
        const double rx = X - tf.cx, ry = Y - tf.cy;
        u = tf.along + rx * tf.dx + ry * tf.dy;
        across = -rx * tf.dy + ry * tf.dx;
        if (dist) *dist = float(std::hypot(rx, ry));
    }
    return m_ribbon->sampleRibbon(float(u / m_ribbonRepeat), float(across / std::max(0.5, w) + 0.5));
}

float BrushStroke::textureAlong(int x, int y, const TexFrame& tf) const {
    if (!m_texture) return 1.f;
    // texture coordinates that move with the stroke: u along it, v across it
    const double rx = x + 0.5 - tf.cx, ry = y + 0.5 - tf.cy;
    const double u = tf.along + rx * tf.dx + ry * tf.dy;
    const double v = tf.across - rx * tf.dy + ry * tf.dx;
    float sc = float(std::max(0.05, m_ctx.brush.textureScale));
    return textureValue(m_texture->sample(float(u) / sc, float(v) / sc));
}

void BrushStroke::begin(const InputSample& s) {
    PathPoint p{s, 1.0, 0.0};
    m_lastPoint = p;
    m_hasLast = true;
    m_pathLen = 0;
    if (m_ctx.brush.taperOut > 0) m_pending.push_back(p);
    else feedPath(p);
    flushPreview();
    notify();
}

void BrushStroke::add(const InputSample& s) {
    if (!m_hasLast) { begin(s); return; }
    double d = dist(m_lastPoint.s.pos, s.pos);
    PathPoint p{s, 1.0, m_lastPoint.cum + d};
    m_lastPoint = p;
    m_pathLen = p.cum;
    if (m_ctx.brush.taperOut > 0) {
        m_pending.push_back(p);
        while (!m_pending.empty() && m_pending.front().cum < m_pathLen - m_ctx.brush.taperOut) {
            feedPath(m_pending.front());
            m_pending.pop_front();
        }
    } else {
        feedPath(p);
    }
    flushPreview();
    notify();
}

void BrushStroke::airbrushTick() {
    if (!m_hasLast) return;
    for (auto& e : m_emitters)
        if (e.started) dab(e, e.last, e.lastDir);
    flushPreview();
    notify();
}

void BrushStroke::feedPath(const PathPoint& p) {
    for (auto& e : m_emitters) emitTo(e, p);
}

void BrushStroke::emitTo(Emitter& e, const PathPoint& p0) {
    PathPoint p = p0;
    p.s.pos = e.xf.map(p0.s.pos);
    if (!e.started) {
        e.started = true;
        e.last = p;
        dab(e, p, e.lastDir);
        return;
    }
    double L = dist(e.last.s.pos, p.s.pos);
    if (L < 1e-6) {
        e.last.s.pressure = p.s.pressure;
        e.last.taper = p.taper;
        return;
    }
    QPointF dir = (p.s.pos - e.last.s.pos) / L;
    e.lastDir = dir;
    const double spacing = m_ctx.brush.spacing;
    double pos = 0;
    int guard = 0;
    for (;;) {
        double t = pos / L;
        double pr = mapPressure(e.last.s.pressure + (p.s.pressure - e.last.s.pressure) * t);
        double taper = e.last.taper + (p.taper - e.last.taper) * t;
        double step = stepFor(pr, taper) * spacing;
        step = std::max(0.25, step);
        if (L / step > 20000) step = L / 20000;  // a huge jump must not hang the program
        double need = step - e.residual;
        if (pos + need > L) {
            e.residual += L - pos;
            break;
        }
        pos += need;
        e.residual = 0;
        e.traveled += need;
        double tt = pos / L;
        PathPoint ip;
        ip.s = lerpSample(e.last.s, p.s, tt);
        ip.taper = e.last.taper + (p.taper - e.last.taper) * tt;
        dab(e, ip, dir);
        if (++guard > 50000) break;
    }
    e.last = p;
}

void BrushStroke::dab(Emitter& e, const PathPoint& p, const QPointF& dir) {
    const BrushSettings& b = m_ctx.brush;
    float pr = mapPressure(p.s.pressure);
    double taperIn = b.taperIn > 0 ? std::clamp(e.traveled / b.taperIn, 0.08, 1.0) : 1.0;
    double diameter = diameterFor(pr, p.taper * taperIn);
    if (b.sizeJitter > 0 && b.particles <= 0) diameter *= 1.0 - b.sizeJitter * m_uni(m_rng);  // particles jitter one by one
    if (diameter < 0.05) return;

    double ang = b.angle;
    if (e.xf.type() != QTransform::TxNone) {
        // keep the tip orientation mirrored together with the stroke
        double a = qDegreesToRadians(ang);
        QPointF v = e.xf.map(QPointF(std::cos(a), std::sin(a))) - e.xf.map(QPointF(0, 0));
        ang = qRadiansToDegrees(std::atan2(v.y(), v.x()));
    }
    if (b.angleFollowsStroke) ang += qRadiansToDegrees(std::atan2(dir.y(), dir.x()));
    if (b.angleFromTilt && (p.s.tiltX != 0 || p.s.tiltY != 0)) ang += qRadiansToDegrees(std::atan2(p.s.tiltY, p.s.tiltX));
    if (b.angleJitter > 0) ang += (m_uni(m_rng) - 0.5) * 360.0 * b.angleJitter;

    QPointF c = p.s.pos;
    if (b.scatter > 0) {
        double a = m_uni(m_rng) * 2 * M_PI, r = std::sqrt(m_uni(m_rng)) * b.scatter * diameter;
        c += QPointF(std::cos(a) * r, std::sin(a) * r);
    }
    double density = b.density * (b.densityPressure ? b.minDensity + (1.0 - b.minDensity) * soften(pr, b.densitySoftness) : 1.0);
    if (b.opacityJitter > 0) density *= 1.0 - b.opacityJitter * m_uni(m_rng);
    if (density <= 0) return;

    DabParams d;
    d.cx = c.x();
    d.cy = c.y();
    d.diameter = diameter;
    d.hardness = b.hardness;
    d.roundness = b.roundness;
    d.angleRad = qDegreesToRadians(ang);
    d.antialias = b.antialias;
    d.shape = m_shape.get();
    d.shapeStrength = b.shapeStrength;
    d.shapeAsTip = b.shapeAsTip;
    d.fringe = b.fringe;

    if (!m_stamps.empty()) {
        // a stamp: one random picture of the set, the round tip only frames it
        d.shape = m_stamps[size_t(m_uni(m_rng) * double(m_stamps.size())) % m_stamps.size()].get();
        d.shapeAsTip = true;
        d.shapeStrength = 1.0;
        d.hardness = 1.0;
    }
    TexFrame tf;
    tf.on = m_texture && b.textureFollowsStroke;
    tf.dx = dir.x();
    tf.dy = dir.y();
    tf.along = e.traveled;
    if (m_ribbon) {
        // The input is a polyline: its direction jumps at every point. Patterns along the
        // stroke follow a smoothed direction so they bend evenly instead of fraying.
        if (e.smoothAt < 0) e.smoothDir = dir;
        else {
            const double k = std::clamp((e.traveled - e.smoothAt) / std::max(2.0, b.size * 0.6), 0.0, 1.0);
            e.smoothDir = e.smoothDir * (1.0 - k) + dir * k;
            const double len = std::hypot(e.smoothDir.x(), e.smoothDir.y());
            e.smoothDir = len > 1e-9 ? e.smoothDir / len : dir;
        }
        e.smoothAt = e.traveled;
        tf.dx = e.smoothDir.x();
        tf.dy = e.smoothDir.y();
        if (e.hasDab) {
            tf.hasPrev = true;
            tf.px = e.dabC.x();
            tf.py = e.dabC.y();
            tf.palong = e.dabAlong;
            tf.pdiam = e.dabDiam;
        }
        e.hasDab = true;
        e.dabC = c;
        e.dabAlong = e.traveled;
        e.dabDiam = diameter;
    }

    if (!m_lines.empty()) {
        // a bundle of parallel lines across the brush (hatching)
        const QPointF n(-dir.y(), dir.x());
        const double gap = diameter / double(m_lines.size());
        for (size_t i = 0; i < m_lines.size(); ++i) {
            const Line& L = m_lines[i];
            const double grown = e.traveled - L.startPx;
            if (grown < 0) continue;
            const double k = std::clamp(grown / std::max(1.0, b.size * 0.5), 0.15, 1.0);  // every line tapers in
            const double off = L.offset * diameter + b.bundleWobble * 0.45 * gap * std::sin(e.traveled * L.freq + L.phase);
            DabParams ld = d;
            const QPointF lc = c + n * off;
            ld.cx = lc.x();
            ld.cy = lc.y();
            ld.diameter = gap * b.bundleThickness * L.widthK * k;
            if (ld.diameter < 0.05) continue;
            TexFrame ltf = tf;
            ltf.cx = ld.cx;
            ltf.cy = ld.cy;
            ltf.across = off;
            put(ld, float(density), L.sub, ltf, e.lineCarry[i], pr);
        }
        return;
    }
    if (b.particles > 0) {
        // stipple: dots scattered inside the brush; light pressure = fewer dots
        double want = b.particles * (b.densityPressure ? b.minDensity + (1.0 - b.minDensity) * soften(pr, b.densitySoftness) : 1.0);
        int count = int(want);
        if (m_uni(m_rng) < want - count) ++count;
        for (int k = 0; k < count; ++k) {
            const double a = m_uni(m_rng) * 2 * M_PI, r = std::sqrt(m_uni(m_rng)) * diameter * 0.5;
            DabParams pd = d;
            pd.cx = c.x() + std::cos(a) * r;
            pd.cy = c.y() + std::sin(a) * r;
            pd.diameter = diameter * b.particleSize * (1.0 - 0.6 * b.sizeJitter * m_uni(m_rng));
            if (pd.diameter < 0.05) continue;
            const bool sub = b.subColorShare > 0 && m_uni(m_rng) < b.subColorShare;
            TexFrame ptf = tf;
            ptf.cx = pd.cx;
            ptf.cy = pd.cy;
            // the particle density is used as is: the dots are what the pressure changes
            put(pd, float(b.density), sub, ptf, e.carry, pr);
        }
        return;
    }
    const bool sub = b.subColorShare > 0 && m_uni(m_rng) < b.subColorShare;
    tf.cx = d.cx;
    tf.cy = d.cy;
    put(d, float(density), sub, tf, e.carry, pr);
}

double BrushStroke::stepFor(double pr, double taper) const {
    const BrushSettings& b = m_ctx.brush;
    double dia = diameterFor(pr, taper);
    if (b.bundleLines > 1) dia = dia / b.bundleLines * b.bundleThickness;  // spaced by the width of one line
    return dia;
}

void BrushStroke::put(const DabParams& d, float density, bool sub, const TexFrame& tf, Carry& carry, float pr) {
    if (sub) m_useSub = true;
    if (m_engine == BrushEngine::Paint || m_engine == BrushEngine::Erase) bufferedDab(d, density, sub, tf);
    else directDab(carry, sub ? m_sub : m_color, d, density, pr, tf);
}

void BrushStroke::bufferedDab(const DabParams& d, float density, bool sub, const TexFrame& tf) {
    QRect box = dabBounds(d) & m_ctx.doc->bounds();
    if (box.isEmpty()) return;
    const size_t n = size_t(box.width()) * box.height();
    if (tlCov.size() < n) tlCov.resize(n);
    if (!rasterizeDab(d, box, tlCov.data())) return;
    const bool along = m_texture && tf.on;
    const bool eachDab = m_texture && (m_ctx.brush.textureEachDab || along);
    const bool ribbon = m_ribbon != nullptr;
    const bool buildup = m_ctx.brush.buildup;
    FloatSurface& mask = sub ? m_mask2 : m_mask;
    TileRange tr = tileRangeFor(box);
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) {
            QRect ir = tileRect(tx, ty) & box;
            float* tm = mask.writable(tx, ty);
            float* td = ribbon ? m_ribbonDist.writable(tx, ty) : nullptr;
            for (int y = ir.top(); y <= ir.bottom(); ++y) {
                float* mrow = tm + ((y & (TILE - 1)) << TILE_SHIFT);
                const float* crow = tlCov.data() + size_t(y - box.top()) * box.width() - box.left();
                for (int x = ir.left(); x <= ir.right(); ++x) {
                    float v = crow[x];
                    if (v <= 0.f) continue;
                    v *= density;
                    if (ribbon) {
                        // On a curve the dabs disagree a little about where along the pattern a pixel
                        // is; the nearest dab decides, so the gaps of the pattern stay open.
                        float dist = 0.f;
                        const float rv = ribbonAt(x, y, tf, d.diameter, &dist);
                        float& owner = td[((y & (TILE - 1)) << TILE_SHIFT) | (x & (TILE - 1))];
                        if (dist >= owner) continue;
                        owner = dist;
                        v = density * rv;  // the pattern itself gives the soft edges
                        if (eachDab) v *= along ? textureAlong(x, y, tf) : textureAt(x, y);
                        mrow[x & (TILE - 1)] = v;
                        continue;
                    }
                    if (eachDab) v *= along ? textureAlong(x, y, tf) : textureAt(x, y);
                    float& a = mrow[x & (TILE - 1)];
                    if (buildup) a = a + v * (1.f - a);
                    else if (v > a) a = v;
                }
            }
        }
    m_previewDirty |= box;
    m_bounds |= box;
}

void BrushStroke::flushPreview() {
    if (m_previewDirty.isEmpty()) return;
    const QRect dirty = m_previewDirty;
    m_previewDirty = {};
    const BrushSettings& b = m_ctx.brush;
    const float opacity = float(b.opacity);
    const bool strokeTex = m_texture && !b.textureEachDab && !b.textureFollowsStroke;
    const bool erase = m_engine == BrushEngine::Erase;
    const bool two = m_useSub;
    thread_local std::vector<float> tlMask2;
    TileRange tr = tileRangeFor(dirty);
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) {
            QRect r = tileRect(tx, ty) & dirty;
            const size_t n = size_t(r.width()) * r.height();
            if (tlMask.size() < n) tlMask.resize(n);
            m_mask.readRect(r, tlMask.data(), r.width());
            if (two) {
                if (tlMask2.size() < n) tlMask2.resize(n);
                m_mask2.readRect(r, tlMask2.data(), r.width());
            }
            if (m_useSelection) {
                if (tlSel.size() < n) tlSel.resize(n);
                readMask(m_ctx.doc->selection(), r, tlSel.data());
                for (size_t i = 0; i < n; ++i) tlMask[i] *= tlSel[i];
                if (two)
                    for (size_t i = 0; i < n; ++i) tlMask2[i] *= tlSel[i];
            }
            for (int y = 0; y < r.height(); ++y)
                for (int x = 0; x < r.width(); ++x) {
                    const size_t i = size_t(y) * r.width() + x;
                    float& m = tlMask[i];
                    m *= opacity;
                    float t = 1.f;
                    if (strokeTex && (m > 0 || (two && tlMask2[i] > 0))) t = textureAt(r.left() + x, r.top() + y);
                    m *= t;
                    if (two) tlMask2[i] *= opacity * t;
                }

            if (m_ctx.targetMask) {
                Layer* l = m_ctx.layer;
                if (tlMaskU8.size() < n) tlMaskU8.resize(n);
                l->mask.readRect(r, tlMaskU8.data(), r.width());
                float pv = erase ? 0.f : luminance(m_color[0], m_color[1], m_color[2]);
                float sv = erase ? 0.f : luminance(m_sub[0], m_sub[1], m_sub[2]);
                for (size_t i = 0; i < n; ++i) {
                    float v = tlMaskU8[i] * kInv255;
                    v += (pv - v) * tlMask[i];
                    if (two) v += (sv - v) * tlMask2[i];
                    tlMaskU8[i] = quint8(std::clamp(int(v * 255.f + 0.5f), 0, 255));
                }
                l->previewMask->writeRect(r, tlMaskU8.data(), r.width());
                continue;
            }
            auto* rl = static_cast<RasterLayer*>(m_ctx.layer);
            if (!rl->preview) continue;
            if (tlDst.size() < n * 4) tlDst.resize(n * 4);
            readFloat(rl->pixels, r, tlDst.data());
            if (erase) {
                for (size_t i = 0; i < n; ++i) {
                    const float m2 = two ? tlMask2[i] : 0.f;
                    float k = (1.f - tlMask[i]) * (1.f - m2);
                    float* p = &tlDst[i * 4];
                    p[0] *= k; p[1] *= k; p[2] *= k; p[3] *= k;
                }
            } else {
                if (tlAux.size() < n * 4) tlAux.resize(n * 4);
                for (size_t i = 0; i < n; ++i) {
                    float m = tlMask[i];
                    float* s = &tlAux[i * 4];
                    s[0] = m_color[0] * m; s[1] = m_color[1] * m; s[2] = m_color[2] * m; s[3] = m;
                    if (two && tlMask2[i] > 0) {
                        // the second color lies on top of the first (premultiplied "over")
                        const float m2 = tlMask2[i], k = 1.f - m2;
                        s[0] = m_sub[0] * m2 + s[0] * k;
                        s[1] = m_sub[1] * m2 + s[1] * k;
                        s[2] = m_sub[2] * m2 + s[2] * k;
                        s[3] = m2 + s[3] * k;
                    }
                }
                if (m_alphaLock) blendRowAtop(b.paintMode, tlDst.data(), tlAux.data(), int(n));
                else blendRow(b.paintMode, tlDst.data(), tlAux.data(), int(n));
            }
            writeFloat(*rl->preview, r, tlDst.data(), true);
        }
    m_dirty |= dirty;
}

void BrushStroke::directDab(Carry& carry, const float* base, const DabParams& d, float density, float pr, const TexFrame& tf) {
    if (!m_ctx.layer->isRaster() || m_ctx.targetMask) return;
    auto* rl = static_cast<RasterLayer*>(m_ctx.layer);
    QRect box = dabBounds(d) & m_ctx.doc->bounds();
    if (box.isEmpty()) return;
    const size_t n = size_t(box.width()) * box.height();
    if (tlCov.size() < n) tlCov.resize(n);
    if (!rasterizeDab(d, box, tlCov.data())) return;
    if (m_texture)
        for (int y = 0; y < box.height(); ++y)
            for (int x = 0; x < box.width(); ++x)
                tlCov[size_t(y) * box.width() + x] *= tf.on ? textureAlong(box.left() + x, box.top() + y, tf) : textureAt(box.left() + x, box.top() + y);
    if (m_ribbon)
        for (int y = 0; y < box.height(); ++y)
            for (int x = 0; x < box.width(); ++x) tlCov[size_t(y) * box.width() + x] *= ribbonAt(box.left() + x, box.top() + y, tf, d.diameter, nullptr);
    if (m_useSelection) {
        if (tlSel.size() < n) tlSel.resize(n);
        readMask(m_ctx.doc->selection(), box, tlSel.data());
        for (size_t i = 0; i < n; ++i) tlCov[i] *= tlSel[i];
    }
    if (tlDst.size() < n * 4) tlDst.resize(n * 4);
    readFloat(rl->pixels, box, tlDst.data());
    const BrushSettings& b = m_ctx.brush;

    float paint[4] = {0, 0, 0, 0};
    bool usePaint = true;
    if (m_engine == BrushEngine::Mix || m_engine == BrushEngine::Smudge) {
        double wsum = 0;
        double S[4] = {0, 0, 0, 0};
        for (size_t i = 0; i < n; ++i) {
            float w = tlCov[i];
            if (w <= 0) continue;
            wsum += w;
            for (int k = 0; k < 4; ++k) S[k] += double(tlDst[i * 4 + size_t(k)]) * w;
        }
        if (wsum < 1e-6) return;
        float s[4] = {float(S[0] / wsum), float(S[1] / wsum), float(S[2] / wsum), float(S[3] / wsum)};
        if (m_origin) {
            if (tlAux.size() < n * 4) tlAux.resize(n * 4);
            readFloat(*m_origin, box, tlAux.data());
            double O[4] = {0, 0, 0, 0};
            for (size_t i = 0; i < n; ++i) {
                float w = tlCov[i];
                if (w <= 0) continue;
                for (int k = 0; k < 4; ++k) O[k] += double(tlAux[i * 4 + size_t(k)]) * w;
            }
            // 70% what was there before the stroke, 30% the wet paint of this stroke
            for (int k = 0; k < 4; ++k) s[k] = float(O[k] / wsum) * 0.7f + s[k] * 0.3f;
        }
        if (m_engine == BrushEngine::Mix) {
            float M[4];
            if (m_ctx.transparentColor) { M[0] = M[1] = M[2] = M[3] = 0; }
            else {
                float c[3] = {base[0], base[1], base[2]};
                jitterColor(c);
                M[0] = c[0]; M[1] = c[1]; M[2] = c[2]; M[3] = 1;
            }
            float bl = float(b.blending);
            if (b.blendingPressure) bl = clamp01(bl * (1.5f - float(soften(pr, b.blendSoftness))));
            // Dilution waters down the brush's own color only (as in SAI): with full blending the
            // brush moves the colors under it and does not wash them away.
            const float dil = 1.f - float(b.dilution);
            float target[4];
            for (int k = 0; k < 4; ++k) target[k] = M[k] * dil * (1 - bl) + s[k] * bl;
            float pers = float(b.persistence);
            if (!carry.has) {
                std::copy(target, target + 4, carry.c);
                carry.has = true;
            } else {
                for (int k = 0; k < 4; ++k) carry.c[k] = target[k] * (1 - pers) + carry.c[k] * pers;
            }
            for (int k = 0; k < 4; ++k) paint[k] = carry.c[k];
        } else {  // Smudge
            float pers = float(b.persistence);
            if (!carry.has) {
                std::copy(s, s + 4, carry.c);
                carry.has = true;
                usePaint = false;
            } else {
                std::copy(carry.c, carry.c + 4, paint);
                for (int k = 0; k < 4; ++k) carry.c[k] = s[k] * (1 - pers) + carry.c[k] * pers;
            }
        }
        if (!usePaint) return;
        for (size_t i = 0; i < n; ++i) {
            float w = tlCov[i] * density;
            if (w <= 0) continue;
            float* p = &tlDst[i * 4];
            float oldA = p[3];
            for (int k = 0; k < 4; ++k) p[k] += (paint[k] - p[k]) * w;
            if (m_alphaLock) {
                float a = p[3];
                float f = a > 1e-6f ? oldA / a : 0.f;
                p[0] *= f; p[1] *= f; p[2] *= f; p[3] = oldA;
            }
        }
    } else if (m_engine == BrushEngine::Blur) {
        int rad = std::max(1, int(std::lround(d.diameter * 0.12)));
        QRect big = box.adjusted(-rad * 3, -rad * 3, rad * 3, rad * 3) & m_ctx.doc->bounds();
        std::vector<float> src(size_t(big.width()) * big.height() * 4);
        readFloat(rl->pixels, big, src.data());
        gaussianBlur(src.data(), big.width(), big.height(), 4, float(rad));
        for (int y = 0; y < box.height(); ++y)
            for (int x = 0; x < box.width(); ++x) {
                size_t i = size_t(y) * box.width() + x;
                float w = tlCov[i] * density;
                if (w <= 0) continue;
                const float* q = &src[(size_t(y + box.top() - big.top()) * big.width() + size_t(x + box.left() - big.left())) * 4];
                float* p = &tlDst[i * 4];
                float oldA = p[3];
                for (int k = 0; k < 4; ++k) p[k] += (q[k] - p[k]) * w;
                if (m_alphaLock) {
                    float a = p[3];
                    float f = a > 1e-6f ? oldA / a : 0.f;
                    p[0] *= f; p[1] *= f; p[2] *= f; p[3] = oldA;
                }
            }
    }
    // Stochastic rounding keeps slow buildups from stalling at 8 bits.
    int ox = int(m_uni(m_rng) * 4), oy = int(m_uni(m_rng) * 4);
    std::vector<quint32> out(n);
    for (int y = 0; y < box.height(); ++y)
        for (int x = 0; x < box.width(); ++x) {
            size_t i = size_t(y) * box.width() + x;
            out[i] = packDither(&tlDst[i * 4], x + ox, y + oy);
        }
    rl->pixels.writeRect(box, out.data(), box.width());
    m_dirty |= box;
    m_bounds |= box;
}

void BrushStroke::notify() {
    if (m_dirty.isEmpty()) return;
    m_ctx.doc->pixelsChanged(m_dirty, m_ctx.layer);
    m_dirty = {};
}

void BrushStroke::end() {
    if (m_ended) return;
    if (!m_pending.empty()) {
        const double to = std::max(1e-6, m_ctx.brush.taperOut);
        for (auto& q : m_pending) {
            q.taper = std::clamp((m_pathLen - q.cum) / to, 0.05, 1.0);
            feedPath(q);
        }
        m_pending.clear();
    }
    flushPreview();
    m_ended = true;
    Layer* l = m_ctx.layer;
    if (m_ctx.targetMask) {
        if (l->previewMask) {
            l->mask = std::move(*l->previewMask);
            l->previewMask.reset();
            l->mask.optimizeRect(m_bounds);
        }
    } else if (l->isRaster()) {
        auto* rl = static_cast<RasterLayer*>(l);
        if (rl->preview) {
            rl->pixels = std::move(*rl->preview);
            rl->preview.reset();
        }
        rl->pixels.optimizeRect(m_bounds);
    }
    m_dirty |= m_bounds;
    notify();
}

void BrushStroke::cancel() {
    if (m_ended) return;
    m_ended = true;
    Layer* l = m_ctx.layer;
    l->previewMask.reset();
    if (l->isRaster()) static_cast<RasterLayer*>(l)->preview.reset();
    m_dirty |= m_bounds;
    notify();
}

}  // namespace gx
