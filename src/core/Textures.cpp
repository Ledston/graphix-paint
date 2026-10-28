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

TextureLibrary& TextureLibrary::instance() {
    static TextureLibrary lib;
    return lib;
}

void TextureLibrary::setRoot(const QString& root) {
    m_root = root;
    for (TextureKind k : {TextureKind::Paper, TextureKind::Brush, TextureKind::Shape}) QDir().mkpath(folder(k));
    generateDefaults(root);
    reload();
}

QString TextureLibrary::folder(TextureKind k) const {
    switch (k) {
    case TextureKind::Paper: return m_root + "/paper";
    case TextureKind::Brush: return m_root + "/brush";
    case TextureKind::Shape: return m_root + "/shapes";
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
    for (TextureKind k : {TextureKind::Paper, TextureKind::Brush, TextureKind::Shape}) {
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
}

}  // namespace gx
