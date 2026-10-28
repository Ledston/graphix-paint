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

    m_color[0] = float(ctx.color.redF());
    m_color[1] = float(ctx.color.greenF());
    m_color[2] = float(ctx.color.blueF());
    jitterColor(m_color);

    m_useSelection = ctx.doc->hasSelection();
    m_alphaLock = !ctx.targetMask && (ctx.layer->alphaLock || ctx.brush.keepOpacity);

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

double BrushStroke::diameterFor(double pr, double taper) const {
    const BrushSettings& b = m_ctx.brush;
    double f = b.sizePressure ? b.minSize + (1.0 - b.minSize) * pr : 1.0;
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

float BrushStroke::textureAt(int x, int y) const {
    if (!m_texture) return 1.f;
    const BrushSettings& b = m_ctx.brush;
    float sc = float(std::max(0.05, b.textureScale));
    float v = m_texture->sample((x + 0.5f) / sc, (y + 0.5f) / sc);
    if (b.textureInvert) v = 1.f - v;
    float s = float(b.textureStrength);
    return 1.f - s + s * v;
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
        double step = std::max(0.25, diameterFor(pr, taper) * spacing);
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
    if (b.sizeJitter > 0) diameter *= 1.0 - b.sizeJitter * m_uni(m_rng);
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
    double density = b.density * (b.densityPressure ? b.minDensity + (1.0 - b.minDensity) * pr : 1.0);
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

    if (m_engine == BrushEngine::Paint || m_engine == BrushEngine::Erase) bufferedDab(d, float(density));
    else directDab(e, d, float(density), pr);
}

void BrushStroke::bufferedDab(const DabParams& d, float density) {
    QRect box = dabBounds(d) & m_ctx.doc->bounds();
    if (box.isEmpty()) return;
    const size_t n = size_t(box.width()) * box.height();
    if (tlCov.size() < n) tlCov.resize(n);
    if (!rasterizeDab(d, box, tlCov.data())) return;
    const bool eachDab = m_texture && m_ctx.brush.textureEachDab;
    const bool buildup = m_ctx.brush.buildup;
    TileRange tr = tileRangeFor(box);
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) {
            QRect ir = tileRect(tx, ty) & box;
            float* tm = m_mask.writable(tx, ty);
            for (int y = ir.top(); y <= ir.bottom(); ++y) {
                float* mrow = tm + ((y & (TILE - 1)) << TILE_SHIFT);
                const float* crow = tlCov.data() + size_t(y - box.top()) * box.width() - box.left();
                for (int x = ir.left(); x <= ir.right(); ++x) {
                    float v = crow[x];
                    if (v <= 0.f) continue;
                    v *= density;
                    if (eachDab) v *= textureAt(x, y);
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
    const bool strokeTex = m_texture && !b.textureEachDab;
    const bool erase = m_engine == BrushEngine::Erase;
    TileRange tr = tileRangeFor(dirty);
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) {
            QRect r = tileRect(tx, ty) & dirty;
            const size_t n = size_t(r.width()) * r.height();
            if (tlMask.size() < n) tlMask.resize(n);
            m_mask.readRect(r, tlMask.data(), r.width());
            if (m_useSelection) {
                if (tlSel.size() < n) tlSel.resize(n);
                readMask(m_ctx.doc->selection(), r, tlSel.data());
                for (size_t i = 0; i < n; ++i) tlMask[i] *= tlSel[i];
            }
            for (int y = 0; y < r.height(); ++y)
                for (int x = 0; x < r.width(); ++x) {
                    float& m = tlMask[size_t(y) * r.width() + x];
                    m *= opacity;
                    if (strokeTex && m > 0) m *= textureAt(r.left() + x, r.top() + y);
                }

            if (m_ctx.targetMask) {
                Layer* l = m_ctx.layer;
                if (tlMaskU8.size() < n) tlMaskU8.resize(n);
                l->mask.readRect(r, tlMaskU8.data(), r.width());
                float pv = erase ? 0.f : luminance(m_color[0], m_color[1], m_color[2]);
                for (size_t i = 0; i < n; ++i) {
                    float v = tlMaskU8[i] * kInv255;
                    v += (pv - v) * tlMask[i];
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
                    float k = 1.f - tlMask[i];
                    float* p = &tlDst[i * 4];
                    p[0] *= k; p[1] *= k; p[2] *= k; p[3] *= k;
                }
            } else {
                if (tlAux.size() < n * 4) tlAux.resize(n * 4);
                for (size_t i = 0; i < n; ++i) {
                    float m = tlMask[i];
                    float* s = &tlAux[i * 4];
                    s[0] = m_color[0] * m; s[1] = m_color[1] * m; s[2] = m_color[2] * m; s[3] = m;
                }
                if (m_alphaLock) blendRowAtop(b.paintMode, tlDst.data(), tlAux.data(), int(n));
                else blendRow(b.paintMode, tlDst.data(), tlAux.data(), int(n));
            }
            writeFloat(*rl->preview, r, tlDst.data(), true);
        }
    m_dirty |= dirty;
}

void BrushStroke::directDab(Emitter& e, const DabParams& d, float density, float pr) {
    if (!m_ctx.layer->isRaster() || m_ctx.targetMask) return;
    auto* rl = static_cast<RasterLayer*>(m_ctx.layer);
    QRect box = dabBounds(d) & m_ctx.doc->bounds();
    if (box.isEmpty()) return;
    const size_t n = size_t(box.width()) * box.height();
    if (tlCov.size() < n) tlCov.resize(n);
    if (!rasterizeDab(d, box, tlCov.data())) return;
    if (m_texture)
        for (int y = 0; y < box.height(); ++y)
            for (int x = 0; x < box.width(); ++x) tlCov[size_t(y) * box.width() + x] *= textureAt(box.left() + x, box.top() + y);
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
        if (m_engine == BrushEngine::Mix) {
            float M[4];
            if (m_ctx.transparentColor) { M[0] = M[1] = M[2] = M[3] = 0; }
            else {
                float c[3] = {m_color[0], m_color[1], m_color[2]};
                jitterColor(c);
                M[0] = c[0]; M[1] = c[1]; M[2] = c[2]; M[3] = 1;
            }
            float bl = float(b.blending);
            if (b.blendingPressure) bl = clamp01(bl * (1.5f - pr));
            float target[4];
            for (int k = 0; k < 4; ++k) target[k] = M[k] * (1 - bl) + s[k] * bl;
            float pers = float(b.persistence);
            if (!e.hasCarried) {
                std::copy(target, target + 4, e.carried);
                e.hasCarried = true;
            } else {
                for (int k = 0; k < 4; ++k) e.carried[k] = target[k] * (1 - pers) + e.carried[k] * pers;
            }
            float dil = 1.f - float(b.dilution);
            for (int k = 0; k < 4; ++k) paint[k] = e.carried[k] * dil;
        } else {  // Smudge
            float pers = float(b.persistence);
            if (!e.hasCarried) {
                std::copy(s, s + 4, e.carried);
                e.hasCarried = true;
                usePaint = false;
            } else {
                std::copy(e.carried, e.carried + 4, paint);
                for (int k = 0; k < 4; ++k) e.carried[k] = s[k] * (1 - pers) + e.carried[k] * pers;
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
