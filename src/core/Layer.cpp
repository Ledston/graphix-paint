#include "core/Layer.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace gx {

static QJsonArray colorJson(const QColor& c) { return QJsonArray{c.red(), c.green(), c.blue(), c.alpha()}; }
static QColor colorFrom(const QJsonValue& v, const QColor& def) {
    QJsonArray a = v.toArray();
    if (a.size() < 3) return def;
    return QColor(a[0].toInt(), a[1].toInt(), a[2].toInt(), a.size() > 3 ? a[3].toInt() : 255);
}

int LayerEffects::margin() const {
    int m = 0;
    if (outline) m = std::max(m, int(std::ceil(outlineSize)) + 2);
    if (shadow)
        m = std::max(m, int(std::ceil(shadowBlur * 3 + std::max(std::fabs(shadowOffset.x()), std::fabs(shadowOffset.y())))) + 2);
    if (blur) m = std::max(m, int(std::ceil(blurRadius * 3)) + 2);
    if (innerShadow)
        m = std::max(m, int(std::ceil(innerShadowBlur * 3 + std::max(std::fabs(innerShadowOffset.x()), std::fabs(innerShadowOffset.y())))) + 2);
    if (outerGlow) m = std::max(m, int(std::ceil(outerGlowSize * 3)) + 2);
    if (innerGlow) m = std::max(m, int(std::ceil(innerGlowSize * 3)) + 2);
    if (chromatic) m = std::max(m, int(std::ceil(std::fabs(chromaShift))) + 2);
    return m;
}

QJsonObject LayerEffects::toJson() const {
    return QJsonObject{
        {"outline", outline}, {"outlineColor", colorJson(outlineColor)}, {"outlineSize", outlineSize},
        {"outlineOpacity", outlineOpacity}, {"shadow", shadow}, {"shadowColor", colorJson(shadowColor)},
        {"shadowOpacity", shadowOpacity}, {"shadowDx", shadowOffset.x()}, {"shadowDy", shadowOffset.y()},
        {"shadowBlur", shadowBlur}, {"blur", blur}, {"blurRadius", blurRadius}, {"overlay", overlay},
        {"overlayColor", colorJson(overlayColor)}, {"overlayOpacity", overlayOpacity}, {"texture", texture},
        {"textureName", textureName}, {"textureStrength", textureStrength}, {"textureScale", textureScale},
        {"innerShadow", innerShadow}, {"innerShadowColor", colorJson(innerShadowColor)}, {"innerShadowOpacity", innerShadowOpacity},
        {"innerShadowDx", innerShadowOffset.x()}, {"innerShadowDy", innerShadowOffset.y()}, {"innerShadowBlur", innerShadowBlur},
        {"outerGlow", outerGlow}, {"outerGlowColor", colorJson(outerGlowColor)}, {"outerGlowOpacity", outerGlowOpacity},
        {"outerGlowSize", outerGlowSize}, {"innerGlow", innerGlow}, {"innerGlowColor", colorJson(innerGlowColor)},
        {"innerGlowOpacity", innerGlowOpacity}, {"innerGlowSize", innerGlowSize}, {"gradientOverlay", gradientOverlay},
        {"gradientStart", colorJson(gradientStart)}, {"gradientEnd", colorJson(gradientEnd)}, {"gradientAngle", gradientAngle},
        {"gradientOpacity", gradientOpacity}, {"gradientBlend", gradientBlend}, {"grain", grain}, {"grainAmount", grainAmount},
        {"grainSize", grainSize}, {"grainMono", grainMono}, {"grainBlend", grainBlend}, {"grainSeed", grainSeed},
        {"screentone", screentone}, {"toneCell", toneCell}, {"toneAngle", toneAngle}, {"toneShape", toneShape},
        {"toneColor", colorJson(toneColor)}, {"toneKeepWhite", toneKeepWhite}, {"chromatic", chromatic},
        {"chromaShift", chromaShift}, {"chromaAngle", chromaAngle}};
}

