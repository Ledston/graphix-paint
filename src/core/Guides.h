#pragma once
#include <QColor>
#include <QJsonObject>
#include <QSize>
#include <QLineF>
#include <QPointF>
#include <QTransform>
#include <QVector>

namespace gx {

// Perspective ruler with 1, 2 or 3 vanishing points.
struct Perspective {
    bool enabled = false;
    bool snap = true;
    int points = 2;  // 1, 2 or 3
    QPointF vp[3];
    QColor color = QColor(70, 140, 255);
    int gridLines = 16;

    QJsonObject toJson() const;
    static Perspective fromJson(const QJsonObject& o);
    void resetFor(const QSize& canvas);
    QLineF horizon() const;
    // Candidate snapping directions at p (unit vectors, including the
    // horizontal/vertical ones used by 1- and 2-point perspective).
    QVector<QPointF> directionsAt(const QPointF& p) const;
    // Point on the snapped line through 'start' toward the chosen target.
    // target index: 0..2 = vanishing point, 3 = horizontal, 4 = vertical.
    QPointF constrain(const QPointF& start, const QPointF& p, int target) const;
    int bestTarget(const QPointF& start, const QPointF& p) const;
};

struct Symmetry {
    enum Mode { Off = 0, Vertical = 1, Horizontal = 2, Both = 3, Radial = 4 };
    int mode = Off;
    QPointF center;
    int radialCount = 6;
    bool radialMirror = false;
    double angle = 0;  // axis rotation in degrees

    bool isOn() const { return mode != Off; }
    // Transforms producing the extra (mirrored) copies of a stroke; identity excluded.
    QVector<QTransform> transforms() const;
    QJsonObject toJson() const;
    static Symmetry fromJson(const QJsonObject& o);
};

}  // namespace gx
