#pragma once
// Sparse tiled pixel storage.
//
// Every layer, mask, selection and stroke buffer is a TileGrid. Only tiles that hold
// something are allocated; tiles that are a single flat value (for example a white
// background) are stored as "uniform" tiles that keep one value instead of TILE*TILE.
// Tiles are reference counted and copied on write, which makes undo snapshots,
// autosave snapshots and previews almost free: they share every tile that was not
// touched since.

#include <QHash>
#include <QRect>
#include <QSet>
#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <vector>

namespace gx {

constexpr int TILE_SHIFT = 7;
constexpr int TILE = 1 << TILE_SHIFT;  // 128
constexpr int TILE_PIXELS = TILE * TILE;

inline int tileCoord(int v) { return v >> TILE_SHIFT; }  // floor division for negatives too
inline qint64 tileKey(int tx, int ty) { return (qint64(ty) << 32) | qint64(quint32(tx)); }
inline int keyTx(qint64 k) { return int(qint32(quint32(quint64(k) & 0xffffffffu))); }
inline int keyTy(qint64 k) { return int(k >> 32); }
inline QRect tileRect(int tx, int ty) { return QRect(tx * TILE, ty * TILE, TILE, TILE); }
inline QRect tileRectForKey(qint64 k) { return tileRect(keyTx(k), keyTy(k)); }

// Inclusive tile-coordinate range covered by a pixel rect.
struct TileRange {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    bool isEmpty() const { return x1 < x0 || y1 < y0; }
    qint64 count() const { return isEmpty() ? 0 : qint64(x1 - x0 + 1) * qint64(y1 - y0 + 1); }
};
inline TileRange tileRangeFor(const QRect& r) {
    if (r.isEmpty()) return {};
    return {tileCoord(r.left()), tileCoord(r.top()), tileCoord(r.right()), tileCoord(r.bottom())};
}

template <class T>
struct Tile {
    std::unique_ptr<T[]> px;  // null when uniform
    T uniform{};
    bool isUniform() const { return !px; }
    T at(int i) const { return px ? px[i] : uniform; }
};

template <class T>
class TileGrid {
public:
    using TileT = Tile<T>;
    using Ptr = std::shared_ptr<TileT>;

    explicit TileGrid(T def = T{}) : m_default(def) {}

    T defaultValue() const { return m_default; }
    const QHash<qint64, Ptr>& tiles() const { return m_tiles; }
    bool isEmpty() const { return m_tiles.isEmpty(); }
    int tileCount() const { return int(m_tiles.size()); }

    const TileT* tile(int tx, int ty) const {
        auto it = m_tiles.constFind(tileKey(tx, ty));
        return it == m_tiles.cend() ? nullptr : it->get();
    }
    Ptr tilePtr(qint64 key) const { return m_tiles.value(key); }
    void setTile(qint64 key, Ptr p) {
        if (p) m_tiles.insert(key, std::move(p));
        else m_tiles.remove(key);
    }
    void removeTile(qint64 key) { m_tiles.remove(key); }

    // Pixel buffer of a tile that may be written to. Detaches shared tiles,
    // materializes uniform tiles and creates missing ones.
    T* writable(int tx, int ty) {
        Ptr& p = m_tiles[tileKey(tx, ty)];
        if (!p) {
            p = std::make_shared<TileT>();
            p->px.reset(new T[TILE_PIXELS]);
            std::fill_n(p->px.get(), TILE_PIXELS, m_default);
        } else if (p.use_count() > 1) {
            auto np = std::make_shared<TileT>();
            np->px.reset(new T[TILE_PIXELS]);
            if (p->px) std::memcpy(np->px.get(), p->px.get(), sizeof(T) * TILE_PIXELS);
            else std::fill_n(np->px.get(), TILE_PIXELS, p->uniform);
            p = std::move(np);
        } else if (!p->px) {
            p->px.reset(new T[TILE_PIXELS]);
            std::fill_n(p->px.get(), TILE_PIXELS, p->uniform);
        }
        return p->px.get();
    }

    T pixel(int x, int y) const {
        const TileT* t = tile(tileCoord(x), tileCoord(y));
        if (!t) return m_default;
        return t->at(((y & (TILE - 1)) << TILE_SHIFT) | (x & (TILE - 1)));
    }
    void setPixel(int x, int y, T v) {
        T* p = writable(tileCoord(x), tileCoord(y));
        p[((y & (TILE - 1)) << TILE_SHIFT) | (x & (TILE - 1))] = v;
    }

