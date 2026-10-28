#include "core/LayerOps.h"

#include <QSet>

#include <QImage>
#include <QMutex>
#include <QPainter>
#include <QtMath>
#include <algorithm>
#include <deque>
#include <cmath>

#include "core/Compositor.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"
#include "vector/VectorRender.h"

namespace gx {
namespace ops {

namespace {

void assignNewIds(Document& doc, Layer* l) {
    l->id = doc.newLayerId();
    if (l->isGroup())
        for (auto& c : static_cast<GroupLayer*>(l)->children) assignNewIds(doc, c.get());
}

LayerPtr sharedOf(Layer* l) {
    if (!l || !l->parent) return nullptr;
    int i = l->parent->indexOf(l);
    return i >= 0 ? l->parent->children[size_t(i)] : nullptr;
}

void ensureOneLayer(Document& doc) {
    if (!doc.root()->children.empty()) return;
    auto l = std::make_shared<RasterLayer>();
    l->id = doc.newLayerId();
    l->name = doc.uniqueLayerName("Layer");
    doc.root()->insert(0, l);
    doc.setCurrentLayer(l->id);
}

// Rasterizes a vector/text layer into a raster layer object with the same properties.
std::shared_ptr<RasterLayer> toRaster(const Layer* l) {
    auto r = std::make_shared<RasterLayer>();
    r->id = l->id;
    r->name = l->name;
    r->visible = l->visible;
    r->opacity = l->opacity;
    r->blend = l->blend;
    r->clipping = l->clipping;
    r->locked = l->locked;
    r->alphaLock = l->alphaLock;
    r->labelColor = l->labelColor;
    r->reference = l->reference;
    r->fx = l->fx;
    r->hasMask = l->hasMask;
    r->maskEnabled = l->maskEnabled;
    r->mask = l->mask;
    if (const RasterSurface* s = l->displaySurface()) r->pixels = *s;
    return r;
}

void replaceLayer(Layer* old, LayerPtr nl) {
    GroupLayer* p = old->parent;
    int i = p->indexOf(old);
    p->take(i);
    p->insert(i, std::move(nl));
}

QImage maskRegionToAlphaImage(const MaskSurface& m, const QRect& r, bool invert) {
    QImage img(r.size(), QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) return img;
    std::vector<quint8> v(size_t(r.width()) * r.height());
    m.readRect(r, v.data(), r.width());
    for (int y = 0; y < r.height(); ++y) {
        quint32* row = reinterpret_cast<quint32*>(img.scanLine(y));
        for (int x = 0; x < r.width(); ++x) {
            quint32 a = v[size_t(y) * r.width() + x];
            if (invert) a = 255 - a;
            row[x] = a * 0x01010101u;
        }
    }
    return img;
}

void alphaImageToMask(const QImage& img, MaskSurface& m, const QPoint& at, bool invert) {
    std::vector<quint8> v(size_t(img.width()) * img.height());
    for (int y = 0; y < img.height(); ++y) {
        const quint32* row = reinterpret_cast<const quint32*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            quint8 a = quint8(row[x] >> 24);
            v[size_t(y) * img.width() + x] = invert ? quint8(255 - a) : a;
        }
    }
    QRect r(at, img.size());
    m.writeRect(r, v.data(), img.width());
    m.optimizeRect(r);
}

}  // namespace

// ============================================================================ layers

Layer* insertLayer(Document& doc, LayerPtr layer, Layer* above, bool undo, const QString& undoName) {
    if (undo) doc.pushUndo(undoName.isEmpty() ? QStringLiteral("New layer") : undoName);
    if (layer->id == 0 || doc.layer(layer->id)) layer->id = doc.newLayerId();
    if (layer->name.isEmpty()) layer->name = doc.uniqueLayerName(layerTypeName(layer->type()));
    Layer* ref = above ? above : doc.currentLayer();
    GroupLayer* parent = doc.root();
    int index = int(parent->children.size());
    if (ref && ref->parent) {
        parent = ref->parent;
        index = parent->indexOf(ref) + 1;
        // A new layer above a clipped layer joins the clipping group (like Photoshop).
        if (ref->clipping && layer->hasPixels()) layer->clipping = true;
    }
    Layer* raw = layer.get();
    parent->insert(index, std::move(layer));
    doc.setCurrentLayer(raw->id);
    doc.structureChanged();
    return raw;
}

RasterLayer* addRasterLayer(Document& doc, const QString& name) {
    auto l = std::make_shared<RasterLayer>();
    l->name = name.isEmpty() ? doc.uniqueLayerName("Layer") : name;
    return static_cast<RasterLayer*>(insertLayer(doc, l, nullptr, true, "New layer"));
}

VectorLayer* addVectorLayer(Document& doc, const QString& name) {
    auto l = std::make_shared<VectorLayer>();
    l->name = name.isEmpty() ? doc.uniqueLayerName("Linework") : name;
    return static_cast<VectorLayer*>(insertLayer(doc, l, nullptr, true, "New linework layer"));
}

GroupLayer* addGroup(Document& doc, const QString& name) {
    auto l = std::make_shared<GroupLayer>();
    l->name = name.isEmpty() ? doc.uniqueLayerName("Folder") : name;
    return static_cast<GroupLayer*>(insertLayer(doc, l, nullptr, true, "New folder"));
}

AdjustmentLayer* addAdjustmentLayer(Document& doc, const Adjustment& adj) {
    auto l = std::make_shared<AdjustmentLayer>();
    l->adjustment = adj;
    l->name = doc.uniqueLayerName(adjustmentTypeName(adj.type));
    return static_cast<AdjustmentLayer*>(insertLayer(doc, l, nullptr, true, "New adjustment layer"));
}

TextLayer* addTextLayer(Document& doc, const TextParams& params) {
    auto l = std::make_shared<TextLayer>();
    l->text = params;
    QString first = params.text.section('\n', 0, 0).left(24).trimmed();
    l->name = first.isEmpty() ? doc.uniqueLayerName("Text") : first;
    renderTextLayer(*l);
    return static_cast<TextLayer*>(insertLayer(doc, l, nullptr, true, "New text layer"));
}

void deleteLayer(Document& doc, quint32 id) {
    Layer* l = doc.layer(id);
    if (!l || !l->parent) return;
    doc.pushUndo("Delete layer");
    GroupLayer* p = l->parent;
    int i = p->indexOf(l);
    p->take(i);
    Layer* next = nullptr;
    if (!p->children.empty()) next = p->children[size_t(std::clamp(i - 1, 0, int(p->children.size()) - 1))].get();
    else if (p != doc.root()) next = p;
    ensureOneLayer(doc);
    if (next) doc.setCurrentLayer(next->id);
    else if (!doc.root()->children.empty()) doc.setCurrentLayer(doc.root()->children.back()->id);
    doc.structureChanged();
}

Layer* duplicateLayer(Document& doc, quint32 id) {
    Layer* l = doc.layer(id);
    if (!l || !l->parent) return nullptr;
    auto c = l->clone();
    assignNewIds(doc, c.get());
    c->name = l->name + " copy";
    return insertLayer(doc, c, l, true, "Duplicate layer");
}

RasterSurface renderLayers(Document& doc, const std::vector<LayerPtr>& layers, const QRect& bounds) {
    RasterSurface out;
    QRect b = bounds & doc.bounds();
    if (b.isEmpty()) return out;
    auto tmp = std::make_shared<GroupLayer>();
    for (const auto& l : layers) {
        auto c = l->clone();
        c->parent = tmp.get();
        tmp->children.push_back(c);
    }
    Compositor::prepare(tmp.get(), b);
    TileRange tr = tileRangeFor(b);
    QVector<qint64> keys;
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) keys << tileKey(tx, ty);
    QMutex mx;
    QHash<qint64, RasterSurface::Ptr> res;
    parallelForTiles(keys, [&](qint64 k) {
        QRect r = tileRectForKey(k) & b;
        std::vector<float> buf(size_t(r.width()) * r.height() * 4);
        Compositor::composite(tmp.get(), r, buf.data());
        auto t = std::make_shared<Tile<quint32>>();
        t->px.reset(new quint32[TILE_PIXELS]);
        std::fill_n(t->px.get(), TILE_PIXELS, 0u);
        QRect tr2 = tileRectForKey(k);
        quint32 any = 0;
        for (int y = 0; y < r.height(); ++y)
            for (int x = 0; x < r.width(); ++x) {
                quint32 v = pack(&buf[(size_t(y) * r.width() + x) * 4]);
                t->px[(r.top() - tr2.top() + y) * TILE + (r.left() - tr2.left() + x)] = v;
                any |= v;
            }
        if (!any) return;
        QMutexLocker lock(&mx);
        res.insert(k, t);
    });
    for (auto it = res.begin(); it != res.end(); ++it) out.setTile(it.key(), it.value());
    out.optimize();
    return out;
}

