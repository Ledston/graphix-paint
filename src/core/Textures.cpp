#include "core/Textures.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

namespace gx {

float Texture::sample(float x, float y) const {
    if (w <= 0 || h <= 0) return 1.f;
    x -= 0.5f; y -= 0.5f;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    float fx = x - x0, fy = y - y0;
    float a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

float Texture::sampleClamped(float u, float vv) const {
    if (w <= 0 || h <= 0) return 1.f;
    float x = u * w - 0.5f, y = vv * h - 0.5f;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    float fx = x - x0, fy = y - y0;
    auto px = [&](int xx, int yy) -> float {
        if (xx < 0 || yy < 0 || xx >= w || yy >= h) return 0.f;
        return v[size_t(yy) * w + xx];
    };
    return (px(x0, y0) * (1 - fx) + px(x0 + 1, y0) * fx) * (1 - fy) + (px(x0, y0 + 1) * (1 - fx) + px(x0 + 1, y0 + 1) * fx) * fy;
}

float Texture::sampleRibbon(float u, float vv) const {
    if (w <= 0 || h <= 0 || vv < 0.f || vv > 1.f) return 0.f;
    u -= std::floor(u);
    float x = u * w - 0.5f, y = vv * h - 0.5f;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    float fx = x - x0, fy = y - y0;
    auto px = [&](int xx, int yy) -> float {
        xx %= w;
        if (xx < 0) xx += w;
        if (yy < 0 || yy >= h) return 0.f;
        return v[size_t(yy) * w + xx];
    };
    return (px(x0, y0) * (1 - fx) + px(x0 + 1, y0) * fx) * (1 - fy) + (px(x0, y0 + 1) * (1 - fx) + px(x0 + 1, y0 + 1) * fx) * fy;
}

TextureLibrary& TextureLibrary::instance() {
    static TextureLibrary lib;
    return lib;
}

void TextureLibrary::setRoot(const QString& root) {
    m_root = root;
    for (TextureKind k : {TextureKind::Paper, TextureKind::Brush, TextureKind::Shape, TextureKind::Ribbon}) QDir().mkpath(folder(k));
    QDir().mkpath(root + "/stamps");
    generateDefaults(root);
    reload();
}

QString TextureLibrary::folder(TextureKind k) const {
    switch (k) {
    case TextureKind::Paper: return m_root + "/paper";
    case TextureKind::Brush: return m_root + "/brush";
    case TextureKind::Shape: return m_root + "/shapes";
    case TextureKind::Ribbon: return m_root + "/ribbons";
    }
    return m_root;
}

TexturePtr TextureLibrary::load(const QString& path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) return nullptr;
    // Keep memory bounded: textures larger than 2048 are downscaled.
    if (img.width() > 2048 || img.height() > 2048) img = img.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_ARGB32);
    bool useAlpha = false;
    if (img.hasAlphaChannel()) {
        for (int y = 0; y < img.height() && !useAlpha; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(row[x]) < 250) { useAlpha = true; break; }
        }
    }
    auto t = std::make_shared<Texture>();
    t->name = QFileInfo(path).completeBaseName();
    t->path = path;
    t->w = img.width();
    t->h = img.height();
    t->v.resize(size_t(t->w) * t->h);
    for (int y = 0; y < t->h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < t->w; ++x) {
            QRgb c = row[x];
            float val = useAlpha ? qAlpha(c) / 255.f : (0.3f * qRed(c) + 0.59f * qGreen(c) + 0.11f * qBlue(c)) / 255.f;
            t->v[size_t(y) * t->w + x] = val;
        }
    }
    QImage prev(t->w, t->h, QImage::Format_Grayscale8);
    for (int y = 0; y < t->h; ++y) {
        uchar* row = prev.scanLine(y);
        for (int x = 0; x < t->w; ++x) row[x] = uchar(255 - int(t->v[size_t(y) * t->w + x] * 255.f + 0.5f));
    }
    t->preview = prev.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return t;
}

