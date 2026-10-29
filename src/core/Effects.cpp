#include "core/Effects.h"

#include <QMutex>
#include <algorithm>
#include <cmath>

#include "core/ImageOps.h"
#include "core/Pixel.h"
#include "core/Textures.h"

namespace gx {

void invalidateEffects(Layer& l, const QRect& canvasRect) {
    if (!l.fxCache) return;
    int m = l.fx.margin();
    QRect r = canvasRect.translated(-l.previewOffset).adjusted(-m, -m, m, m);
    TileRange tr = tileRangeFor(r);
    if (tr.count() > l.fxCache->valid.size()) {
        for (auto it = l.fxCache->valid.begin(); it != l.fxCache->valid.end();) {
            if (tileRectForKey(*it).intersects(r)) it = l.fxCache->valid.erase(it);
            else ++it;
        }
        return;
    }
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) l.fxCache->valid.remove(tileKey(tx, ty));
}

void ensureEffects(Layer& l, const QRect& canvasRect) {
    if (!l.fx.anyActive() || !l.hasPixels()) {
        l.fxCache.reset();
        return;
    }
    if (!l.fxCache || l.fxCache->params != l.fx) {
        l.fxCache = std::make_shared<FxCache>();
        l.fxCache->params = l.fx;
        if (const RasterSurface* s = l.displaySurface(); s && l.fx.gradientOverlay) l.fxCache->content = s->exactBounds();
    }
    QRect r = canvasRect.translated(-l.previewOffset);
    // Only tiles that can contain something: content bounds grown by the effect margin.
    const RasterSurface* src = l.displaySurface();
    if (!src) return;
    int m = l.fx.margin();
    QRect content = src->tileBounds().adjusted(-m, -m, m, m);
    r &= content;
    if (r.isEmpty()) return;
    TileRange tr = tileRangeFor(r);
    QVector<qint64> todo;
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) {
            qint64 k = tileKey(tx, ty);
            if (!l.fxCache->valid.contains(k)) todo << k;
        }
    if (todo.isEmpty()) return;
    QHash<qint64, RasterSurface::Ptr> results;
    QMutex mutex;
    parallelForTiles(todo, [&](qint64 k) {
        auto t = std::make_shared<Tile<quint32>>();
        t->px.reset(new quint32[TILE_PIXELS]);
        renderEffectsTile(l, tileRectForKey(k), t->px.get());
        bool empty = true;
        for (int i = 0; i < TILE_PIXELS; ++i)
            if (t->px[i]) { empty = false; break; }
        QMutexLocker lock(&mutex);
        results.insert(k, empty ? nullptr : t);
    });
    for (auto it = results.begin(); it != results.end(); ++it) {
        l.fxCache->tiles.setTile(it.key(), it.value());
        l.fxCache->valid.insert(it.key());
    }
}