bool mergeDown(Document& doc, quint32 id, QString* error) {
    Layer* l = doc.layer(id);
    if (!l || !l->parent) return false;
    GroupLayer* p = l->parent;
    int i = p->indexOf(l);
    if (i <= 0) {
        if (error) *error = "There is no layer below to merge into.";
        return false;
    }
    Layer* below = p->children[size_t(i - 1)].get();
    if (below->isGroup() || below->type() == LayerType::Adjustment) {
        if (error) *error = "Layers can only be merged into a pixel layer.";
        return false;
    }
    if (below->locked || l->locked) {
        if (error) *error = "The layer is locked.";
        return false;
    }
    doc.pushUndo("Merge down");
    QRect b = doc.layerBounds(below);
    if (l->type() != LayerType::Adjustment) b |= doc.layerBounds(l);
    auto bc = below->clone();
    bc->opacity = 1;
    bc->blend = BlendMode::Normal;
    bc->clipping = false;
    bc->visible = true;
    auto lc = l->clone();
    lc->clipping = l->clipping && !below->clipping;
    RasterSurface merged = renderLayers(doc, {bc, lc}, b);
    auto r = toRaster(below);
    r->pixels = std::move(merged);
    r->fx = LayerEffects();
    r->hasMask = false;
    r->mask = MaskSurface(255);
    p->take(i);  // remove upper layer
    replaceLayer(below, r);
    doc.setCurrentLayer(r->id);
    doc.structureChanged();
    return true;
}

void mergeVisible(Document& doc) {
    doc.pushUndo("Merge visible");
    RasterSurface merged = renderLayers(doc, doc.root()->children, doc.bounds());
    auto r = std::make_shared<RasterLayer>();
    r->id = doc.newLayerId();
    r->name = "Merged";
    r->pixels = std::move(merged);
    auto& ch = doc.root()->children;
    int pos = -1;
    for (int i = int(ch.size()) - 1; i >= 0; --i)
        if (ch[size_t(i)]->visible) {
            doc.root()->take(i);
            pos = i;
        }
    if (pos < 0) pos = int(ch.size());
    doc.root()->insert(std::min(pos, int(ch.size())), r);
    doc.setCurrentLayer(r->id);
    doc.structureChanged();
}

void flatten(Document& doc) {
    doc.pushUndo("Flatten image");
    RasterSurface merged = renderLayers(doc, doc.root()->children, doc.bounds());
    auto r = std::make_shared<RasterLayer>();
    r->id = doc.newLayerId();
    r->name = "Background";
    r->pixels = std::move(merged);
    doc.root()->children.clear();
    doc.root()->insert(0, r);
    doc.setCurrentLayer(r->id);
    doc.structureChanged();
}

Layer* rasterize(Document& doc, quint32 id) {
    Layer* l = doc.layer(id);
    if (!l || l->isRaster() || l->type() == LayerType::Adjustment) return l;
    doc.pushUndo("Rasterize layer");
    std::shared_ptr<RasterLayer> r;
    if (l->isGroup()) {
        auto gc = l->clone();
        gc->opacity = 1;
        gc->blend = BlendMode::Normal;
        gc->clipping = false;
        gc->visible = true;
        static_cast<GroupLayer*>(gc.get())->passThrough = false;
        r = std::make_shared<RasterLayer>();
        r->id = l->id;
        r->name = l->name;
        r->visible = l->visible;
        r->opacity = l->opacity;
        r->blend = l->blend;
        r->clipping = l->clipping;
        r->pixels = renderLayers(doc, {gc}, doc.layerBounds(l));
    } else {
        r = toRaster(l);
    }
    Layer* raw = r.get();
    replaceLayer(l, r);
    doc.setCurrentLayer(raw->id);
    doc.structureChanged();
    return raw;
}

void moveLayer(Document& doc, quint32 id, GroupLayer* newParent, int index) {
    Layer* l = doc.layer(id);
    if (!l || !l->parent || !newParent) return;
    if (newParent == l || isAncestor(l, newParent)) return;
    GroupLayer* old = l->parent;
    int oi = old->indexOf(l);
    if (old == newParent && (index == oi || index == oi + 1)) return;
    doc.pushUndo("Move layer");
    LayerPtr keep = old->take(oi);
    if (old == newParent && oi < index) --index;
    newParent->insert(index, keep);
    doc.setCurrentLayer(id);
    doc.structureChanged();
}

void moveLayerUp(Document& doc, quint32 id) {
    Layer* l = doc.layer(id);
    if (!l || !l->parent) return;
    GroupLayer* p = l->parent;
    int i = p->indexOf(l);
    if (i < int(p->children.size()) - 1) moveLayer(doc, id, p, i + 2);
    else if (p->parent) moveLayer(doc, id, p->parent, p->parent->indexOf(p) + 1);
}

void moveLayerDown(Document& doc, quint32 id) {
    Layer* l = doc.layer(id);
    if (!l || !l->parent) return;
    GroupLayer* p = l->parent;
    int i = p->indexOf(l);
    if (i > 0) moveLayer(doc, id, p, i - 1);
    else if (p->parent) moveLayer(doc, id, p->parent, p->parent->indexOf(p));
}

GroupLayer* groupLayer(Document& doc, quint32 id) {
    Layer* l = doc.layer(id);
    if (!l || !l->parent) return nullptr;
    doc.pushUndo("Group into folder");
    GroupLayer* p = l->parent;
    int i = p->indexOf(l);
    LayerPtr keep = p->take(i);
    auto g = std::make_shared<GroupLayer>();
    g->id = doc.newLayerId();
    g->name = doc.uniqueLayerName("Folder");
    keep->clipping = false;
    g->insert(0, keep);
    p->insert(i, g);
    doc.setCurrentLayer(g->id);
    doc.structureChanged();
    return g.get();
}

void clearLayer(Document& doc, quint32 id) {
    Layer* l = doc.layer(id);
    if (!l) return;
    if (l->type() == LayerType::Vector) {
        doc.pushUndo("Clear layer");
        auto* v = static_cast<VectorLayer*>(l);
        QRect r = v->cache.tileBounds();
        v->strokes.clear();
        v->cache.clear();
        doc.pixelsChanged(r, l);
        return;
    }
    if (!l->isRaster()) return;
    auto* rl = static_cast<RasterLayer*>(l);
    doc.pushUndo("Clear layer");
    QRect changed = rl->pixels.tileBounds();
    if (!doc.hasSelection()) {
        rl->pixels.clear();
    } else {
        QRect r = doc.selectionBounds() & changed;
        std::vector<float> px(size_t(r.width()) * r.height() * 4), sel(size_t(r.width()) * r.height());
        readFloat(rl->pixels, r, px.data());
        readMask(doc.selection(), r, sel.data());
        for (size_t i = 0; i < sel.size(); ++i)
            for (int k = 0; k < 4; ++k) px[i * 4 + size_t(k)] *= 1.f - sel[i];
        writeFloat(rl->pixels, r, px.data());
        rl->pixels.optimizeRect(r);
        changed = r;
    }
    doc.pixelsChanged(changed, l);
}

