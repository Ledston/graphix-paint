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

// ------------------------------------------------------------------ special ruler
namespace gx {

namespace {
QPointF unit(QPointF v) {
    const double l = std::hypot(v.x(), v.y());
    return l > 1e-9 ? v / l : QPointF(1, 0);
}
double dot(QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); }
QPointF onLine(QPointF origin, QPointF dir, QPointF p) { return origin + dir * dot(p - origin, dir); }
}  // namespace

QStringList Ruler::kindNames() {
    return {"No ruler", "Straight", "Curve", "Ellipse", "Concentric", "Parallel lines", "Fan (lines from a point)"};
}

void Ruler::resetFor(const QSize& s) {
    const double w = s.width(), h = s.height();
    const QPointF mid(w / 2, h / 2);
    a = mid - QPointF(w * 0.3, 0);
    b = mid + QPointF(w * 0.3, 0);
    c = mid + QPointF(w * 0.3, 0);
    if (kind == Curve) {
        a = mid + QPointF(-w * 0.3, h * 0.15);
        b = mid - QPointF(0, h * 0.15);
        c = mid + QPointF(w * 0.3, h * 0.15);
    }
    if (kind == Parallel) {
        a = mid + QPointF(-w * 0.2, h * 0.1);
        b = mid + QPointF(w * 0.2, -h * 0.1);
    }
    if (kind == Ellipse || kind == Concentric || kind == Fan) a = mid;
    rx = std::max(20.0, std::min(w, h) * 0.3);
    ry = rx * 0.6;
    angle = 0;
}

QJsonObject Ruler::toJson() const {
    auto pt = [](QPointF p) { return QJsonArray{p.x(), p.y()}; };
    return {{"kind", kind}, {"snap", snap}, {"a", pt(a)}, {"b", pt(b)}, {"c", pt(c)},
            {"rx", rx}, {"ry", ry}, {"angle", angle}, {"color", color.name()}};
}

Ruler Ruler::fromJson(const QJsonObject& o) {
    Ruler r;
    auto pt = [&](const char* k) {
        const QJsonArray v = o.value(k).toArray();
        return QPointF(v.size() > 0 ? v[0].toDouble() : 0, v.size() > 1 ? v[1].toDouble() : 0);
    };
    r.kind = std::clamp(o.value("kind").toInt(), 0, 6);
    r.snap = o.value("snap").toBool(true);
    r.a = pt("a");
    r.b = pt("b");
    r.c = pt("c");
    r.rx = std::max(1.0, o.value("rx").toDouble(200));
    r.ry = std::max(1.0, o.value("ry").toDouble(120));
    r.angle = o.value("angle").toDouble();
    if (o.contains("color")) r.color = QColor(o.value("color").toString());
    return r;
}

QPointF Ruler::ellipsePoint(double t, double scale) const {
    const double an = angle * M_PI / 180;
    const double x = rx * scale * std::cos(t), y = ry * scale * std::sin(t);
    return a + QPointF(x * std::cos(an) - y * std::sin(an), x * std::sin(an) + y * std::cos(an));
}

QPointF Ruler::constrain(const QPointF& start, const QPointF& p) const {
    switch (kind) {
    case Straight: return onLine(a, unit(b - a), p);
    case Parallel: return onLine(start, unit(b - a), p);
    case Fan: {
        if (std::hypot(start.x() - a.x(), start.y() - a.y()) < 0.5) return p;
        return onLine(start, unit(start - a), p);
    }
    case Curve: {
        // nearest point of the curve: coarse search, then a finer one around it
        const QPointF q = curveControl();
        auto at = [&](double t) { return (1 - t) * (1 - t) * a + 2 * (1 - t) * t * q + t * t * c; };
        double bestT = 0, best = 1e300;
        for (int i = 0; i <= 200; ++i) {
            const double t = i / 200.0;
            const QPointF d = at(t) - p;
            if (dot(d, d) < best) best = dot(d, d), bestT = t;
        }
        for (int i = -20; i <= 20; ++i) {
            const double t = std::clamp(bestT + i / 4000.0, 0.0, 1.0);
            const QPointF d = at(t) - p;
            if (dot(d, d) < best) best = dot(d, d), bestT = t;
        }
        return at(bestT);
    }
    case Ellipse:
    case Concentric: {
        const double an = angle * M_PI / 180;
        auto local = [&](QPointF v) {
            v -= a;
            return QPointF(v.x() * std::cos(an) + v.y() * std::sin(an), -v.x() * std::sin(an) + v.y() * std::cos(an));
        };
        double scale = 1;
        if (kind == Concentric) {
            const QPointF s = local(start);
            scale = std::hypot(s.x() / rx, s.y() / ry);
            if (scale < 1e-4) return p;
        }
        const QPointF l = local(p);
        if (std::hypot(l.x(), l.y()) < 1e-6) return p;
        return ellipsePoint(std::atan2(l.y() / ry, l.x() / rx), scale);
    }
    default: return p;
    }
}