LayerEffects LayerEffects::fromJson(const QJsonObject& o) {
    LayerEffects e;
    e.outline = o.value("outline").toBool();
    e.outlineColor = colorFrom(o.value("outlineColor"), e.outlineColor);
    e.outlineSize = float(o.value("outlineSize").toDouble(e.outlineSize));
    e.outlineOpacity = float(o.value("outlineOpacity").toDouble(e.outlineOpacity));
    e.shadow = o.value("shadow").toBool();
    e.shadowColor = colorFrom(o.value("shadowColor"), e.shadowColor);
    e.shadowOpacity = float(o.value("shadowOpacity").toDouble(e.shadowOpacity));
    e.shadowOffset = QPointF(o.value("shadowDx").toDouble(6), o.value("shadowDy").toDouble(6));
    e.shadowBlur = float(o.value("shadowBlur").toDouble(e.shadowBlur));
    e.blur = o.value("blur").toBool();
    e.blurRadius = float(o.value("blurRadius").toDouble(e.blurRadius));
    e.overlay = o.value("overlay").toBool();
    e.overlayColor = colorFrom(o.value("overlayColor"), e.overlayColor);
    e.overlayOpacity = float(o.value("overlayOpacity").toDouble(e.overlayOpacity));
    e.texture = o.value("texture").toBool();
    e.textureName = o.value("textureName").toString();
    e.textureStrength = float(o.value("textureStrength").toDouble(e.textureStrength));
    e.textureScale = float(o.value("textureScale").toDouble(e.textureScale));
    auto f = [&o](const char* k, float def) { return float(o.value(k).toDouble(def)); };
    e.innerShadow = o.value("innerShadow").toBool();
    e.innerShadowColor = colorFrom(o.value("innerShadowColor"), e.innerShadowColor);
    e.innerShadowOpacity = f("innerShadowOpacity", e.innerShadowOpacity);
    e.innerShadowOffset = QPointF(o.value("innerShadowDx").toDouble(4), o.value("innerShadowDy").toDouble(4));
    e.innerShadowBlur = f("innerShadowBlur", e.innerShadowBlur);
    e.outerGlow = o.value("outerGlow").toBool();
    e.outerGlowColor = colorFrom(o.value("outerGlowColor"), e.outerGlowColor);
    e.outerGlowOpacity = f("outerGlowOpacity", e.outerGlowOpacity);
    e.outerGlowSize = f("outerGlowSize", e.outerGlowSize);
    e.innerGlow = o.value("innerGlow").toBool();
    e.innerGlowColor = colorFrom(o.value("innerGlowColor"), e.innerGlowColor);
    e.innerGlowOpacity = f("innerGlowOpacity", e.innerGlowOpacity);
    e.innerGlowSize = f("innerGlowSize", e.innerGlowSize);
    e.gradientOverlay = o.value("gradientOverlay").toBool();
    e.gradientStart = colorFrom(o.value("gradientStart"), e.gradientStart);
    e.gradientEnd = colorFrom(o.value("gradientEnd"), e.gradientEnd);
    e.gradientAngle = f("gradientAngle", e.gradientAngle);
    e.gradientOpacity = f("gradientOpacity", e.gradientOpacity);
    e.gradientBlend = o.value("gradientBlend").toInt(e.gradientBlend);
    e.grain = o.value("grain").toBool();
    e.grainAmount = f("grainAmount", e.grainAmount);
    e.grainSize = f("grainSize", e.grainSize);
    e.grainMono = o.value("grainMono").toBool(e.grainMono);
    e.grainBlend = o.value("grainBlend").toInt(e.grainBlend);
    e.grainSeed = o.value("grainSeed").toInt(e.grainSeed);
    e.screentone = o.value("screentone").toBool();
    e.toneCell = f("toneCell", e.toneCell);
    e.toneAngle = f("toneAngle", e.toneAngle);
    e.toneShape = o.value("toneShape").toInt(e.toneShape);
    e.toneColor = colorFrom(o.value("toneColor"), e.toneColor);
    e.toneKeepWhite = o.value("toneKeepWhite").toBool(e.toneKeepWhite);
    e.chromatic = o.value("chromatic").toBool();
    e.chromaShift = f("chromaShift", e.chromaShift);
    e.chromaAngle = f("chromaAngle", e.chromaAngle);
    return e;
}

bool LayerEffects::operator==(const LayerEffects& o) const { return toJson() == o.toJson(); }

