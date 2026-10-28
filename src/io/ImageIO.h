#pragma once
#include <QImage>
#include <QString>
#include <QStringList>
#include <memory>

#include "core/Document.h"

namespace gx {
namespace io {

QString openFilter();
QString saveFilter();
QString exportFilter();
bool canKeepLayers(const QString& path);  // gxp / psd / psb

std::unique_ptr<Document> openFile(const QString& path, QString* error, QStringList* warnings = nullptr);
bool saveFile(Document& doc, const QString& path, QString* error, QStringList* warnings = nullptr, int quality = 92);

QImage flatten(Document& doc, bool opaque = false, const QColor& background = Qt::white);
std::unique_ptr<Document> documentFromImage(const QImage& img, const QString& layerName);
QImage readImage(const QString& path, QString* error);

}  // namespace io
}  // namespace gx