    // Copies a rect into a caller buffer (stride in elements).
    void readRect(const QRect& r, T* dst, int stride) const {
        if (r.isEmpty()) return;
        TileRange tr = tileRangeFor(r);
        for (int ty = tr.y0; ty <= tr.y1; ++ty)
            for (int tx = tr.x0; tx <= tr.x1; ++tx) {
                QRect ir = tileRect(tx, ty) & r;
                const TileT* t = tile(tx, ty);
                for (int y = ir.top(); y <= ir.bottom(); ++y) {
                    T* d = dst + qint64(y - r.top()) * stride + (ir.left() - r.left());
                    if (!t) std::fill_n(d, ir.width(), m_default);
                    else if (!t->px) std::fill_n(d, ir.width(), t->uniform);
                    else
                        std::memcpy(d, t->px.get() + ((y & (TILE - 1)) << TILE_SHIFT) + (ir.left() & (TILE - 1)),
                                    sizeof(T) * ir.width());
                }
            }
    }

    void writeRect(const QRect& r, const T* src, int stride) {
        if (r.isEmpty()) return;
        TileRange tr = tileRangeFor(r);
        for (int ty = tr.y0; ty <= tr.y1; ++ty)
            for (int tx = tr.x0; tx <= tr.x1; ++tx) {
                QRect ir = tileRect(tx, ty) & r;
                T* p = writable(tx, ty);
                for (int y = ir.top(); y <= ir.bottom(); ++y)
                    std::memcpy(p + ((y & (TILE - 1)) << TILE_SHIFT) + (ir.left() & (TILE - 1)),
                                src + qint64(y - r.top()) * stride + (ir.left() - r.left()), sizeof(T) * ir.width());
            }
    }

    void fillRect(const QRect& r, T v) {
        if (r.isEmpty()) return;
        TileRange tr = tileRangeFor(r);
        for (int ty = tr.y0; ty <= tr.y1; ++ty)
            for (int tx = tr.x0; tx <= tr.x1; ++tx) {
                QRect tre = tileRect(tx, ty);
                QRect ir = tre & r;
                if (ir == tre) {
                    if (v == m_default) m_tiles.remove(tileKey(tx, ty));
                    else {
                        auto u = std::make_shared<TileT>();
                        u->uniform = v;
                        m_tiles.insert(tileKey(tx, ty), u);
                    }
                    continue;
                }
                T* p = writable(tx, ty);
                for (int y = ir.top(); y <= ir.bottom(); ++y)
                    std::fill_n(p + ((y & (TILE - 1)) << TILE_SHIFT) + (ir.left() & (TILE - 1)), ir.width(), v);
            }
    }

    void clear() { m_tiles.clear(); }

    // Turns flat tiles into uniform ones and drops tiles equal to the default value.
    void optimize() { optimizeKeys(m_tiles.keys()); }
    void optimizeRect(const QRect& r) {
        QList<qint64> keys;
        TileRange tr = tileRangeFor(r);
        if (tr.count() > m_tiles.size()) keys = m_tiles.keys();
        else
            for (int ty = tr.y0; ty <= tr.y1; ++ty)
                for (int tx = tr.x0; tx <= tr.x1; ++tx) keys.push_back(tileKey(tx, ty));
        optimizeKeys(keys);
    }

    // Tile-aligned bounds of everything that is not default.
    QRect tileBounds() const {
        QRect b;
        for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) b |= tileRectForKey(it.key());
        return b;
    }

