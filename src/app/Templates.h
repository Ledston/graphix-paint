#pragma once
#include <QColor>
#include <QSize>
#include <QStringList>
#include <memory>

namespace gx {

class Document;

// File > New: canvas templates — ready layer sets (illustration, comic page, character
// sheet, seamless pattern) and the person's own templates (File > Save as template).
namespace templates {

struct Info {
    QString id;      // "plain", "illustration", "comic", "character", "pattern", or "user:<file>"
    QString name;    // English for the built-in ones (T()), the file name for the person's own
    QSize size;      // suggested size (empty = keep what is chosen)
    double dpi = 0;  // suggested resolution (0 = keep)
};

QList<Info> all();
QString folder();  // the person's own templates (.gxp)
// A new document from a template. size/dpi/background: as chosen in the New window
// (the person's own templates keep their own size).
std::shared_ptr<Document> make(const QString& id, QSize size, double dpi, const QColor& background, QString* error = nullptr);
bool saveAsTemplate(const Document& doc, const QString& name, QString* error = nullptr);

}  // namespace templates
}  // namespace gx