void fillLayer(Document& doc, quint32 id, const QColor& c) {
    Layer* l = doc.layer(id);
    if (!l || !l->isRaster()) return;
    auto* rl = static_cast<RasterLayer*>(l);
    doc.pushUndo("Fill");
    float col[4] = {float(c.redF()), float(c.greenF()), float(c.blueF()), 1.f};
    QRect r = doc.hasSelection() ? doc.selectionBounds() : doc.bounds();
    if (r.isEmpty()) return;
    if (!doc.hasSelection() && !l->alphaLock) {
        rl->pixels.fillRect(r, premulFromColor(c));
    } else {
        std::vector<float> px(size_t(r.width()) * r.height() * 4), sel(size_t(r.width()) * r.height(), 1.f);
        readFloat(rl->pixels, r, px.data());
        if (doc.hasSelection()) readMask(doc.selection(), r, sel.data());
        for (size_t i = 0; i < sel.size(); ++i) {
            float* p = &px[i * 4];
            float w = sel[i];
            if (w <= 0) continue;
            float oldA = p[3];
            for (int k = 0; k < 4; ++k) p[k] += (col[k] - p[k]) * w;
            if (l->alphaLock) {
                float f = p[3] > 1e-6f ? oldA / p[3] : 0.f;
                p[0] *= f; p[1] *= f; p[2] *= f; p[3] = oldA;
            }
        }
        writeFloat(rl->pixels, r, px.data());
        rl->pixels.optimizeRect(r);
    }
    doc.pixelsChanged(r, l);
}

void flipLayer(Document& doc, quint32 id, bool horizontal) {
    Layer* l = doc.layer(id);
    if (!l || l->isGroup() || l->type() == LayerType::Adjustment) return;
    if (l->type() == LayerType::Text) l = rasterize(doc, id);
    doc.pushUndo(horizontal ? "Flip layer horizontally" : "Flip layer vertically");
    QRect before = doc.layerBounds(l);
    QTransform t = horizontal ? QTransform(-1, 0, 0, 1, doc.size().width(), 0) : QTransform(1, 0, 0, -1, 0, doc.size().height());
    transformLayerContent(*l, t, false);
    doc.pixelsChanged(before | doc.layerBounds(l), l);
}

// ============================================================================ masks

// ---------------------------------------------------------------------------- channels

bool mapLayerPixels(Document& doc, quint32 id, const QString& undoName, const std::function<void(float*, int)>& fn, bool wholeCanvas) {
    auto* rl = dynamic_cast<RasterLayer*>(doc.layer(id));
    if (!rl || rl->isEffectivelyLocked()) return false;
    QRect area = wholeCanvas ? doc.bounds() : rl->pixels.tileBounds();
    if (doc.hasSelection()) area &= doc.selectionBounds();
    if (area.isEmpty()) return false;
    doc.pushUndo(undoName);
    rl = static_cast<RasterLayer*>(doc.layer(id));
    const RasterSurface original = rl->pixels;
    TileRange tr = tileRangeFor(area);
    QVector<qint64> keys;
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) keys << tileKey(tx, ty);
    QMutex mx;
    QHash<qint64, std::vector<float>> results;
    parallelForTiles(keys, [&](qint64 k) {
        QRect r = tileRectForKey(k) & area;
        std::vector<float> buf(size_t(r.width()) * r.height() * 4);
        readFloat(original, r, buf.data());
        fn(buf.data(), r.width() * r.height());
        QMutexLocker lock(&mx);
        results.insert(k, std::move(buf));
    });
    for (auto it = results.begin(); it != results.end(); ++it) writeFloat(rl->pixels, tileRectForKey(it.key()) & area, it.value().data());
    applyToSelection(doc, rl->pixels, original, area);
    rl->pixels.optimizeRect(area);
    doc.pixelsChanged(area, rl);
    return true;
}

GroupLayer* splitChannels(Document& doc, quint32 id) {
    auto* rl = dynamic_cast<RasterLayer*>(doc.layer(id));
    if (!rl) return nullptr;
    doc.pushUndo("Split channels");
    rl = static_cast<RasterLayer*>(doc.layer(id));
    auto g = std::make_shared<GroupLayer>();
    g->id = doc.newLayerId();
    g->name = rl->name + " RGB";
    g->expanded = true;
    const char* names[3] = {"Red", "Green", "Blue"};
    QRect b = rl->pixels.tileBounds();
    for (int ch = 0; ch < 3; ++ch) {
        auto c = std::make_shared<RasterLayer>();
        c->id = doc.newLayerId();
        c->name = QString::fromLatin1(names[ch]);
        c->blend = ch == 0 ? BlendMode::Normal : BlendMode::LinearDodge;  // added together they give the picture back
        if (!b.isEmpty()) {
            std::vector<float> buf(size_t(b.width()) * b.height() * 4);
            readFloat(rl->pixels, b, buf.data());
            for (size_t i = 0; i < buf.size() / 4; ++i)
                for (int k = 0; k < 3; ++k)
                    if (k != ch) buf[i * 4 + size_t(k)] = 0.f;
            writeFloat(c->pixels, b, buf.data());
            c->pixels.optimize();
        }
        g->insert(ch, c);
    }
    GroupLayer* p = rl->parent;
    int at = p->indexOf(rl);
    rl->visible = false;  // keep the original, hidden, under the channels
    p->insert(at + 1, g);
    doc.setCurrentLayer(g->id);
    doc.structureChanged();
    return g.get();
}

Layer* alphaAsLayer(Document& doc, quint32 id) {
    auto* rl = dynamic_cast<RasterLayer*>(doc.layer(id));
    if (!rl) return nullptr;
    auto c = std::make_shared<RasterLayer>();
    c->id = doc.newLayerId();
    c->name = rl->name + " alpha";
    c->pixels.fillRect(doc.bounds(), 0xff000000u);
    QRect b = rl->pixels.tileBounds() & doc.bounds();
    if (!b.isEmpty()) {
        std::vector<float> buf(size_t(b.width()) * b.height() * 4);
        readFloat(rl->pixels, b, buf.data());
        for (size_t i = 0; i < buf.size() / 4; ++i) {
            float a = buf[i * 4 + 3];
            buf[i * 4] = buf[i * 4 + 1] = buf[i * 4 + 2] = a;
            buf[i * 4 + 3] = 1.f;
        }
        writeFloat(c->pixels, b, buf.data());
    }
    return insertLayer(doc, c, rl, true, "Alpha as layer");
}

void transparencyToMask(Document& doc, quint32 id) {
    auto* rl = dynamic_cast<RasterLayer*>(doc.layer(id));
    if (!rl || rl->isEffectivelyLocked()) return;
    doc.pushUndo("Transparency to mask");
    rl = static_cast<RasterLayer*>(doc.layer(id));
    QRect b = rl->pixels.tileBounds() & doc.bounds();
    MaskSurface mask(0);
    if (!b.isEmpty()) {
        const size_t n = size_t(b.width()) * b.height();
        std::vector<float> buf(n * 4), m(n);
        readFloat(rl->pixels, b, buf.data());
        for (size_t i = 0; i < n; ++i) {
            float a = buf[i * 4 + 3];
            m[i] = a;
            if (a > 0) {
                for (int k = 0; k < 3; ++k) buf[i * 4 + size_t(k)] /= a;
                buf[i * 4 + 3] = 1.f;
            }
        }
        writeFloat(rl->pixels, b, buf.data());
        writeMask(mask, b, m.data());
        mask.optimize();
    }
    if (rl->hasMask) {
        // combine with the existing mask
        QRect mb = b;
        const size_t n = size_t(mb.width()) * mb.height();
        std::vector<float> a(n), c(n);
        readMask(rl->mask, mb, a.data());
        readMask(mask, mb, c.data());
        for (size_t i = 0; i < n; ++i) c[i] *= a[i];
        writeMask(mask, mb, c.data());
    }
    rl->mask = std::move(mask);
    rl->hasMask = true;
    rl->maskEnabled = true;
    doc.pixelsChanged(b, rl);
    doc.layersChanged();
}

