#include "core/Gradient.h"

#include <QJsonArray>
#include <algorithm>

namespace gx {

void Gradient::normalize() {
    for (auto& s : stops) s.pos = std::clamp(s.pos, 0.0, 1.0);
    std::stable_sort(stops.begin(), stops.end(), [](const GradientStop& a, const GradientStop& b) { return a.pos < b.pos; });
    if (stops.isEmpty()) stops = {{0, Qt::black, 1}, {1, Qt::white, 1}};
    if (stops.size() == 1) stops.push_back({1, stops[0].color, stops[0].opacity});
}

void Gradient::eval(double t, float* o) const {
    if (stops.isEmpty()) { o[0] = o[1] = o[2] = 0; o[3] = 1; return; }
    t = std::clamp(t, 0.0, 1.0);
    const GradientStop* a = &stops.front();
    const GradientStop* b = &stops.back();
    if (t <= a->pos) b = a;
    else if (t >= b->pos) a = b;
    else
        for (int i = 0; i + 1 < stops.size(); ++i)
            if (t >= stops[i].pos && t <= stops[i + 1].pos) { a = &stops[i]; b = &stops[i + 1]; break; }
    double span = b->pos - a->pos;
    double f = span > 1e-9 ? (t - a->pos) / span : 0.0;
    o[0] = float(a->color.redF() + (b->color.redF() - a->color.redF()) * f);
    o[1] = float(a->color.greenF() + (b->color.greenF() - a->color.greenF()) * f);
    o[2] = float(a->color.blueF() + (b->color.blueF() - a->color.blueF()) * f);
    o[3] = float(a->opacity + (b->opacity - a->opacity) * f);
}

std::vector<float> Gradient::buildLut(int n, bool reverse) const {
    std::vector<float> lut(size_t(n) * 4);
    for (int i = 0; i < n; ++i) {
        double t = n > 1 ? double(i) / (n - 1) : 0.0;
        if (reverse) t = 1.0 - t;
        float* p = &lut[size_t(i) * 4];
        eval(t, p);
        p[0] *= p[3]; p[1] *= p[3]; p[2] *= p[3];
    }
    return lut;
}

QJsonObject Gradient::toJson() const {
    QJsonArray arr;
    for (const auto& s : stops)
        arr.append(QJsonObject{{"pos", s.pos}, {"color", s.color.name(QColor::HexRgb)}, {"opacity", s.opacity}});
    return QJsonObject{{"name", name}, {"stops", arr}};
}

Gradient Gradient::fromJson(const QJsonObject& o) {
    Gradient g;
    g.name = o.value("name").toString();
    for (const auto& v : o.value("stops").toArray()) {
        QJsonObject so = v.toObject();
        g.stops.push_back({so.value("pos").toDouble(), QColor(so.value("color").toString()), so.value("opacity").toDouble(1)});
    }
    g.normalize();
    return g;
}

Gradient Gradient::twoColor(const QColor& a, const QColor& b) {
    Gradient g;
    g.name = "Foreground to Background";
    g.stops = {{0, a, 1}, {1, b, 1}};
    return g;
}

Gradient Gradient::toTransparent(const QColor& a) {
    Gradient g;
    g.name = "Foreground to Transparent";
    g.stops = {{0, a, 1}, {1, a, 0}};
    return g;
}

QVector<Gradient> Gradient::presets(const QColor& fg, const QColor& bg) {
    QVector<Gradient> v;
    v << twoColor(fg, bg) << toTransparent(fg);
    Gradient bw; bw.name = "Black to White"; bw.stops = {{0, Qt::black, 1}, {1, Qt::white, 1}}; v << bw;
    Gradient sunset; sunset.name = "Sunset";
    sunset.stops = {{0, QColor("#2b1055"), 1}, {0.45, QColor("#d53369"), 1}, {0.75, QColor("#f39c4f"), 1}, {1, QColor("#ffe29a"), 1}};
    v << sunset;
    Gradient sky; sky.name = "Sky";
    sky.stops = {{0, QColor("#1e3c72"), 1}, {0.6, QColor("#5fa8d3"), 1}, {1, QColor("#e8f4fa"), 1}};
    v << sky;
    Gradient skin; skin.name = "Skin shading";
    skin.stops = {{0, QColor("#5a2e2a"), 1}, {0.35, QColor("#b86b5a"), 1}, {0.7, QColor("#efb79c"), 1}, {1, QColor("#fff0e6"), 1}};
    v << skin;
    Gradient rainbow; rainbow.name = "Spectrum";
    for (int i = 0; i <= 6; ++i) rainbow.stops.push_back({i / 6.0, QColor::fromHsvF(i / 6.0 * 0.999, 1, 1), 1});
    v << rainbow;
    return v;
}

}  // namespace gx