QJsonObject TextParams::toJson() const {
    return QJsonObject{{"text", text}, {"family", family}, {"size", sizePx}, {"bold", bold}, {"italic", italic},
                       {"underline", underline}, {"color", colorJson(color)}, {"align", align},
                       {"lineSpacing", lineSpacing}, {"letterSpacing", letterSpacing}, {"vertical", vertical},
                       {"x", origin.x()}, {"y", origin.y()}, {"rotation", rotation}, {"scale", scale}};
}

TextParams TextParams::fromJson(const QJsonObject& o) {
    TextParams t;
    t.text = o.value("text").toString(t.text);
    t.family = o.value("family").toString();
    t.sizePx = o.value("size").toDouble(t.sizePx);
    t.bold = o.value("bold").toBool();
    t.italic = o.value("italic").toBool();
    t.underline = o.value("underline").toBool();
    t.color = colorFrom(o.value("color"), t.color);
    t.align = o.value("align").toInt();
    t.lineSpacing = o.value("lineSpacing").toDouble(1.0);
    t.letterSpacing = o.value("letterSpacing").toDouble(0.0);
    t.vertical = o.value("vertical").toBool();
    t.origin = QPointF(o.value("x").toDouble(), o.value("y").toDouble());
    t.rotation = o.value("rotation").toDouble();
    t.scale = o.value("scale").toDouble(1.0);
    return t;
}