void renderEffectsTile(const Layer& l, const QRect& tr, quint32* out) {
    const LayerEffects& fx = l.fx;
    const RasterSurface* surf = l.displaySurface();
    const int m = fx.margin();
    const QRect R = tr.adjusted(-m, -m, m, m);
    const int W = R.width(), H = R.height();
    const size_t N = size_t(W) * size_t(H);
    std::vector<float> c(N * 4, 0.f);
    if (surf) readFloat(*surf, R, c.data());
    if (const MaskSurface* mask = (l.hasMask && l.maskEnabled) ? &l.mask : nullptr) {
        std::vector<float> mk(N);
        readMask(*mask, R, mk.data());
        for (size_t i = 0; i < N; ++i)
            for (int k = 0; k < 4; ++k) c[i * 4 + size_t(k)] *= mk[i];
    }
    bool any = false;
    for (size_t i = 0; i < N; ++i)
        if (c[i * 4 + 3] > 0) { any = true; break; }
    if (!any) {
        std::fill_n(out, TILE_PIXELS, 0u);
        return;
    }

    if (fx.blur && fx.blurRadius > 0.3f) gaussianBlur(c.data(), W, H, 4, fx.blurRadius * 0.5f);

    if (fx.overlay) {
        float oc[3] = {float(fx.overlayColor.redF()), float(fx.overlayColor.greenF()), float(fx.overlayColor.blueF())};
        float op = clamp01(fx.overlayOpacity);
        for (size_t i = 0; i < N; ++i) {
            float* p = &c[i * 4];
            for (int k = 0; k < 3; ++k) p[k] = p[k] + (oc[k] * p[3] - p[k]) * op;
        }
    }

    if (fx.gradientOverlay) {
        // a gradient across the whole layer, at an angle, drawn over the paint
        QRect content = (l.fxCache && !l.fxCache->content.isEmpty()) ? l.fxCache->content : (surf ? surf->tileBounds() : R);
        const double a = fx.gradientAngle * M_PI / 180.0, dx = std::cos(a), dy = std::sin(a);
        const QPointF cc = QRectF(content).center();
        const double half = 0.5 * (std::fabs(dx) * content.width() + std::fabs(dy) * content.height());
        const float c0[3] = {float(fx.gradientStart.redF()), float(fx.gradientStart.greenF()), float(fx.gradientStart.blueF())};
        const float c1[3] = {float(fx.gradientEnd.redF()), float(fx.gradientEnd.greenF()), float(fx.gradientEnd.blueF())};
        const float op = clamp01(fx.gradientOpacity);
        std::vector<float> g(size_t(W) * 4);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                double t = ((R.left() + x + 0.5 - cc.x()) * dx + (R.top() + y + 0.5 - cc.y()) * dy) / std::max(1.0, 2 * half) + 0.5;
                float tt = clamp01(float(t));
                for (int k = 0; k < 3; ++k) g[size_t(x) * 4 + size_t(k)] = (c0[k] + (c1[k] - c0[k]) * tt) * op;
                g[size_t(x) * 4 + 3] = op;
            }
            blendRowAtop(BlendMode(fx.gradientBlend), &c[size_t(y) * W * 4], g.data(), W);
        }
    }

    if (fx.texture) {
        TexturePtr tex = TextureLibrary::instance().get(TextureKind::Paper, fx.textureName);
        if (!tex) tex = TextureLibrary::instance().findAny(fx.textureName);
        if (tex) {
            float s = clamp01(fx.textureStrength), sc = std::max(0.05f, fx.textureScale);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    float t = tex->sample((R.left() + x + 0.5f) / sc, (R.top() + y + 0.5f) / sc);
                    float f = 1.f - s + s * t;
                    float* p = &c[(size_t(y) * W + x) * 4];
                    p[0] *= f; p[1] *= f; p[2] *= f; p[3] *= f;
                }
        }
    }

    if (fx.grain && fx.grainAmount > 0) {
        // film grain, only where the layer has paint
        const float amt = std::clamp(fx.grainAmount, 0.f, 1.f) * 2.f;
        std::vector<float> g(size_t(W) * 4);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const double px = R.left() + x, py = R.top() + y;
                const float base = grainNoise(px, py, 0, unsigned(fx.grainSeed), fx.grainSize);
                for (int k = 0; k < 3; ++k) {
                    float n = fx.grainMono ? base : grainNoise(px, py, unsigned(k + 1), unsigned(fx.grainSeed), fx.grainSize);
                    g[size_t(x) * 4 + size_t(k)] = clamp01(0.5f + n * amt);
                }
                g[size_t(x) * 4 + 3] = 1.f;
            }
            blendRowAtop(BlendMode(fx.grainBlend), &c[size_t(y) * W * 4], g.data(), W);
        }
    }

    if (fx.screentone) {
        // tones become printed dots / lines in one ink color (manga screentone)
        const float tc[3] = {float(fx.toneColor.redF()), float(fx.toneColor.greenF()), float(fx.toneColor.blueF())};
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float* p = &c[(size_t(y) * W + x) * 4];
                const float a = p[3];
                if (a <= 0) continue;
                const float lum = luminance(p[0], p[1], p[2]) / a;
                const float cov = halftoneCoverage(R.left() + x + 0.5, R.top() + y + 0.5, fx.toneCell, fx.toneAngle, fx.toneShape, 1.f - lum);
                if (fx.toneKeepWhite) {
                    const float na = cov * a;
                    p[0] = tc[0] * na; p[1] = tc[1] * na; p[2] = tc[2] * na; p[3] = na;
                } else {
                    for (int k = 0; k < 3; ++k) p[k] = (1.f + (tc[k] - 1.f) * cov) * a;  // ink on white paper
                }
            }
    }

    if (fx.innerGlow || fx.innerShadow) {
        // both are drawn inside the shape only
        std::vector<float> inner(N * 4, 0.f);
        if (fx.innerShadow) {
            const int dx = int(std::lround(fx.innerShadowOffset.x())), dy = int(std::lround(fx.innerShadowOffset.y()));
            std::vector<float> a(N, 1.f);  // "outside" moved by the offset
            for (int y = 0; y < H; ++y) {
                const int sy = y - dy;
                for (int x = 0; x < W; ++x) {
                    const int sx = x - dx;
                    const float inside = (sx < 0 || sy < 0 || sx >= W || sy >= H) ? 0.f : c[(size_t(sy) * W + sx) * 4 + 3];
                    a[size_t(y) * W + x] = 1.f - inside;
                }
            }
            if (fx.innerShadowBlur > 0.3f) gaussianBlur(a.data(), W, H, 1, fx.innerShadowBlur * 0.5f);
            const float sc[3] = {float(fx.innerShadowColor.redF()), float(fx.innerShadowColor.greenF()), float(fx.innerShadowColor.blueF())};
            const float op = clamp01(fx.innerShadowOpacity);
            std::vector<float> sh(N * 4);
            for (size_t i = 0; i < N; ++i) {
                const float al = a[i] * op;
                sh[i * 4] = sc[0] * al; sh[i * 4 + 1] = sc[1] * al; sh[i * 4 + 2] = sc[2] * al; sh[i * 4 + 3] = al;
            }
            blendRow(BlendMode::Normal, inner.data(), sh.data(), int(N));
        }
        if (fx.innerGlow) {
            std::vector<float> a(N);
            for (size_t i = 0; i < N; ++i) a[i] = 1.f - c[i * 4 + 3];
            if (fx.innerGlowSize > 0.3f) gaussianBlur(a.data(), W, H, 1, fx.innerGlowSize * 0.5f);
            const float gc[3] = {float(fx.innerGlowColor.redF()), float(fx.innerGlowColor.greenF()), float(fx.innerGlowColor.blueF())};
            const float op = clamp01(fx.innerGlowOpacity);
            std::vector<float> gl(N * 4);
            for (size_t i = 0; i < N; ++i) {
                const float al = clamp01(a[i] * 2.f) * op;
                gl[i * 4] = gc[0] * al; gl[i * 4 + 1] = gc[1] * al; gl[i * 4 + 2] = gc[2] * al; gl[i * 4 + 3] = al;
            }
            blendRow(BlendMode::Screen, inner.data(), gl.data(), int(N));
        }
        blendRowAtop(BlendMode::Normal, c.data(), inner.data(), int(N));
    }

    std::vector<float> o(N * 4, 0.f);
    if (fx.outerGlow && fx.outerGlowSize > 0) {
        std::vector<float> a(N);
        for (size_t i = 0; i < N; ++i) a[i] = c[i * 4 + 3];
        gaussianBlur(a.data(), W, H, 1, fx.outerGlowSize * 0.5f);
        const float gc[3] = {float(fx.outerGlowColor.redF()), float(fx.outerGlowColor.greenF()), float(fx.outerGlowColor.blueF())};
        const float op = clamp01(fx.outerGlowOpacity);
        for (size_t i = 0; i < N; ++i) {
            const float al = clamp01(a[i] * 1.6f) * op;
            o[i * 4] = gc[0] * al; o[i * 4 + 1] = gc[1] * al; o[i * 4 + 2] = gc[2] * al; o[i * 4 + 3] = al;
        }
    }
    if (fx.shadow) {
        int dx = int(std::lround(fx.shadowOffset.x())), dy = int(std::lround(fx.shadowOffset.y()));
        std::vector<float> a(N, 0.f);
        for (int y = 0; y < H; ++y) {
            int sy = y - dy;
            if (sy < 0 || sy >= H) continue;
            for (int x = 0; x < W; ++x) {
                int sx = x - dx;
                if (sx < 0 || sx >= W) continue;
                a[size_t(y) * W + x] = c[(size_t(sy) * W + sx) * 4 + 3];
            }
        }
        if (fx.shadowBlur > 0.3f) gaussianBlur(a.data(), W, H, 1, fx.shadowBlur * 0.5f);
        float sc[3] = {float(fx.shadowColor.redF()), float(fx.shadowColor.greenF()), float(fx.shadowColor.blueF())};
        float op = clamp01(fx.shadowOpacity);
        std::vector<float> sh(N * 4);
        for (size_t i = 0; i < N; ++i) {
            float al = a[i] * op;
            sh[i * 4 + 0] = sc[0] * al; sh[i * 4 + 1] = sc[1] * al; sh[i * 4 + 2] = sc[2] * al; sh[i * 4 + 3] = al;
        }
        blendRow(BlendMode::Normal, o.data(), sh.data(), int(N));
    }

    if (fx.outline && fx.outlineSize > 0) {
        // Distance to the shape measured at double resolution, so the outline edge is smooth.
        const int W2 = W * 2, H2 = H * 2;
        std::vector<uint8_t> inside(size_t(W2) * H2);
        for (int y = 0; y < H2; ++y)
            for (int x = 0; x < W2; ++x) {
                // bilinear alpha at the sub-pixel center
                const float fx_ = (x + 0.5f) * 0.5f - 0.5f, fy_ = (y + 0.5f) * 0.5f - 0.5f;
                const int x0 = std::clamp(int(std::floor(fx_)), 0, W - 1), y0 = std::clamp(int(std::floor(fy_)), 0, H - 1);
                const int x1 = std::min(x0 + 1, W - 1), y1 = std::min(y0 + 1, H - 1);
                const float tx = std::clamp(fx_ - float(x0), 0.f, 1.f), ty = std::clamp(fy_ - float(y0), 0.f, 1.f);
                auto A = [&](int xx, int yy) { return c[(size_t(yy) * W + xx) * 4 + 3]; };
                const float a = (A(x0, y0) * (1 - tx) + A(x1, y0) * tx) * (1 - ty) + (A(x0, y1) * (1 - tx) + A(x1, y1) * tx) * ty;
                inside[size_t(y) * W2 + x] = a >= 0.5f ? 1 : 0;
            }
        std::vector<float> d2;
        distanceTransform(inside, W2, H2, d2);
        const float size2 = fx.outlineSize * 2.f;
        float oc[3] = {float(fx.outlineColor.redF()), float(fx.outlineColor.greenF()), float(fx.outlineColor.blueF())};
        float op = clamp01(fx.outlineOpacity);
        std::vector<float> ol(N * 4);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float cov = 0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) cov += clamp01(size2 + 0.5f - std::sqrt(d2[size_t(y * 2 + j) * W2 + size_t(x * 2 + i)]));
                const size_t p = size_t(y) * W + x;
                float al = std::max(cov * 0.25f, c[p * 4 + 3]) * op;
                ol[p * 4 + 0] = oc[0] * al; ol[p * 4 + 1] = oc[1] * al; ol[p * 4 + 2] = oc[2] * al; ol[p * 4 + 3] = al;
            }
        blendRow(BlendMode::Normal, o.data(), ol.data(), int(N));
    }

    blendRow(BlendMode::Normal, o.data(), c.data(), int(N));

    if (fx.chromatic && std::fabs(fx.chromaShift) >= 0.25f) {
        // chromatic aberration: the red channel drifts one way, blue the other, green stays.
        // Alpha follows the widest channel so the colored fringes show outside the shape.
        const double a = fx.chromaAngle * M_PI / 180.0;
        const float sx = float(std::cos(a) * fx.chromaShift), sy = float(std::sin(a) * fx.chromaShift);
        std::vector<float> res(N * 4);
        auto at = [&](float fx_, float fy_, int ch) {
            // bilinear sample of the straight color channel 'ch' (with its alpha) at a fractional position
            const int x0 = int(std::floor(fx_)), y0 = int(std::floor(fy_));
            const float tx = fx_ - float(x0), ty = fy_ - float(y0);
            float v = 0, al = 0;
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    const int xx = x0 + i, yy = y0 + j;
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    const float w = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
                    const float* p = &o[(size_t(yy) * W + xx) * 4];
                    v += p[ch] * w;
                    al += p[3] * w;
                }
            return std::make_pair(v, al);
        };
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const auto [r, ra] = at(float(x) - sx, float(y) - sy, 0);
                const float* gp = &o[(size_t(y) * W + x) * 4];
                const float g = gp[1], ga = gp[3];
                const auto [b, ba] = at(float(x) + sx, float(y) + sy, 2);
                const float al = std::max({ra, ga, ba});
                float* q = &res[(size_t(y) * W + x) * 4];
                // over transparent areas each channel fades in with its own coverage
                q[0] = r + (al - ra);  // where only green/blue exist, the missing red counts as white
                q[1] = g + (al - ga);
                q[2] = b + (al - ba);
                q[3] = al;
                for (int k = 0; k < 3; ++k) q[k] = std::clamp(q[k], 0.f, al);
            }
        o.swap(res);
    }

    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) out[y * TILE + x] = pack(&o[(size_t(y + m) * W + size_t(x + m)) * 4]);
}

