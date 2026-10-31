#include "app/MaterialActions.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>

#include "app/I18n.h"
#include "app/Materials.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerOps.h"
#include "io/ImageIO.h"
#include "tools/FillTools.h"

namespace gx::materials {

namespace {
void fail(QString* error, const QString& msg) {
    if (error) *error = msg;
}

// The area a fill covers: the selection, or the whole picture.
QRect fillArea(const Document& d) {
    QRect r = d.hasSelection() ? d.selectionBounds() : d.bounds();
    return r & d.bounds();
}

RasterLayer* paintable(Document& d, QString* error) {
    Layer* l = d.currentLayer();
    if (!l || !l->isRaster()) {
        fail(error, T("Select a pixel layer to fill."));
        return nullptr;
    }
    if (l->isEffectivelyLocked()) {
        fail(error, T("The layer is locked."));
        return nullptr;
    }
    return static_cast<RasterLayer*>(l);
}
}  // namespace

Layer* placeImage(Document& d, const QString& file, std::optional<QPointF> center, QString* error) {
    QImage img = io::readImage(file, error);
    if (img.isNull()) {
        if (error && error->isEmpty()) *error = T("Could not open the picture.");
        return nullptr;
    }
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QPointF c = center.value_or(QPointF(d.size().width() / 2.0, d.size().height() / 2.0));
    auto l = std::make_shared<RasterLayer>();
    l->name = QFileInfo(file).completeBaseName();
    imageToSurface(img, l->pixels, QPoint(int(std::lround(c.x() - img.width() / 2.0)), int(std::lround(c.y() - img.height() / 2.0))));
    return ops::insertLayer(d, l, nullptr, true, "Place picture");
}

Layer* placePattern(Document& d, const QString& name, const QColor& ink, QString* error) {
    Materials& m = Materials::instance();
    const QImage tile = m.pattern(name);
    if (tile.isNull()) {
        fail(error, T("The pattern was not found."));
        return nullptr;
    }
    const QRect area = fillArea(d);
    if (area.isEmpty()) {
        fail(error, T("The selection is outside the picture."));
        return nullptr;
    }
    m.setCurrentPattern(name);
    auto l = std::make_shared<RasterLayer>();
    l->name = T(name);
    // the layer is not in the document yet, so build its pixels directly
    const int W = area.width(), H = area.height();
    std::vector<float> px(size_t(W) * H * 4, 0.f), sel;
    if (d.hasSelection()) {
        sel.resize(size_t(W) * H);
        readMask(d.selection(), area, sel.data());
    }
    const double scale = std::max(0.05, m.patternScale);
    const float c[3] = {float(ink.redF()), float(ink.greenF()), float(ink.blueF())};
    for (int y = 0; y < H; ++y) {
        int ty = int(std::floor((area.top() + y) / scale)) % tile.height();
        if (ty < 0) ty += tile.height();
        const QRgb* row = reinterpret_cast<const QRgb*>(tile.constScanLine(ty));
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * W + x;
            int tx = int(std::floor((area.left() + x) / scale)) % tile.width();
            if (tx < 0) tx += tile.width();
            const QRgb t = row[tx];
            const float a = qAlpha(t) / 255.f * (sel.empty() ? 1.f : sel[i]);
            float* p = &px[i * 4];
            if (m.patternTint) p[0] = c[0] * a, p[1] = c[1] * a, p[2] = c[2] * a;
            else p[0] = qRed(t) / 255.f * a, p[1] = qGreen(t) / 255.f * a, p[2] = qBlue(t) / 255.f * a;
            p[3] = a;
        }
    }
    writeFloat(l->pixels, area, px.data(), true);
    l->pixels.optimizeRect(area);
    return ops::insertLayer(d, l, nullptr, true, "Place pattern");
}

