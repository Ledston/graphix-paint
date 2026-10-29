#include "core/FocusLines.h"

#include <QJsonArray>
#include <QPainter>
#include <cmath>
#include <random>

#include "core/Document.h"
#include "core/ImageOps.h"

namespace gx {

QJsonObject FocusLines::toJson() const {
    return {{"type", "lines"},
            {"kind", kind},
            {"area", QJsonArray{area.x(), area.y(), area.width(), area.height()}},
            {"center", QJsonArray{center.x(), center.y()}},
            {"rx", rx},
            {"ry", ry},
            {"angle", angle},
            {"count", count},
            {"width", width},
            {"widthJitter", widthJitter},
            {"lengthJitter", lengthJitter},
            {"spread", spread},
            {"gaps", gaps},
            {"length", length},
            {"color", color.name(QColor::HexArgb)},
            {"seed", qint64(seed)}};
}

FocusLines FocusLines::fromJson(const QJsonObject& o) {
    FocusLines f;
    f.kind = std::clamp(o.value("kind").toInt(), 0, 1);
    const QJsonArray a = o.value("area").toArray();
    if (a.size() == 4) f.area = QRect(a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt());
    const QJsonArray c = o.value("center").toArray();
    if (c.size() == 2) f.center = QPointF(c[0].toDouble(), c[1].toDouble());
    f.rx = std::max(0.0, o.value("rx").toDouble(f.rx));
    f.ry = std::max(0.0, o.value("ry").toDouble(f.ry));
    f.angle = o.value("angle").toDouble();
    f.count = std::clamp(o.value("count").toInt(f.count), 2, 2000);
    f.width = std::clamp(o.value("width").toDouble(f.width), 0.2, 200.0);
    f.widthJitter = std::clamp(o.value("widthJitter").toDouble(f.widthJitter), 0.0, 1.0);
    f.lengthJitter = std::clamp(o.value("lengthJitter").toDouble(f.lengthJitter), 0.0, 1.0);
    f.spread = std::clamp(o.value("spread").toDouble(f.spread), 0.0, 1.0);
    f.gaps = std::clamp(o.value("gaps").toDouble(f.gaps), 0.0, 1.0);
    f.length = std::clamp(o.value("length").toDouble(f.length), 0.05, 1.0);
    f.color = QColor(o.value("color").toString("#ff000000"));
    f.seed = quint32(o.value("seed").toInteger(1));
    return f;
}

QVector<QPolygonF> FocusLines::shapes() const {
    QVector<QPolygonF> out;
    if (area.isEmpty() || count < 1) return out;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    auto rnd = [&] { return uni(rng); };
    // groups of lines are left out to make the gaps of hand-drawn focus lines
    int skip = 0;
    auto skipped = [&] {
        if (skip > 0) {
            --skip;
            return true;
        }
        if (rnd() < gaps * 0.12) {
            skip = int(rnd() * std::max(1, count / 25));
            return true;
        }
        return false;
    };
    const QRectF ar(area);
    if (kind == Focus) {
        double reach = 0;
        for (const QPointF& p : {ar.topLeft(), ar.topRight(), ar.bottomLeft(), ar.bottomRight()})
            reach = std::max(reach, std::hypot(p.x() - center.x(), p.y() - center.y()));
        reach += width * 4 + 4;
        const double rxe = std::max(1.0, rx), rye = std::max(1.0, ry);
        for (int k = 0; k < count; ++k) {
            const double th = 2 * M_PI * (k + spread * (rnd() - 0.5) * 1.8) / count;
            const double wj = rnd(), lj = rnd();  // drawn before skipping so a gap does not reshuffle the rest
            if (skipped()) continue;
            const QPointF u(std::cos(th), std::sin(th)), n(-u.y(), u.x());
            const double w = width * (1.0 - widthJitter * wj * 0.85);
            // where the line ends: on the empty ellipse, or further out for uneven ends
            const double f = 1.0 + lengthJitter * lj * lj * 1.6;
            const QPointF tip = center + QPointF(rxe * u.x(), rye * u.y()) * f;
            const QPointF far = center + u * reach;
            if (QLineF(center, tip).length() >= reach) continue;
            out << QPolygonF({far + n * (w / 2), tip, far - n * (w / 2)});
        }
    } else {
        const double an = angle * M_PI / 180;
        const QPointF d(std::cos(an), std::sin(an)), n(-d.y(), d.x()), C = ar.center();
        double dmin = 1e300, dmax = -1e300, nmin = 1e300, nmax = -1e300;
        for (const QPointF& p : {ar.topLeft(), ar.topRight(), ar.bottomLeft(), ar.bottomRight()}) {
            const QPointF v = p - C;
            const double dd = v.x() * d.x() + v.y() * d.y(), nn = v.x() * n.x() + v.y() * n.y();
            dmin = std::min(dmin, dd), dmax = std::max(dmax, dd);
            nmin = std::min(nmin, nn), nmax = std::max(nmax, nn);
        }
        const double span = dmax - dmin, step = (nmax - nmin) / count;
        for (int k = 0; k < count; ++k) {
            const double o = nmin + (k + 0.5 + spread * (rnd() - 0.5) * 1.6) * step;
            const double L = std::max(4.0, span * length * (1.0 - lengthJitter * rnd() * 0.8));
            const double s = dmin - L * 0.3 + rnd() * std::max(0.0, span - L * 0.4);
            const double w = width * (1.0 - widthJitter * rnd() * 0.85);
            if (skipped()) continue;
            // a long spindle, widest a third of the way from its back end
            const QPointF p0 = C + n * o + d * s, p1 = p0 + d * L, wide = p0 + d * (L * 0.35);
            out << QPolygonF({p0, wide + n * (w / 2), p1, wide - n * (w / 2)});
        }
    }
    return out;
}

void FocusLines::renderInto(Document& doc, RasterLayer& layer) const {
    QRect r = area & doc.bounds();
    if (r.isEmpty()) r = doc.bounds();
    QImage img(r.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(-r.topLeft());
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (const QPolygonF& poly : shapes()) p.drawPolygon(poly);
    }
    if (doc.hasSelection()) {
        std::vector<float> sel(size_t(r.width()) * r.height());
        readMask(doc.selection(), r, sel.data());
        for (int y = 0; y < img.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const float m = sel[size_t(y) * r.width() + x];
                if (m >= 1.f) continue;
                const QRgb c = row[x];
                row[x] = qRgba(int(qRed(c) * m + 0.5f), int(qGreen(c) * m + 0.5f), int(qBlue(c) * m + 0.5f), int(qAlpha(c) * m + 0.5f));
            }
        }
    }
    const QRect old = layer.pixels.exactBounds();
    layer.pixels = RasterSurface();
    imageToSurface(img, layer.pixels, r.topLeft());
    layer.pixels.optimizeRect(r);
    layer.generator = toJson();
    doc.pixelsChanged((old | r) & doc.bounds(), &layer);
    layer.generatorVersion = layer.version;
}

}  // namespace gx
