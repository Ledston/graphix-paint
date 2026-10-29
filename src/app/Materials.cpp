#include "app/Materials.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>

#include "app/App.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"

namespace gx {

Materials& Materials::instance() {
    static Materials m;
    return m;
}

Materials::Materials(QObject* parent) : QObject(parent) {}

void Materials::setRoot(const QString& dir) {
    m_root = dir;
    QDir().mkpath(imagesFolder());
    QDir().mkpath(patternsFolder());
    generateDefaults();
    loadGradients();
    if (app()) {
        m_pattern = app()->settings().value("materials/pattern").toString();
        patternScale = app()->settings().value("materials/patternScale", 1.0).toDouble();
        patternTint = app()->settings().value("materials/patternTint", true).toBool();
    }
    if (!patterns().contains(m_pattern) && !patterns().isEmpty()) m_pattern = patterns().first();
}

void Materials::setCurrentPattern(const QString& name) {
    m_pattern = name;
    if (app()) {
        app()->settings().setValue("materials/pattern", name);
        app()->settings().setValue("materials/patternScale", patternScale);
        app()->settings().setValue("materials/patternTint", patternTint);
    }
    Q_EMIT currentPatternChanged();
}

static const QStringList kImageFilters = {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.tif", "*.tiff"};

QStringList Materials::images() const {
    QStringList out;
    QDir d(imagesFolder());
    for (const QString& f : d.entryList(kImageFilters, QDir::Files, QDir::Time)) out << d.filePath(f);
    return out;
}

QStringList Materials::patterns() const {
    QStringList out;
    for (const QString& f : QDir(patternsFolder()).entryList(kImageFilters, QDir::Files, QDir::Name)) out << QFileInfo(f).completeBaseName();
    return out;
}

QImage Materials::pattern(const QString& name) const {
    auto it = m_patternCache.constFind(name);
    if (it != m_patternCache.cend()) return it.value();
    QImage img;
    QDir d(patternsFolder());
    for (const QString& f : d.entryList(kImageFilters, QDir::Files))
        if (QFileInfo(f).completeBaseName() == name) img = QImage(d.filePath(f)).convertToFormat(QImage::Format_ARGB32);
    if (!img.isNull()) m_patternCache.insert(name, img);
    return img;
}

QString Materials::addImage(const QImage& img, const QString& name) {
    QString base = name.isEmpty() ? QStringLiteral("picture") : name;
    base.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString path = imagesFolder() + "/" + base + ".png";
    if (QFileInfo::exists(path)) path = imagesFolder() + "/" + base + "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) + ".png";
    img.save(path);
    Q_EMIT changed();
    return path;
}

bool Materials::removeFile(const QString& path) {
    const bool ok = QFile::remove(path);
    m_patternCache.clear();
    Q_EMIT changed();
    return ok;
}

QList<TonePreset> Materials::tones() {
    QList<TonePreset> t;
    t << TonePreset{"Dots 10%", 0.9, false, 7, 45, 0} << TonePreset{"Dots 30%", 0.7, false, 7, 45, 0}
      << TonePreset{"Dots 50%", 0.5, false, 7, 45, 0} << TonePreset{"Big dots", 0.7, false, 14, 45, 0}
      << TonePreset{"Lines", 0.7, false, 6, 0, 1} << TonePreset{"Diagonal lines", 0.7, false, 6, 45, 1}
      << TonePreset{"Squares", 0.6, false, 9, 45, 2} << TonePreset{"Gradient dots", 0.0, true, 7, 45, 0}
      << TonePreset{"Gradient lines", 0.0, true, 6, 0, 1};
    return t;
}

std::shared_ptr<RasterLayer> Materials::makeToneLayer(Document& doc, const TonePreset& t, const QColor& ink) {
    auto l = std::make_shared<RasterLayer>();
    l->id = doc.newLayerId();
    l->name = t.name;
    QRect area = doc.hasSelection() ? doc.selectionBounds() : doc.bounds();
    area &= doc.bounds();
    if (area.isEmpty()) return l;
    const int W = area.width(), H = area.height();
    std::vector<float> px(size_t(W) * H * 4);
    std::vector<float> sel;
    if (doc.hasSelection()) {
        sel.resize(size_t(W) * H);
        readMask(doc.selection(), area, sel.data());
    }
    for (int y = 0; y < H; ++y) {
        // a gradient tone: dark at the top, light at the bottom
        const float g = t.gradient ? float(0.15 + 0.85 * (H > 1 ? double(y) / (H - 1) : 0.0)) : float(t.gray);
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * W + x;
            const float a = sel.empty() ? 1.f : sel[i];
            float* p = &px[i * 4];
            p[0] = p[1] = p[2] = g * a;
            p[3] = a;
        }
    }
    writeFloat(l->pixels, area, px.data(), true);
    l->pixels.optimizeRect(area);
    l->fx.screentone = true;
    l->fx.toneCell = t.cell;
    l->fx.toneAngle = t.angle;
    l->fx.toneShape = t.shape;
    l->fx.toneColor = ink;
    l->fx.toneKeepWhite = true;
    return l;
}

// ---- gradient maps

QList<Gradient> Materials::gradients() const {
    QList<Gradient> v;
    auto g = [&](const char* name, std::initializer_list<std::pair<double, const char*>> stops) {
        Gradient gr;
        gr.name = name;
        for (const auto& s : stops) gr.stops.push_back({s.first, QColor(s.second), 1});
        v << gr;
    };
    g("Warm evening", {{0, "#1d1131"}, {0.35, "#7a2e4a"}, {0.65, "#e0784e"}, {1, "#ffe3a8"}});
    g("Cold night", {{0, "#05070f"}, {0.4, "#1b2a55"}, {0.75, "#4f7fb5"}, {1, "#d8ecff"}});
    g("Sepia", {{0, "#20140c"}, {0.5, "#8a6a45"}, {1, "#f6ead2"}});
    g("Cinema", {{0, "#0b1a1f"}, {0.4, "#20515a"}, {0.7, "#d1a067"}, {1, "#fff2dc"}});
    g("Golden hour", {{0, "#2a1406"}, {0.45, "#b8541c"}, {0.8, "#f6b64e"}, {1, "#fff6d8"}});
    g("Forest", {{0, "#0b1408"}, {0.45, "#2f5b2a"}, {0.8, "#a9c46b"}, {1, "#f2f5d8"}});
    g("Pastel", {{0, "#5b4b7a"}, {0.4, "#c08bb5"}, {0.75, "#f2c3c0"}, {1, "#fff6ea"}});
    g("Neon", {{0, "#12002b"}, {0.4, "#6b0fb3"}, {0.7, "#ff3fa4"}, {1, "#7ff8ff"}});
    g("Black and white", {{0, "#000000"}, {1, "#ffffff"}});
    g("Blue print", {{0, "#08224a"}, {0.6, "#2e66b8"}, {1, "#dbe9ff"}});
    for (const Gradient& u : m_userGradients) v << u;
    return v;
}

bool Materials::isUserGradient(const QString& name) const {
    for (const Gradient& u : m_userGradients)
        if (u.name == name) return true;
    return false;
}

void Materials::saveGradient(const Gradient& g) {
    for (Gradient& u : m_userGradients)
        if (u.name == g.name) {
            u = g;
            saveGradients();
            Q_EMIT changed();
            return;
        }
    m_userGradients << g;
    saveGradients();
    Q_EMIT changed();
}

void Materials::removeGradient(const QString& name) {
    for (int i = 0; i < m_userGradients.size(); ++i)
        if (m_userGradients[i].name == name) {
            m_userGradients.removeAt(i);
            break;
        }
    saveGradients();
    Q_EMIT changed();
}

void Materials::loadGradients() {
    m_userGradients.clear();
    QFile f(m_root + "/gradients.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).array()) m_userGradients << Gradient::fromJson(v.toObject());
}

void Materials::saveGradients() const {
    QJsonArray a;
    for (const Gradient& g : m_userGradients) a.append(g.toJson());
    QFile f(m_root + "/gradients.json");
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(a).toJson());
}

// ---- default patterns (black on transparent: the fill paints them in the main color)

void Materials::generateDefaults() {
    const QString dir = patternsFolder();
    if (QFileInfo::exists(dir + "/.defaults_written")) return;
    auto save = [&](const QString& name, int w, int h, const std::function<void(QPainter&)>& draw) {
        const QString path = dir + "/" + name + ".png";
        if (QFileInfo::exists(path)) return;
        QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        draw(p);
        p.end();
        img.save(path);
    };
    // each picture is drawn so that it repeats without seams
    auto wrap = [](QPainter& p, int w, int h, const std::function<void()>& one) {
        for (int ox : {-w, 0, w})
            for (int oy : {-h, 0, h}) {
                p.save();
                p.translate(ox, oy);
                one();
                p.restore();
            }
    };
    save("Checker", 32, 32, [](QPainter& p) {
        p.drawRect(0, 0, 16, 16);
        p.drawRect(16, 16, 16, 16);
    });
    save("Stripes", 16, 16, [](QPainter& p) { p.drawRect(0, 0, 16, 8); });
    save("Diagonal stripes", 24, 24, [&](QPainter& p) {
        wrap(p, 24, 24, [&] { p.drawPolygon(QPolygonF({QPointF(0, 0), QPointF(8, 0), QPointF(32, 24), QPointF(24, 24)})); });
    });
    save("Polka dots", 32, 32, [&](QPainter& p) {
        wrap(p, 32, 32, [&] {
            p.drawEllipse(QPointF(8, 8), 5, 5);
            p.drawEllipse(QPointF(24, 24), 5, 5);
        });
    });
    save("Gingham", 32, 32, [](QPainter& p) {
        p.setBrush(QColor(0, 0, 0, 110));
        p.drawRect(0, 0, 32, 12);
        p.drawRect(0, 0, 12, 32);
    });
    save("Plaid", 48, 48, [](QPainter& p) {
        p.setBrush(QColor(0, 0, 0, 90));
        p.drawRect(0, 6, 48, 10);
        p.drawRect(6, 0, 10, 48);
        p.setBrush(QColor(0, 0, 0, 200));
        p.drawRect(0, 30, 48, 3);
        p.drawRect(30, 0, 3, 48);
    });
    save("Grid", 24, 24, [](QPainter& p) {
        p.drawRect(0, 0, 24, 2);
        p.drawRect(0, 0, 2, 24);
    });
    save("Hearts", 40, 40, [&](QPainter& p) {
        auto heart = [&](double cx, double cy, double s) {
            QPainterPath h;
            h.moveTo(cx, cy + s * 0.9);
            h.cubicTo(cx - s * 1.4, cy - s * 0.1, cx - s * 0.6, cy - s * 1.2, cx, cy - s * 0.35);
            h.cubicTo(cx + s * 0.6, cy - s * 1.2, cx + s * 1.4, cy - s * 0.1, cx, cy + s * 0.9);
            p.drawPath(h);
        };
        wrap(p, 40, 40, [&] {
            heart(10, 11, 6);
            heart(30, 31, 6);
        });
    });
    save("Stars", 40, 40, [&](QPainter& p) {
        auto star = [&](double cx, double cy, double r) {
            QPolygonF s;
            for (int i = 0; i < 10; ++i) {
                const double a = -M_PI / 2 + i * M_PI / 5, rr = i % 2 ? r * 0.45 : r;
                s << QPointF(cx + std::cos(a) * rr, cy + std::sin(a) * rr);
            }
            p.drawPolygon(s);
        };
        wrap(p, 40, 40, [&] {
            star(10, 10, 7);
            star(30, 30, 7);
        });
    });
    save("Zigzag", 32, 16, [&](QPainter& p) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 3));
        wrap(p, 32, 16, [&] { p.drawPolyline(QPolygonF({QPointF(0, 12), QPointF(8, 4), QPointF(16, 12), QPointF(24, 4), QPointF(32, 12)})); });
    });
    save("Waves", 48, 24, [&](QPainter& p) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 3));
        wrap(p, 48, 24, [&] {
            QPainterPath w;
            w.moveTo(0, 12);
            w.cubicTo(12, 2, 12, 2, 24, 12);
            w.cubicTo(36, 22, 36, 22, 48, 12);
            p.drawPath(w);
        });
    });
    save("Fish scales", 32, 32, [&](QPainter& p) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 2.5));
        wrap(p, 32, 32, [&] {
            p.drawArc(QRectF(0, -8, 32, 32), 180 * 16, 180 * 16);
            p.drawArc(QRectF(-16, 8, 32, 32), 180 * 16, 180 * 16);
            p.drawArc(QRectF(16, 8, 32, 32), 180 * 16, 180 * 16);
        });
    });
    save("Herringbone", 32, 32, [&](QPainter& p) {
        wrap(p, 32, 32, [&] {
            p.drawPolygon(QPolygonF({QPointF(0, 0), QPointF(4, 0), QPointF(16, 12), QPointF(12, 12)}));
            p.drawPolygon(QPolygonF({QPointF(16, 12), QPointF(28, 0), QPointF(32, 0), QPointF(20, 12)}));
            p.drawPolygon(QPolygonF({QPointF(0, 16), QPointF(4, 16), QPointF(16, 28), QPointF(12, 28)}));
            p.drawPolygon(QPolygonF({QPointF(16, 28), QPointF(28, 16), QPointF(32, 16), QPointF(20, 28)}));
        });
    });
    QFile f(dir + "/.defaults_written");
    if (f.open(QIODevice::WriteOnly)) f.write("Delete this file to get the default patterns back.\n");
}

}  // namespace gx
