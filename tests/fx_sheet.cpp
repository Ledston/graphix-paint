// Renders a sample shape with each layer effect, to check them visually.
// Usage: gx_fxsheet out.png
#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <functional>

#include "core/Compositor.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/Layer.h"
#include "core/Textures.h"

using namespace gx;

int main(int argc, char** argv) {
    QGuiApplication a(argc, argv);
    TextureLibrary::instance().setRoot(QDir::tempPath() + "/gx-sheet-textures");
    const QList<QPair<QString, std::function<void(LayerEffects&)>>> cases = {
        {"none", [](LayerEffects&) {}},
        {"outline", [](LayerEffects& f) { f.outline = true; f.outlineSize = 5; f.outlineColor = Qt::black; }},
        {"shadow", [](LayerEffects& f) { f.shadow = true; }},
        {"outer glow", [](LayerEffects& f) { f.outerGlow = true; f.outerGlowColor = QColor(255, 200, 40); }},
        {"inner shadow", [](LayerEffects& f) { f.innerShadow = true; f.innerShadowOpacity = 0.8f; }},
        {"inner glow", [](LayerEffects& f) { f.innerGlow = true; }},
        {"gradient", [](LayerEffects& f) { f.gradientOverlay = true; }},
        {"grain", [](LayerEffects& f) { f.grain = true; f.grainAmount = 0.5f; f.grainSize = 1.5f; }},
        {"screentone", [](LayerEffects& f) { f.screentone = true; }},
        {"lines tone", [](LayerEffects& f) { f.screentone = true; f.toneShape = 1; f.toneCell = 6; }},
        {"chromatic", [](LayerEffects& f) { f.chromatic = true; f.chromaShift = 6; }},
        {"blur", [](LayerEffects& f) { f.blur = true; f.blurRadius = 6; }},
    };
    const int cw = 260, ch = 220;
    QImage sheet(cw * 4, ch * 3, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(Qt::white);
    QPainter sp(&sheet);
    for (int i = 0; i < cases.size(); ++i) {
        Document doc(QSize(cw, ch));
        auto paper = std::make_shared<RasterLayer>();
        paper->id = doc.newLayerId();
        paper->pixels.fillRect(doc.bounds(), 0xffffffffu);
        doc.root()->insert(0, paper);
        auto l = std::make_shared<RasterLayer>();
        l->id = doc.newLayerId();
        // a gray-shaded ball plus a dark line
        QImage shape(cw, ch, QImage::Format_ARGB32_Premultiplied);
        shape.fill(0);
        {
            QPainter p(&shape);
            p.setRenderHint(QPainter::Antialiasing);
            QRadialGradient g(QPointF(110, 90), 80);
            g.setColorAt(0, QColor(240, 120, 120));
            g.setColorAt(1, QColor(60, 30, 90));
            p.setBrush(g);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(120, 105), 70, 70);
            p.setPen(QPen(QColor(20, 20, 30), 6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(30, 190), QPointF(230, 160));
        }
        imageToSurface(shape, l->pixels, QPoint(0, 0));
        cases[i].second(l->fx);
        doc.root()->insert(1, l);
        QImage img = Compositor::renderImage(doc.root(), doc.bounds());
        sp.drawImage((i % 4) * cw, (i / 4) * ch, img);
        sp.setPen(Qt::black);
        sp.drawText((i % 4) * cw + 6, (i / 4) * ch + 16, cases[i].first);
    }
    sp.end();
    sheet.save(argc > 1 ? argv[1] : "fx.png");
    return 0;
}
