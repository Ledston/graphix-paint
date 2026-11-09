#include "core/Effects.h"

#include <QMutex>
#include <algorithm>
#include <array>
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

namespace {

float smooth01(float e0, float e1, float x) {
    if (e1 <= e0) return x < e0 ? 0.f : 1.f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// Color steps: smooth shading breaks into flat steps; noise on the borders makes them rough and grainy.
void applySteps(std::vector<float>& o, int W, int H, const QRect& R, const LayerEffects& fx) {
    const int n = std::clamp(fx.stepsCount, 2, 16);
    const float soft = std::clamp(fx.stepsSoftness, 0.f, 1.f) * 0.5f;
    const float noise = std::clamp(fx.stepsNoise, 0.f, 1.f);
    const float lo = std::min(fx.stepsLow, fx.stepsHigh), hi = std::max(fx.stepsLow, fx.stepsHigh);
    const float op = std::clamp(fx.stepsOpacity, 0.f, 1.f);
    auto step = [&](float v, float nz) {
        const float q = v * float(n - 1) + nz;
        const float f = std::floor(q);
        const float t = smooth01(0.5f - soft - 1e-4f, 0.5f + soft + 1e-4f, q - f);
        return std::clamp((f + t) / float(n - 1), 0.f, 1.f);
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float* p = &o[(size_t(y) * W + x) * 4];
            const float a = p[3];
            if (a <= 1e-4f) continue;
            float col[3] = {p[0] / a, p[1] / a, p[2] / a};
            const float lum = luminance(col[0], col[1], col[2]);
            // where it works: the brightness range, and not on dark lines
            float w = smooth01(lo - 0.04f, lo + 0.001f, lum) * (1.f - smooth01(hi - 0.001f, hi + 0.04f, lum));
            if (lo <= 0.f) w = 1.f - smooth01(hi - 0.001f, hi + 0.04f, lum);
            if (hi >= 1.f) w = lo <= 0.f ? 1.f : smooth01(lo - 0.04f, lo + 0.001f, lum);
            if (fx.stepsKeepLines) w *= smooth01(0.1f, 0.2f, lum);
            w *= op;
            if (w <= 0.f) continue;
            const double px = R.left() + x, py = R.top() + y;
            const float nz = grainNoise(px, py, 7, unsigned(fx.stepsSeed), fx.stepsGrain) * 2.f * noise * 0.9f;  // about +-0.9 of a step
            float out[3];
            if (fx.stepsMode == 0) {
                const float l2 = step(lum, nz);
                if (lum > 1e-4f) {
                    const float k = l2 / lum;
                    for (int c = 0; c < 3; ++c) out[c] = std::clamp(col[c] * k, 0.f, 1.f);
                } else {
                    out[0] = out[1] = out[2] = l2;
                }
            } else {
                for (int c = 0; c < 3; ++c) out[c] = step(col[c], nz);
            }
            for (int c = 0; c < 3; ++c) p[c] = (col[c] + (out[c] - col[c]) * w) * a;
        }
}

// Chromatic aberration: red drifts one way, blue the other, green stays. Each channel keeps its
// own coverage, so the colored fringes show outside the shape too (no white is added).
void applyChromatic(std::vector<float>& o, int W, int H, const QRect& R, const LayerEffects& fx, const QRect& content) {
    const size_t N = size_t(W) * size_t(H);
    const double a = fx.chromaAngle * M_PI / 180.0;
    const float shift = fx.chromaShift;
    const QPointF cc = QRectF(content).center();
    const double rad = std::max(1.0, 0.5 * std::hypot(content.width(), content.height()));
    std::vector<float> rp(N * 2), bp(N * 2);  // premultiplied red / blue with their coverage
    auto sample = [&](float sx, float sy, int ch, float* dst) {
        const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
        const float tx = sx - float(x0), ty = sy - float(y0);
        float v = 0, al = 0;
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                const int xx = x0 + i, yy = y0 + j;
                if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                const float wgt = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
                const float* p = &o[(size_t(yy) * W + xx) * 4];
                v += p[ch] * wgt;
                al += p[3] * wgt;
            }
        dst[0] = v;
        dst[1] = al;
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float dx, dy;
            if (fx.chromaLens) {  // outward from the middle of the picture, growing toward the edges
                const double vx = (R.left() + x + 0.5 - cc.x()) / rad, vy = (R.top() + y + 0.5 - cc.y()) / rad;
                const double d = std::hypot(vx, vy);
                const double f = std::pow(std::min(1.0, d), 1.5);
                dx = d > 1e-6 ? float(vx / d * f * shift) : 0.f;
                dy = d > 1e-6 ? float(vy / d * f * shift) : 0.f;
            } else {
                dx = float(std::cos(a) * shift);
                dy = float(std::sin(a) * shift);
            }
            const size_t i = size_t(y) * W + x;
            sample(float(x) - dx, float(y) - dy, 0, &rp[i * 2]);
            sample(float(x) + dx, float(y) + dy, 2, &bp[i * 2]);
        }
    const float soft = std::clamp(fx.chromaSoftness, 0.f, 1.f);
    if (soft > 0.01f) {
        // Smooth only the color difference to green: fine noise stays gray noise (no colored specks),
        // while the wide colored edges of the aberration stay.
        for (size_t i = 0; i < N; ++i) {
            const float g = o[i * 4 + 1], ga = o[i * 4 + 3];
            rp[i * 2] -= g; rp[i * 2 + 1] -= ga;
            bp[i * 2] -= g; bp[i * 2 + 1] -= ga;
        }
        const float sigma = 0.6f + soft * std::max(1.f, std::fabs(shift)) * 0.9f;
        gaussianBlur(rp.data(), W, H, 2, sigma);
        gaussianBlur(bp.data(), W, H, 2, sigma);
        for (size_t i = 0; i < N; ++i) {
            const float g = o[i * 4 + 1], ga = o[i * 4 + 3];
            rp[i * 2] += g; rp[i * 2 + 1] = std::clamp(rp[i * 2 + 1] + ga, 0.f, 1.f);
            bp[i * 2] += g; bp[i * 2 + 1] = std::clamp(bp[i * 2 + 1] + ga, 0.f, 1.f);
        }
    }
    for (size_t i = 0; i < N; ++i) {
        float* q = &o[i * 4];
        const float ga = q[3];
        const float al = std::max({rp[i * 2 + 1], ga, bp[i * 2 + 1]});
        q[0] = std::clamp(rp[i * 2], 0.f, al);
        q[2] = std::clamp(bp[i * 2], 0.f, al);
        q[1] = std::clamp(q[1], 0.f, al);
        q[3] = std::min(al, 1.f);
    }
}

float bayer(int x, int y, int size) {
    // ordered dither threshold in [0, 1) for a 2x2, 4x4 or 8x8 grid: the bits of (x^y, y) interleaved
    // and reversed, which gives the classic Bayer matrix ([[0, 2], [3, 1]] for 2x2)
    int r = 0, xx = x, yy = y;
    for (int b = 1; b < size; b *= 2) {
        r = r * 4 + ((((xx ^ yy) & 1) << 1) | (yy & 1));
        xx >>= 1;
        yy >>= 1;
    }
    return (float(r) + 0.5f) / float(size * size);
}

float ditherThreshold(int pattern, qint64 x, qint64 y) {
    switch (pattern) {
    case 0: return bayer(int(x & 1), int(y & 1), 2);
    case 1: return bayer(int(x & 3), int(y & 3), 4);
    case 2: return bayer(int(x & 7), int(y & 7), 8);
    case 3: return grainNoise(double(x), double(y), 11, 3) + 0.5f;  // white noise
    default: {  // smooth noise (interleaved gradient noise): even, without a visible grid
        const double v = 52.9829189 * std::fmod(0.06711056 * double(x) + 0.00583715 * double(y), 1.0);
        return float(v - std::floor(v));
    }
    }
}

// Dithering: few colors, the tones in between laid out as a pattern of dots.
void applyDither(std::vector<float>& o, int W, int H, const QRect& R, const LayerEffects& fx) {
    const int ps = std::clamp(fx.ditherPixel, 1, 8);
    const int L = std::clamp(fx.ditherLevels, 2, 256);
    const float str = std::clamp(fx.ditherStrength, 0.f, 1.f);
    const float op = std::clamp(fx.ditherOpacity, 0.f, 1.f);
    static const float gb[4][3] = {{15 / 255.f, 56 / 255.f, 15 / 255.f}, {48 / 255.f, 98 / 255.f, 48 / 255.f},
                                   {139 / 255.f, 172 / 255.f, 15 / 255.f}, {155 / 255.f, 188 / 255.f, 15 / 255.f}};
    std::vector<std::array<float, 3>> own;
    for (const QColor& c : fx.ditherColors) own.push_back({float(c.redF()), float(c.greenF()), float(c.blueF())});
    const bool ownPal = fx.ditherPalette == 3 && !own.empty();
    const std::vector<float> src = o;  // big pixels read from the untouched picture
    auto floorDiv = [](qint64 a, qint64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const qint64 gx = R.left() + x, gy = R.top() + y;
            float px[4];
            if (ps > 1) {  // one big pixel: the average of its block (blocks sit on the canvas grid)
                const qint64 bx = floorDiv(gx, ps) * ps - R.left(), by = floorDiv(gy, ps) * ps - R.top();
                float acc[4] = {0, 0, 0, 0};
                int cnt = 0;
                for (int j = 0; j < ps; ++j)
                    for (int i = 0; i < ps; ++i) {
                        const qint64 xx = bx + i, yy = by + j;
                        if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                        const float* q = &src[(size_t(yy) * W + size_t(xx)) * 4];
                        for (int k = 0; k < 4; ++k) acc[k] += q[k];
                        ++cnt;
                    }
                for (int k = 0; k < 4; ++k) px[k] = cnt ? acc[k] / float(cnt) : 0.f;
            } else {
                for (int k = 0; k < 4; ++k) px[k] = src[(size_t(y) * W + x) * 4 + size_t(k)];
            }
            const float a = px[3];
            float* q = &o[(size_t(y) * W + x) * 4];
            if (a <= 1e-4f) {
                if (ps > 1) q[0] = q[1] = q[2] = q[3] = 0.f;
                continue;
            }
            const float col[3] = {px[0] / a, px[1] / a, px[2] / a};
            const float t = ditherThreshold(fx.ditherPattern, floorDiv(gx, ps), floorDiv(gy, ps));
            const float d = (t - 0.5f) * str;
            float res[3];
            if (fx.ditherPalette == 1 || fx.ditherPalette == 2) {
                const float lum = luminance(col[0], col[1], col[2]);
                const int lv = fx.ditherPalette == 2 ? 4 : L;
                const int idx = std::clamp(int(std::floor(lum * float(lv - 1) + 0.5f + d)), 0, lv - 1);
                if (fx.ditherPalette == 2) {
                    for (int k = 0; k < 3; ++k) res[k] = gb[idx][k];
                } else {
                    for (int k = 0; k < 3; ++k) res[k] = float(idx) / float(lv - 1);
                }
            } else if (ownPal) {
                const float spread = 0.6f / std::sqrt(float(own.size()));
                float best = 1e9f;
                int bi = 0;
                for (size_t i = 0; i < own.size(); ++i) {
                    float dd = 0;
                    for (int k = 0; k < 3; ++k) {
                        const float e = col[k] + d * spread - own[i][size_t(k)];
                        dd += e * e;
                    }
                    if (dd < best) { best = dd; bi = int(i); }
                }
                for (int k = 0; k < 3; ++k) res[k] = own[size_t(bi)][size_t(k)];
            } else {
                for (int k = 0; k < 3; ++k)
                    res[k] = float(std::clamp(int(std::floor(col[k] * float(L - 1) + 0.5f + d)), 0, L - 1)) / float(L - 1);
            }
            const float* orig = &src[(size_t(y) * W + x) * 4];
            const float oa = orig[3];
            for (int k = 0; k < 3; ++k) {
                const float o0 = oa > 1e-4f ? orig[k] / oa : 0.f;
                const float v = o0 + (res[k] - o0) * op;
                q[k] = v * (ps > 1 ? (oa + (a - oa) * op) : oa);
            }
            if (ps > 1) q[3] = oa + (a - oa) * op;
        }
}

}  // namespace

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

    // film grain, only where the layer has paint. With chromatic aberration it comes after the
    // aberration (else its dots would drift apart into colored speckles).
    auto grainOn = [&](std::vector<float>& buf) {
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
            blendRowAtop(BlendMode(fx.grainBlend), &buf[size_t(y) * W * 4], g.data(), W);
        }
    };
    const bool grainNow = fx.grain && fx.grainAmount > 0;
    const bool grainLate = grainNow && fx.chromatic && std::fabs(fx.chromaShift) >= 0.25f;
    if (grainNow && !grainLate) grainOn(c);

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

    if (fx.steps) applySteps(o, W, H, R, fx);
    if (fx.chromatic && std::fabs(fx.chromaShift) >= 0.25f) {
        const QRect content = (l.fxCache && !l.fxCache->content.isEmpty()) ? l.fxCache->content : (surf ? surf->tileBounds() : R);
        applyChromatic(o, W, H, R, fx, content);
    }
    if (grainLate) grainOn(o);
    if (fx.dither) applyDither(o, W, H, R, fx);

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
