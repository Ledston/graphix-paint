#include "vector/VectorRender.h"

#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

#include "brush/Dab.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"

namespace gx {

namespace {

QPointF catmull(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, double t) {
    double t2 = t * t, t3 = t2 * t;
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}

double segDist(const QPointF& p, const QPointF& a, const QPointF& b) {
    QPointF ab = b - a;
    double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = len2 > 0 ? std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2, 0.0, 1.0) : 0.0;
    QPointF q = a + ab * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

void rdp(const QVector<VectorPoint>& pts, int a, int b, double tol, QVector<bool>& keep) {
    if (b <= a + 1) return;
    double best = -1;
    int idx = -1;
    for (int i = a + 1; i < b; ++i) {
        double d = segDist(pts[i].pos, pts[a].pos, pts[b].pos);
        // keep points where pressure changes a lot too
        double pd = std::fabs(pts[i].pressure - (pts[a].pressure + pts[b].pressure) * 0.5) * 20.0;
        d = std::max(d, pd);
        if (d > best) { best = d; idx = i; }
    }
    if (best > tol && idx > 0) {
        keep[idx] = true;
        rdp(pts, a, idx, tol, keep);
        rdp(pts, idx, b, tol, keep);
    }
}

}  // namespace

QVector<VectorPoint> sampleStroke(const VectorStroke& s, double step) {
    QVector<VectorPoint> out;
    const auto& p = s.points;
    if (p.isEmpty()) return out;
    if (p.size() == 1) { out << p[0]; return out; }
    for (int i = 0; i + 1 < p.size(); ++i) {
        QPointF p0 = p[std::max(0, i - 1)].pos, p1 = p[i].pos, p2 = p[i + 1].pos, p3 = p[std::min(int(p.size()) - 1, i + 2)].pos;
        double len = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
        int n = std::max(1, int(std::ceil(len / std::max(0.2, step))));
        for (int k = 0; k < n; ++k) {
            double t = double(k) / n;
            VectorPoint v;
            v.pos = catmull(p0, p1, p2, p3, t);
            v.pressure = float(p[i].pressure + (p[i + 1].pressure - p[i].pressure) * t);
            out << v;
        }
    }
    out << p.back();
    return out;
}

QVector<VectorPoint> simplifyPoints(const QVector<VectorPoint>& pts, double tol) {
    if (pts.size() <= 2) return pts;
    QVector<bool> keep(pts.size(), false);
    keep[0] = keep[pts.size() - 1] = true;
    rdp(pts, 0, int(pts.size()) - 1, tol, keep);
    QVector<VectorPoint> out;
    for (int i = 0; i < pts.size(); ++i)
        if (keep[i]) out << pts[i];
    return out;
}

QRect renderVectorLayer(VectorLayer& l, QRect rect) {
    if (rect.isEmpty()) {
        rect = l.cache.tileBounds();
        for (const auto& s : l.strokes) rect |= s.bounds().toAlignedRect();
    }
    if (rect.isEmpty()) {
        l.cache.clear();
        return {};
    }
    const int W = rect.width(), H = rect.height();
    std::vector<float> acc(size_t(W) * H * 4, 0.f);
    std::vector<float> cov, dab, src;
    for (const auto& s : l.strokes) {
        QRect sb = s.bounds().toAlignedRect() & rect;
        if (sb.isEmpty()) continue;
        const int w = sb.width(), h = sb.height();
        cov.assign(size_t(w) * h, 0.f);
        double step = std::max(0.3, s.width * 0.08);
        QVector<VectorPoint> pts = sampleStroke(s, step);
        for (const auto& vp : pts) {
            DabParams d;
            d.cx = vp.pos.x();
            d.cy = vp.pos.y();
            d.diameter = s.width * (s.minWidth + (1.0 - s.minWidth) * vp.pressure);
            d.hardness = s.hardness;
            if (d.diameter < 0.05) continue;
            QRect db = dabBounds(d) & sb;
            if (db.isEmpty()) continue;
            dab.resize(size_t(db.width()) * db.height());
            if (!rasterizeDab(d, db, dab.data())) continue;
            for (int y = 0; y < db.height(); ++y)
                for (int x = 0; x < db.width(); ++x) {
                    float v = dab[size_t(y) * db.width() + x];
                    float& c = cov[size_t(y + db.top() - sb.top()) * w + size_t(x + db.left() - sb.left())];
                    if (v > c) c = v;
                }
        }
        float col[3] = {float(s.color.redF()), float(s.color.greenF()), float(s.color.blueF())};
        float op = clamp01(s.opacity);
        src.assign(size_t(w) * 4, 0.f);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float m = cov[size_t(y) * w + x] * op;
                float* q = &src[size_t(x) * 4];
                q[0] = col[0] * m; q[1] = col[1] * m; q[2] = col[2] * m; q[3] = m;
            }
            float* dst = &acc[(size_t(y + sb.top() - rect.top()) * W + size_t(sb.left() - rect.left())) * 4];
            blendRow(BlendMode::Normal, dst, src.data(), w);
        }
    }
    writeFloat(l.cache, rect, acc.data());
    l.cache.optimizeRect(rect);
    return rect;
}

