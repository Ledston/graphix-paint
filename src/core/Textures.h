#pragma once
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace gx {

// A grayscale texture used by brushes (paper grain, tip shapes) and by the
// "paper texture" layer effect. Values are 0..1 where 1 = full paint.
struct Texture {
    QString name;
    QString path;
    int w = 0, h = 0;
    std::vector<float> v;
    QImage preview;  // small thumbnail for the UI

    float at(int x, int y) const {
        x %= w; if (x < 0) x += w;
        y %= h; if (y < 0) y += h;
        return v[size_t(y) * w + x];
    }
    // Wrapping bilinear sample
    float sample(float x, float y) const;
    // Clamped bilinear sample in 0..1 uv (for tip shapes), outside = 0
    float sampleClamped(float u, float vv) const;
    // Ribbon pattern: u repeats (along the stroke), v is 0..1 across it (outside = 0)
    float sampleRibbon(float u, float vv) const;
};
using TexturePtr = std::shared_ptr<const Texture>;

enum class TextureKind { Paper = 0, Brush = 1, Shape = 2, Ribbon = 3 };

// Textures are loaded from folders next to the program (or the user data folder):
//   textures/paper   paper textures for layers and brushes
//   textures/brush   brush grain textures
//   textures/shapes  brush tip shapes
//   textures/ribbons patterns drawn along the stroke (lace, chain...), as wide as the brush
//   textures/stamps/<set>  stamp sets: every dab of a stamp brush is one of these pictures
// Any PNG, BMP, JPG or TIFF works. Images with transparency use their alpha,
// others use brightness (white = full paint, dark = less paint, like SAI).
class TextureLibrary {
public:
    static TextureLibrary& instance();

    void setRoot(const QString& root);
    QString root() const { return m_root; }
    QString folder(TextureKind k) const;
    void reload();

    QStringList names(TextureKind k) const;
    TexturePtr get(TextureKind k, const QString& name) const;
    // Brush textures can come from both the brush and paper folders.
    TexturePtr findAny(const QString& name) const;
    QStringList stampSets() const;
    const std::vector<TexturePtr>& stampSet(const QString& name) const;

    static TexturePtr load(const QString& path);
    static void generateDefaults(const QString& root);
    static void generateDecor(const QString& root);

private:
    QString m_root;
    QHash<int, QHash<QString, TexturePtr>> m_tex;
    QHash<int, QStringList> m_order;
    QHash<QString, std::vector<TexturePtr>> m_stamps;
    QStringList m_stampOrder;
};

}  // namespace gx
