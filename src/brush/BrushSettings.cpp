#include "brush/BrushSettings.h"

#include <algorithm>

#include "core/Adjustments.h"
#include "core/Pixel.h"

namespace gx {

QString brushEngineName(BrushEngine e) {
    switch (e) {
    case BrushEngine::Paint: return "Paint";
    case BrushEngine::Mix: return "Mix (brush / watercolor)";
    case BrushEngine::Erase: return "Eraser";
    case BrushEngine::Blur: return "Blur";
    case BrushEngine::Smudge: return "Smudge";
    }
    return "Paint";
}

QJsonObject BrushSettings::toJson() const {
    QJsonObject o;
    o["name"] = name;
    o["icon"] = iconKey;
    o["engine"] = int(engine);
    o["size"] = size;
    o["minSize"] = minSize;
    o["sizePressure"] = sizePressure;
    o["opacity"] = opacity;
    o["density"] = density;
    o["minDensity"] = minDensity;
    o["densityPressure"] = densityPressure;
    o["buildup"] = buildup;
    o["airbrush"] = airbrush;
    o["airbrushRate"] = airbrushRate;
    o["hardness"] = hardness;
    o["antialias"] = antialias;
    o["spacing"] = spacing;
    o["roundness"] = roundness;
    o["angle"] = angle;
    o["angleFollowsStroke"] = angleFollowsStroke;
    o["angleFromTilt"] = angleFromTilt;
    o["shape"] = shape;
    o["shapeStrength"] = shapeStrength;
    o["shapeAsTip"] = shapeAsTip;
    o["texture"] = texture;
    o["textureStrength"] = textureStrength;
    o["textureScale"] = textureScale;
    o["textureInvert"] = textureInvert;
    o["textureEachDab"] = textureEachDab;
    o["blending"] = blending;
    o["dilution"] = dilution;
    o["persistence"] = persistence;
    o["blendingPressure"] = blendingPressure;
    o["fringe"] = fringe;
    o["sizeJitter"] = sizeJitter;
    o["angleJitter"] = angleJitter;
    o["scatter"] = scatter;
    o["opacityJitter"] = opacityJitter;
    o["hueJitter"] = hueJitter;
    o["saturationJitter"] = saturationJitter;
    o["valueJitter"] = valueJitter;
    o["taperIn"] = taperIn;
    o["taperOut"] = taperOut;
    o["keepOpacity"] = keepOpacity;
    o["paintMode"] = int(paintMode);
    o["customPressureCurve"] = customPressureCurve;
    o["pressureCurve"] = pointsToJson(pressureCurve);
    return o;
}

BrushSettings BrushSettings::fromJson(const QJsonObject& o) {
    BrushSettings b;
    auto d = [&](const char* k, double def) { return o.contains(k) ? o.value(k).toDouble(def) : def; };
    auto bl = [&](const char* k, bool def) { return o.contains(k) ? o.value(k).toBool(def) : def; };
    b.name = o.value("name").toString(b.name);
    b.iconKey = o.value("icon").toString(b.iconKey);
    int e = o.value("engine").toInt(0);
    b.engine = (e >= 0 && e <= 4) ? BrushEngine(e) : BrushEngine::Paint;
    b.size = std::clamp(d("size", b.size), 0.5, 5000.0);
    b.minSize = std::clamp(d("minSize", b.minSize), 0.0, 1.0);
    b.sizePressure = bl("sizePressure", b.sizePressure);
    b.opacity = std::clamp(d("opacity", b.opacity), 0.0, 1.0);
    b.density = std::clamp(d("density", b.density), 0.0, 1.0);
    b.minDensity = std::clamp(d("minDensity", b.minDensity), 0.0, 1.0);
    b.densityPressure = bl("densityPressure", b.densityPressure);
    b.buildup = bl("buildup", b.buildup);
    b.airbrush = bl("airbrush", b.airbrush);
    b.airbrushRate = std::clamp(d("airbrushRate", b.airbrushRate), 1.0, 200.0);
    b.hardness = std::clamp(d("hardness", b.hardness), 0.0, 1.0);
    b.antialias = bl("antialias", b.antialias);
    b.spacing = std::clamp(d("spacing", b.spacing), 0.01, 5.0);
    b.roundness = std::clamp(d("roundness", b.roundness), 0.02, 1.0);
    b.angle = d("angle", b.angle);
    b.angleFollowsStroke = bl("angleFollowsStroke", b.angleFollowsStroke);
    b.angleFromTilt = bl("angleFromTilt", b.angleFromTilt);
    b.shape = o.value("shape").toString();
    b.shapeStrength = std::clamp(d("shapeStrength", b.shapeStrength), 0.0, 1.0);
    b.shapeAsTip = bl("shapeAsTip", b.shapeAsTip);
    b.texture = o.value("texture").toString();
    b.textureStrength = std::clamp(d("textureStrength", b.textureStrength), 0.0, 1.0);
    b.textureScale = std::clamp(d("textureScale", b.textureScale), 0.05, 20.0);
    b.textureInvert = bl("textureInvert", b.textureInvert);
    b.textureEachDab = bl("textureEachDab", b.textureEachDab);
    b.blending = std::clamp(d("blending", b.blending), 0.0, 1.0);
    b.dilution = std::clamp(d("dilution", b.dilution), 0.0, 1.0);
    b.persistence = std::clamp(d("persistence", b.persistence), 0.0, 1.0);
    b.blendingPressure = bl("blendingPressure", b.blendingPressure);
    b.fringe = std::clamp(d("fringe", b.fringe), 0.0, 1.0);
    b.sizeJitter = std::clamp(d("sizeJitter", 0), 0.0, 1.0);
    b.angleJitter = std::clamp(d("angleJitter", 0), 0.0, 1.0);
    b.scatter = std::clamp(d("scatter", 0), 0.0, 5.0);
    b.opacityJitter = std::clamp(d("opacityJitter", 0), 0.0, 1.0);
    b.hueJitter = std::clamp(d("hueJitter", 0), 0.0, 1.0);
    b.saturationJitter = std::clamp(d("saturationJitter", 0), 0.0, 1.0);
    b.valueJitter = std::clamp(d("valueJitter", 0), 0.0, 1.0);
    b.taperIn = std::max(0.0, d("taperIn", 0));
    b.taperOut = std::max(0.0, d("taperOut", 0));
    b.keepOpacity = bl("keepOpacity", false);
    int pm = o.value("paintMode").toInt(0);
    b.paintMode = (pm >= 0 && pm < int(BlendMode::Count)) ? BlendMode(pm) : BlendMode::Normal;
    b.customPressureCurve = bl("customPressureCurve", false);
    if (o.contains("pressureCurve")) b.pressureCurve = pointsFromJson(o.value("pressureCurve"));
    return b;
}

QVector<BrushSettings> BrushSettings::defaults() {
    QVector<BrushSettings> v;
    {
        BrushSettings b; b.name = "Pen"; b.iconKey = "pen";
        b.size = 6; b.minSize = 0.0; b.hardness = 0.92; b.spacing = 0.06;
        v << b;
    }
    {
        BrushSettings b; b.name = "Ink"; b.iconKey = "ink";
        b.size = 10; b.minSize = 0.05; b.hardness = 1.0; b.spacing = 0.05; b.taperIn = 12;
        v << b;
    }
    {
        BrushSettings b; b.name = "Pencil"; b.iconKey = "pencil";
        b.size = 4; b.minSize = 0.4; b.hardness = 0.55; b.densityPressure = true; b.minDensity = 0.15;
        b.density = 0.85; b.texture = "Fine paper"; b.textureStrength = 0.6; b.spacing = 0.1;
        v << b;
    }
    {
        BrushSettings b; b.name = "AirBrush"; b.iconKey = "airbrush";
        b.size = 140; b.sizePressure = false; b.hardness = 0.0; b.density = 0.06; b.densityPressure = true;
        b.minDensity = 0.0; b.buildup = true; b.airbrush = true; b.spacing = 0.06;
        v << b;
    }
    {
        BrushSettings b; b.name = "Brush"; b.iconKey = "brush"; b.engine = BrushEngine::Mix;
        b.size = 26; b.minSize = 0.25; b.hardness = 0.7; b.blending = 0.5; b.persistence = 0.8;
        b.density = 0.85; b.spacing = 0.05; b.blendingPressure = true;
        v << b;
    }
    {
        BrushSettings b; b.name = "Water Color"; b.iconKey = "watercolor"; b.engine = BrushEngine::Mix;
        b.size = 45; b.minSize = 0.5; b.hardness = 0.45; b.blending = 0.6; b.dilution = 0.35; b.persistence = 0.75;
        b.density = 0.75; b.fringe = 0.2; b.texture = "Watercolor paper"; b.textureStrength = 0.25; b.spacing = 0.05;
        b.blendingPressure = true;
        v << b;
    }
    {
        BrushSettings b; b.name = "Marker"; b.iconKey = "marker";
        b.size = 22; b.minSize = 0.6; b.hardness = 0.85; b.opacity = 0.55; b.roundness = 0.45; b.angle = 35;
        v << b;
    }
    {
        BrushSettings b; b.name = "Chalk"; b.iconKey = "chalk";
        b.size = 28; b.minSize = 0.5; b.shape = "Chalk tip"; b.shapeAsTip = true; b.texture = "Rough grain";
        b.textureStrength = 0.6; b.spacing = 0.12; b.angleJitter = 1.0; b.densityPressure = true; b.minDensity = 0.3;
        v << b;
    }
    {
        BrushSettings b; b.name = "Bristle"; b.iconKey = "bristle"; b.engine = BrushEngine::Mix;
        b.size = 34; b.minSize = 0.4; b.shape = "Bristle tip"; b.shapeAsTip = true; b.angleFollowsStroke = true;
        b.blending = 0.35; b.persistence = 0.85; b.spacing = 0.04; b.hardness = 0.6; b.blendingPressure = true;
        v << b;
    }
    {
        BrushSettings b; b.name = "Binary"; b.iconKey = "binary";
        b.size = 2; b.sizePressure = false; b.antialias = false; b.hardness = 1.0; b.spacing = 0.2;
        v << b;
    }
    {
        BrushSettings b; b.name = "Eraser"; b.iconKey = "eraser"; b.engine = BrushEngine::Erase;
        b.size = 30; b.minSize = 0.3; b.hardness = 0.85;
        v << b;
    }
    {
        BrushSettings b; b.name = "Soft Eraser"; b.iconKey = "softeraser"; b.engine = BrushEngine::Erase;
        b.size = 120; b.sizePressure = false; b.hardness = 0.0; b.density = 0.25; b.densityPressure = true; b.buildup = true;
        v << b;
    }
    {
        BrushSettings b; b.name = "Blur"; b.iconKey = "blur"; b.engine = BrushEngine::Blur;
        b.size = 60; b.sizePressure = false; b.hardness = 0.3; b.density = 0.5; b.densityPressure = true; b.spacing = 0.1;
        v << b;
    }
    {
        BrushSettings b; b.name = "Smudge"; b.iconKey = "smudge"; b.engine = BrushEngine::Smudge;
        b.size = 35; b.sizePressure = false; b.hardness = 0.4; b.persistence = 0.8; b.density = 0.85; b.spacing = 0.05;
        b.densityPressure = true;
        v << b;
    }
    return v;
}

void PressureCurve::set(const QVector<QPointF>& pts) {
    m_points = pts.size() >= 2 ? pts : QVector<QPointF>{QPointF(0, 0), QPointF(1, 1)};
    m_lut = curveLut(m_points, 1024);
}

float PressureCurve::map(float p) const {
    float f = clamp01(p) * 1023.f;
    int i = int(f);
    if (i >= 1023) return m_lut[1023];
    float t = f - float(i);
    return m_lut[size_t(i)] + (m_lut[size_t(i) + 1] - m_lut[size_t(i)]) * t;
}

}  // namespace gx
