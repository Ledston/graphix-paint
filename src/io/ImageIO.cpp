#include "io/ImageIO.h"

#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>

#include "core/Compositor.h"
#include "core/ImageOps.h"
#include "io/NativeFormat.h"
#include "io/PsdFormat.h"

namespace gx {
namespace io {

namespace {
QString ext(const QString& path) { return QFileInfo(path).suffix().toLower(); }

QStringList readable() {
    QStringList l;
    for (const QByteArray& f : QImageReader::supportedImageFormats()) l << QString::fromLatin1(f).toLower();
    return l;
}
bool canWrite(const QString& e) {
    for (const QByteArray& f : QImageWriter::supportedImageFormats())
        if (QString::fromLatin1(f).toLower() == e) return true;
    return false;
}
}  // namespace

QString openFilter() {
    QStringList r = readable();
    QStringList pats = {"*.gxp", "*.psd", "*.psb"};
    for (const QString& e : {"png", "jpg", "jpeg", "bmp", "webp", "tif", "tiff", "gif", "tga"})
        if (r.contains(e)) pats << "*." + e;
    QStringList f;
    f << QStringLiteral("All supported (%1)").arg(pats.join(' '));
    f << "GraphiX (*.gxp)" << "Photoshop (*.psd *.psb)";
    f << "PNG (*.png)" << "JPEG (*.jpg *.jpeg)" << "BMP (*.bmp)";
    if (r.contains("webp")) f << "WebP (*.webp)";
    if (r.contains("tiff")) f << "TIFF (*.tif *.tiff)";
    f << "All files (*)";
    return f.join(";;");
}

QString saveFilter() {
    QStringList f;
    f << "GraphiX - all layers and features (*.gxp)" << "Photoshop (*.psd)";
    f << "PNG (*.png)" << "JPEG (*.jpg)";
    if (canWrite("webp")) f << "WebP (*.webp)";
    if (canWrite("tiff")) f << "TIFF (*.tif)";
    f << "BMP (*.bmp)";
    return f.join(";;");
}

QString exportFilter() {
    QStringList f;
    f << "PNG (*.png)" << "JPEG (*.jpg)" << "Photoshop (*.psd)";
    if (canWrite("webp")) f << "WebP (*.webp)";
    if (canWrite("tiff")) f << "TIFF (*.tif)";
    f << "BMP (*.bmp)";
    return f.join(";;");
}

bool canKeepLayers(const QString& path) {
    QString e = ext(path);
    return e == "gxp" || e == "psd" || e == "psb";
}

QImage readImage(const QString& path, QString* error) {
    QImageReader r(path);
    r.setAutoTransform(true);
    r.setAllocationLimit(4096);  // MB
    QImage img = r.read();
    if (img.isNull() && error) *error = r.errorString();
    return img;
}

std::unique_ptr<Document> documentFromImage(const QImage& img, const QString& layerName) {
    auto doc = std::make_unique<Document>(img.size());
    auto l = std::make_shared<RasterLayer>();
    l->id = doc->newLayerId();
    l->name = layerName;
    imageToSurface(img, l->pixels, QPoint(0, 0));
    doc->root()->insert(0, l);
    doc->setCurrentLayer(l->id);
    return doc;
}

std::unique_ptr<Document> openFile(const QString& path, QString* error, QStringList* warnings) {
    QString e = ext(path);
    std::unique_ptr<Document> doc;
    if (e == "gxp") doc = loadNative(path, error);
    else if (e == "psd" || e == "psb") doc = loadPsd(path, error, warnings);
    else {
        QImage img = readImage(path, error);
        if (img.isNull()) return nullptr;
        doc = documentFromImage(img, QFileInfo(path).completeBaseName());
        doc->setDpi(img.dotsPerMeterX() > 0 ? img.dotsPerMeterX() * 0.0254 : 72);
        doc->setFilePath(path);
        doc->setModified(false);
    }
    return doc;
}

QImage flatten(Document& doc, bool opaque, const QColor& bg) {
    QImage img = Compositor::renderImage(doc.root(), doc.bounds());
    if (img.isNull() || !opaque) return img;
    QImage out(img.size(), QImage::Format_RGB32);
    out.fill(bg);
    QPainter p(&out);
    p.drawImage(0, 0, img);
    return out;
}

bool saveFile(Document& doc, const QString& path, QString* error, QStringList* warnings, int quality) {
    QString e = ext(path);
    if (e == "gxp") {
        QImage thumb;
        doc.projection()->thumbnail(256, 50, thumb);
        return saveNative(takeSnapshot(doc), path, error, thumb);
    }
    if (e == "psd" || e == "psb") return savePsd(doc, path, error, warnings);
    const bool opaque = e == "jpg" || e == "jpeg" || e == "bmp";
    QImage img = flatten(doc, opaque);
    if (img.isNull()) {
        if (error) *error = "Not enough memory to flatten the image.";
        return false;
    }
    if (!opaque) img = img.convertToFormat(QImage::Format_ARGB32);
    img.setDotsPerMeterX(int(doc.dpi() / 0.0254));
    img.setDotsPerMeterY(int(doc.dpi() / 0.0254));
    QImageWriter w(path);
    if (e == "jpg" || e == "jpeg" || e == "webp") w.setQuality(quality);
    if (e == "png") w.setCompression(6);
    if (!w.write(img)) {
        if (error) *error = w.errorString();
        return false;
    }
    if (warnings && doc.root()->children.size() > 1)
        *warnings << "This format keeps only the flattened image. Save as .gxp or .psd to keep the layers.";
    return true;
}

}  // namespace io
}  // namespace gx
