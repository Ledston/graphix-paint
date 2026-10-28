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

    std::vector<float> o(N * 4, 0.f);
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
        for (size_t i = 0; i < N; ++i) {
            float al = a[i] * op;
            o[i * 4 + 0] = sc[0] * al; o[i * 4 + 1] = sc[1] * al; o[i * 4 + 2] = sc[2] * al; o[i * 4 + 3] = al;
        }
    }

    if (fx.outline && fx.outlineSize > 0) {
        std::vector<uint8_t> inside(N);
        for (size_t i = 0; i < N; ++i) inside[i] = c[i * 4 + 3] >= 0.5f ? 1 : 0;
        std::vector<float> d2;
        distanceTransform(inside, W, H, d2);
        float oc[3] = {float(fx.outlineColor.redF()), float(fx.outlineColor.greenF()), float(fx.outlineColor.blueF())};
        float op = clamp01(fx.outlineOpacity);
        std::vector<float> ol(N * 4);
        for (size_t i = 0; i < N; ++i) {
            float d = std::sqrt(d2[i]);
            float al = clamp01(fx.outlineSize + 0.5f - d);
            al = std::max(al, c[i * 4 + 3]) * op;
            ol[i * 4 + 0] = oc[0] * al; ol[i * 4 + 1] = oc[1] * al; ol[i * 4 + 2] = oc[2] * al; ol[i * 4 + 3] = al;
        }
        blendRow(BlendMode::Normal, o.data(), ol.data(), int(N));
    }

    blendRow(BlendMode::Normal, o.data(), c.data(), int(N));

    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) out[y * TILE + x] = pack(&o[(size_t(y + m) * W + size_t(x + m)) * 4]);
}

}  // namespace gx