void invertAlpha(Document& doc, quint32 id) {
    mapLayerPixels(doc, id, "Invert alpha", [](float* px, int n) {
        for (int i = 0; i < n; ++i) {
            float* p = px + size_t(i) * 4;
            const float a = p[3], na = 1.f - a;
            if (a > 0) for (int k = 0; k < 3; ++k) p[k] = p[k] / a * na;
            else p[0] = p[1] = p[2] = 0.f;  // transparent had no color: black
            p[3] = na;
        }
    }, true);
}

void removeWhite(Document& doc, quint32 id) {
    // "color to alpha" with white: keeps every shade of gray as partly transparent black,
    // so scanned line art can be painted under
    mapLayerPixels(doc, id, "Remove white", [](float* px, int n) {
        for (int i = 0; i < n; ++i) {
            float* p = px + size_t(i) * 4;
            const float a = p[3];
            if (a <= 0) continue;
            float c[3] = {p[0] / a, p[1] / a, p[2] / a};
            const float na = std::max({1.f - c[0], 1.f - c[1], 1.f - c[2]});
            if (na <= 1e-4f) {
                p[0] = p[1] = p[2] = p[3] = 0.f;
                continue;
            }
            for (int k = 0; k < 3; ++k) c[k] = clamp01((c[k] - 1.f) / na + 1.f);
            const float fa = na * a;
            for (int k = 0; k < 3; ++k) p[k] = c[k] * fa;
            p[3] = fa;
        }
    });
}

void recolorLayer(Document& doc, quint32 id, const QColor& col) {
    const float c[3] = {float(col.redF()), float(col.greenF()), float(col.blueF())};
    mapLayerPixels(doc, id, "Recolor", [c](float* px, int n) {
        for (int i = 0; i < n; ++i) {
            float* p = px + size_t(i) * 4;
            for (int k = 0; k < 3; ++k) p[k] = c[k] * p[3];
        }
    });
}

Layer* layerFromSelection(Document& doc, bool cut) {
    auto* rl = dynamic_cast<RasterLayer*>(doc.currentLayer());
    if (!rl || !doc.hasSelection()) return nullptr;
    QRect r = doc.selectionBounds() & rl->pixels.tileBounds();
    if (r.isEmpty()) return nullptr;
    doc.pushUndo(cut ? "Cut to new layer" : "Copy to new layer");
    rl = static_cast<RasterLayer*>(doc.currentLayer());
    const size_t n = size_t(r.width()) * r.height();
    std::vector<float> px(n * 4), m(n), keep(n * 4);
    readFloat(rl->pixels, r, px.data());
    readMask(doc.selection(), r, m.data());
    for (size_t i = 0; i < n; ++i)
        for (int k = 0; k < 4; ++k) {
            keep[i * 4 + size_t(k)] = px[i * 4 + size_t(k)] * (1.f - m[i]);
            px[i * 4 + size_t(k)] *= m[i];
        }
    auto c = std::make_shared<RasterLayer>();
    c->id = doc.newLayerId();
    c->name = doc.uniqueLayerName("Layer");
    writeFloat(c->pixels, r, px.data());
    c->pixels.optimize();
    if (cut && !rl->isEffectivelyLocked()) {
        writeFloat(rl->pixels, r, keep.data());
        rl->pixels.optimizeRect(r);
        doc.pixelsChanged(r, rl);
    }
    GroupLayer* p = rl->parent;
    p->insert(p->indexOf(rl) + 1, c);
    doc.setCurrentLayer(c->id);
    doc.structureChanged();
    return c.get();
}

void centerLayer(Document& doc, quint32 id) {
    Layer* l = doc.layer(id);
    if (!l || l->isEffectivelyLocked()) return;
    const RasterSurface* s = l->displaySurface();
    QRect b = s ? s->exactBounds() : QRect();
    if (b.isEmpty()) return;
    QPoint d = doc.bounds().center() - b.center();
    if (d.isNull()) return;
    doc.pushUndo("Center on canvas");
    l = doc.layer(id);
    if (l->hasMask) l->mask = l->mask.translated(d.x(), d.y());
    if (auto* r = dynamic_cast<RasterLayer*>(l)) r->pixels = r->pixels.translated(d.x(), d.y());
    else if (auto* v = dynamic_cast<VectorLayer*>(l)) {
        for (auto& st : v->strokes)
            for (auto& p : st.points) p.pos += d;
        v->cache = v->cache.translated(d.x(), d.y());
    } else if (auto* t = dynamic_cast<TextLayer*>(l)) {
        t->text.origin += d;
        t->cache = t->cache.translated(d.x(), d.y());
    }
    l->version++;
    doc.pixelsChanged(b | b.translated(d), l);
}

// ---------------------------------------------------------------------------- flats

