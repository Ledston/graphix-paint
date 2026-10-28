#include "core/Projection.h"

#include <QElapsedTimer>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <cstring>

#include "core/Compositor.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"

namespace gx {

Projection::Projection(Document* doc, bool recording) : m_doc(doc), m_recording(recording) { reset(); }

int Projection::maxLevel() const {
    QSize s = m_doc->size();
    int m = std::max(s.width(), s.height());
    int L = 0;
    while ((TILE << L) < m && L < 16) ++L;
    return L;
}

void Projection::reset() {
    m_levels.clear();
    m_levels.resize(size_t(maxLevel() + 1));
}

void Projection::invalidateAll() {
    for (auto& lv : m_levels)
        for (auto it = lv.begin(); it != lv.end(); ++it) it->dirty = true;
}

void Projection::invalidate(const QRect& r0) {
    QRect r = r0 & m_doc->bounds();
    if (r.isEmpty()) return;
    for (int L = 0; L < int(m_levels.size()); ++L) {
        auto& lv = m_levels[size_t(L)];
        if (lv.isEmpty()) continue;
        int sh = TILE_SHIFT + L;
        int x0 = r.left() >> sh, y0 = r.top() >> sh, x1 = r.right() >> sh, y1 = r.bottom() >> sh;
        qint64 count = qint64(x1 - x0 + 1) * (y1 - y0 + 1);
        if (count > lv.size()) {
            for (auto it = lv.begin(); it != lv.end(); ++it) {
                int tx = keyTx(it.key()), ty = keyTy(it.key());
                if (tx >= x0 && tx <= x1 && ty >= y0 && ty <= y1) it->dirty = true;
            }
        } else {
            for (int ty = y0; ty <= y1; ++ty)
                for (int tx = x0; tx <= x1; ++tx) {
                    auto it = lv.find(tileKey(tx, ty));
                    if (it != lv.end()) it->dirty = true;
                }
        }
    }
}

Projection::Entry& Projection::get(int level, qint64 key) { return m_levels[size_t(level)][key]; }

const Projection::Entry* Projection::entry(int level, qint64 key) const {
    if (level < 0 || level >= int(m_levels.size())) return nullptr;
    auto it = m_levels[size_t(level)].constFind(key);
    return it == m_levels[size_t(level)].cend() ? nullptr : &it.value();
}

void Projection::touch(int level, qint64 key) {
    if (level < 0 || level >= int(m_levels.size())) return;
    auto it = m_levels[size_t(level)].find(key);
    if (it != m_levels[size_t(level)].end()) it->lastUse = m_frame;
}

bool Projection::intersectsCanvas(int level, qint64 key) const {
    return levelTileRect(level, keyTx(key), keyTy(key)).intersects(m_doc->bounds());
}

void Projection::gatherLevel0(int level, qint64 key, QVector<qint64>& out, QSet<qint64>& seen, int limit) {
    if (out.size() >= limit) return;
    const Entry* e = entry(level, key);
    if (e && !e->dirty) return;
    if (!intersectsCanvas(level, key)) return;
    if (level == 0) {
        if (!seen.contains(key)) { seen.insert(key); out << key; }
        return;
    }
    int tx = keyTx(key), ty = keyTy(key);
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) gatherLevel0(level - 1, tileKey(tx * 2 + i, ty * 2 + j), out, seen, limit);
}

namespace {

bool surfaceUniformAt(const RasterSurface* s, const QRect& r, const QPoint& offset) {
    if (!s || s->isEmpty()) return true;
    if ((offset.x() & (TILE - 1)) || (offset.y() & (TILE - 1))) {
        QRect src = r.translated(-offset);
        TileRange tr = tileRangeFor(src);
        for (int ty = tr.y0; ty <= tr.y1; ++ty)
            for (int tx = tr.x0; tx <= tr.x1; ++tx)
                if (s->tile(tx, ty)) return false;
        return true;
    }
    QRect src = r.translated(-offset);
    const auto* t = s->tile(tileCoord(src.left()), tileCoord(src.top()));
    return !t || !t->px;
}

bool maskUniformAt(const MaskSurface* m, const QRect& r, const QPoint& offset) {
    if (!m || m->isEmpty()) return true;
    if ((offset.x() & (TILE - 1)) || (offset.y() & (TILE - 1))) return false;
    QRect src = r.translated(-offset);
    const auto* t = m->tile(tileCoord(src.left()), tileCoord(src.top()));
    return !t || !t->px;
}

bool regionUniform(const GroupLayer* g, const QRect& r) {
    for (const auto& c : g->children) {
        const Layer* l = c.get();
        if (!l->visible) continue;
        if (!maskUniformAt(l->displayMask(), r, l->previewOffset)) return false;
        if (l->isGroup()) {
            if (!regionUniform(static_cast<const GroupLayer*>(l), r)) return false;
            continue;
        }
        if (l->type() == LayerType::Adjustment) continue;
        const RasterSurface* s = (l->fx.anyActive() && l->fxCache) ? &l->fxCache->tiles : l->displaySurface();
        if (l->fx.anyActive() && !l->fxCache) return false;
        if (!surfaceUniformAt(s, r, l->previewOffset)) return false;
    }
    return true;
}

}  // namespace