int hitTestStroke(const VectorLayer& l, const QPointF& p, double tol) {
    for (int i = int(l.strokes.size()) - 1; i >= 0; --i) {
        const auto& s = l.strokes[i];
        if (!s.bounds().adjusted(-tol, -tol, tol, tol).contains(p)) continue;
        QVector<VectorPoint> pts = sampleStroke(s, 2.0);
        for (int k = 0; k + 1 < pts.size(); ++k) {
            double r = s.width * 0.5 * (s.minWidth + (1.0 - s.minWidth) * pts[k].pressure);
            if (segDist(p, pts[k].pos, pts[k + 1].pos) <= r + tol) return i;
        }
        if (pts.size() == 1 && std::hypot(p.x() - pts[0].pos.x(), p.y() - pts[0].pos.y()) <= s.width * 0.5 + tol) return i;
    }
    return -1;
}

int hitTestControlPoint(const VectorLayer& l, const QPointF& p, double tol, int* pointIndex) {
    int best = -1;
    double bestD = tol;
    for (int i = int(l.strokes.size()) - 1; i >= 0; --i) {
        const auto& s = l.strokes[i];
        for (int k = 0; k < s.points.size(); ++k) {
            double d = std::hypot(p.x() - s.points[k].pos.x(), p.y() - s.points[k].pos.y());
            if (d <= bestD) {
                bestD = d;
                best = i;
                if (pointIndex) *pointIndex = k;
            }
        }
    }
    return best;
}

// ----------------------------------------------------------------------------- text

namespace {

struct TextLine {
    QString text;
    double width = 0;
};

QTransform textTransform(const TextParams& t) {
    QTransform m;
    m.translate(t.origin.x(), t.origin.y());
    m.rotate(t.rotation);
    m.scale(t.scale, t.scale);
    return m;
}

QSizeF layoutSize(const TextParams& t, QList<TextLine>& lines, double& lineH, double& ascent) {
    QFont f = t.font();
    QFontMetricsF fm(f);
    lineH = fm.height() * std::max(0.1, t.lineSpacing);
    ascent = fm.ascent();
    lines.clear();
    double maxW = 0;
    for (const QString& s : t.text.split('\n')) {
        TextLine l;
        l.text = s;
        l.width = t.vertical ? fm.height() : fm.horizontalAdvance(s);
        maxW = std::max(maxW, l.width);
        lines << l;
    }
    if (t.vertical) {
        int maxChars = 0;
        for (const auto& l : lines) maxChars = std::max(maxChars, int(l.text.size()));
        return QSizeF(lines.size() * lineH, maxChars * fm.height());
    }
    return QSizeF(maxW, lines.size() * lineH);
}

}  // namespace

QRectF textBounds(const TextParams& t) {
    QList<TextLine> lines;
    double lh, asc;
    QSizeF sz = layoutSize(t, lines, lh, asc);
    QRectF local(QPointF(0, 0), sz);
    return textTransform(t).mapRect(local.adjusted(-4, -4, 4, 4));
}

QRect renderTextLayer(TextLayer& l) {
    QRect old = l.cache.tileBounds();
    l.cache.clear();
    const TextParams& t = l.text;
    QList<TextLine> lines;
    double lineH, ascent;
    QSizeF sz = layoutSize(t, lines, lineH, ascent);
    QRect r = textBounds(t).toAlignedRect();
    if (r.isEmpty() || t.text.isEmpty()) return old;
    if (qint64(r.width()) * r.height() > 400ll * 1000 * 1000) return old;  // refuse absurd sizes
    QImage img(r.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        p.translate(-r.topLeft());
        p.setTransform(textTransform(t), true);
        QFont f = t.font();
        p.setFont(f);
        p.setPen(t.color);
        QFontMetricsF fm(f);
        if (t.vertical) {
            for (int li = 0; li < lines.size(); ++li) {
                double x = sz.width() - (li + 1) * lineH + (lineH - fm.height()) * 0.5;
                double y = 0;
                for (const QChar& ch : lines[li].text) {
                    QString s(ch);
                    double cw = fm.horizontalAdvance(s);
                    p.drawText(QPointF(x + (fm.height() - cw) * 0.5, y + fm.ascent()), s);
                    y += fm.height();
                }
            }
        } else {
            for (int li = 0; li < lines.size(); ++li) {
                double x = 0;
                if (t.align == 1) x = (sz.width() - lines[li].width) * 0.5;
                else if (t.align == 2) x = sz.width() - lines[li].width;
                p.drawText(QPointF(x, li * lineH + ascent), lines[li].text);
            }
        }
    }
    imageToSurface(img, l.cache, r.topLeft());
    return old | r;
}

}  // namespace gx