Layer* autoFlats(Document& doc, int gapClose, bool skipBackground, int colorMode, const QColor& main, QString* error) {
    // line art: the reference layers, or the current layer
    std::vector<LayerPtr> refs;
    Layer* lowestRef = nullptr;
    forEachLayer(doc.root(), [&](Layer* l) {
        if (l->reference && !l->isGroup()) {
            LayerPtr c = l->clone();
            c->visible = true;
            c->clipping = false;
            refs.push_back(c);
            if (!lowestRef) lowestRef = l;
        }
        return true;
    });
    if (refs.empty())
        if (Layer* cur = doc.currentLayer(); cur && !cur->isGroup() && cur->type() != LayerType::Adjustment) {
            refs.push_back(cur->clone());
            refs.back()->visible = true;
            lowestRef = cur;
        }
    if (refs.empty()) {
        if (error) *error = "Select the line art layer first.";
        return nullptr;
    }
    const QRect area = doc.bounds();
    const int W = area.width(), H = area.height();
    if (qint64(W) * H > 200ll * 1000 * 1000) {
        if (error) *error = "The area is too large to fill at once. Select a smaller area first.";
        return nullptr;
    }
    RasterSurface lines = renderLayers(doc, refs, area);
    const size_t N = size_t(W) * H;
    std::vector<float> px(N * 4);
    readFloat(lines, area, px.data());
    std::vector<uint8_t> ink(N);
    for (size_t i = 0; i < N; ++i) {
        const float a = px[i * 4 + 3];
        const float lum = a > 0 ? luminance(px[i * 4], px[i * 4 + 1], px[i * 4 + 2]) / a : 1.f;
        ink[i] = a * (1.f - lum) >= 0.35f ? 1 : 0;  // dark, solid paint = border
    }
    std::vector<uint8_t> wall = ink;
    if (gapClose > 0) {
        std::vector<float> d2;
        distanceTransform(ink, W, H, d2);
        const float r2 = float(gapClose) * float(gapClose) * 0.25f;
        for (size_t i = 0; i < N; ++i) wall[i] = d2[i] <= r2 ? 1 : 0;
    }
    // label the closed areas
    std::vector<int> label(N, -1);
    std::vector<int> stack;
    std::vector<int> areas;
    std::vector<bool> touchesEdge;
    for (int start = 0; start < int(N); ++start) {
        if (wall[size_t(start)] || label[size_t(start)] >= 0) continue;
        const int id = int(areas.size());
        int count = 0;
        bool edge = false;
        stack.push_back(start);
        label[size_t(start)] = id;
        while (!stack.empty()) {
            const int p = stack.back();
            stack.pop_back();
            ++count;
            const int x = p % W, y = p / W;
            if (x == 0 || y == 0 || x == W - 1 || y == H - 1) edge = true;
            const int nb[4] = {x > 0 ? p - 1 : -1, x < W - 1 ? p + 1 : -1, y > 0 ? p - W : -1, y < H - 1 ? p + W : -1};
            for (int q : nb)
                if (q >= 0 && !wall[size_t(q)] && label[size_t(q)] < 0) {
                    label[size_t(q)] = id;
                    stack.push_back(q);
                }
        }
        areas.push_back(count);
        touchesEdge.push_back(edge);
    }
    if (areas.empty()) {
        if (error) *error = "No closed areas were found in the line art.";
        return nullptr;
    }
    // tiny specks and (optionally) the background stay out; lines get the color of a neighbor
    std::vector<bool> keep(areas.size());
    for (size_t i = 0; i < areas.size(); ++i) keep[i] = areas[i] >= 12 && !(skipBackground && touchesEdge[i]);
    std::vector<int> owner(N, -1);
    std::deque<int> queue;
    for (size_t i = 0; i < N; ++i)
        if (label[i] >= 0 && keep[size_t(label[i])]) {
            owner[i] = label[i];
            queue.push_back(int(i));
        }
    // grow into lines, closed-gap borders and specks (up to a limit so the background stays empty)
    std::vector<uint8_t> dist(N, 0);
    const int maxGrow = std::max(6, gapClose + 6);
    while (!queue.empty()) {
        const int p = queue.front();
        queue.pop_front();
        const int x = p % W, y = p / W;
        const int nb[4] = {x > 0 ? p - 1 : -1, x < W - 1 ? p + 1 : -1, y > 0 ? p - W : -1, y < H - 1 ? p + W : -1};
        for (int q : nb) {
            if (q < 0 || owner[size_t(q)] >= 0) continue;
            const bool lineOrGap = wall[size_t(q)] || (label[size_t(q)] >= 0 && !keep[size_t(label[size_t(q)])] && areas[size_t(label[size_t(q)])] < 12);
            if (!lineOrGap || dist[size_t(p)] >= maxGrow) continue;
            owner[size_t(q)] = owner[size_t(p)];
            dist[size_t(q)] = uint8_t(std::min(255, dist[size_t(p)] + 1));
            queue.push_back(q);
        }
    }
    // colors
    std::vector<quint32> colors(areas.size());
    double hue = 0.11;
    for (size_t i = 0; i < areas.size(); ++i) {
        QColor c;
        if (colorMode == 2) c = main;
        else if (colorMode == 1) c = QColor::fromHsvF(0, 0, 0.55 + 0.4 * std::fmod(i * 0.381966, 1.0));
        else {
            hue = std::fmod(hue + 0.618034, 1.0);
            c = QColor::fromHsvF(float(hue), float(0.32 + 0.2 * std::fmod(i * 0.7548, 1.0)), float(0.86 + 0.1 * std::fmod(i * 0.5698, 1.0)));
        }
        colors[i] = 0xff000000u | (quint32(c.red()) << 16) | (quint32(c.green()) << 8) | quint32(c.blue());
    }
    std::vector<quint32> out(N, 0u);
    for (size_t i = 0; i < N; ++i)
        if (owner[i] >= 0) out[i] = colors[size_t(owner[i])];
    doc.pushUndo("Auto-fill colors");
    auto flats = std::make_shared<RasterLayer>();
    flats->id = doc.newLayerId();
    flats->name = doc.uniqueLayerName("Flats");
    flats->pixels.writeRect(area, out.data(), W);
    flats->pixels.optimize();
    // under the line art
    Layer* ref = lowestRef ? doc.layer(lowestRef->id) : nullptr;
    GroupLayer* parent = ref && ref->parent ? ref->parent : doc.root();
    const int at = ref && ref->parent ? parent->indexOf(ref) : int(parent->children.size());
    parent->insert(std::max(0, at), flats);
    doc.setCurrentLayer(flats->id);
    doc.structureChanged();
    return flats.get();
}

// ---------------------------------------------------------------------------- several layers

namespace {
// Keeps only layers that exist and are not inside another listed folder (bottom first).
QList<quint32> topLevel(Document& doc, const QList<quint32>& ids) {
    QSet<quint32> set(ids.begin(), ids.end());
    QList<quint32> out;
    forEachLayer(doc.root(), [&](Layer* l) {
        if (!set.contains(l->id)) return true;
        for (const GroupLayer* p = l->parent; p; p = p->parent)
            if (set.contains(p->id)) return true;
        out << l->id;
        return true;
    });
    return out;
}
}  // namespace

void deleteLayers(Document& doc, const QList<quint32>& ids) {
    QList<quint32> list = topLevel(doc, ids);
    if (list.isEmpty()) return;
    if (list.size() == 1) {
        deleteLayer(doc, list.first());
        return;
    }
    doc.pushUndo("Delete layers");
    // the layer just below the lowest deleted one becomes current
    Layer* first = doc.layer(list.first());
    GroupLayer* fp = first ? first->parent : nullptr;
    int fi = fp ? fp->indexOf(first) : -1;
    quint32 nextId = 0;
    if (fp && fi > 0) nextId = fp->children[size_t(fi - 1)]->id;
    for (quint32 id : list)
        if (Layer* l = doc.layer(id); l && l->parent) l->parent->take(l->parent->indexOf(l));
    ensureOneLayer(doc);
    if (!doc.layer(nextId)) nextId = doc.root()->children.empty() ? 0 : doc.root()->children.back()->id;
    doc.setCurrentLayer(nextId);
    doc.structureChanged();
}

void duplicateLayers(Document& doc, const QList<quint32>& ids) {
    QList<quint32> list = topLevel(doc, ids);
    if (list.isEmpty()) return;
    if (list.size() == 1) {
        if (Layer* c = duplicateLayer(doc, list.first())) doc.setCurrentLayer(c->id);
        return;
    }
    doc.pushUndo("Duplicate layers");
    QList<quint32> copies;
    for (quint32 id : list) {
        Layer* l = doc.layer(id);
        if (!l || !l->parent) continue;
        auto c = l->clone();
        assignNewIds(doc, c.get());
        c->name = l->name + " copy";
        GroupLayer* p = l->parent;
        p->insert(p->indexOf(l) + 1, c);
        copies << c->id;
    }
    if (!copies.isEmpty()) doc.setLayerSelection(copies, copies.last());
    doc.structureChanged();
}

GroupLayer* groupLayers(Document& doc, const QList<quint32>& ids) {
    QList<quint32> list = topLevel(doc, ids);
    if (list.isEmpty()) return nullptr;
    if (list.size() == 1) return groupLayer(doc, list.first());
    doc.pushUndo("Group into folder");
    // the folder goes where the topmost layer was
    Layer* top = doc.layer(list.last());
    GroupLayer* parent = top->parent;
    std::vector<LayerPtr> moving;
    for (quint32 id : list) moving.push_back(sharedOf(doc.layer(id)));
    // position: right above the topmost layer, counted after the others leave
    for (const LayerPtr& m : moving)
        if (m && m.get() != top && m->parent) m->parent->take(m->parent->indexOf(m.get()));
    int at = parent->indexOf(top);
    parent->take(at);
    auto g = std::make_shared<GroupLayer>();
    g->id = doc.newLayerId();
    g->name = doc.uniqueLayerName("Folder");
    int k = 0;
    for (const LayerPtr& m : moving) {
        if (!m) continue;
        g->insert(k++, m);
    }
    parent->insert(std::clamp(at, 0, int(parent->children.size())), g);
    g->expanded = true;
    doc.setCurrentLayer(g->id);
    doc.structureChanged();
    return g.get();
}