void TextureLibrary::reload() {
    m_tex.clear();
    m_order.clear();
    m_stamps.clear();
    m_stampOrder.clear();
    // stamp sets: one folder per set
    const QStringList filters = {"*.png", "*.bmp", "*.jpg", "*.jpeg", "*.tif", "*.tiff", "*.webp"};
    for (const QString& set : QDir(m_root + "/stamps").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QDir d(m_root + "/stamps/" + set);
        std::vector<TexturePtr> pics;
        for (const QString& f : d.entryList(filters, QDir::Files, QDir::Name))
            if (TexturePtr t = load(d.filePath(f))) pics.push_back(t);
        if (pics.empty()) continue;
        m_stamps.insert(set, pics);
        m_stampOrder << set;
    }
    for (TextureKind k : {TextureKind::Paper, TextureKind::Brush, TextureKind::Shape, TextureKind::Ribbon}) {
        QDir d(folder(k));
        QStringList files = d.entryList({"*.png", "*.bmp", "*.jpg", "*.jpeg", "*.tif", "*.tiff", "*.webp"}, QDir::Files, QDir::Name);
        for (const QString& f : files) {
            TexturePtr t = load(d.filePath(f));
            if (!t) continue;
            if (m_tex[int(k)].contains(t->name)) continue;
            m_tex[int(k)].insert(t->name, t);
            m_order[int(k)] << t->name;
        }
    }
}

QStringList TextureLibrary::names(TextureKind k) const { return m_order.value(int(k)); }

QStringList TextureLibrary::stampSets() const { return m_stampOrder; }

const std::vector<TexturePtr>& TextureLibrary::stampSet(const QString& name) const {
    static const std::vector<TexturePtr> none;
    auto it = m_stamps.constFind(name);
    return it == m_stamps.cend() ? none : it.value();
}

TexturePtr TextureLibrary::get(TextureKind k, const QString& name) const {
    if (name.isEmpty()) return nullptr;
    return m_tex.value(int(k)).value(name);
}

TexturePtr TextureLibrary::findAny(const QString& name) const {
    if (name.isEmpty()) return nullptr;
    if (auto t = get(TextureKind::Brush, name)) return t;
    if (auto t = get(TextureKind::Paper, name)) return t;
    return get(TextureKind::Shape, name);
}

// ---------------------------------------------------------------------------
// Procedural default textures, written once so the folders are not empty and
// users can see the expected format.

namespace {

struct Noise {
    explicit Noise(unsigned seed) : seed(seed) {}
    unsigned seed;
    float hash(int x, int y) const {
        unsigned h = unsigned(x) * 374761393u + unsigned(y) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return float(h & 0xffffff) / float(0xffffff);
    }
    // Tileable value noise with integer period
    float value(float x, float y, int period) const {
        int x0 = int(std::floor(x)), y0 = int(std::floor(y));
        float fx = x - x0, fy = y - y0;
        fx = fx * fx * (3 - 2 * fx);
        fy = fy * fy * (3 - 2 * fy);
        auto H = [&](int a, int b) { return hash(((a % period) + period) % period, ((b % period) + period) % period); };
        float a = H(x0, y0), b = H(x0 + 1, y0), c = H(x0, y0 + 1), d = H(x0 + 1, y0 + 1);
        return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
    }
    float fbm(float x, float y, int basePeriod, int octaves, float gain = 0.5f) const {
        float sum = 0, amp = 1, norm = 0;
        int period = basePeriod;
        float fx = x, fy = y;
        for (int o = 0; o < octaves; ++o) {
            sum += value(fx, fy, period) * amp;
            norm += amp;
            amp *= gain;
            fx *= 2; fy *= 2; period *= 2;
        }
        return sum / norm;
    }
};

QImage makeGray(int size, const std::function<float(int, int)>& f) {
    QImage img(size, size, QImage::Format_Grayscale8);
    for (int y = 0; y < size; ++y) {
        uchar* row = img.scanLine(y);
        for (int x = 0; x < size; ++x) row[x] = uchar(std::clamp(int(f(x, y) * 255.f + 0.5f), 0, 255));
    }
    return img;
}

QImage makeAlpha(int size, const std::function<float(int, int)>& f) {
    QImage img(size, size, QImage::Format_ARGB32);
    for (int y = 0; y < size; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < size; ++x) row[x] = qRgba(0, 0, 0, std::clamp(int(f(x, y) * 255.f + 0.5f), 0, 255));
    }
    return img;
}