Layer* placeTone(Document& d, int index, const QColor& ink, QString* error) {
    const QList<TonePreset> tones = Materials::tones();
    if (index < 0 || index >= tones.size()) {
        fail(error, T("The tone was not found."));
        return nullptr;
    }
    if (fillArea(d).isEmpty()) {
        fail(error, T("The selection is outside the picture."));
        return nullptr;
    }
    auto l = Materials::makeToneLayer(d, tones[index], ink);
    l->name = T(tones[index].name);
    return ops::insertLayer(d, l, nullptr, true, "Place screentone");
}

Layer* placeGradient(Document& d, int index, QString* error) {
    const QList<Gradient> grads = Materials::instance().gradients();
    if (index < 0 || index >= grads.size()) {
        fail(error, T("The gradient map was not found."));
        return nullptr;
    }
    Adjustment a = Adjustment::defaults(AdjustmentType::GradientMap);
    a.params["gradient"] = grads[index].toJson();
    a.params["reverse"] = false;
    auto l = std::make_shared<AdjustmentLayer>();
    l->adjustment = a;
    l->name = d.uniqueLayerName(T(grads[index].name));
    return ops::insertLayer(d, l, nullptr, true, "New adjustment layer");
}

bool fillWithPattern(Document& d, const QColor& ink, QString* error) {
    RasterLayer* l = paintable(d, error);
    if (!l) return false;
    Materials& m = Materials::instance();
    if (m.pattern(m.currentPattern()).isNull()) {
        fail(error, T("Choose a pattern first: Window > Materials > Patterns."));
        return false;
    }
    const QRect area = fillArea(d);
    if (area.isEmpty()) {
        fail(error, T("The selection is outside the picture."));
        return false;
    }
    MaskSurface mask(0);
    mask.fillRect(area, 255);
    paintThroughMask(&d, l, mask, ink, 1.0, false, "Fill with pattern", true);
    return true;
}

bool strokeSelection(Document& d, double width, StrokePosition pos, const QColor& color, bool pattern, QString* error) {
    if (!d.hasSelection()) {
        fail(error, T("Select an area first."));
        return false;
    }
    RasterLayer* l = paintable(d, error);
    if (!l) return false;
    Materials& m = Materials::instance();
    if (pattern && m.pattern(m.currentPattern()).isNull()) {
        fail(error, T("Choose a pattern first: Window > Materials > Patterns."));
        return false;
    }
    width = std::clamp(width, 0.5, 500.0);
    const QRect sb = d.selectionBounds();
    if (sb.isEmpty()) {
        fail(error, T("Select an area first."));
        return false;
    }
    const int pad = int(std::ceil(width)) + 2;
    const QRect r = sb.adjusted(-pad, -pad, pad, pad);
    const int W = r.width(), H = r.height();
    const size_t n = size_t(W) * H;
    std::vector<float> sel(n);
    readMask(d.selection(), r, sel.data());
    std::vector<uint8_t> inside(n), outside(n);
    for (size_t i = 0; i < n; ++i) {
        inside[i] = sel[i] >= 0.5f;
        outside[i] = !inside[i];
    }
    // distance from each outside pixel to the selection, and from each inside pixel to its edge
    std::vector<float> toInside, toOutside;
    distanceTransform(inside, W, H, toInside);
    distanceTransform(outside, W, H, toOutside);
    double wIn = 0, wOut = 0;
    switch (pos) {
    case StrokePosition::Inside: wIn = width; break;
    case StrokePosition::Outside: wOut = width; break;
    case StrokePosition::Center: wIn = wOut = width / 2; break;
    }
    std::vector<float> cov(n, 0.f);
    bool any = false;
    for (size_t i = 0; i < n; ++i) {
        // a pixel next to the edge is at distance 1; soft by one pixel at the far side
        const double dist = std::sqrt(double(inside[i] ? toOutside[i] : toInside[i]));
        const double w = inside[i] ? wIn : wOut;
        const float c = float(std::clamp(w + 1.0 - dist, 0.0, 1.0));
        cov[i] = c;
        any |= c > 0;
    }
    if (!any) {
        fail(error, T("The stroke is too thin."));
        return false;
    }
    MaskSurface mask(0);
    writeMask(mask, r, cov.data());
    paintThroughMask(&d, l, mask, color, 1.0, false, "Stroke selection", pattern, false);
    return true;
}