void Projection::computeLevel0(const QVector<qint64>& keys) {
    if (keys.isEmpty()) return;
    GroupLayer* root = m_doc->root();
    const QRect canvas = m_doc->bounds();
    QRect u;
    for (qint64 k : keys) u |= tileRectForKey(k) & canvas;
    Compositor::prepare(root, u);
    auto& lv = m_levels[0];
    auto store = [&](qint64 k, RasterSurface::Ptr t) {
        Entry& e = lv[k];
        e.tile = std::move(t);
        e.version = ++m_version;
        e.dirty = false;
        e.lastUse = m_frame;
    };
    // Flat regions (backgrounds, empty areas) are resolved right here; that keeps
    // huge, mostly empty canvases fast.
    QVector<qint64> heavy;
    for (qint64 k : keys) {
        QRect tr = tileRectForKey(k);
        if (canvas.contains(tr) && regionUniform(root, tr)) {
            float px[4];
            CompositeOptions opt;
            opt.recording = m_recording;
            Compositor::samplePixel(root, tr.left(), tr.top(), px, opt);
            quint32 v = pack(px);
            RasterSurface::Ptr t;
            if (v) {
                t = std::make_shared<Tile<quint32>>();
                t->uniform = v;
            }
            store(k, t);
        } else {
            heavy << k;
        }
    }
    if (heavy.isEmpty()) return;
    std::vector<RasterSurface::Ptr> results(size_t(heavy.size()));
    parallelFor(int(heavy.size()), [&](int i) {
        QRect tr = tileRectForKey(heavy[i]);
        QRect cr = tr & canvas;
        std::vector<float> buf(size_t(cr.width()) * cr.height() * 4);
        CompositeOptions opt;
        opt.recording = m_recording;
        Compositor::composite(root, cr, buf.data(), opt);
        auto t = std::make_shared<Tile<quint32>>();
        t->px.reset(new quint32[TILE_PIXELS]);
        if (cr != tr) std::memset(t->px.get(), 0, sizeof(quint32) * TILE_PIXELS);
        quint32 orAll = 0, first = 0;
        bool flat = (cr == tr);
        for (int y = 0; y < cr.height(); ++y) {
            quint32* row = t->px.get() + (cr.top() - tr.top() + y) * TILE + (cr.left() - tr.left());
            const float* src = &buf[size_t(y) * cr.width() * 4];
            for (int x = 0; x < cr.width(); ++x) {
                quint32 v = pack(src + x * 4);
                row[x] = v;
                orAll |= v;
                if (y == 0 && x == 0) first = v;
                else if (flat && v != first) flat = false;
            }
        }
        if (orAll == 0) return;  // transparent
        if (flat) { t->px.reset(); t->uniform = first; }
        results[size_t(i)] = t;
    });
    for (int i = 0; i < heavy.size(); ++i) store(heavy[i], results[size_t(i)]);
}