Layer* mergeLayers(Document& doc, const QList<quint32>& ids, QString* error) {
    QList<quint32> list = topLevel(doc, ids);
    if (list.size() < 2) {
        if (error) *error = "Select two or more layers to merge.";
        return nullptr;
    }
    for (quint32 id : list)
        if (Layer* l = doc.layer(id); l && l->isEffectivelyLocked()) {
            if (error) *error = "The layer is locked.";
            return nullptr;
        }
    doc.pushUndo("Merge layers");
    std::vector<LayerPtr> parts;
    QRect b;
    for (quint32 id : list) {
        Layer* l = doc.layer(id);
        if (!l) continue;
        parts.push_back(sharedOf(l));
        if (l->type() != LayerType::Adjustment) b |= doc.layerBounds(l);
    }
    // the lowest layer keeps its own look, everything is drawn as it was
    RasterSurface merged = renderLayers(doc, parts, b);
    Layer* top = doc.layer(list.last());
    GroupLayer* parent = top->parent;
    int at = parent->indexOf(top);
    auto r = std::make_shared<RasterLayer>();
    r->id = doc.newLayerId();
    r->name = top->name;
    r->pixels = std::move(merged);
    r->pixels.optimize();
    parent->insert(at + 1, r);
    for (quint32 id : list)
        if (Layer* l = doc.layer(id); l && l->parent) l->parent->take(l->parent->indexOf(l));
    doc.setCurrentLayer(r->id);
    doc.structureChanged();
    return r.get();
}

void moveLayers(Document& doc, const QList<quint32>& ids, GroupLayer* newParent, int index) {
    QList<quint32> list = topLevel(doc, ids);
    if (list.isEmpty() || !newParent) return;
    if (list.size() == 1) {
        moveLayer(doc, list.first(), newParent, index);
        return;
    }
    for (quint32 id : list) {
        Layer* l = doc.layer(id);
        if (!l || l == newParent || isAncestor(l, newParent)) return;  // a folder can't go inside itself
    }
    const quint32 parentId = newParent->id;
    const bool toRoot = newParent == doc.root();
    // remember a layer to insert next to, since indexes change when layers leave
    Layer* anchor = index < int(newParent->children.size()) ? newParent->children[size_t(std::max(0, index))].get() : nullptr;
    const quint32 anchorId = anchor ? anchor->id : 0;
    const quint32 current = doc.currentLayerId();
    doc.pushUndo("Move layer");
    GroupLayer* target = toRoot ? doc.root() : static_cast<GroupLayer*>(doc.layer(parentId));
    if (!target) return;
    std::vector<LayerPtr> moving;
    for (quint32 id : list) {
        Layer* l = doc.layer(id);
        if (!l || !l->parent) continue;
        moving.push_back(sharedOf(l));
    }
    for (const LayerPtr& m : moving) m->parent->take(m->parent->indexOf(m.get()));
    int at = int(target->children.size());
    if (anchorId && !list.contains(anchorId))
        if (Layer* a = doc.layer(anchorId); a && a->parent == target) at = target->indexOf(a);
    for (const LayerPtr& m : moving) target->insert(at++, m);
    doc.setLayerSelection(list, current);
    doc.structureChanged();
}

void setLayersVisible(Document& doc, const QList<quint32>& ids, bool visible) {
    if (ids.isEmpty()) return;
    doc.pushUndo("Layer visibility", "visibility");
    for (quint32 id : ids)
        if (Layer* l = doc.layer(id)) {
            l->visible = visible;
            doc.layerChanged(l);
        }
}

void addMask(Document& doc, quint32 id, bool fromSelection) {
    Layer* l = doc.layer(id);
    if (!l || l->hasMask) return;
    doc.pushUndo("Add layer mask");
    l->hasMask = true;
    l->maskEnabled = true;
    l->mask = MaskSurface(255);
    if (fromSelection && doc.hasSelection()) {
        l->mask.fillRect(doc.bounds(), 0);
        const auto& sel = doc.selection();
        for (auto it = sel.tiles().cbegin(); it != sel.tiles().cend(); ++it) l->mask.setTile(it.key(), it.value());
    }
    doc.layerChanged(l);
    doc.structureChanged();
}

void deleteMask(Document& doc, quint32 id) {
    Layer* l = doc.layer(id);
    if (!l || !l->hasMask) return;
    doc.pushUndo("Delete layer mask");
    l->hasMask = false;
    l->mask = MaskSurface(255);
    doc.structureChanged();
}

void applyMask(Document& doc, quint32 id) {
    Layer* l = doc.layer(id);
    if (!l || !l->hasMask || !l->isRaster()) return;
    doc.pushUndo("Apply layer mask");
    auto* rl = static_cast<RasterLayer*>(l);
    QRect r = rl->pixels.tileBounds();
    if (!r.isEmpty()) {
        std::vector<float> px(size_t(r.width()) * r.height() * 4), m(size_t(r.width()) * r.height());
        readFloat(rl->pixels, r, px.data());
        readMask(l->mask, r, m.data());
        for (size_t i = 0; i < m.size(); ++i)
            for (int k = 0; k < 4; ++k) px[i * 4 + size_t(k)] *= m[i];
        writeFloat(rl->pixels, r, px.data());
        rl->pixels.optimizeRect(r);
    }
    l->hasMask = false;
    l->mask = MaskSurface(255);
    doc.structureChanged();
}

// ============================================================================ selection

MaskSurface rasterizePath(const QPainterPath& path, const QRect& clip, bool antialias) {
    MaskSurface m(0);
    QRect r = path.boundingRect().toAlignedRect().adjusted(-1, -1, 1, 1) & clip;
    if (r.isEmpty()) return m;
    QImage img(r.size(), QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) return m;
    img.fill(0);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, antialias);
        p.translate(-r.topLeft());
        p.fillPath(path, Qt::white);
    }
    alphaImageToMask(img, m, r.topLeft(), false);
    return m;
}

void selectAll(Document& doc) {
    doc.pushUndo("Select all");
    doc.selection().clear();
    doc.selection().fillRect(doc.bounds(), 255);
    doc.setHasSelection(true);
    doc.selectionModified();
}

void deselect(Document& doc) {
    if (!doc.hasSelection()) return;
    doc.savedSelection() = doc.selection();
    doc.pushUndo("Deselect");
    doc.setHasSelection(false);
    doc.selectionModified();
}

void reselect(Document& doc) {
    if (doc.savedSelection().isEmpty()) return;
    doc.pushUndo("Reselect");
    doc.selection() = doc.savedSelection();
    doc.setHasSelection(true);
    doc.selectionModified();
}

void invertSelection(Document& doc) {
    if (!doc.hasSelection()) return;
    doc.pushUndo("Invert selection");
    MaskSurface& s = doc.selection();
    MaskSurface out(0);
    const QRect canvas = doc.bounds();
    TileRange tr = tileRangeFor(canvas);
    for (int ty = tr.y0; ty <= tr.y1; ++ty)
        for (int tx = tr.x0; tx <= tr.x1; ++tx) {
            QRect r = tileRect(tx, ty);
            const auto* t = s.tile(tx, ty);
            if (canvas.contains(r) && (!t || !t->px)) {
                quint8 v = 255 - (t ? t->uniform : 0);
                if (v) {
                    auto u = std::make_shared<Tile<quint8>>();
                    u->uniform = v;
                    out.setTile(tileKey(tx, ty), u);
                }
                continue;
            }
            QRect ir = r & canvas;
            quint8* p = out.writable(tx, ty);
            for (int y = ir.top(); y <= ir.bottom(); ++y)
                for (int x = ir.left(); x <= ir.right(); ++x) {
                    int i = ((y & (TILE - 1)) << TILE_SHIFT) | (x & (TILE - 1));
                    p[i] = quint8(255 - (t ? t->at(i) : 0));
                }
        }
    out.optimize();
    s = std::move(out);
    doc.setHasSelection(!s.isEmpty());
    doc.selectionModified();
}

