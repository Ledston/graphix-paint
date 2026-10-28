// Runs the line art filter on pictures with a few settings, side by side.
// Usage: gx_lineartsheet out.png in1.png [in2.png ...]
#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <cstdio>

#include "core/Lineart.h"
#include "core/Pixel.h"
#include "core/Textures.h"

using namespace gx;

int main(int argc, char** argv) {
    QGuiApplication a(argc, argv);
    TextureLibrary::instance().setRoot(QDir::tempPath() + "/gx-sheet-textures");
    if (argc < 3) return 1;
    QList<QPair<QString, LineartParams>> sets;
    sets << qMakePair(QStringLiteral("auto"), LineartParams{});
    LineartParams pencil;
    pencil.detail = 60; pencil.thickness = 1.2; pencil.smooth = 0.5; pencil.cleanup = 3; pencil.wobble = 0.8; pencil.pressure = 0.5;
    pencil.texture = 1; pencil.textureStrength = 0.6; pencil.sketchy = 0.35; pencil.color = QColor(60, 60, 70);
    sets << qMakePair(QStringLiteral("pencil"), pencil);
    LineartParams comic;
    comic.detail = 50; comic.thickness = 2.5; comic.smooth = 2; comic.cleanup = 8; comic.fillDarks = true; comic.pressure = 0.3;
    sets << qMakePair(QStringLiteral("comic"), comic);
    const int cell = 300;
    QImage sheet(cell * (sets.size() + 1), cell * (argc - 2), QImage::Format_RGB32);
    sheet.fill(Qt::white);
    QPainter sp(&sheet);
    for (int r = 2; r < argc; ++r) {
        QImage src = QImage(QString::fromLocal8Bit(argv[r])).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const int W = src.width(), H = src.height();
        std::vector<float> in(size_t(W) * H * 4), out(in.size());
        for (int y = 0; y < H; ++y) {
            const quint32* row = reinterpret_cast<const quint32*>(src.constScanLine(y));
            for (int x = 0; x < W; ++x) unpack(row[x], &in[(size_t(y) * W + x) * 4]);
        }
        QRect box((0), (r - 2) * cell, cell, cell);
        sp.drawImage(QRectF(box).adjusted(4, 4, -4, -4), src);
        for (int s = 0; s < sets.size(); ++s) {
            int mode = makeLineart(in.data(), W, H, sets[s].second, out.data());
            QImage o(W, H, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < H; ++y) {
                quint32* row = reinterpret_cast<quint32*>(o.scanLine(y));
                for (int x = 0; x < W; ++x) row[x] = pack(&out[(size_t(y) * W + x) * 4]);
            }
            QRect b((s + 1) * cell, (r - 2) * cell, cell, cell);
            sp.drawImage(QRectF(b).adjusted(4, 4, -4, -4), o);
            sp.setPen(Qt::red);
            sp.drawText(b.left() + 6, b.top() + 14, sets[s].first + QStringLiteral(" (mode %1)").arg(mode));
        }
    }
    sp.end();
    sheet.save(QString::fromLocal8Bit(argv[1]));
    return 0;
}
