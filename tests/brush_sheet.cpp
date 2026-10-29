// Draws every default brush with a pressure ramp into one image, to check brush feel visually.
// Usage: gx_brushsheet out.png
#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <cmath>
#include <cstdio>

#include "brush/BrushStroke.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/Layer.h"
#include "core/Textures.h"

using namespace gx;

int main(int argc, char** argv) {
    QGuiApplication a(argc, argv);
    QDir(QDir::tempPath() + "/gx-sheet-textures").removeRecursively();  // always the newest default textures
    TextureLibrary::instance().setRoot(QDir::tempPath() + "/gx-sheet-textures");
    const auto brushes = BrushSettings::defaults();
    const int rowH = 110, W = 1400;
    const int H = rowH * int(brushes.size()) + 20;
    Document doc(QSize(W, H));
    auto paper = std::make_shared<RasterLayer>();
    paper->id = doc.newLayerId();
    paper->pixels.fillRect(doc.bounds(), 0xffffffffu);
    doc.root()->insert(0, paper);
    auto layer = std::make_shared<RasterLayer>();
    layer->id = doc.newLayerId();
    doc.root()->insert(1, layer);
    // colored blocks to show mixing / blur / smudge
    for (int i = 0; i < int(brushes.size()); ++i) {
        layer->pixels.fillRect(QRect(900, 10 + i * rowH + 10, 160, rowH - 30), 0xffd04040u);
        layer->pixels.fillRect(QRect(1060, 10 + i * rowH + 10, 160, rowH - 30), 0xff3060d0u);
    }

    PressureCurve curve;
    for (int i = 0; i < int(brushes.size()); ++i) {
        BrushSettings b = brushes[i];
        if (b.size > 60) b.size = 60;
        StrokeContext ctx;
        ctx.doc = &doc;
        ctx.layer = layer.get();
        ctx.brush = b;
        ctx.color = QColor(30, 30, 40);
        ctx.subColor = QColor(255, 220, 90);  // a visible second color on white paper
        ctx.pressureCurve = &curve;
        BrushStroke st(ctx);
        const double y0 = 10 + i * rowH + rowH / 2.0;
        const int N = 400;
        for (int k = 0; k <= N; ++k) {
            double t = double(k) / N;
            InputSample s;
            s.pos = QPointF(40 + t * 1300, y0 + std::sin(t * 12.566) * 28);
            s.pressure = std::sin(t * M_PI);  // 0 -> 1 -> 0
            s.timeMs = k * 4.0;
            if (k == 0) st.begin(s);
            else st.add(s);
        }
        st.end();
        std::fprintf(stderr, "%s done\n", qPrintable(b.name));
    }
    QImage img = Compositor::renderImage(doc.root(), doc.bounds());
    img.save(argc > 1 ? argv[1] : "brushes.png");
    return 0;
}
