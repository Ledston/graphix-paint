#pragma once
#include <QHash>
#include <QImage>
#include <QObject>
#include <QStringList>

#include "core/Gradient.h"
#include "core/Layer.h"

namespace gx {

class Document;

// Ready-made screentone: a gray fill shown through the screentone effect.
struct TonePreset {
    QString name;        // English, shown through T()
    double gray = 0.7;   // 1 = white (no dots) .. 0 = black
    bool gradient = false;  // from dark at the top to light at the bottom
    float cell = 8.f;
    float angle = 45.f;
    int shape = 0;       // 0 dots, 1 lines, 2 squares
};

// The materials panel (Window > Materials): the user's pictures, tiling patterns,
// screentones and gradient map sets. Kept in the data folder:
//   materials/images     pictures (also parts of drawings saved from the canvas)
//   materials/patterns   tiles for "fill with pattern"
//   materials/gradients.json   gradient maps the user saved
class Materials : public QObject {
    Q_OBJECT
public:
    static Materials& instance();
    void setRoot(const QString& dir);
    QString root() const { return m_root; }
    QString imagesFolder() const { return m_root + "/images"; }
    QString patternsFolder() const { return m_root + "/patterns"; }

    QStringList images() const;    // file paths
    QStringList patterns() const;  // names
    QImage pattern(const QString& name) const;
    QString addImage(const QImage& img, const QString& name);  // returns the file
    bool removeFile(const QString& path);

    static QList<TonePreset> tones();
    QList<Gradient> gradients() const;  // built-in sets, then the user's
    bool isUserGradient(const QString& name) const;
    void saveGradient(const Gradient& g);
    void removeGradient(const QString& name);

    // "Fill with pattern": the pattern used by the fill tools and Edit > Fill
    QString currentPattern() const { return m_pattern; }
    void setCurrentPattern(const QString& name);
    double patternScale = 1.0;
    bool patternTint = true;  // paint the pattern in the main color (keeps its transparency)

    // A tone layer: the selection (or the whole picture) filled with gray, shown as dots / lines.
    static std::shared_ptr<RasterLayer> makeToneLayer(Document& doc, const TonePreset& t, const QColor& ink);

Q_SIGNALS:
    void changed();                // the lists of materials changed
    void currentPatternChanged();  // another pattern, size or color mode was chosen

private:
    explicit Materials(QObject* parent = nullptr);
    void generateDefaults();
    void loadGradients();
    void saveGradients() const;
    QString m_root;
    QString m_pattern;
    QList<Gradient> m_userGradients;
    mutable QHash<QString, QImage> m_patternCache;
};

// The material being dragged from the panel to the canvas.
inline constexpr char kMaterialMime[] = "application/x-graphix-material";

}  // namespace gx