QString saveSelection(Document& d, bool asPattern, const QString& name, QString* error) {
    Layer* cur = d.currentLayer();
    if (!cur || !cur->isRaster()) {
        fail(error, T("Select a pixel layer."));
        return {};
    }
    auto* l = static_cast<RasterLayer*>(cur);
    QRect r = d.hasSelection() ? d.selectionBounds() : l->pixels.exactBounds();
    r &= d.bounds();
    if (r.isEmpty()) {
        fail(error, T("There is nothing to save here."));
        return {};
    }
    const size_t n = size_t(r.width()) * r.height();
    std::vector<float> px(n * 4);
    readFloat(l->pixels, r, px.data());
    if (d.hasSelection()) {
        std::vector<float> sel(n);
        readMask(d.selection(), r, sel.data());
        for (size_t i = 0; i < n; ++i)
            for (int k = 0; k < 4; ++k) px[i * 4 + k] *= sel[i];
    }
    RasterSurface tmp;
    writeFloat(tmp, r, px.data());
    QImage img = surfaceToImage(tmp, r);
    if (img.isNull()) {
        fail(error, T("There is nothing to save here."));
        return {};
    }
    // crop to what is really there
    QRect used;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(row[x])) used |= QRect(x, y, 1, 1);
    }
    if (used.isEmpty()) {
        fail(error, T("There is nothing to save here."));
        return {};
    }
    // patterns keep the exact selection size so they repeat as framed
    if (!asPattern) img = img.copy(used);
    Materials& m = Materials::instance();
    QString base = name.trimmed().isEmpty() ? (asPattern ? QStringLiteral("Pattern") : QStringLiteral("Picture")) : name.trimmed();
    base.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    if (!asPattern) return QFileInfo(m.addImage(img, base)).completeBaseName();
    QString file = m.patternsFolder() + "/" + base + ".png";
    for (int i = 2; QFileInfo::exists(file); ++i) file = m.patternsFolder() + "/" + base + " " + QString::number(i) + ".png";
    if (!img.convertToFormat(QImage::Format_ARGB32).save(file)) {
        fail(error, T("Could not save the file."));
        return {};
    }
    const QString saved = QFileInfo(file).completeBaseName();
    Q_EMIT m.changed();
    m.setCurrentPattern(saved);
    return saved;
}

bool saveLayerGradient(Document& d, const QString& name, QString* error) {
    auto* l = dynamic_cast<AdjustmentLayer*>(d.currentLayer());
    if (!l || l->adjustment.type != AdjustmentType::GradientMap) {
        fail(error, T("Select a gradient map layer first."));
        return false;
    }
    Gradient g = Gradient::fromJson(l->adjustment.params.value("gradient").toObject());
    g.name = name.trimmed().isEmpty() ? l->name : name.trimmed();
    Materials::instance().saveGradient(g);
    return true;
}

QString savePictureAsPattern(Document& d, const QString& name, QString* error) {
    const RasterSurface all = ops::renderLayers(d, d.root()->children, d.bounds());
    const QImage img = surfaceToImage(all, d.bounds());
    if (img.isNull()) {
        fail(error, T("There is nothing to save here."));
        return {};
    }
    Materials& m = Materials::instance();
    QString base = name.trimmed().isEmpty() ? QStringLiteral("Pattern") : name.trimmed();
    base.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString file = m.patternsFolder() + "/" + base + ".png";
    for (int i = 2; QFileInfo::exists(file); ++i) file = m.patternsFolder() + "/" + base + " " + QString::number(i) + ".png";
    if (!img.convertToFormat(QImage::Format_ARGB32).save(file)) {
        fail(error, T("Could not save the file."));
        return {};
    }
    const QString saved = QFileInfo(file).completeBaseName();
    Q_EMIT m.changed();
    m.setCurrentPattern(saved);
    return saved;
}

}  // namespace gx::materials
