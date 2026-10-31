#pragma once
#include <QImage>
#include <QString>
#include <memory>

#include "core/Document.h"

namespace gx {

// Everything needed to write a document, detached from the live Document so it
// can be written from a background thread (autosave).
struct DocSnapshot {
    DocState state;
    double dpi = 350;
    Perspective perspective;
    Symmetry symmetry;
    Ruler ruler;
    bool seamless = false;
};
DocSnapshot takeSnapshot(const Document& doc);

// GraphiX native format (.gxp): keeps every layer type, effects, masks,
// vector strokes, text, adjustments, rulers and the selection.
bool saveNative(const DocSnapshot& snap, const QString& path, QString* error, const QImage& thumbnail = {});
std::unique_ptr<Document> loadNative(const QString& path, QString* error);

}  // namespace gx
