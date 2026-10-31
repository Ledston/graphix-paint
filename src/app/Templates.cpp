#include "app/Templates.h"

#include <QDir>
#include <functional>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"
#include "io/NativeFormat.h"

namespace gx::templates {

QString folder() {
    const QString f = app() ? app()->dataDir() + "/templates" : QDir::tempPath() + "/graphix-templates";
    QDir().mkpath(f);
    return f;
}

QList<Info> all() {
    QList<Info> v = {
        {"plain", "Plain: paper and one layer", {}, 0},
        {"illustration", "Illustration: sketch, line art, flats, shading, light, background", {}, 0},
        {"comic", "Comic page: panel frames, line art, tones", QSize(2894, 4093), 350},
        {"character", "Character sheet: front, side and back with height lines", QSize(4200, 2600), 350},
        {"pattern", "Seamless pattern (seamless mode on)", QSize(1024, 1024), 350},
    };
    for (const QString& f : QDir(folder()).entryList({"*.gxp"}, QDir::Files, QDir::Name))
        v << Info{"user:" + f, QFileInfo(f).completeBaseName(), {}, 0};
    return v;
}

namespace {
std::shared_ptr<RasterLayer> raster(Document& d, const QString& name) {
    auto l = std::make_shared<RasterLayer>();
    l->id = d.newLayerId();
    l->name = T(name);
    return l;
}
std::shared_ptr<GroupLayer> folderLayer(Document& d, const QString& name) {
    auto g = std::make_shared<GroupLayer>();
    g->id = d.newLayerId();
    g->name = T(name);
    g->expanded = true;
    return g;
}
// lines drawn with QPainter onto a layer
void drawOn(RasterLayer& l, const QSize& size, const std::function<void(QPainter&)>& fn) {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        fn(p);
    }
    imageToSurface(img, l.pixels, QPoint(0, 0));
    l.pixels.optimizeRect(QRect(QPoint(0, 0), size));
}
}  // namespace

std::shared_ptr<Document> make(const QString& id, QSize size, double dpi, const QColor& background, QString* error) {
    if (id.startsWith("user:")) {
        auto doc = loadNative(folder() + "/" + id.mid(5), error);
        if (!doc) return nullptr;
        doc->setFilePath({});
        doc->setModified(false);
        return std::shared_ptr<Document>(doc.release());
    }
    auto doc = std::make_shared<Document>(size);
    doc->setDpi(dpi);
    Document& d = *doc;
    GroupLayer* root = d.root();
    const QRect b = d.bounds();
    const int W = size.width(), H = size.height();
    auto add = [&](LayerPtr l) { root->insert(int(root->children.size()), l); };
    if (background.alpha() > 0) {
        auto paper = raster(d, "Paper");
        paper->pixels.fillRect(b, premulFromColor(background));
        add(paper);
    }
    Layer* current = nullptr;
    if (id == "illustration") {
        auto bgL = raster(d, "Background");
        auto flats = raster(d, "Flats");
        auto shade = raster(d, "Shading");
        shade->blend = BlendMode::Multiply;
        shade->clipping = true;
        auto light = raster(d, "Light");
        light->blend = BlendMode::Shine;
        light->clipping = true;
        auto lines = raster(d, "Line art");
        lines->reference = true;  // the fill tools take their borders from it
        auto sketch = raster(d, "Sketch");
        sketch->opacity = 0.35f;
        for (auto l : std::initializer_list<LayerPtr>{bgL, flats, shade, light, lines, sketch}) add(l);
        current = sketch.get();
    } else if (id == "comic") {
        auto tones = raster(d, "Tones");
        auto lines = raster(d, "Line art");
        lines->reference = true;
        auto frames = raster(d, "Frames");
        frames->reference = true;
        // three tiers of panels inside the margins, the middle one split in two
        drawOn(*frames, size, [&](QPainter& p) {
            const double m = std::min(W, H) * 0.07, gut = std::min(W, H) * 0.025, pen = std::max(3.0, std::min(W, H) / 400.0);
            p.setPen(QPen(Qt::black, pen, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            p.setBrush(Qt::NoBrush);
            const QRectF area(m, m, W - 2 * m, H - 2 * m);
            const double rowH = (area.height() - 2 * gut) / 3;
            for (int r = 0; r < 3; ++r) {
                const double y = area.top() + r * (rowH + gut);
                if (r == 1) {
                    const double w2 = (area.width() - gut) / 2;
                    p.drawRect(QRectF(area.left(), y, w2, rowH));
                    p.drawRect(QRectF(area.left() + w2 + gut, y, w2, rowH));
                } else {
                    p.drawRect(QRectF(area.left(), y, area.width(), rowH));
                }
            }
        });
        auto sketch = raster(d, "Sketch");
        sketch->opacity = 0.35f;
        for (auto l : std::initializer_list<LayerPtr>{tones, lines, frames, sketch}) add(l);
        current = sketch.get();
    } else if (id == "character") {
        auto guides = raster(d, "Guides");
        guides->opacity = 0.6f;
        guides->hideInTimelapse = true;
        drawOn(*guides, size, [&](QPainter& p) {
            const QColor c(70, 140, 255);
            const double pen = std::max(2.0, std::min(W, H) / 800.0);
            p.setPen(QPen(c, pen, Qt::DashLine));
            // height lines: top of the head, chin, chest, waist, hips, knees, feet
            const double top = H * 0.08, feet = H * 0.92;
            for (double t : {0.0, 0.125, 0.25, 0.375, 0.5, 0.75, 1.0}) p.drawLine(QPointF(W * 0.03, top + (feet - top) * t), QPointF(W * 0.97, top + (feet - top) * t));
            // three columns: front, side, back
            p.setPen(QPen(c, pen * 1.5));
            for (int i = 1; i < 3; ++i) p.drawLine(QPointF(W * i / 3.0, H * 0.03), QPointF(W * i / 3.0, H * 0.97));
            QFont f = p.font();
            f.setPixelSize(int(std::max(16.0, H * 0.03)));
            p.setFont(f);
            p.setPen(c);
            const QStringList names = {T("Front"), T("Side"), T("Back")};
            for (int i = 0; i < 3; ++i) p.drawText(QRectF(W * i / 3.0, H * 0.005, W / 3.0, H * 0.06), Qt::AlignCenter, names[i]);
        });
        auto flats = raster(d, "Colors");
        auto lines = raster(d, "Line art");
        lines->reference = true;
        auto sketch = raster(d, "Sketch");
        sketch->opacity = 0.35f;
        for (auto l : std::initializer_list<LayerPtr>{guides, flats, lines, sketch}) add(l);
        current = sketch.get();
    } else {
        auto l = raster(d, "Layer 1");
        add(l);
        current = l.get();
        if (id == "pattern") d.seamless = true;
    }
    if (!current && !root->children.empty()) current = root->children.back().get();
    if (current) d.setCurrentLayer(current->id);
    d.setModified(false);
    return doc;
}

bool saveAsTemplate(const Document& doc, const QString& name, QString* error) {
    QString base = name.trimmed().isEmpty() ? QStringLiteral("Template") : name.trimmed();
    base.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    return saveNative(takeSnapshot(doc), folder() + "/" + base + ".gxp", error);
}

}  // namespace gx::templates