QPainterPath Ruler::mainPath() const {
    QPainterPath path;
    switch (kind) {
    case Straight:
    case Parallel:
        path.moveTo(a);
        path.lineTo(b);
        break;
    case Curve:
        path.moveTo(a);
        path.quadTo(curveControl(), c);
        break;
    case Ellipse:
    case Concentric:
        for (int i = 0; i <= 128; ++i) {
            const QPointF e = ellipsePoint(2 * M_PI * i / 128);
            if (i == 0) path.moveTo(e);
            else path.lineTo(e);
        }
        break;
    default: break;
    }
    return path;
}

QPainterPath Ruler::guidePath(const QRectF& vis) const {
    QPainterPath path;
    const double reach = std::hypot(vis.width(), vis.height()) + std::hypot(vis.center().x() - a.x(), vis.center().y() - a.y()) + 100;
    switch (kind) {
    case Straight: {
        const QPointF d = unit(b - a);
        path.moveTo(a - d * reach);
        path.lineTo(b + d * reach);
        break;
    }
    case Parallel: {
        // lines across the view, about 60 canvas px apart
        const QPointF d = unit(b - a), n(-d.y(), d.x());
        const double step = std::max(8.0, std::hypot(vis.width(), vis.height()) / 24);
        const double o0 = dot(vis.center() - a, n);
        for (int k = -14; k <= 14; ++k) {
            const double o = std::round(o0 / step + k) * step;
            const QPointF base = a + n * o + d * dot(vis.center() - a, d);
            path.moveTo(base - d * reach);
            path.lineTo(base + d * reach);
        }
        break;
    }
    case Fan:
        for (int k = 0; k < 36; ++k) {
            const double t = 2 * M_PI * k / 36;
            path.moveTo(a);
            path.lineTo(a + QPointF(std::cos(t), std::sin(t)) * reach);
        }
        break;
    case Concentric: {
        const double maxScale = reach / std::max(1.0, std::min(rx, ry));
        for (double s = 0.25; s <= maxScale && s < 64; s += 0.25)
            for (int i = 0; i <= 96; ++i) {
                const QPointF e = ellipsePoint(2 * M_PI * i / 96, s);
                if (i == 0) path.moveTo(e);
                else path.lineTo(e);
            }
        break;
    }
    default: path = mainPath(); break;
    }
    return path;
}

QVector<QPointF> Ruler::handles() const {
    switch (kind) {
    case Straight:
    case Parallel: return {a, b};
    case Curve: return {a, b, c};
    case Ellipse:
    case Concentric: return {a, ellipsePoint(0), ellipsePoint(M_PI / 2)};
    case Fan: return {a};
    default: return {};
    }
}

void Ruler::setHandle(int i, const QPointF& p, bool shift) {
    auto snap45 = [&](QPointF from, QPointF to) {
        if (!shift) return to;
        const QPointF d = to - from;
        const double an = std::round(std::atan2(d.y(), d.x()) / (M_PI / 4)) * (M_PI / 4), len = std::hypot(d.x(), d.y());
        return from + QPointF(std::cos(an), std::sin(an)) * len;
    };
    switch (kind) {
    case Straight:
    case Parallel:
        if (i == 0) a = snap45(b, p);
        else b = snap45(a, p);
        break;
    case Curve:
        if (i == 0) a = p;
        else if (i == 1) b = p;
        else c = p;
        break;
    case Ellipse:
    case Concentric: {
        if (i == 0) {
            a = p;
            break;
        }
        const QPointF d = p - a;
        if (i == 1) {
            double an = std::atan2(d.y(), d.x()) * 180 / M_PI;
            if (shift) an = std::round(an / 15) * 15;
            angle = an;
            rx = std::max(2.0, std::hypot(d.x(), d.y()));
        } else {
            const double r = angle * M_PI / 180;
            ry = std::max(2.0, std::abs(dot(d, QPointF(-std::sin(r), std::cos(r)))));
            if (shift) ry = rx;
        }
        break;
    }
    case Fan: a = p; break;
    default: break;
    }
}

void Ruler::transform(const QTransform& t) {
    const QPointF h1 = ellipsePoint(0), h2 = ellipsePoint(M_PI / 2);
    a = t.map(a);
    b = t.map(b);
    c = t.map(c);
    const QPointF m1 = t.map(h1) - a, m2 = t.map(h2) - a;
    rx = std::max(1.0, std::hypot(m1.x(), m1.y()));
    ry = std::max(1.0, std::hypot(m2.x(), m2.y()));
    angle = std::atan2(m1.y(), m1.x()) * 180 / M_PI;
}

}  // namespace gx
