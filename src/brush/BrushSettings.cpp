#include "brush/BrushSettings.h"

#include <QHash>
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
    o["group"] = group;
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
    o["textureDensity"] = textureDensity;
    o["textureFollowsStroke"] = textureFollowsStroke;
    o["bundleLines"] = bundleLines;
    o["bundleThickness"] = bundleThickness;
    o["bundleThicknessJitter"] = bundleThicknessJitter;
    o["bundleWobble"] = bundleWobble;
    o["bundleLengthJitter"] = bundleLengthJitter;
    o["particles"] = particles;
    o["particleSize"] = particleSize;
    o["subColorShare"] = subColorShare;
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
    b.group = o.value("group").toString();
    if (b.group.isEmpty()) b.group = defaultGroupFor(b.name);
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
    b.textureDensity = std::clamp(d("textureDensity", b.textureDensity), 0.0, 1.0);
    b.textureFollowsStroke = bl("textureFollowsStroke", b.textureFollowsStroke);
    b.bundleLines = std::clamp(int(d("bundleLines", b.bundleLines)), 1, 40);
    b.bundleThickness = std::clamp(d("bundleThickness", b.bundleThickness), 0.05, 1.0);
    b.bundleThicknessJitter = std::clamp(d("bundleThicknessJitter", b.bundleThicknessJitter), 0.0, 1.0);
    b.bundleWobble = std::clamp(d("bundleWobble", b.bundleWobble), 0.0, 1.0);
    b.bundleLengthJitter = std::clamp(d("bundleLengthJitter", b.bundleLengthJitter), 0.0, 1.0);
    b.particles = std::clamp(int(d("particles", b.particles)), 0, 60);
    b.particleSize = std::clamp(d("particleSize", b.particleSize), 0.01, 1.0);
    b.subColorShare = std::clamp(d("subColorShare", b.subColorShare), 0.0, 1.0);
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
    v << defaultsSince(2);
    for (BrushSettings& b : v)
        if (b.group.isEmpty()) b.group = defaultGroupFor(b.name);
    return v;
}