void combineSelection(Document& doc, const MaskSurface& shape, SelectMode mode, const QString& undoName) {
    doc.pushUndo(undoName);
    MaskSurface& s = doc.selection();
    if (mode == SelectMode::Replace || (!doc.hasSelection() && (mode == SelectMode::Add || mode == SelectMode::Intersect))) {
        s = shape;
    } else if (!doc.hasSelection() && mode == SelectMode::Subtract) {
        // nothing selected: subtracting changes nothing
    } else {
        QSet<qint64> keys;
        for (auto it = s.tiles().cbegin(); it != s.tiles().cend(); ++it) keys.insert(it.key());
        for (auto it = shape.tiles().cbegin(); it != shape.tiles().cend(); ++it) keys.insert(it.key());
        for (qint64 k : keys) {
            const auto* a = s.tile(keyTx(k), keyTy(k));
            const auto* b = shape.tile(keyTx(k), keyTy(k));
            if (mode == SelectMode::Add && !b) continue;
            if (mode == SelectMode::Subtract && !b) continue;
            if (mode == SelectMode::Intersect && !a) continue;
            std::vector<quint8> res(TILE_PIXELS);
            for (int i = 0; i < TILE_PIXELS; ++i) {
                int va = a ? a->at(i) : 0, vb = b ? b->at(i) : 0;
                int v = 0;
                switch (mode) {
                case SelectMode::Add: v = std::max(va, vb); break;
                case SelectMode::Subtract: v = std::min(va, 255 - vb); break;
                case SelectMode::Intersect: v = std::min(va, vb); break;
                default: v = vb; break;
                }
                res[size_t(i)] = quint8(v);
            }
            quint8* p = s.writable(keyTx(k), keyTy(k));
            std::copy(res.begin(), res.end(), p);
        }
    }
    s.optimize();
    doc.setHasSelection(!s.isEmpty());
    doc.selectionModified();
}

void selectPath(Document& doc, const QPainterPath& path, SelectMode mode, bool antialias, const QString& undoName) {
    MaskSurface m = rasterizePath(path, doc.bounds(), antialias);
    combineSelection(doc, m, mode, undoName);
}

void selectLayerAlpha(Document& doc, quint32 id, SelectMode mode) {
    Layer* l = doc.layer(id);
    if (!l) return;
    MaskSurface m(0);
    const RasterSurface* s = l->displaySurface();
    RasterSurface rendered;
    if (l->isGroup()) {
        rendered = renderLayers(doc, {sharedOf(l)}, doc.layerBounds(l));
        s = &rendered;
    }
    if (!s) return;
    for (auto it = s->tiles().cbegin(); it != s->tiles().cend(); ++it) {
        QRect r = tileRectForKey(it.key());
        if (!r.intersects(doc.bounds())) continue;
        quint8* p = m.writable(keyTx(it.key()), keyTy(it.key()));
        for (int i = 0; i < TILE_PIXELS; ++i) p[i] = quint8(it.value()->at(i) >> 24);
    }
    if (l->hasMask && l->maskEnabled)
        for (auto it = m.tiles().cbegin(); it != m.tiles().cend(); ++it) {
            // multiply by the layer mask
            int tx = keyTx(it.key()), ty = keyTy(it.key());
            const auto* mt = l->mask.tile(tx, ty);
            if (!mt) continue;
            quint8* p = m.writable(tx, ty);
            for (int i = 0; i < TILE_PIXELS; ++i) p[i] = quint8(p[i] * mt->at(i) / 255);
        }
    m.optimize();
    combineSelection(doc, m, mode, "Select layer opacity");
}

namespace {
// Region of the selection grown by margin, as a float buffer
struct SelBuf {
    QRect r;
    std::vector<float> v;
};
SelBuf readSel(Document& doc, int margin) {
    SelBuf b;
    b.r = doc.selectionBounds().adjusted(-margin, -margin, margin, margin) & doc.bounds();
    b.v.resize(size_t(b.r.width()) * b.r.height());
    readMask(doc.selection(), b.r, b.v.data());
    return b;
}
void writeSel(Document& doc, const SelBuf& b) {
    std::vector<quint8> u(b.v.size());
    for (size_t i = 0; i < u.size(); ++i) u[i] = quint8(std::clamp(int(b.v[i] * 255.f + 0.5f), 0, 255));
    doc.selection().clear();
    doc.selection().writeRect(b.r, u.data(), b.r.width());
    doc.selection().optimize();
    doc.setHasSelection(!doc.selection().isEmpty());
    doc.selectionModified();
}
std::vector<float> grownCoverage(const SelBuf& b, int px) {
    // px > 0: distance outward from inside; px < 0: shrink
    const int w = b.r.width(), h = b.r.height();
    std::vector<uint8_t> seeds(b.v.size());
    bool grow = px >= 0;
    for (size_t i = 0; i < b.v.size(); ++i) seeds[i] = grow ? (b.v[i] >= 0.5f) : (b.v[i] < 0.5f);
    std::vector<float> d2;
    distanceTransform(seeds, w, h, d2);
    std::vector<float> out(b.v.size());
    float R = float(std::abs(px));
    for (size_t i = 0; i < out.size(); ++i) {
        float d = std::sqrt(d2[i]);
        float c = clamp01(R + 0.5f - d);
        out[i] = grow ? std::max(c, b.v[i]) : std::min(1.f - c, b.v[i]);
    }
    return out;
}
}  // namespace

void growSelection(Document& doc, int px) {
    if (!doc.hasSelection() || px == 0) return;
    doc.pushUndo(px > 0 ? "Expand selection" : "Contract selection");
    SelBuf b = readSel(doc, std::abs(px) + 2);
    b.v = grownCoverage(b, px);
    writeSel(doc, b);
}

void featherSelection(Document& doc, double radius) {
    if (!doc.hasSelection() || radius <= 0) return;
    doc.pushUndo("Feather selection");
    SelBuf b = readSel(doc, int(std::ceil(radius * 3)) + 2);
    gaussianBlur(b.v.data(), b.r.width(), b.r.height(), 1, float(radius) * 0.5f);
    writeSel(doc, b);
}

void borderSelection(Document& doc, int px) {
    if (!doc.hasSelection() || px <= 0) return;
    doc.pushUndo("Border selection");
    SelBuf b = readSel(doc, px + 2);
    std::vector<float> g = grownCoverage(b, px);
    std::vector<float> s = grownCoverage(b, -px);
    for (size_t i = 0; i < b.v.size(); ++i) b.v[i] = clamp01(g[i] - s[i]);
    writeSel(doc, b);
}

// ============================================================================ transforms

void transformSurface(RasterSurface& s, const QTransform& t, bool smooth, const QRect& srcBounds) {
    QRect R = srcBounds.isEmpty() ? s.tileBounds() : srcBounds;
    if (R.isEmpty()) return;
    QImage src = surfaceToImage(s, R);
    QRect dr = t.mapRect(QRectF(R)).toAlignedRect();
    if (dr.isEmpty() || src.isNull()) return;
    QImage dst(dr.size(), QImage::Format_ARGB32_Premultiplied);
    if (dst.isNull()) return;
    dst.fill(0);
    {
        QPainter p(&dst);
        p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
        p.setRenderHint(QPainter::Antialiasing, smooth);
        p.setTransform(t * QTransform::fromTranslate(-dr.left(), -dr.top()));
        p.drawImage(R.topLeft(), src);
    }
    if (srcBounds.isEmpty()) s.clear();
    else s.fillRect(R, 0);
    // merge (paint over what is left)
    RasterSurface tmp;
    imageToSurface(dst, tmp, dr.topLeft());
    for (auto it = tmp.tiles().cbegin(); it != tmp.tiles().cend(); ++it) {
        if (srcBounds.isEmpty() || !s.tile(keyTx(it.key()), keyTy(it.key()))) {
            s.setTile(it.key(), it.value());
            continue;
        }
        QRect tr = tileRectForKey(it.key());
        std::vector<float> a(size_t(TILE_PIXELS) * 4), b(size_t(TILE_PIXELS) * 4);
        readFloat(s, tr, a.data());
        readFloat(tmp, tr, b.data());
        blendRow(BlendMode::Normal, a.data(), b.data(), TILE_PIXELS);
        writeFloat(s, tr, a.data());
    }
    s.optimize();
}

