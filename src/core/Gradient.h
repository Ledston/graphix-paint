#pragma once
#include <QColor>
#include <QJsonObject>
#include <QVector>
#include <vector>

namespace gx {

struct GradientStop {
    double pos = 0;       // 0..1
    QColor color;         // straight color
    double opacity = 1;   // 0..1
};

struct Gradient {
    QString name;
    QVector<GradientStop> stops;

    void normalize();  // sort, clamp, ensure at least two stops
    // Straight RGBA at t (0..1)
    void eval(double t, float* rgba) const;
    // n entries of premultiplied RGBA floats
    std::vector<float> buildLut(int n, bool reverse = false) const;

    QJsonObject toJson() const;
    static Gradient fromJson(const QJsonObject& o);

    static Gradient twoColor(const QColor& a, const QColor& b);
    static Gradient toTransparent(const QColor& a);
    static QVector<Gradient> presets(const QColor& fg, const QColor& bg);
};

}  // namespace gx
