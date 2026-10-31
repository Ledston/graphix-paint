#include "core/ReplaceColor.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <vector>

#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerOps.h"

namespace gx {

namespace {
void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 1e-6f ? d / mx : 0.f;
    if (d < 1e-6f) h = 0;
    else if (mx == r) h = std::fmod((g - b) / d + 6.f, 6.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    h /= 6.f;
}
void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    h = (h - std::floor(h)) * 6.f;
    const int i = int(h) % 6;
    const float f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
}
}  // namespace

void replaceColorPixels(float* px, int n, const ReplaceColorParams& p) {
    const float fr = float(p.from.redF()), fg = float(p.from.greenF()), fb = float(p.from.blueF());
    const float tr = float(p.to.redF()), tg = float(p.to.greenF()), tb = float(p.to.blueF());
    float fh, fs, fv, th, ts, tv;
    rgbToHsv(fr, fg, fb, fh, fs, fv);
    rgbToHsv(tr, tg, tb, th, ts, tv);
    const float tol = float(std::max(0.0, p.tolerance)) / 255.f, soft = float(std::max(0.0, p.softness)) / 255.f;
    for (int i = 0; i < n; ++i) {
        float* q = px + size_t(i) * 4;
        const float a = q[3];
        if (a <= 1e-5f) continue;
        const float r = q[0] / a, g = q[1] / a, b = q[2] / a;
        // distance in RGB, roughly as the eye sees it
        const float d = std::sqrt(0.3f * (r - fr) * (r - fr) + 0.59f * (g - fg) * (g - fg) + 0.11f * (b - fb) * (b - fb));
        float w = d <= tol ? 1.f : soft > 1e-6f ? std::clamp(1.f - (d - tol) / soft, 0.f, 1.f) : 0.f;
        if (w <= 0) continue;
        float nr = tr, ng = tg, nb = tb;
        if (p.keepShading) {
            // the shade of this pixel against `from`, moved onto `to`
            float h, s, v;
            rgbToHsv(r, g, b, h, s, v);
            const float ns = std::clamp(ts + (s - fs), 0.f, 1.f), nv = std::clamp(tv + (v - fv), 0.f, 1.f);
            hsvToRgb(th, ns, nv, nr, ng, nb);
        }
        q[0] = (r + (nr - r) * w) * a;
        q[1] = (g + (ng - g) * w) * a;
        q[2] = (b + (nb - b) * w) * a;
    }
}

bool replaceColor(Document& doc, quint32 layerId, const ReplaceColorParams& p) {
    return ops::mapLayerPixels(doc, layerId, "Replace color", [p](float* px, int n) { replaceColorPixels(px, n, p); });
}

void offsetWrap(Document& doc, int dx, int dy) {
    const QRect b = doc.bounds();
    const int W = b.width(), H = b.height();
    dx = ((dx % W) + W) % W;
    dy = ((dy % H) + H) % H;
    if (!dx && !dy) return;
    doc.pushUndo("Offset");
    auto rollRaster = [&](RasterSurface& s) {
        std::vector<quint32> src(size_t(W) * H), dst(src.size());
        s.readRect(b, src.data(), W);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) dst[size_t((y + dy) % H) * W + (x + dx) % W] = src[size_t(y) * W + x];
        s.writeRect(b, dst.data(), W);
        s.optimizeRect(b);
    };
    auto rollMask = [&](MaskSurface& s) {
        std::vector<quint8> src(size_t(W) * H), dst(src.size());
        s.readRect(b, src.data(), W);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) dst[size_t((y + dy) % H) * W + (x + dx) % W] = src[size_t(y) * W + x];
        s.writeRect(b, dst.data(), W);
        s.optimizeRect(b);
    };
    std::function<void(GroupLayer*)> walk = [&](GroupLayer* g) {
        for (auto& c : g->children) {
            if (c->hasMask) rollMask(c->mask);
            if (auto* r = dynamic_cast<RasterLayer*>(c.get())) rollRaster(r->pixels);
            if (auto* gg = dynamic_cast<GroupLayer*>(c.get())) walk(gg);
        }
    };
    walk(doc.root());
    if (doc.hasSelection()) rollMask(doc.selection());
    doc.pixelsChanged(b);
    doc.structureChanged();
}

}  // namespace gx
