#pragma once
#include <QString>
#include <memory>

#include "core/Document.h"

namespace gx {

// Photoshop PSD / PSB with layers, folders, blend modes, opacity, visibility,
// clipping, transparency lock and layer masks. Canvases larger than 30000 px
// are written as PSB automatically.
bool savePsd(Document& doc, const QString& path, QString* error, QStringList* warnings = nullptr);
std::unique_ptr<Document> loadPsd(const QString& path, QString* error, QStringList* warnings = nullptr);

}  // namespace gx