QVector<BrushSettings> BrushSettings::defaultsSince(int version) {
    QVector<BrushSettings> v;
    if (version > 2) return v;
    // ---- hatching: one stroke draws a bundle of lines
    {
        BrushSettings b; b.name = "Hatching"; b.iconKey = "hatching"; b.group = "Hatching";
        b.size = 34; b.minSize = 0.55; b.hardness = 0.9; b.spacing = 0.35;
        b.bundleLines = 7; b.bundleThickness = 0.4; b.bundleThicknessJitter = 0.45; b.bundleWobble = 0.2; b.bundleLengthJitter = 0.5;
        b.taperIn = 25; b.taperOut = 45;
        v << b;
    }
    {
        BrushSettings b; b.name = "Hatching with white"; b.iconKey = "hatching"; b.group = "Hatching";
        b.size = 34; b.minSize = 0.55; b.hardness = 0.9; b.spacing = 0.35;
        b.bundleLines = 9; b.bundleThickness = 0.3; b.bundleThicknessJitter = 0.5; b.bundleWobble = 0.2; b.bundleLengthJitter = 0.6;
        b.subColorShare = 0.35; b.taperIn = 25; b.taperOut = 45;
        v << b;
    }
    {
        // smears the colors under it along the lines, like a finger with a comb
        BrushSettings b; b.name = "Hatching blender"; b.iconKey = "hatching"; b.group = "Hatching"; b.engine = BrushEngine::Mix;
        b.size = 40; b.minSize = 0.5; b.hardness = 0.75; b.spacing = 0.3;
        b.bundleLines = 6; b.bundleThickness = 0.45; b.bundleThicknessJitter = 0.4; b.bundleWobble = 0.15; b.bundleLengthJitter = 0.3;
        b.blending = 0.7; b.persistence = 0.85; b.density = 0.8; b.blendingPressure = true; b.taperIn = 20; b.taperOut = 40;
        b.texture = "Pencil grain"; b.textureStrength = 0.35;
        v << b;
    }
    {
        BrushSettings b; b.name = "Textured hatching"; b.iconKey = "hatching"; b.group = "Hatching";
        b.size = 30; b.minSize = 0.4; b.hardness = 0.8; b.spacing = 0.06; b.densityPressure = true; b.minDensity = 0.3;
        b.texture = "Hatch lines"; b.textureStrength = 1.0; b.textureFollowsStroke = true; b.textureScale = 0.8;
        b.taperIn = 20; b.taperOut = 30;
        v << b;
    }
    {
        BrushSettings b; b.name = "Stipple"; b.iconKey = "stipple"; b.group = "Hatching";
        b.size = 50; b.sizePressure = false; b.hardness = 0.95; b.spacing = 0.22; b.particles = 7; b.particleSize = 0.09;
        b.sizeJitter = 0.6; b.densityPressure = true; b.minDensity = 0.1;
        v << b;
    }
    {
        BrushSettings b; b.name = "Glitter"; b.iconKey = "stipple"; b.group = "Effects";
        b.size = 60; b.sizePressure = false; b.hardness = 0.9; b.spacing = 0.45; b.particles = 2; b.particleSize = 0.22;
        b.sizeJitter = 0.8; b.densityPressure = true; b.minDensity = 0.1; b.subColorShare = 0.5;
        b.shape = "Sparkle"; b.shapeAsTip = true; b.angleJitter = 0.15;
        v << b;
    }
    // ---- painting
    {
        BrushSettings b; b.name = "Charcoal"; b.iconKey = "charcoal"; b.group = "Paint";
        b.size = 24; b.minSize = 0.6; b.hardness = 0.6; b.spacing = 0.08; b.densityPressure = true; b.minDensity = 0.2; b.density = 0.9;
        b.shape = "Chalk tip"; b.shapeStrength = 0.6; b.texture = "Charcoal"; b.textureStrength = 0.8; b.textureScale = 1.5;
        v << b;
    }
    {
        BrushSettings b; b.name = "Soft pencil"; b.iconKey = "pencil"; b.group = "Basic";
        b.size = 8; b.minSize = 0.5; b.hardness = 0.5; b.spacing = 0.08; b.densityPressure = true; b.minDensity = 0.1; b.density = 0.8;
        b.shape = "Pencil tip"; b.shapeStrength = 0.5; b.texture = "Pencil grain"; b.textureStrength = 0.7;
        v << b;
    }
    {
        // picks the color up and spreads it: blending soft shadows (the "finger" with paint)
        BrushSettings b; b.name = "Blender"; b.iconKey = "blender"; b.group = "Paint"; b.engine = BrushEngine::Mix;
        b.size = 40; b.minSize = 0.4; b.hardness = 0.35; b.blending = 1.0; b.persistence = 0.6; b.density = 0.7; b.spacing = 0.05;
        b.densityPressure = true; b.minDensity = 0.2;
        v << b;
    }
    {
        BrushSettings b; b.name = "Soft brush"; b.iconKey = "brush"; b.group = "Paint"; b.engine = BrushEngine::Mix;
        b.size = 60; b.minSize = 0.3; b.hardness = 0.1; b.blending = 0.3; b.persistence = 0.6; b.density = 0.5; b.spacing = 0.05;
        b.densityPressure = true; b.minDensity = 0.0;
        v << b;
    }
    {
        BrushSettings b; b.name = "Dry brush"; b.iconKey = "bristle"; b.group = "Paint"; b.engine = BrushEngine::Mix;
        b.size = 40; b.minSize = 0.6; b.hardness = 0.7; b.blending = 0.25; b.persistence = 0.7; b.density = 0.9; b.spacing = 0.04;
        b.texture = "Dry brush"; b.textureStrength = 0.9; b.textureFollowsStroke = true; b.blendingPressure = true;
        v << b;
    }
    // ---- effects (two colors: the second one is the background color of the color panel)
    {
        BrushSettings b; b.name = "Glow"; b.iconKey = "glow"; b.group = "Effects";
        b.size = 70; b.sizePressure = false; b.hardness = 0.0; b.density = 0.12; b.densityPressure = true; b.minDensity = 0.0;
        b.buildup = true; b.spacing = 0.06; b.paintMode = BlendMode::Shine;
        v << b;
    }
    {
        BrushSettings b; b.name = "Hair highlight"; b.iconKey = "highlight"; b.group = "Effects";
        b.size = 18; b.minSize = 0.2; b.hardness = 0.85; b.spacing = 0.3;
        b.bundleLines = 3; b.bundleThickness = 0.55; b.bundleThicknessJitter = 0.5; b.bundleWobble = 0.1; b.bundleLengthJitter = 0.8;
        b.subColorShare = 0.34; b.taperIn = 40; b.taperOut = 60;
        v << b;
    }
    {
        BrushSettings b; b.name = "Droplets"; b.iconKey = "droplet"; b.group = "Effects";
        b.size = 50; b.sizePressure = false; b.hardness = 0.95; b.spacing = 1.1; b.particles = 1; b.particleSize = 0.45;
        b.sizeJitter = 0.8; b.shape = "Droplet"; b.shapeAsTip = true; b.subColorShare = 0.2; b.densityPressure = true; b.minDensity = 0.3;
        v << b;
    }
    return v;
}

QStringList BrushSettings::groups() { return {"Basic", "Paint", "Hatching", "Effects", "Erasers"}; }

QString BrushSettings::defaultGroupFor(const QString& name) {
    static const QHash<QString, QString> map = {
        {"Pen", "Basic"},          {"Ink", "Basic"},       {"Pencil", "Basic"},       {"Binary", "Basic"},
        {"Marker", "Basic"},       {"AirBrush", "Basic"},  {"Soft pencil", "Basic"},  {"Brush", "Paint"},
        {"Water Color", "Paint"},  {"Chalk", "Paint"},     {"Bristle", "Paint"},      {"Charcoal", "Paint"},
        {"Blender", "Paint"},      {"Soft brush", "Paint"}, {"Dry brush", "Paint"},   {"Hatching", "Hatching"},
        {"Hatching with white", "Hatching"}, {"Hatching blender", "Hatching"}, {"Textured hatching", "Hatching"},
        {"Stipple", "Hatching"},   {"Glitter", "Effects"}, {"Glow", "Effects"},       {"Hair highlight", "Effects"},
        {"Droplets", "Effects"},   {"Eraser", "Erasers"},  {"Soft Eraser", "Erasers"}, {"Blur", "Erasers"},
        {"Smudge", "Erasers"}};
    return map.value(name, "Basic");
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
