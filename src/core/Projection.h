#pragma once
#include <QHash>
#include <QImage>
#include <QRect>
#include <QSize>
#include <vector>

#include "core/Tile.h"

namespace gx {

class Document;

// The flattened image of a document, kept as a sparse pyramid of tiles.
// Level 0 is full resolution; level k tiles cover (TILE << k) canvas pixels.
// Tiles are computed lazily, only for what views actually show, with a time
// budget per frame so huge canvases stay responsive.
class Projection {
public:
    struct Entry {
        RasterSurface::Ptr tile;  // null = fully transparent
        quint32 version = 0;      // changes whenever the content changes
        bool dirty = true;
        quint32 lastUse = 0;
    };

    explicit Projection(Document* doc);

    void invalidate(const QRect& canvasRect);
    void invalidateAll();
    void reset();  // drops everything (canvas size changed)

    int maxLevel() const;
    static QRect levelTileRect(int level, int tx, int ty) {  // in canvas pixels
        int s = TILE << level;
        return QRect(tx * s, ty * s, s, s);
    }
    // Computes missing or dirty tiles of 'level' (most important first) within budget.
    // Returns true when everything requested is up to date.
    bool update(int level, const QVector<qint64>& keys, double budgetMs);
    const Entry* entry(int level, qint64 key) const;
    void touch(int level, qint64 key);

    void nextFrame() { ++m_frame; }
    void trim(int maxLevel0Tiles);

    // Small image of the whole canvas (for the navigator). May be incomplete
    // while tiles are still being computed; returns true when complete.
    bool thumbnail(int maxSize, double budgetMs, QImage& out);

private:
    Entry& get(int level, qint64 key);
    bool intersectsCanvas(int level, qint64 key) const;
    void gatherLevel0(int level, qint64 key, QVector<qint64>& out, QSet<qint64>& seen, int limit);
    bool computeUp(int level, qint64 key);
    void computeLevel0(const QVector<qint64>& keys);

    Document* m_doc;
    std::vector<QHash<qint64, Entry>> m_levels;
    quint32 m_version = 1;
    quint32 m_frame = 1;
};

}  // namespace gx