void transformMask(MaskSurface& m, const QTransform& t, bool smooth) {
    const bool inv = m.defaultValue() == 255;
    QRect R = m.tileBounds();
    if (R.isEmpty()) return;
    QImage src = maskRegionToAlphaImage(m, R, inv);
    QRect dr = t.mapRect(QRectF(R)).toAlignedRect();
    QImage dst(dr.size(), QImage::Format_ARGB32_Premultiplied);
    if (dst.isNull() || src.isNull()) return;
    dst.fill(0);
    {
        QPainter p(&dst);
        p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
        p.setTransform(t * QTransform::fromTranslate(-dr.left(), -dr.top()));
        p.drawImage(R.topLeft(), src);
    }
    m.clear();
    alphaImageToMask(dst, m, dr.topLeft(), inv);
}

void transformLayerContent(Layer& l, const QTransform& t, bool smooth) {
    if (l.hasMask) transformMask(l.mask, t, smooth);
    switch (l.type()) {
    case LayerType::Raster: transformSurface(static_cast<RasterLayer&>(l).pixels, t, smooth); break;
    case LayerType::Vector: {
        auto& v = static_cast<VectorLayer&>(l);
        double sc = std::sqrt(std::fabs(t.determinant()));
        for (auto& s : v.strokes) {
            for (auto& p : s.points) p.pos = t.map(p.pos);
            s.width = float(s.width * sc);
        }
        v.cache.clear();
        renderVectorLayer(v);
        break;
    }
    case LayerType::Text: {
        auto& tl = static_cast<TextLayer&>(l);
        double sc = std::sqrt(std::fabs(t.determinant()));
        tl.text.origin = t.map(tl.text.origin);
        QPointF ax = t.map(QPointF(1, 0)) - t.map(QPointF(0, 0));
        tl.text.rotation += qRadiansToDegrees(std::atan2(ax.y(), ax.x()));
        tl.text.scale *= sc;
        renderTextLayer(tl);
        break;
    }
    case LayerType::Group:
        for (auto& c : static_cast<GroupLayer&>(l).children) transformLayerContent(*c, t, smooth);
        break;
    default: break;
    }
}

void applyToSelection(Document& doc, RasterSurface& target, const RasterSurface& original, const QRect& r) {
    if (!doc.hasSelection() || r.isEmpty()) return;
    const size_t n = size_t(r.width()) * r.height();
    std::vector<float> a(n * 4), b(n * 4), m(n);
    readFloat(original, r, a.data());
    readFloat(target, r, b.data());
    readMask(doc.selection(), r, m.data());
    for (size_t i = 0; i < n; ++i)
        for (int k = 0; k < 4; ++k) {
            float& av = a[i * 4 + size_t(k)];
            av += (b[i * 4 + size_t(k)] - av) * m[i];
        }
    writeFloat(target, r, a.data());
    target.optimizeRect(r);
}

// ============================================================================ canvas

namespace {

void forAllLayers(Document& doc, const std::function<void(Layer&)>& fn) {
    forEachLayer(doc.root(), [&](Layer* l) { fn(*l); return true; });
}

// Text layers cannot be mirrored and stay editable; they are rasterized first.
void rasterizeTextForMirror(Document& doc) {
    QList<Layer*> texts;
    forEachLayer(doc.root(), [&](Layer* l) {
        if (l->type() == LayerType::Text) texts << l;
        return true;
    });
    for (Layer* l : texts) replaceLayer(l, toRaster(l));
}

void transformDocument(Document& doc, const QTransform& t, const QSize& newSize, bool smooth) {
    forAllLayers(doc, [&](Layer& l) {
        if (l.isGroup()) {
            if (l.hasMask) transformMask(l.mask, t, smooth);
            return;  // children are visited separately
        }
        transformLayerContent(l, t, smooth);
    });
    if (doc.hasSelection()) transformMask(doc.selection(), t, smooth);
    for (auto& p : doc.perspective.vp) p = t.map(p);
    doc.symmetry.center = t.map(doc.symmetry.center);
    doc.setSizeNoUndo(newSize);
}

}  // namespace

void resizeCanvas(Document& doc, const QSize& size, const QPoint& offset) {
    if (size.isEmpty()) return;
    doc.pushUndo("Canvas size");
    forAllLayers(doc, [&](Layer& l) {
        if (l.hasMask) l.mask = l.mask.translated(offset.x(), offset.y());
        switch (l.type()) {
        case LayerType::Raster: {
            auto& r = static_cast<RasterLayer&>(l);
            r.pixels = r.pixels.translated(offset.x(), offset.y());
            break;
        }
        case LayerType::Vector: {
            auto& v = static_cast<VectorLayer&>(l);
            for (auto& s : v.strokes)
                for (auto& p : s.points) p.pos += offset;
            v.cache = v.cache.translated(offset.x(), offset.y());
            break;
        }
        case LayerType::Text: {
            auto& t = static_cast<TextLayer&>(l);
            t.text.origin += offset;
            t.cache = t.cache.translated(offset.x(), offset.y());
            break;
        }
        default: break;
        }
    });
    if (doc.hasSelection()) doc.selection() = doc.selection().translated(offset.x(), offset.y());
    for (auto& p : doc.perspective.vp) p += offset;
    doc.symmetry.center += offset;
    doc.setSizeNoUndo(size);
    doc.structureChanged();
}

void scaleImage(Document& doc, const QSize& size, bool smooth) {
    if (size.isEmpty() || size == doc.size()) return;
    doc.pushUndo("Image size");
    double sx = double(size.width()) / doc.size().width(), sy = double(size.height()) / doc.size().height();
    transformDocument(doc, QTransform::fromScale(sx, sy), size, smooth);
    doc.structureChanged();
}

void rotateCanvas(Document& doc, int q) {
    q = ((q % 4) + 4) % 4;
    if (q == 0) return;
    doc.pushUndo("Rotate canvas");
    const int W = doc.size().width(), H = doc.size().height();
    QTransform t;
    QSize ns(W, H);
    if (q == 1) { t = QTransform(0, 1, -1, 0, H, 0); ns = QSize(H, W); }
    else if (q == 2) { t = QTransform(-1, 0, 0, -1, W, H); }
    else { t = QTransform(0, -1, 1, 0, 0, W); ns = QSize(H, W); }
    transformDocument(doc, t, ns, false);
    doc.structureChanged();
}

void flipCanvas(Document& doc, bool horizontal) {
    doc.pushUndo(horizontal ? "Flip canvas horizontally" : "Flip canvas vertically");
    rasterizeTextForMirror(doc);
    const int W = doc.size().width(), H = doc.size().height();
    QTransform t = horizontal ? QTransform(-1, 0, 0, 1, W, 0) : QTransform(1, 0, 0, -1, 0, H);
    transformDocument(doc, t, doc.size(), false);
    doc.structureChanged();
}

void cropToSelection(Document& doc) {
    if (!doc.hasSelection()) return;
    QRect r = doc.selectionBounds();
    if (r.isEmpty()) return;
    resizeCanvas(doc, r.size(), -r.topLeft());
    doc.setHasSelection(false);
    doc.selectionModified();
}

void trimCanvas(Document& doc) {
    QRect b;
    forEachLayer(doc.root(), [&](Layer* l) {
        if (const RasterSurface* s = l->displaySurface())
            if (l->visible) b |= s->exactBounds();
        return true;
    });
    b &= doc.bounds();
    if (b.isEmpty() || b == doc.bounds()) return;
    resizeCanvas(doc, b.size(), -b.topLeft());
}

}  // namespace ops
}  // namespace gx