bool Projection::computeUp(int level, qint64 key) {
    const Entry* e = entry(level, key);
    if (e && !e->dirty) return true;
    if (!intersectsCanvas(level, key)) return true;
    if (level == 0) return false;
    int tx = keyTx(key), ty = keyTy(key);
    bool ok = true;
    const Entry* ch[4] = {};
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            qint64 ck = tileKey(tx * 2 + i, ty * 2 + j);
            if (!computeUp(level - 1, ck)) ok = false;
        }
    if (!ok) return false;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) ch[j * 2 + i] = entry(level - 1, tileKey(tx * 2 + i, ty * 2 + j));

    // Uniform fast path
    bool allUniform = true;
    quint32 uv = 0;
    for (int q = 0; q < 4; ++q) {
        const Entry* c = ch[q];
        quint32 v;
        if (!c || !c->tile) v = 0;
        else if (!c->tile->px) v = c->tile->uniform;
        else { allUniform = false; break; }
        if (q == 0) uv = v;
        else if (v != uv) { allUniform = false; break; }
    }
    // Edge tiles that hang over the canvas are never uniform (outside is transparent)
    QRect lr = levelTileRect(level, tx, ty);
    if (!m_doc->bounds().contains(lr)) allUniform = allUniform && uv == 0;

    RasterSurface::Ptr out;
    if (allUniform) {
        if (uv != 0) {
            out = std::make_shared<Tile<quint32>>();
            out->uniform = uv;
        }
    } else {
        out = std::make_shared<Tile<quint32>>();
        out->px.reset(new quint32[TILE_PIXELS]);
        const int H = TILE / 2;
        for (int q = 0; q < 4; ++q) {
            const Entry* c = ch[q];
            int ox = (q & 1) * H, oy = (q >> 1) * H;
            for (int y = 0; y < H; ++y) {
                quint32* row = out->px.get() + (oy + y) * TILE + ox;
                if (!c || !c->tile) { std::memset(row, 0, sizeof(quint32) * H); continue; }
                const auto& t = *c->tile;
                for (int x = 0; x < H; ++x) {
                    quint32 a = t.at((2 * y) * TILE + 2 * x), b = t.at((2 * y) * TILE + 2 * x + 1);
                    quint32 cc = t.at((2 * y + 1) * TILE + 2 * x), d = t.at((2 * y + 1) * TILE + 2 * x + 1);
                    quint32 v = 0;
                    for (int s = 0; s < 32; s += 8) {
                        quint32 sum = ((a >> s) & 255u) + ((b >> s) & 255u) + ((cc >> s) & 255u) + ((d >> s) & 255u);
                        v |= ((sum + 2) >> 2) << s;
                    }
                    row[x] = v;
                }
            }
        }
    }
    Entry& ne = get(level, key);
    ne.tile = out;
    ne.version = ++m_version;
    ne.dirty = false;
    ne.lastUse = m_frame;
    return true;
}

bool Projection::update(int level, const QVector<qint64>& keys, double budgetMs) {
    if (m_levels.size() != size_t(maxLevel() + 1)) reset();
    level = std::clamp(level, 0, int(m_levels.size()) - 1);
    QElapsedTimer timer;
    timer.start();
    const int batch = std::max(64, QThread::idealThreadCount() * 16);
    const int limit = 16384;
    QVector<qint64> need;
    QSet<qint64> seen;
    for (qint64 k : keys) gatherLevel0(level, k, need, seen, limit);
    bool complete = true;
    for (int i = 0; i < need.size(); i += batch) {
        computeLevel0(need.mid(i, batch));
        if (timer.nsecsElapsed() / 1e6 > budgetMs && i + batch < need.size()) { complete = false; break; }
    }
    if (need.size() >= limit) complete = false;
    for (qint64 k : keys)
        if (!computeUp(level, k)) complete = false;
    return complete;
}

void Projection::trim(int maxLevel0Tiles) {
    for (size_t L = 0; L < m_levels.size(); ++L) {
        auto& lv = m_levels[L];
        int limit = std::max(64, maxLevel0Tiles >> (2 * L));
        if (lv.size() <= limit) continue;
        std::vector<std::pair<quint32, qint64>> order;
        order.reserve(size_t(lv.size()));
        for (auto it = lv.cbegin(); it != lv.cend(); ++it) order.push_back({it->lastUse, it.key()});
        std::sort(order.begin(), order.end());
        size_t remove = size_t(lv.size() - limit * 4 / 5);
        for (size_t i = 0; i < remove && i < order.size(); ++i) {
            if (order[i].first == m_frame) break;  // never drop what is on screen right now
            lv.remove(order[i].second);
        }
    }
}

bool Projection::thumbnail(int maxSize, double budgetMs, QImage& out) {
    QSize s = m_doc->size();
    int L = 0;
    while (L < maxLevel() && std::max(s.width(), s.height()) / (1 << L) > maxSize) ++L;
    int w = std::max(1, (s.width() + (1 << L) - 1) >> L), h = std::max(1, (s.height() + (1 << L) - 1) >> L);
    int tx1 = (w - 1) / TILE, ty1 = (h - 1) / TILE;
    QVector<qint64> keys;
    for (int ty = 0; ty <= ty1; ++ty)
        for (int tx = 0; tx <= tx1; ++tx) keys << tileKey(tx, ty);
    bool done = update(L, keys, budgetMs);
    if (out.size() != QSize(w, h) || out.format() != QImage::Format_ARGB32_Premultiplied)
        out = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(0);
    for (qint64 k : keys) {
        const Entry* e = entry(L, k);
        if (!e || !e->tile) continue;
        int ox = keyTx(k) * TILE, oy = keyTy(k) * TILE;
        for (int y = 0; y < TILE && oy + y < h; ++y) {
            quint32* row = reinterpret_cast<quint32*>(out.scanLine(oy + y));
            for (int x = 0; x < TILE && ox + x < w; ++x) row[ox + x] = e->tile->at(y * TILE + x);
        }
    }
    return done;
}

}  // namespace gx