QFont TextParams::font() const {
    QFont f(family.isEmpty() ? QFont().family() : family);
    f.setPixelSize(std::max(1, int(std::round(sizePx))));
    f.setBold(bold);
    f.setItalic(italic);
    f.setUnderline(underline);
    if (letterSpacing != 0) f.setLetterSpacing(QFont::PercentageSpacing, 100.0 + letterSpacing);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QRectF VectorStroke::bounds() const {
    if (points.isEmpty()) return {};
    qreal x0 = points[0].pos.x(), x1 = x0, y0 = points[0].pos.y(), y1 = y0;
    for (const auto& p : points) {
        x0 = std::min(x0, p.pos.x()); x1 = std::max(x1, p.pos.x());
        y0 = std::min(y0, p.pos.y()); y1 = std::max(y1, p.pos.y());
    }
    qreal r = width * 0.5 + 2;
    return QRectF(QPointF(x0 - r, y0 - r), QPointF(x1 + r, y1 + r));
}

QJsonObject VectorStroke::toJson() const {
    QJsonArray pts;
    for (const auto& p : points) pts.append(QJsonArray{p.pos.x(), p.pos.y(), p.pressure});
    return QJsonObject{{"id", int(id)}, {"points", pts}, {"width", width}, {"minWidth", minWidth},
                       {"hardness", hardness}, {"opacity", opacity}, {"color", colorJson(color)}};
}

VectorStroke VectorStroke::fromJson(const QJsonObject& o) {
    VectorStroke s;
    s.id = quint32(o.value("id").toInt());
    for (const auto& v : o.value("points").toArray()) {
        QJsonArray a = v.toArray();
        if (a.size() >= 3) s.points.push_back({QPointF(a[0].toDouble(), a[1].toDouble()), float(a[2].toDouble())});
    }
    s.width = float(o.value("width").toDouble(4));
    s.minWidth = float(o.value("minWidth").toDouble(0.15));
    s.hardness = float(o.value("hardness").toDouble(0.85));
    s.opacity = float(o.value("opacity").toDouble(1));
    s.color = colorFrom(o.value("color"), Qt::black);
    return s;
}

bool Layer::isEffectivelyVisible() const {
    for (const Layer* l = this; l; l = l->parent)
        if (!l->visible) return false;
    return true;
}

bool Layer::isEffectivelyLocked() const {
    for (const Layer* l = this; l; l = l->parent)
        if (l->locked) return true;
    return false;
}

int Layer::depth() const {
    int d = 0;
    for (const Layer* l = parent; l && l->parent; l = l->parent) ++d;
    return d;
}

void Layer::copyBaseTo(Layer& o) const {
    o.id = id;
    o.name = name;
    o.visible = visible;
    o.opacity = opacity;
    o.blend = blend;
    o.clipping = clipping;
    o.locked = locked;
    o.alphaLock = alphaLock;
    o.labelColor = labelColor;
    o.reference = reference;
    o.hideInTimelapse = hideInTimelapse;
    o.fx = fx;
    o.hasMask = hasMask;
    o.maskEnabled = maskEnabled;
    o.mask = mask;
    o.version = version;
    // runtime state is intentionally not copied
}

std::shared_ptr<Layer> RasterLayer::clone() const {
    auto l = std::make_shared<RasterLayer>();
    copyBaseTo(*l);
    l->pixels = pixels;  // previews are runtime state
    l->generator = generator;
    l->generatorVersion = generatorVersion;
    return l;
}

std::shared_ptr<Layer> VectorLayer::clone() const {
    auto l = std::make_shared<VectorLayer>();
    copyBaseTo(*l);
    l->strokes = strokes;
    l->nextStrokeId = nextStrokeId;
    l->cache = cache;
    return l;
}

VectorStroke* VectorLayer::strokeById(quint32 sid) {
    for (auto& s : strokes)
        if (s.id == sid) return &s;
    return nullptr;
}

std::shared_ptr<Layer> TextLayer::clone() const {
    auto l = std::make_shared<TextLayer>();
    copyBaseTo(*l);
    l->text = text;
    l->cache = cache;
    return l;
}

std::shared_ptr<Layer> AdjustmentLayer::clone() const {
    auto l = std::make_shared<AdjustmentLayer>();
    copyBaseTo(*l);
    l->adjustment = adjustment;
    return l;
}

std::shared_ptr<Layer> GroupLayer::clone() const {
    auto g = std::make_shared<GroupLayer>();
    copyBaseTo(*g);
    g->expanded = expanded;
    g->passThrough = passThrough;
    g->children.reserve(children.size());
    for (const auto& c : children) {
        auto cc = c->clone();
        cc->parent = g.get();
        g->children.push_back(cc);
    }
    return g;
}

int GroupLayer::indexOf(const Layer* l) const {
    for (size_t i = 0; i < children.size(); ++i)
        if (children[i].get() == l) return int(i);
    return -1;
}

void GroupLayer::insert(int index, std::shared_ptr<Layer> l) {
    index = std::clamp(index, 0, int(children.size()));
    l->parent = this;
    children.insert(children.begin() + index, std::move(l));
}

std::shared_ptr<Layer> GroupLayer::take(int index) {
    if (index < 0 || index >= int(children.size())) return nullptr;
    auto l = children[size_t(index)];
    children.erase(children.begin() + index);
    l->parent = nullptr;
    return l;
}

bool forEachLayer(const GroupLayer* g, const std::function<bool(Layer*)>& fn) {
    if (!g) return true;
    for (const auto& c : g->children) {
        if (!fn(c.get())) return false;
        if (c->isGroup() && !forEachLayer(static_cast<GroupLayer*>(c.get()), fn)) return false;
    }
    return true;
}

Layer* findLayer(const GroupLayer* root, quint32 id) {
    Layer* found = nullptr;
    forEachLayer(root, [&](Layer* l) {
        if (l->id == id) { found = l; return false; }
        return true;
    });
    return found;
}

static void collectTopToBottom(const GroupLayer* g, bool onlyExpanded, QList<Layer*>& out) {
    for (auto it = g->children.rbegin(); it != g->children.rend(); ++it) {
        out << it->get();
        if ((*it)->isGroup()) {
            auto* cg = static_cast<GroupLayer*>(it->get());
            if (!onlyExpanded || cg->expanded) collectTopToBottom(cg, onlyExpanded, out);
        }
    }
}

QList<Layer*> layersTopToBottom(const GroupLayer* root, bool onlyExpanded) {
    QList<Layer*> out;
    if (root) collectTopToBottom(root, onlyExpanded, out);
    return out;
}

bool isAncestor(const Layer* a, const Layer* l) {
    for (const Layer* p = l ? l->parent : nullptr; p; p = p->parent)
        if (p == a) return true;
    return false;
}

QString layerTypeName(LayerType t) {
    switch (t) {
    case LayerType::Raster: return "Layer";
    case LayerType::Group: return "Folder";
    case LayerType::Vector: return "Linework";
    case LayerType::Text: return "Text";
    case LayerType::Adjustment: return "Adjustment";
    }
    return "Layer";
}

}  // namespace gx