float contrast(float v, float c, float mid = 0.5f) { return std::clamp((v - mid) * c + mid, 0.f, 1.f); }

}  // namespace

namespace {
QImage canvasRGBA(int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

QPainterPath leafPath(double len, double width, double bend) {
    QPainterPath p;
    p.moveTo(0, 0);
    p.cubicTo(len * 0.3, -width + bend, len * 0.7, -width * 0.8 + bend, len, bend * 0.5);
    p.cubicTo(len * 0.7, width * 0.8 + bend, len * 0.3, width + bend, 0, 0);
    return p;
}

QPainterPath starPath(int points, double outer, double inner) {
    QPainterPath p;
    for (int i = 0; i < points * 2; ++i) {
        const double a = -M_PI / 2 + i * M_PI / points;
        const double r = i % 2 ? inner : outer;
        const QPointF pt(std::cos(a) * r, std::sin(a) * r);
        if (i == 0) p.moveTo(pt);
        else p.lineTo(pt);
    }
    p.closeSubpath();
    return p;
}
}  // namespace

// Ribbons (patterns along the stroke) and stamp sets for the "Decor" brushes.
void TextureLibrary::generateDecor(const QString& root) {
    const QString ribbons = root + "/ribbons", stamps = root + "/stamps";
    QDir().mkpath(ribbons);
    if (QFileInfo::exists(ribbons + "/.defaults3_written")) return;
    auto save = [](const QString& dir, const QString& name, const QImage& img) {
        QDir().mkpath(dir);
        const QString path = dir + "/" + name + ".png";
        if (!QFileInfo::exists(path)) img.save(path);
    };
    const QColor ink(0, 0, 0);
    auto painter = [](QImage& img) {
        auto* p = new QPainter(&img);
        p->setRenderHint(QPainter::Antialiasing);
        return p;
    };
    // ---- ribbons: x runs along the stroke (repeats), y across it (the brush width)
    {
        QImage img = canvasRGBA(64, 64);
        QPainter* p = painter(img);
        p->setPen(QPen(ink, 3));
        p->drawLine(QPointF(0, 6), QPointF(64, 6));                      // the edge
        p->drawArc(QRectF(0, 26, 32, 32), 180 * 16, 180 * 16);           // scallops
        p->drawArc(QRectF(32, 26, 32, 32), 180 * 16, 180 * 16);
        p->setPen(QPen(ink, 2));
        for (double x : {16.0, 48.0}) p->drawEllipse(QPointF(x, 24), 7, 7);  // little rings
        for (double x : {0.0, 32.0, 64.0}) p->drawEllipse(QPointF(x, 30), 3, 3);
        p->setPen(QPen(ink, 1.5));
        for (double x = 0; x < 64; x += 8) p->drawLine(QPointF(x, 6), QPointF(x + 4, 14));
        delete p;
        save(ribbons, "Lace", img);
    }
    {
        QImage img = canvasRGBA(96, 48);
        QPainter* p = painter(img);
        p->setPen(QPen(ink, 7));
        p->drawEllipse(QRectF(6, 10, 46, 28));       // a flat link
        p->setPen(QPen(ink, 9, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(44, 24), QPointF(100, 24));  // a link seen from the side
        p->drawLine(QPointF(-4, 24), QPointF(14, 24));
        delete p;
        save(ribbons, "Chain", img);
    }
    {
        QImage img = canvasRGBA(32, 32);
        QPainter* p = painter(img);
        p->setPen(QPen(ink, 2.2));
        for (int k = -1; k <= 1; ++k) {
            p->drawLine(QPointF(k * 32, 0), QPointF(k * 32 + 32, 32));
            p->drawLine(QPointF(k * 32 + 32, 0), QPointF(k * 32, 32));
        }
        delete p;
        save(ribbons, "Fishnet", img);
    }
    {
        QImage img = canvasRGBA(24, 24);
        QPainter* p = painter(img);
        p->setPen(QPen(ink, 3, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(6, 6), QPointF(16, 18));
        delete p;
        save(ribbons, "Stitches", img);
    }
    {
        QImage img = canvasRGBA(32, 32);
        QPainter* p = painter(img);
        p->setPen(Qt::NoPen);
        p->setBrush(ink);
        p->drawEllipse(QPointF(16, 16), 13, 13);
        p->setCompositionMode(QPainter::CompositionMode_Clear);
        p->drawEllipse(QPointF(11, 11), 3.5, 3.5);  // the shine
        delete p;
        save(ribbons, "Beads", img);
    }
    {
        QImage img = canvasRGBA(32, 32);
        QPainter* p = painter(img);
        p->setPen(QPen(ink, 2));
        for (int k = -1; k <= 2; ++k) {
            QPainterPath s;
            s.moveTo(k * 16 - 6, 30);
            s.cubicTo(k * 16 + 2, 22, k * 16 + 10, 10, k * 16 + 18, 2);
            p->drawPath(s);
        }
        p->drawLine(QPointF(0, 1), QPointF(32, 1));
        p->drawLine(QPointF(0, 31), QPointF(32, 31));
        delete p;
        save(ribbons, "Rope", img);
    }
    {
        QImage img = canvasRGBA(64, 32);
        QPainter* p = painter(img);
        p->setPen(Qt::NoPen);
        p->setBrush(ink);
        // two rows of bricks with mortar gaps, the second row shifted by half a brick
        for (int x : {1, 33}) p->drawRect(QRectF(x, 1, 30, 13));
        for (int x : {-15, 17, 49}) p->drawRect(QRectF(x, 17, 30, 13));
        delete p;
        save(ribbons, "Bricks", img);
    }
    {
        QImage img = canvasRGBA(32, 32);
        QPainter* p = painter(img);
        p->setPen(QPen(ink, 2.5));
        p->drawArc(QRectF(0, -14, 32, 30), 180 * 16, 180 * 16);
        p->drawArc(QRectF(-16, 2, 32, 30), 180 * 16, 180 * 16);
        p->drawArc(QRectF(16, 2, 32, 30), 180 * 16, 180 * 16);
        delete p;
        save(ribbons, "Roof tiles", img);
    }
    // ---- stamp sets (every picture fits in the circle of the brush)
    const int S = 128;
    auto stamp = [&](const QString& set, const QString& name, const std::function<void(QPainter&)>& draw) {
        QImage img = canvasRGBA(S, S);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(S / 2.0, S / 2.0);
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        draw(p);
        p.end();
        save(stamps + "/" + set, name, img);
    };
    for (int i = 0; i < 5; ++i) {
        stamp("Leaves", QString("leaf %1").arg(i + 1), [i](QPainter& p) {
            const double len = 92 - i * 6, wid = 18 + (i % 3) * 6, bend = (i - 2) * 4.0;
            p.rotate(-30 + i * 17);
            p.translate(-len / 2, 0);
            p.drawPath(leafPath(len, wid, bend));
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.setPen(QPen(Qt::black, 2.5));
            p.drawLine(QPointF(4, bend * 0.2), QPointF(len * 0.85, bend * 0.45));  // the vein
        });
    }
    for (int i = 0; i < 4; ++i) {
        stamp("Flowers", QString("flower %1").arg(i + 1), [i](QPainter& p) {
            const int petals = 4 + i * 2;
            const double len = 50 - i * 2, wid = 24 - i * 3;
            for (int k = 0; k < petals; ++k) {
                p.save();
                p.rotate(k * 360.0 / petals + i * 11);
                p.drawEllipse(QRectF(8, -wid / 2, len, wid));
                p.restore();
            }
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.drawEllipse(QPointF(0, 0), 13, 13);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.drawEllipse(QPointF(0, 0), 8, 8);
        });
    }
    for (int i = 0; i < 4; ++i) {
        stamp("Stars", QString("star %1").arg(i + 1), [i](QPainter& p) {
            const int points = 4 + i;
            p.drawPath(starPath(points, 58, i == 0 ? 12 : 24));
        });
    }
    for (int i = 0; i < 4; ++i) {
        stamp("Clouds", QString("cloud %1").arg(i + 1), [i](QPainter& p) {
            std::mt19937 rng(100 + i);
            std::uniform_real_distribution<double> U(0, 1);
            for (int k = 0; k < 7; ++k) {
                const double x = -38 + U(rng) * 76, y = -6 + U(rng) * 22 - std::cos(x / 40.0) * 18, r = 16 + U(rng) * 14;
                p.drawEllipse(QPointF(x, y), r, r * 0.85);
            }
            p.drawRect(QRectF(-44, 10, 88, 14));
        });
    }
    for (int i = 0; i < 4; ++i) {
        stamp("Grass", QString("grass %1").arg(i + 1), [i](QPainter& p) {
            std::mt19937 rng(200 + i);
            std::uniform_real_distribution<double> U(0, 1);
            for (int k = 0; k < 7; ++k) {
                const double x = -34 + k * 11 + U(rng) * 6, h = 50 + U(rng) * 30, lean = (U(rng) - 0.5) * 40, w = 4 + U(rng) * 4;
                QPainterPath b;
                b.moveTo(x - w, 56);
                b.quadTo(x - w * 0.3 + lean * 0.3, 56 - h * 0.6, x + lean, 56 - h);
                b.quadTo(x + w * 0.3 + lean * 0.3, 56 - h * 0.6, x + w, 56);
                b.closeSubpath();
                p.drawPath(b);
            }
        });
    }
    for (int i = 0; i < 3; ++i) {
        stamp("Snowflakes", QString("snowflake %1").arg(i + 1), [i](QPainter& p) {
            p.setPen(QPen(Qt::black, 5 - i, Qt::SolidLine, Qt::RoundCap));
            for (int k = 0; k < 6; ++k) {
                p.save();
                p.rotate(k * 60);
                p.drawLine(QPointF(0, 0), QPointF(0, -54));
                const double b = 18 + i * 8;
                p.drawLine(QPointF(0, -b), QPointF(-12, -b - 12));
                p.drawLine(QPointF(0, -b), QPointF(12, -b - 12));
                p.drawLine(QPointF(0, -b - 18), QPointF(-8, -b - 26));
                p.drawLine(QPointF(0, -b - 18), QPointF(8, -b - 26));
                p.restore();
            }
        });
    }
    for (int i = 0; i < 3; ++i) {
        stamp("Bubbles", QString("bubble %1").arg(i + 1), [i](QPainter& p) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Qt::black, 4 + i));
            p.drawEllipse(QPointF(0, 0), 54, 54);
            p.setPen(QPen(Qt::black, 6, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(-38, -38, 76, 76), 100 * 16, 50 * 16);  // the shine
        });
    }
    for (int i = 0; i < 3; ++i) {
        stamp("Rain", QString("drop %1").arg(i + 1), [i](QPainter& p) {
            p.setPen(QPen(Qt::black, 2.5 + i, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(-6 - i * 2, -50 + i * 8), QPointF(6 + i * 2, 50 - i * 8));
        });
    }
    for (int i = 0; i < 3; ++i) {
        stamp("Bush", QString("bush %1").arg(i + 1), [i](QPainter& p) {
            std::mt19937 rng(300 + i);
            std::uniform_real_distribution<double> U(0, 1);
            for (int k = 0; k < 16; ++k) {
                const double a = U(rng) * 2 * M_PI, r = std::sqrt(U(rng)) * 34;
                p.save();
                p.translate(std::cos(a) * r, std::sin(a) * r);
                p.rotate(U(rng) * 360);
                p.drawPath(leafPath(30, 9, (U(rng) - 0.5) * 6));
                p.restore();
            }
        });
    }
    QFile f(ribbons + "/.defaults3_written");
    if (f.open(QIODevice::WriteOnly)) f.write("Delete this file to regenerate the default ribbons and stamps.\n");
}

void TextureLibrary::generateDefaults(const QString& root) {
    const int S = 512;
    auto saveIfMissing = [](const QString& dir, const QString& name, const std::function<QImage()>& make) {
        QDir().mkpath(dir);
        QString marker = dir + "/.defaults_written";
        QString path = dir + "/" + name + ".png";
        if (QFileInfo::exists(marker) || QFileInfo::exists(path)) return;
        make().save(path);
    };
    QString paper = root + "/paper", brush = root + "/brush", shapes = root + "/shapes";

    saveIfMissing(paper, "Fine paper", [&] {
        Noise n(11);
        return makeGray(S, [&](int x, int y) { return contrast(0.82f + (n.fbm(x / 8.f, y / 8.f, S / 8, 4) - 0.5f) * 0.6f, 1.4f, 0.8f); });
    });
    saveIfMissing(paper, "Watercolor paper", [&] {
        Noise n(23), m(57);
        return makeGray(S, [&](int x, int y) {
            float big = n.fbm(x / 32.f, y / 32.f, S / 32, 5);
            float fine = m.fbm(x / 4.f, y / 4.f, S / 4, 3);
            return contrast(0.7f + (big - 0.5f) * 0.7f + (fine - 0.5f) * 0.35f, 1.6f, 0.65f);
        });
    });
    saveIfMissing(paper, "Canvas", [&] {
        Noise n(5);
        return makeGray(S, [&](int x, int y) {
            float wx = 0.5f + 0.5f * std::sin(x * float(M_PI) * 2.f / 8.f);
            float wy = 0.5f + 0.5f * std::sin(y * float(M_PI) * 2.f / 8.f);
            bool over = ((x / 8) + (y / 8)) % 2 == 0;
            float weave = over ? wy : wx;
            return contrast(0.55f + weave * 0.35f + (n.fbm(x / 3.f, y / 3.f, S / 3 + 1, 2) - 0.5f) * 0.2f, 1.2f);
        });
    });
    saveIfMissing(paper, "Rough grain", [&] {
        Noise n(91);
        return makeGray(S, [&](int x, int y) { return contrast(n.fbm(x / 2.f, y / 2.f, S / 2, 3), 2.2f, 0.45f); });
    });

    saveIfMissing(brush, "Bristles", [&] {
        Noise n(3);
        return makeGray(S, [&](int x, int y) { return contrast(n.fbm(x / 1.5f, y / 40.f, int(S / 1.5f), 3), 2.5f, 0.45f); });
    });
    saveIfMissing(brush, "Chalk", [&] {
        Noise n(77);
        return makeGray(S, [&](int x, int y) { return n.fbm(x / 3.f, y / 3.f, S / 3 + 1, 4) > 0.45f ? 1.f : 0.25f; });
    });
    saveIfMissing(brush, "Sponge", [&] {
        Noise n(41);
        return makeGray(S, [&](int x, int y) { return contrast(n.fbm(x / 12.f, y / 12.f, S / 12 + 1, 4), 3.f, 0.5f); });
    });

    const int T = 256;
    saveIfMissing(shapes, "Bristle tip", [&] {
        Noise n(8);
        return makeAlpha(T, [&](int x, int y) {
            float dx = (x + 0.5f) / T * 2 - 1, dy = (y + 0.5f) / T * 2 - 1;
            float r = std::sqrt(dx * dx + dy * dy);
            if (r >= 1) return 0.f;
            float streak = n.value(x / 3.f, y / 60.f, 1000);
            return std::clamp((1 - r) * 4.f, 0.f, 1.f) * (streak > 0.4f ? 1.f : 0.15f);
        });
    });
    saveIfMissing(shapes, "Spatter", [&] {
        QImage img(T, T, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> U(0, 1);
        for (int i = 0; i < 90; ++i) {
            float a = U(rng) * 6.2832f, rr = std::sqrt(U(rng)) * T * 0.46f;
            float s = 2 + U(rng) * 12 * (1 - rr / (T * 0.5f));
            p.setBrush(Qt::black);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(T / 2 + std::cos(a) * rr, T / 2 + std::sin(a) * rr), s, s);
        }
        return img;
    });
    saveIfMissing(shapes, "Chalk tip", [&] {
        Noise n(19);
        return makeAlpha(T, [&](int x, int y) {
            float dx = (x + 0.5f) / T * 2 - 1, dy = (y + 0.5f) / T * 2 - 1;
            float r = std::sqrt(dx * dx + dy * dy) + (n.fbm(x / 10.f, y / 10.f, 1000, 3) - 0.5f) * 0.35f;
            float grain = n.value(x / 2.f, y / 2.f, 1000) > 0.35f ? 1.f : 0.f;
            return r < 0.92f ? grain : 0.f;
        });
    });
    saveIfMissing(shapes, "Flat", [&] {
        return makeAlpha(T, [&](int x, int y) {
            float dx = (x + 0.5f) / T * 2 - 1, dy = ((y + 0.5f) / T * 2 - 1) * 3.f;
            float r = std::sqrt(dx * dx + dy * dy);
            return std::clamp((1 - r) * 12.f, 0.f, 1.f);
        });
    });
    saveIfMissing(shapes, "Square", [&] {
        return makeAlpha(T, [&](int x, int y) {
            float dx = std::fabs((x + 0.5f) / T * 2 - 1), dy = std::fabs((y + 0.5f) / T * 2 - 1);
            return std::clamp((0.9f - std::max(dx, dy)) * 40.f, 0.f, 1.f);
        });
    });

    for (const QString& d : {paper, brush, shapes}) {
        QFile f(d + "/.defaults_written");
        if (!f.exists() && f.open(QIODevice::WriteOnly)) f.write("Delete this file to regenerate the default textures.\n");
    }

    // ---- second set (version 1.1): written once into existing folders too
    auto saveNew = [](const QString& dir, const QString& name, const std::function<QImage()>& make) {
        QDir().mkpath(dir);
        QString path = dir + "/" + name + ".png";
        if (QFileInfo::exists(dir + "/.defaults2_written") || QFileInfo::exists(path)) return;
        make().save(path);
    };
    saveNew(paper, "Cold press paper", [&] {
        Noise n(131), m(7);
        return makeGray(S, [&](int x, int y) {
            float bumps = n.fbm(x / 10.f, y / 10.f, S / 10 + 1, 4);
            float fine = m.fbm(x / 2.f, y / 2.f, S / 2, 2);
            return contrast(0.72f + (bumps - 0.5f) * 0.9f + (fine - 0.5f) * 0.25f, 1.8f, 0.62f);
        });
    });
    saveNew(paper, "Kraft fibers", [&] {
        QImage img(S, S, QImage::Format_RGB32);
        img.fill(QColor::fromRgbF(0.85, 0.85, 0.85));
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        std::mt19937 rng(77);
        std::uniform_real_distribution<float> U(0, 1);
        for (int i = 0; i < 900; ++i) {
            QPointF a(U(rng) * S, U(rng) * S);
            float ang = U(rng) * 6.2832f, len = 6 + U(rng) * 30;
            QPointF d(std::cos(ang) * len, std::sin(ang) * len);
            p.setPen(QPen(QColor::fromRgbF(0.45 + U(rng) * 0.3, 0.45 + U(rng) * 0.3, 0.45 + U(rng) * 0.3), 0.6 + U(rng) * 0.9));
            // drawn three times shifted, so the texture tiles
            for (int ox : {-S, 0, S})
                for (int oy : {-S, 0, S}) p.drawLine(a + QPointF(ox, oy), a + d + QPointF(ox, oy));
        }
        p.end();
        return img.convertToFormat(QImage::Format_Grayscale8);
    });
    saveNew(paper, "Toothy paper", [&] {
        Noise n(313);
        return makeGray(S, [&](int x, int y) { return contrast(n.fbm(x / 4.f, y / 4.f, S / 4, 3), 2.8f, 0.42f); });
    });
    saveNew(brush, "Pencil grain", [&] {
        Noise n(17), m(29);
        return makeGray(S, [&](int x, int y) {
            float g = n.fbm(x / 1.6f, y / 2.4f, int(S / 1.6f), 2) * 0.7f + m.fbm(x / 6.f, y / 6.f, S / 6 + 1, 3) * 0.3f;
            return contrast(g, 3.2f, 0.47f);
        });
    });
    saveNew(brush, "Charcoal", [&] {
        Noise n(53), m(61);
        return makeGray(S, [&](int x, int y) {
            float streak = m.fbm(x / 30.f, y / 1.5f, S / 30 + 1, 3);
            float grain = n.fbm(x / 2.f, y / 2.f, S / 2, 3);
            return contrast(grain * 0.6f + streak * 0.4f, 3.5f, 0.5f);
        });
    });
    saveNew(brush, "Hatch lines", [&] {
        // lines along x: with "Texture follows the stroke" they run along the stroke
        Noise n(101);
        return makeGray(S, [&](int x, int y) {
            const int period = 8;
            float d = std::fabs(float(y % period) + 0.5f - period / 2.f);
            float line = std::clamp(2.2f - d, 0.f, 1.f);
            float breaks = n.fbm(x / 24.f, y / float(period), S / 24 + 1, 2) > 0.27f ? 1.f : 0.f;
            return line * breaks;
        });
    });
    saveNew(brush, "Crosshatch", [&] {
        return makeGray(S, [&](int x, int y) {
            const int period = 12;
            auto l = [&](int v) {
                float d = std::fabs(float(((v % period) + period) % period) + 0.5f - period / 2.f);
                return std::clamp(1.8f - d, 0.f, 1.f);
            };
            return std::max(l(x + y), l(x - y + S));
        });
    });
    saveNew(brush, "Halftone dots", [&] {
        return makeGray(S, [&](int x, int y) {
            const float cell = 8.f;
            float fx = std::fmod(float(x) + 0.5f, cell) - cell / 2, fy = std::fmod(float(y) + 0.5f, cell) - cell / 2;
            float r = std::sqrt(fx * fx + fy * fy);
            return std::clamp(2.6f - r, 0.f, 1.f);
        });
    });
    saveNew(brush, "Dry brush", [&] {
        Noise n(211);
        return makeGray(S, [&](int x, int y) { return contrast(n.fbm(x / 60.f, y / 1.2f, S / 60 + 1, 3), 3.f, 0.5f); });
    });
    saveNew(brush, "Sand", [&] {
        Noise n(401);
        return makeGray(S, [&](int x, int y) { return n.hash(x, y) > 0.82f ? 1.f : 0.1f + n.fbm(x / 8.f, y / 8.f, S / 8, 2) * 0.2f; });
    });
    saveNew(shapes, "Pencil tip", [&] {
        Noise n(23);
        return makeAlpha(T, [&](int x, int y) {
            float dx = (x + 0.5f) / T * 2 - 1, dy = (y + 0.5f) / T * 2 - 1;
            float r = std::sqrt(dx * dx + dy * dy);
            if (r >= 1) return 0.f;
            float grain = n.value(x / 1.5f, y / 1.5f, 1000);
            return std::clamp((1 - r) * 6.f, 0.f, 1.f) * (grain > 0.3f ? 1.f : 0.35f);
        });
    });
    saveNew(shapes, "Sparkle", [&] {
        return makeAlpha(T, [&](int x, int y) {
            float dx = std::fabs((x + 0.5f) / T * 2 - 1), dy = std::fabs((y + 0.5f) / T * 2 - 1);
            float star = std::max(1.f - (dx * 7.f + dy) , 1.f - (dy * 7.f + dx));
            float core = 1.f - std::sqrt(dx * dx + dy * dy) * 3.f;
            return std::clamp(std::max(star, core) * 1.5f, 0.f, 1.f);
        });
    });
    saveNew(shapes, "Droplet", [&] {
        // a drop: round bottom, pointed top
        return makeAlpha(T, [&](int x, int y) {
            float dx = (x + 0.5f) / T * 2 - 1, dy = (y + 0.5f) / T * 2 - 1;
            float rr = dy > 0.f ? std::sqrt(dx * dx + dy * dy) / 0.62f : std::fabs(dx) / (0.62f * (1.f + dy * 0.95f)) + std::max(0.f, -dy - 0.95f) * 20.f;
            if (dy > 0.62f) rr = 2.f;
            return std::clamp((1.f - rr) * 30.f, 0.f, 1.f);
        });
    });
    generateDecor(root);
    // ---- the SAI textures of the user's own brushes ("Paper", "Fabric 01"), as paper and brush textures
    if (!QFileInfo::exists(brush + "/.defaults4_written")) {
        for (const QString& name : {QStringLiteral("Paper"), QStringLiteral("Fabric 01")})
            for (const QString& dir : {paper, brush}) {
                const QString path = dir + "/" + name + ".png";
                if (!QFileInfo::exists(path)) QFile::copy(":/textures/" + name + ".png", path);
                QFile(path).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser);
            }
        QFile f(brush + "/.defaults4_written");
        if (f.open(QIODevice::WriteOnly)) f.write("Delete this file to copy the SAI textures (Paper, Fabric 01) again.\n");
    }
    for (const QString& d : {paper, brush, shapes}) {
        QFile f(d + "/.defaults2_written");
        if (!f.exists() && f.open(QIODevice::WriteOnly)) f.write("Delete this file to regenerate the second set of default textures.\n");
    }
}

}  // namespace gx