RasterSurface bakeEffects(const Layer& layer, const LayerEffects& fx, const QRect& area, const MaskSurface* selection) {
    const RasterSurface* src = layer.isRaster() ? &static_cast<const RasterLayer&>(layer).pixels : nullptr;
    RasterSurface out = src ? *src : RasterSurface();  // shares the tiles: only changed tiles are copied
    if (!src || area.isEmpty()) return out;
    auto tmp = std::make_shared<RasterLayer>();
    tmp->pixels = *src;
    tmp->fx = fx;
    tmp->hasMask = false;
    TileRange tr = tileRangeFor(area);
    QVector<qint64> keys;
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) keys << tileKey(tx, ty);
    QHash<qint64, RasterSurface::Ptr> results;
    QMutex mutex;
    parallelForTiles(keys, [&](qint64 k) {
        const QRect trc = tileRectForKey(k);
        auto t = std::make_shared<Tile<quint32>>();
        t->px.reset(new quint32[TILE_PIXELS]);
        renderEffectsTile(*tmp, trc, t->px.get());
        // outside the area (and the selection) the pixels stay as they were
        const QRect inside = trc & area;
        std::vector<quint32> orig(TILE_PIXELS);
        src->readRect(trc, orig.data(), TILE);
        std::vector<float> sel;
        if (selection) {
            sel.resize(TILE_PIXELS);
            readMask(*selection, trc, sel.data());
        }
        bool empty = true;
        for (int y = 0; y < TILE; ++y)
            for (int x = 0; x < TILE; ++x) {
                const int i = y * TILE + x;
                const bool in = inside.contains(trc.left() + x, trc.top() + y);
                quint32 v = in ? t->px[i] : orig[size_t(i)];
                if (in && selection) {
                    const float w = sel[size_t(i)];
                    if (w <= 0.f) v = orig[size_t(i)];
                    else if (w < 1.f) {
                        float a[4], b[4];
                        unpack(orig[size_t(i)], a);
                        unpack(v, b);
                        for (int c = 0; c < 4; ++c) a[c] += (b[c] - a[c]) * w;
                        v = pack(a);
                    }
                }
                t->px[i] = v;
                if (v) empty = false;
            }
        QMutexLocker lock(&mutex);
        results.insert(k, empty ? nullptr : t);
    });
    for (auto it = results.begin(); it != results.end(); ++it) out.setTile(it.key(), it.value());
    return out;
}

}  // namespace gx
