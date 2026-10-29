#pragma once
#include <QColor>
#include <QJsonObject>
#include <QSize>
#include <QLineF>
#include <QPointF>
#include <QTransform>
#include <QVector>
#include <QStringList>
#include <QPainterPath>
#include <QRectF>

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

// Special ruler (like Clip Studio): strokes snap to a straight line, a curve, an ellipse,
// or follow concentric ellipses, parallel lines or lines from one point (fan).
struct Ruler {
    enum Kind { None = 0, Straight = 1, Curve = 2, Ellipse = 3, Concentric = 4, Parallel = 5, Fan = 6 };
    int kind = None;
    bool snap = true;
    QPointF a, b, c;  // straight / parallel: a-b; curve: from a through b to c; ellipse, concentric, fan: a = center
    double rx = 200, ry = 120, angle = 0;  // ellipse and concentric (degrees)
    QColor color = QColor(40, 190, 120);

    bool isOn() const { return kind != None; }
    bool snaps() const { return kind != None && snap; }
    static QStringList kindNames();  // English, in Kind order
    void resetFor(const QSize& canvas);  // a sensible ruler in the middle of the picture
    QJsonObject toJson() const;
    static Ruler fromJson(const QJsonObject& o);

    // The point of a stroke that started at `start`, moved onto the ruler.
    QPointF constrain(const QPointF& start, const QPointF& p) const;
    // Lines to draw on the canvas (canvas coordinates), covering `visible`.
    QPainterPath guidePath(const QRectF& visible) const;
    // The ruler itself (no helper lines): the segment, the curve, the ellipse.
    QPainterPath mainPath() const;
    // Handles for the Ruler tool. setHandle keeps the shape consistent.
    QVector<QPointF> handles() const;
    void setHandle(int i, const QPointF& p, bool shift);
    void transform(const QTransform& t);  // canvas rotated, flipped, resized...

private:
    QPointF ellipsePoint(double t, double scale = 1) const;
    QPointF curveControl() const { return 2 * b - (a + c) / 2; }
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
