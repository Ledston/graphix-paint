#include "core/Compositor.h"

#include <cstring>

#include "core/Effects.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"

namespace gx {

namespace {

// Per-thread scratch buffers, reused across tiles to avoid allocations.
struct ScratchPool {
    std::vector<std::vector<float>> bufs;
    int used = 0;
};
thread_local ScratchPool tlPool;
thread_local std::vector<quint32> tlU32;
thread_local std::vector<quint8> tlU8;

struct Scratch {
    float* p;
    explicit Scratch(size_t n) {
        if (tlPool.used >= int(tlPool.bufs.size())) tlPool.bufs.emplace_back();
        auto& b = tlPool.bufs[size_t(tlPool.used++)];
        if (b.size() < n) b.resize(n);
        p = b.data();
    }
    ~Scratch() { --tlPool.used; }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

struct Ctx {
    QRect r;
    int n;
};

bool anyTileIn(const RasterSurface& s, const QRect& r) {
    if (s.isEmpty()) return false;
    TileRange tr = tileRangeFor(r);
    if (tr.count() > s.tileCount()) {
        for (auto it = s.tiles().cbegin(); it != s.tiles().cend(); ++it)
            if (tileRectForKey(it.key()).intersects(r)) return true;
        return false;
    }
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx)
            if (s.tile(tx, ty)) return true;
    return false;
}

void applyMask(const MaskSurface& m, const QRect& r, float* buf, int n) {
    if (tlU8.size() < size_t(n)) tlU8.resize(size_t(n));
    m.readRect(r, tlU8.data(), r.width());
    for (int i = 0; i < n; ++i) {
        quint8 v = tlU8[size_t(i)];
        if (v == 255) continue;
        float f = v * kInv255;
        float* p = buf + i * 4;
        p[0] *= f; p[1] *= f; p[2] *= f; p[3] *= f;
    }
}

// Loads the pixels of a raster-like layer (raster, vector, text) including mask and effects.
bool loadContent(const Layer* l, const Ctx& c, float* out) {
    const bool fx = l->fx.anyActive() && l->fxCache;
    const RasterSurface* s = fx ? &l->fxCache->tiles : l->displaySurface();
    if (!s) return false;
    QRect src = c.r.translated(-l->previewOffset);
    if (!anyTileIn(*s, src)) return false;
    if (tlU32.size() < size_t(c.n)) tlU32.resize(size_t(c.n));
    s->readRect(src, tlU32.data(), src.width());
    for (int i = 0; i < c.n; ++i) unpack(tlU32[size_t(i)], out + i * 4);
    if (!fx)
        if (const MaskSurface* m = l->displayMask()) applyMask(*m, src, out, c.n);
    return true;
}

void dissolveRect(float* p, const QRect& r) {
    for (int y = 0; y < r.height(); ++y) dissolveRow(p + size_t(y) * r.width() * 4, r.width(), r.left(), r.top() + y);
}

void scale(float* p, int n, float k) {
    if (k >= 1.f) return;
    for (int i = 0; i < n * 4; ++i) p[i] *= k;
}

void lerpInto(float* acc, const float* other, int n, float t, const float* mask) {
    for (int i = 0; i < n; ++i) {
        float w = mask ? t * mask[i] : t;
        if (w <= 0.f) continue;
        float* a = acc + i * 4;
        const float* o = other + i * 4;
        for (int k = 0; k < 4; ++k) a[k] += (o[k] - a[k]) * w;
    }
}

void compositeStack(const GroupLayer* g, const Ctx& c, float* acc);

bool renderContent(const Layer* l, const Ctx& c, float* out) {
    if (l->isGroup()) {
        std::memset(out, 0, sizeof(float) * size_t(c.n) * 4);
        compositeStack(static_cast<const GroupLayer*>(l), c, out);
        if (const MaskSurface* m = l->displayMask()) applyMask(*m, c.r.translated(-l->previewOffset), out, c.n);
        return true;
    }
    if (l->type() == LayerType::Adjustment) return false;
    return loadContent(l, c, out);
}

std::vector<float>& maskScratch() {
    thread_local std::vector<float> m;
    return m;
}

void applyAdjustment(const AdjustmentLayer* a, const Ctx& c, float* acc) {
    if (!a->processor || a->processor->isIdentity()) return;
    Scratch tmp(size_t(c.n) * 4);
    std::memcpy(tmp.p, acc, sizeof(float) * size_t(c.n) * 4);
    a->processor->apply(tmp.p, c.n);
    const float* mk = nullptr;
    std::vector<float>& m = maskScratch();
    if (const MaskSurface* ms = a->displayMask()) {
        if (m.size() < size_t(c.n)) m.resize(size_t(c.n));
        readMask(*ms, c.r, m.data());
        mk = m.data();
    }
    lerpInto(acc, tmp.p, c.n, a->opacity, mk);
}

void applyLayer(const Layer* l, const Ctx& c, float* acc, bool atop) {
    if (!l->visible) return;
    if (l->type() == LayerType::Adjustment) {
        applyAdjustment(static_cast<const AdjustmentLayer*>(l), c, acc);
        return;
    }
    if (l->isGroup() && static_cast<const GroupLayer*>(l)->passThrough && !atop) {
        Scratch copy(size_t(c.n) * 4);
        std::memcpy(copy.p, acc, sizeof(float) * size_t(c.n) * 4);
        compositeStack(static_cast<const GroupLayer*>(l), c, copy.p);
        lerpInto(acc, copy.p, c.n, l->opacity, nullptr);
        return;
    }
    Scratch tmp(size_t(c.n) * 4);
    if (!renderContent(l, c, tmp.p)) return;
    scale(tmp.p, c.n, l->opacity);
    if (l->blend == BlendMode::Dissolve) dissolveRect(tmp.p, c.r);
    if (atop) blendRowAtop(l->blend, acc, tmp.p, c.n);
    else blendRow(l->blend, acc, tmp.p, c.n);
}

void compositeStack(const GroupLayer* g, const Ctx& c, float* acc) {
    const auto& ch = g->children;
    const int n = int(ch.size());
    int i = 0;
    while (i < n) {
        const Layer* base = ch[size_t(i)].get();
        int j = i + 1;
        while (j < n && ch[size_t(j)]->clipping) ++j;
        if (!base->visible) { i = j; continue; }
        // Clipping onto folders works too: the folder is rendered on its own and
        // the clipped layers are limited to its combined opacity.
        bool canClip = base->type() != LayerType::Adjustment;
        if (j == i + 1 || !canClip) {
            applyLayer(base, c, acc, false);
            for (int k = i + 1; k < j; ++k) applyLayer(ch[size_t(k)].get(), c, acc, false);
            i = j;
            continue;
        }
        Scratch tmp(size_t(c.n) * 4);
        if (renderContent(base, c, tmp.p)) {
            for (int k = i + 1; k < j; ++k) applyLayer(ch[size_t(k)].get(), c, tmp.p, true);
            scale(tmp.p, c.n, base->opacity);
            if (base->blend == BlendMode::Dissolve) dissolveRect(tmp.p, c.r);
            blendRow(base->blend, acc, tmp.p, c.n);
        }
        i = j;
    }
}

}  // namespace

void Compositor::prepare(GroupLayer* root, const QRect& r) {
    forEachLayer(root, [&](Layer* l) {
        if (l->fx.anyActive()) ensureEffects(*l, r);
        else if (l->fxCache) l->fxCache.reset();
        if (l->type() == LayerType::Adjustment) {
            auto* a = static_cast<AdjustmentLayer*>(l);
            QJsonObject key = a->adjustment.toJson();
            if (!a->processor || a->processorKey != key) {
                a->processor = std::make_shared<AdjustmentProcessor>(a->adjustment);
                a->processorKey = key;
            }
        }
        return true;
    });
}

void Compositor::composite(const GroupLayer* root, const QRect& r, float* out, const CompositeOptions& opt) {
    Ctx c{r, r.width() * r.height()};
    std::memset(out, 0, sizeof(float) * size_t(c.n) * 4);
    if (!root || r.isEmpty()) return;
    if (opt.only) {
        if (opt.only->type() == LayerType::Adjustment) return;
        if (renderContent(opt.only, c, out)) scale(out, c.n, opt.only->opacity);
        return;
    }
    compositeStack(root, c, out);
}

QImage Compositor::renderImage(GroupLayer* root, const QRect& r, const CompositeOptions& opt) {
    QImage img(r.size(), QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) return img;
    img.fill(0);
    prepare(root, r);
    TileRange tr = tileRangeFor(r);
    QVector<qint64> keys;
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) keys << tileKey(tx, ty);
    uchar* bits = img.bits();
    const qsizetype bpl = img.bytesPerLine();
    parallelForTiles(keys, [&](qint64 k) {
        QRect tr2 = tileRectForKey(k) & r;
        if (tr2.isEmpty()) return;
        std::vector<float> buf(size_t(tr2.width()) * tr2.height() * 4);
        composite(root, tr2, buf.data(), opt);
        for (int y = 0; y < tr2.height(); ++y) {
            quint32* row = reinterpret_cast<quint32*>(bits + bpl * (tr2.top() - r.top() + y)) + (tr2.left() - r.left());
            for (int x = 0; x < tr2.width(); ++x) row[x] = pack(&buf[(size_t(y) * tr2.width() + x) * 4]);
        }
    });
    return img;
}

void Compositor::samplePixel(const GroupLayer* root, int x, int y, float* rgba) {
    composite(root, QRect(x, y, 1, 1), rgba);
}

}  // namespace gx