    // Exact bounds of pixels that differ from the default value.
    QRect exactBounds() const {
        int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
        for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) {
            const TileT* t = it->get();
            QRect tr = tileRectForKey(it.key());
            if (!t->px) {
                if (t->uniform == m_default) continue;
                x0 = std::min(x0, tr.left()); y0 = std::min(y0, tr.top());
                x1 = std::max(x1, tr.right()); y1 = std::max(y1, tr.bottom());
                continue;
            }
            // Skip tiles that cannot extend the current box.
            if (tr.left() >= x0 && tr.right() <= x1 && tr.top() >= y0 && tr.bottom() <= y1) continue;
            for (int y = 0; y < TILE; ++y) {
                const T* row = t->px.get() + y * TILE;
                int first = -1, last = -1;
                for (int x = 0; x < TILE; ++x)
                    if (!(row[x] == m_default)) { if (first < 0) first = x; last = x; }
                if (first < 0) continue;
                x0 = std::min(x0, tr.left() + first); x1 = std::max(x1, tr.left() + last);
                y0 = std::min(y0, tr.top() + y); y1 = std::max(y1, tr.top() + y);
            }
        }
        if (x0 > x1) return {};
        return QRect(QPoint(x0, y0), QPoint(x1, y1));
    }

    // New grid with the content shifted by an integer offset.
    TileGrid translated(int dx, int dy) const {
        TileGrid out(m_default);
        if (dx == 0 && dy == 0) { out.m_tiles = m_tiles; return out; }
        if ((dx & (TILE - 1)) == 0 && (dy & (TILE - 1)) == 0) {
            int tdx = dx >> TILE_SHIFT, tdy = dy >> TILE_SHIFT;
            for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it)
                out.m_tiles.insert(tileKey(keyTx(it.key()) + tdx, keyTy(it.key()) + tdy), it.value());
            return out;
        }
        std::vector<T> buf(TILE_PIXELS);
        for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) {
            QRect src = tileRectForKey(it.key());
            const TileT* t = it->get();
            for (int i = 0; i < TILE_PIXELS; ++i) buf[i] = t->at(i);
            out.writeRectBlend(src.translated(dx, dy), buf.data(), TILE);
        }
        out.optimize();
        return out;
    }

    qint64 memoryBytes() const {
        qint64 n = 0;
        for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it)
            n += it.value()->px ? qint64(sizeof(T)) * TILE_PIXELS + 64 : 64;
        return n;
    }

    bool sharesAllTilesWith(const TileGrid& o) const {
        if (m_tiles.size() != o.m_tiles.size()) return false;
        for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it)
            if (o.m_tiles.value(it.key()) != it.value()) return false;
        return true;
    }

private:
    // Writes only elements that differ from default (used by translated()).
    void writeRectBlend(const QRect& r, const T* src, int stride) {
        TileRange tr = tileRangeFor(r);
        for (int ty = tr.y0; ty <= tr.y1; ++ty)
            for (int tx = tr.x0; tx <= tr.x1; ++tx) {
                QRect ir = tileRect(tx, ty) & r;
                bool any = false;
                for (int y = ir.top(); y <= ir.bottom() && !any; ++y) {
                    const T* s = src + qint64(y - r.top()) * stride + (ir.left() - r.left());
                    for (int x = 0; x < ir.width(); ++x)
                        if (!(s[x] == m_default)) { any = true; break; }
                }
                if (!any) continue;
                T* p = writable(tx, ty);
                for (int y = ir.top(); y <= ir.bottom(); ++y)
                    std::memcpy(p + ((y & (TILE - 1)) << TILE_SHIFT) + (ir.left() & (TILE - 1)),
                                src + qint64(y - r.top()) * stride + (ir.left() - r.left()), sizeof(T) * ir.width());
            }
    }

    void optimizeKeys(const QList<qint64>& keys) {
        for (qint64 k : keys) {
            auto it = m_tiles.find(k);
            if (it == m_tiles.end()) continue;
            Ptr& p = it.value();
            if (!p->px) {
                if (p->uniform == m_default) m_tiles.erase(it);
                continue;
            }
            const T* d = p->px.get();
            T v = d[0];
            bool flat = true;
            for (int i = 1; i < TILE_PIXELS; ++i)
                if (!(d[i] == v)) { flat = false; break; }
            if (!flat) continue;
            if (v == m_default) { m_tiles.erase(it); continue; }
            if (p.use_count() > 1) {
                auto u = std::make_shared<TileT>();
                u->uniform = v;
                p = u;
            } else {
                p->uniform = v;
                p->px.reset();
            }
        }
    }

    QHash<qint64, Ptr> m_tiles;
    T m_default;
};

// Layer pixels: 8-bit premultiplied ARGB, same bit layout as QImage::Format_ARGB32_Premultiplied.
using RasterSurface = TileGrid<quint32>;
// Masks and selections: 0 = nothing, 255 = full.
using MaskSurface = TileGrid<quint8>;
// Stroke coverage buffers.
using FloatSurface = TileGrid<float>;

}  // namespace gx
