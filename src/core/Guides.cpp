#include "core/Guides.h"

#include <QColor>
#include <QJsonArray>
#include <QSize>
#include <cmath>

namespace gx {

QJsonObject Perspective::toJson() const {
    QJsonArray v;
    for (const auto& p : vp) v.append(QJsonArray{p.x(), p.y()});
    return QJsonObject{{"enabled", enabled}, {"snap", snap}, {"points", points}, {"vp", v},
                       {"color", color.name()}, {"grid", gridLines}};
}

Perspective Perspective::fromJson(const QJsonObject& o) {
    Perspective p;
    p.enabled = o.value("enabled").toBool();
    p.snap = o.value("snap").toBool(true);
    p.points = std::clamp(o.value("points").toInt(2), 1, 3);
    QJsonArray v = o.value("vp").toArray();
    for (int i = 0; i < 3 && i < v.size(); ++i) {
        QJsonArray a = v[i].toArray();
        p.vp[i] = QPointF(a[0].toDouble(), a[1].toDouble());
    }
    p.color = QColor(o.value("color").toString("#468cff"));
    p.gridLines = o.value("grid").toInt(16);
    return p;
}

void Perspective::resetFor(const QSize& c) {
    double w = c.width(), h = c.height();
    if (points == 1) vp[0] = QPointF(w * 0.5, h * 0.4);
    else {
        vp[0] = QPointF(-w * 0.25, h * 0.4);
        vp[1] = QPointF(w * 1.25, h * 0.4);
    }
    vp[2] = QPointF(w * 0.5, h * 2.2);
}

QLineF Perspective::horizon() const {
    if (points == 1) return QLineF(vp[0] - QPointF(1e5, 0), vp[0] + QPointF(1e5, 0));
    QPointF d = vp[1] - vp[0];
    double len = std::hypot(d.x(), d.y());
    if (len < 1e-6) return QLineF(vp[0] - QPointF(1e5, 0), vp[0] + QPointF(1e5, 0));
    d /= len;
    return QLineF(vp[0] - d * 1e5, vp[1] + d * 1e5);
}

QVector<QPointF> Perspective::directionsAt(const QPointF& p) const {
    QVector<QPointF> dirs;
    for (int i = 0; i < points; ++i) {
        QPointF d = vp[i] - p;
        double l = std::hypot(d.x(), d.y());
        dirs << (l > 1e-6 ? d / l : QPointF(1, 0));
    }
    if (points <= 2) dirs << QPointF(0, 1);   // verticals
    if (points == 1) dirs << QPointF(1, 0);   // horizontals
    return dirs;
}

int Perspective::bestTarget(const QPointF& start, const QPointF& p) const {
    QPointF m = p - start;
    double ml = std::hypot(m.x(), m.y());
    if (ml < 1e-6) return 0;
    m /= ml;
    int best = 0;
    double bestDot = -1;
    auto consider = [&](int idx, QPointF d) {
        double dot = std::fabs(m.x() * d.x() + m.y() * d.y());
        if (dot > bestDot) { bestDot = dot; best = idx; }
    };
    for (int i = 0; i < points; ++i) {
        QPointF d = vp[i] - start;
        double l = std::hypot(d.x(), d.y());
        if (l > 1e-6) consider(i, d / l);
    }
    if (points == 1) consider(3, QPointF(1, 0));
    if (points <= 2) consider(4, QPointF(0, 1));
    return best;
}

QPointF Perspective::constrain(const QPointF& start, const QPointF& p, int target) const {
    QPointF d;
    if (target == 3) d = QPointF(1, 0);
    else if (target == 4) d = QPointF(0, 1);
    else {
        d = vp[std::clamp(target, 0, 2)] - start;
        double l = std::hypot(d.x(), d.y());
        if (l < 1e-6) return p;
        d /= l;
    }
    QPointF v = p - start;
    double t = v.x() * d.x() + v.y() * d.y();
    return start + d * t;
}

QVector<QTransform> Symmetry::transforms() const {
    QVector<QTransform> out;
    auto around = [&](const QTransform& t) {
        QTransform m;
        m.translate(center.x(), center.y());
        m.rotate(angle);
        QTransform back;
        back.rotate(-angle);
        back.translate(-center.x(), -center.y());
        // p -> center + R * t * R^-1 * (p - center)
        return back * t * m;
    };
    QTransform flipX(-1, 0, 0, 1, 0, 0), flipY(1, 0, 0, -1, 0, 0);
    switch (mode) {
    case Vertical: out << around(flipX); break;
    case Horizontal: out << around(flipY); break;
    case Both: out << around(flipX) << around(flipY) << around(flipX * flipY); break;
    case Radial: {
        int n = std::max(2, radialCount);
        for (int i = 1; i < n; ++i) {
            QTransform r;
            r.rotate(360.0 * i / n);
            out << around(r);
        }
        if (radialMirror)
            for (int i = 0; i < n; ++i) {
                QTransform r;
                r.rotate(360.0 * i / n);
                out << around(flipX * r);
            }
        break;
    }
    default: break;
    }
    return out;
}

QJsonObject Symmetry::toJson() const {
    return QJsonObject{{"mode", mode}, {"cx", center.x()}, {"cy", center.y()}, {"count", radialCount},
                       {"mirror", radialMirror}, {"angle", angle}};
}

Symmetry Symmetry::fromJson(const QJsonObject& o) {
    Symmetry s;
    s.mode = o.value("mode").toInt();
    s.center = QPointF(o.value("cx").toDouble(), o.value("cy").toDouble());
    s.radialCount = o.value("count").toInt(6);
    s.radialMirror = o.value("mirror").toBool();
    s.angle = o.value("angle").toDouble();
    return s;
}

}  // namespace gx
