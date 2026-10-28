// Engine tests. Run headless: QT_QPA_PLATFORM=offscreen ./gx_tests
#include <QDir>
#include <QElapsedTimer>
#include <QPainterPath>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "brush/BrushStroke.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerOps.h"
#include "core/Pixel.h"
#include "core/Textures.h"
#include "io/NativeFormat.h"
#include "tools/SelectTools.h"
#include "io/PsdFormat.h"
#include "vector/VectorRender.h"

using namespace gx;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (cond) ++g_pass;                                                           \
        else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static bool near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) <= eps; }

static QColor merged(Document& d, int x, int y) {
    Compositor::prepare(d.root(), QRect(x, y, 1, 1));
    float p[4];
    Compositor::samplePixel(d.root(), x, y, p);
    return colorFromPremul(pack(p));
}

static void testTiles() {
    RasterSurface s;
    s.setPixel(-5, -7, 0xff112233u);
    CHECK(s.pixel(-5, -7) == 0xff112233u);
    CHECK(s.pixel(-6, -7) == 0u);
    s.fillRect(QRect(0, 0, 300, 300), 0xffffffffu);
    s.optimize();
    CHECK(s.tile(0, 0) && s.tile(0, 0)->isUniform());
    RasterSurface copy = s;  // shares tiles
    copy.setPixel(1, 1, 0xff000000u);
    CHECK(s.pixel(1, 1) == 0xffffffffu);  // copy on write
    CHECK(copy.pixel(1, 1) == 0xff000000u);
    QRect b = s.exactBounds();
    CHECK(b.contains(QPoint(299, 299)) && b.contains(QPoint(-5, -7)));
    RasterSurface t = copy.translated(3, 2);
    CHECK(t.pixel(4, 3) == 0xff000000u);
}

static void testBlend() {
    float d[4] = {0.5f, 0.5f, 0.5f, 1.f};
    float s[4] = {0.5f, 0.0f, 0.0f, 0.5f};  // premultiplied red at 50%
    blendRow(BlendMode::Normal, d, s, 1);
    CHECK(near(d[0], 0.75f) && near(d[1], 0.25f) && near(d[3], 1.f));
    float d2[4] = {0.5f, 0.5f, 0.5f, 1.f};
    float s2[4] = {1.f, 1.f, 1.f, 1.f};
    blendRow(BlendMode::Multiply, d2, s2, 1);
    CHECK(near(d2[0], 0.5f));
    float d3[4] = {0, 0, 0, 0};
    float s3[4] = {0.2f, 0.2f, 0.2f, 0.4f};
    blendRow(BlendMode::Screen, d3, s3, 1);
    CHECK(near(d3[3], 0.4f) && near(d3[0], 0.2f));
    for (int m = 0; m < int(BlendMode::Count); ++m) {
        float a[4] = {0.3f, 0.2f, 0.1f, 0.6f}, b[4] = {0.1f, 0.4f, 0.2f, 0.5f};
        blendRow(BlendMode(m), a, b, 1);
        bool ok = true;
        for (float v : a) ok &= std::isfinite(v) && v >= -1e-4f && v <= 1.0001f;
        ok &= a[0] <= a[3] + 1e-3f && a[1] <= a[3] + 1e-3f && a[2] <= a[3] + 1e-3f;
        CHECK(ok);
    }
}

static void testCompositeAndClipping() {
    Document doc(QSize(64, 64));
    auto* base = ops::addRasterLayer(doc, "base");
    base->pixels.fillRect(QRect(0, 0, 32, 64), premulFromColor(Qt::white));
    auto* top = ops::addRasterLayer(doc, "red");
    top->pixels.fillRect(QRect(0, 0, 64, 64), premulFromColor(Qt::red));
    top->clipping = true;
    QColor in = merged(doc, 10, 10), out = merged(doc, 50, 10);
    CHECK(in.red() > 250 && in.green() < 5 && in.alpha() == 255);
    CHECK(out.alpha() == 0);  // clipped away outside the base

    // Clipping onto a folder
    Document d2(QSize(64, 64));
    auto* g = ops::addGroup(d2, "folder");
    auto a = std::make_shared<RasterLayer>();
    a->id = d2.newLayerId();
    a->pixels.fillRect(QRect(0, 0, 16, 64), premulFromColor(Qt::blue));
    g->insert(0, a);
    auto b = std::make_shared<RasterLayer>();
    b->id = d2.newLayerId();
    b->pixels.fillRect(QRect(40, 0, 16, 64), premulFromColor(Qt::blue));
    g->insert(1, b);
    d2.setCurrentLayer(g->id);
    auto* clip = ops::addRasterLayer(d2, "clip");
    clip->pixels.fillRect(QRect(0, 0, 64, 64), premulFromColor(Qt::green));
    clip->clipping = true;
    CHECK(merged(d2, 5, 5).green() > 250);
    CHECK(merged(d2, 45, 5).green() > 250);
    CHECK(merged(d2, 30, 5).alpha() == 0);

    // Adjustment layer: invert
    Document d3(QSize(16, 16));
    auto* l = ops::addRasterLayer(d3);
    l->pixels.fillRect(d3.bounds(), premulFromColor(QColor(200, 100, 50)));
    ops::addAdjustmentLayer(d3, Adjustment::defaults(AdjustmentType::Invert));
    QColor inv = merged(d3, 3, 3);
    CHECK(std::abs(inv.red() - 55) <= 2 && std::abs(inv.green() - 155) <= 2);

    // Hidden layers do not render
    l->visible = false;
    d3.layerChanged(l);
    CHECK(merged(d3, 3, 3).alpha() == 0);
}

static void testUndo() {
    Document doc(QSize(32, 32));
    auto* l = ops::addRasterLayer(doc);
    quint32 id = l->id;
    doc.pushUndo("paint");
    static_cast<RasterLayer*>(doc.layer(id))->pixels.setPixel(3, 3, 0xffff0000u);
    CHECK(static_cast<RasterLayer*>(doc.layer(id))->pixels.pixel(3, 3) == 0xffff0000u);
    doc.undo();
    CHECK(static_cast<RasterLayer*>(doc.layer(id))->pixels.pixel(3, 3) == 0u);
    doc.redo();
    CHECK(static_cast<RasterLayer*>(doc.layer(id))->pixels.pixel(3, 3) == 0xffff0000u);
    ops::deleteLayer(doc, id);
    CHECK(doc.layer(id) == nullptr);
    CHECK(!doc.root()->children.empty());  // always at least one layer
    doc.undo();
    CHECK(doc.layer(id) != nullptr);
    // merge key collapsing
    int before = doc.historyIndex();
    doc.pushUndo("opacity", "op");
    doc.pushUndo("opacity", "op");
    CHECK(doc.historyIndex() == before + 1);
}

static void testBrush() {
    Document doc(QSize(200, 200));
    auto* l = ops::addRasterLayer(doc);
    StrokeContext ctx;
    ctx.doc = &doc;
    ctx.layer = l;
    ctx.brush = BrushSettings::defaults()[0];  // Pen
    ctx.brush.size = 10;
    ctx.color = Qt::black;
    PressureCurve curve;
    ctx.pressureCurve = &curve;
    {
        BrushStroke s(ctx);
        InputSample a; a.pos = QPointF(20, 100); a.pressure = 1;
        s.begin(a);
        for (int i = 1; i <= 50; ++i) {
            InputSample b; b.pos = QPointF(20 + i * 3, 100); b.pressure = 1;
            s.add(b);
        }
        s.end();
    }
    QColor c = colorFromPremul(l->pixels.pixel(80, 100));
    CHECK(c.alpha() > 250 && c.red() < 5);
    CHECK(l->pixels.pixel(80, 130) == 0u);
    CHECK(!l->preview);
    // Opacity cap: stroke at 50% never exceeds 50% even with overlaps
    auto* l2 = ops::addRasterLayer(doc);
    ctx.layer = l2;
    ctx.brush.opacity = 0.5;
    {
        BrushStroke s(ctx);
        InputSample a; a.pos = QPointF(50, 50); a.pressure = 1;
        s.begin(a);
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i <= 20; ++i) {
                InputSample b; b.pos = QPointF(50 + (k % 2 ? 20 - i : i), 50); b.pressure = 1;
                s.add(b);
            }
        s.end();
    }
    int a = int(l2->pixels.pixel(60, 50) >> 24);
    CHECK(std::abs(a - 128) <= 2);
    // Watercolor brush mixes colors and does not crash on transparent areas
    auto* l3 = ops::addRasterLayer(doc);
    l3->pixels.fillRect(QRect(0, 0, 100, 200), premulFromColor(Qt::blue));
    ctx.layer = l3;
    ctx.brush = BrushSettings::defaults()[5];
    ctx.color = Qt::yellow;
    {
        BrushStroke s(ctx);
        InputSample p; p.pos = QPointF(60, 150); p.pressure = 0.8;
        s.begin(p);
        for (int i = 1; i < 40; ++i) { p.pos = QPointF(60 + i * 2, 150); s.add(p); }
        s.end();
    }
    QColor m = colorFromPremul(l3->pixels.pixel(98, 150));
    CHECK(m.alpha() > 0);
    // Eraser
    ctx.layer = l;
    ctx.brush = BrushSettings::defaults()[10];
    {
        BrushStroke s(ctx);
        InputSample p; p.pos = QPointF(80, 100); p.pressure = 1;
        s.begin(p);
        s.end();
    }
    CHECK((l->pixels.pixel(80, 100) >> 24) < 10);
}

static void testSelection() {
    Document doc(QSize(100, 100));
    QPainterPath p;
    p.addRect(10, 10, 30, 30);
    ops::selectPath(doc, p, ops::SelectMode::Replace, false, "sel");
    CHECK(doc.hasSelection());
    CHECK(doc.selection().pixel(20, 20) == 255 && doc.selection().pixel(50, 50) == 0);
    ops::invertSelection(doc);
    CHECK(doc.selection().pixel(20, 20) == 0 && doc.selection().pixel(50, 50) == 255);
    ops::invertSelection(doc);
    ops::growSelection(doc, 5);
    CHECK(doc.selection().pixel(7, 20) > 200);
    auto* l = ops::addRasterLayer(doc);
    ops::fillLayer(doc, l->id, Qt::red);
    CHECK((l->pixels.pixel(20, 20) >> 24) == 255 && l->pixels.pixel(80, 80) == 0u);
    ops::deselect(doc);
    CHECK(!doc.hasSelection());
}

static void testVectorAndText() {
    Document doc(QSize(200, 100));
    auto* v = ops::addVectorLayer(doc);
    VectorStroke s;
    s.id = 1;
    s.width = 6;
    s.points = {{QPointF(10, 50), 1}, {QPointF(100, 40), 1}, {QPointF(190, 50), 1}};
    v->strokes << s;
    renderVectorLayer(*v);
    CHECK((v->cache.pixel(100, 41) >> 24) > 200);
    CHECK(hitTestStroke(*v, QPointF(100, 41), 2) == 0);
    TextParams tp;
    tp.text = "Hi";
    tp.sizePx = 30;
    tp.origin = QPointF(10, 10);
    auto* t = ops::addTextLayer(doc, tp);
    CHECK(!t->cache.isEmpty());
}

static void testFiles() {
    QTemporaryDir dir;
    Document doc(QSize(300, 200));
    auto* l = ops::addRasterLayer(doc, "Paint");
    l->pixels.fillRect(QRect(10, 10, 50, 50), premulFromColor(QColor(255, 0, 0, 128)));
    auto* g = ops::addGroup(doc, "Folder");
    auto inner = std::make_shared<RasterLayer>();
    inner->id = doc.newLayerId();
    inner->name = "Inner";
    inner->blend = BlendMode::Multiply;
    inner->opacity = 0.5f;
    inner->pixels.fillRect(QRect(100, 100, 20, 20), premulFromColor(Qt::green));
    g->insert(0, inner);
    auto* clip = ops::addRasterLayer(doc, "Clipped");
    clip->clipping = true;
    clip->pixels.setPixel(5, 5, 0xff0000ffu);
    ops::addMask(doc, clip->id, false);
    clip->mask.fillRect(QRect(0, 0, 10, 10), 0);

    QString err;
    QString gxp = dir.filePath("t.gxp");
    CHECK(saveNative(takeSnapshot(doc), gxp, &err));
    auto back = loadNative(gxp, &err);
    CHECK(back != nullptr);
    if (back) {
        CHECK(back->root()->children.size() == doc.root()->children.size());
        CHECK(merged(*back, 20, 20) == merged(doc, 20, 20));
        CHECK(merged(*back, 110, 110) == merged(doc, 110, 110));
    }
    QString psd = dir.filePath("t.psd");
    QStringList warn;
    CHECK(savePsd(doc, psd, &err, &warn));
    auto p = loadPsd(psd, &err, &warn);
    CHECK(p != nullptr);
    if (p) {
        CHECK(p->root()->children.size() == 3);
        auto* pg = p->root()->children[1].get();
        CHECK(pg->isGroup() && static_cast<GroupLayer*>(pg)->children.size() == 1);
        CHECK(p->root()->children[2]->clipping);
        CHECK(p->root()->children[2]->hasMask);
        QColor a = merged(*p, 20, 20), b = merged(doc, 20, 20);
        CHECK(std::abs(a.red() - b.red()) <= 2 && std::abs(a.alpha() - b.alpha()) <= 2);
        QColor c = merged(*p, 110, 110), d = merged(doc, 110, 110);
        CHECK(std::abs(c.green() - d.green()) <= 3 && std::abs(c.alpha() - d.alpha()) <= 3);
    } else {
        std::printf("PSD error: %s\n", qPrintable(err));
    }
}

static void testProjection() {
    Document doc(QSize(1000, 700));
    auto* l = ops::addRasterLayer(doc);
    l->pixels.fillRect(doc.bounds(), premulFromColor(Qt::white));
    l->pixels.setPixel(500, 300, 0xff000000u);
    QImage thumb;
    bool done = false;
    for (int i = 0; i < 50 && !done; ++i) done = doc.projection()->thumbnail(128, 100, thumb);
    CHECK(done);
    CHECK(!thumb.isNull() && thumb.width() <= 128);
    CHECK(qAlpha(thumb.pixel(1, 1)) == 255);
    // huge canvas, mostly uniform: must be fast
    Document big(QSize(100000, 100000));
    auto* bl = ops::addRasterLayer(big);
    bl->pixels.fillRect(big.bounds(), premulFromColor(Qt::white));
    QElapsedTimer t;
    t.start();
    done = false;
    for (int i = 0; i < 400 && !done; ++i) done = big.projection()->thumbnail(256, 200, thumb);
    CHECK(done);
    std::printf("huge canvas thumbnail: %lld ms\n", t.elapsed());
}

static void testCanvasOps() {
    Document doc(QSize(100, 50));
    auto* l = ops::addRasterLayer(doc);
    l->pixels.setPixel(10, 5, 0xffff0000u);
    ops::rotateCanvas(doc, 1);
    CHECK(doc.size() == QSize(50, 100));
    auto* r = static_cast<RasterLayer*>(doc.layer(l->id));
    CHECK(r->pixels.pixel(50 - 1 - 5, 10) == 0xffff0000u);
    ops::flipCanvas(doc, true);
    r = static_cast<RasterLayer*>(doc.layer(l->id));
    CHECK(r->pixels.pixel(5, 10) == 0xffff0000u);
    ops::resizeCanvas(doc, QSize(80, 120), QPoint(10, 10));
    r = static_cast<RasterLayer*>(doc.layer(l->id));
    CHECK(r->pixels.pixel(15, 20) == 0xffff0000u);
    ops::scaleImage(doc, QSize(40, 60), true);
    CHECK(doc.size() == QSize(40, 60));
}

static quint32 compositeAt(Document& doc, int x, int y) {
    float px[4];
    Compositor::samplePixel(doc.root(), x, y, px);
    return pack(px);
}

static void testChannels() {
    Document doc(QSize(256, 256));
    auto paper = std::make_shared<RasterLayer>();
    paper->id = doc.newLayerId();
    paper->pixels.fillRect(doc.bounds(), 0xffffffffu);
    doc.root()->insert(0, paper);
    auto l = std::make_shared<RasterLayer>();
    l->id = doc.newLayerId();
    l->pixels.fillRect(QRect(10, 10, 100, 100), 0xff3070c0u);
    l->pixels.fillRect(QRect(150, 150, 50, 50), 0x80402010u);  // half transparent
    doc.root()->insert(1, l);
    doc.setCurrentLayer(l->id);
    const quint32 before = compositeAt(doc, 50, 50);

    // split into channels: the folder looks like the original
    ops::splitChannels(doc, l->id);
    auto near = [](quint32 a, quint32 b) {
        for (int s = 0; s < 32; s += 8)
            if (std::abs(int((a >> s) & 255) - int((b >> s) & 255)) > 2) return false;
        return true;
    };
    CHECK(near(compositeAt(doc, 50, 50), before));
    doc.undo();

    // invert alpha twice gives the same alpha
    ops::invertAlpha(doc, l->id);
    auto* rl = static_cast<RasterLayer*>(doc.layer(l->id));
    CHECK((rl->pixels.pixel(50, 50) >> 24) == 0);
    CHECK((rl->pixels.pixel(5, 5) >> 24) == 255);
    ops::invertAlpha(doc, l->id);
    CHECK((static_cast<RasterLayer*>(doc.layer(l->id))->pixels.pixel(50, 50) >> 24) == 255);
    doc.undo();
    doc.undo();

    // transparency to mask keeps the look
    ops::transparencyToMask(doc, l->id);
    CHECK(doc.layer(l->id)->hasMask);
    CHECK(near(compositeAt(doc, 160, 160), compositeAt(doc, 160, 160)));
    doc.undo();

    // remove white: white disappears, black stays opaque
    auto scan = std::make_shared<RasterLayer>();
    scan->id = doc.newLayerId();
    scan->pixels.fillRect(QRect(0, 0, 64, 64), 0xffffffffu);
    scan->pixels.fillRect(QRect(10, 10, 4, 40), 0xff000000u);
    scan->pixels.fillRect(QRect(30, 10, 4, 40), 0xff808080u);
    doc.root()->insert(2, scan);
    ops::removeWhite(doc, scan->id);
    auto* sc = static_cast<RasterLayer*>(doc.layer(scan->id));
    CHECK((sc->pixels.pixel(2, 2) >> 24) == 0);
    CHECK((sc->pixels.pixel(11, 20) >> 24) == 255);
    const int ga = int(sc->pixels.pixel(31, 20) >> 24);
    CHECK(ga > 100 && ga < 150);  // gray becomes half transparent black

    // effects render without crashing and change pixels
    l = std::static_pointer_cast<RasterLayer>(doc.layer(l->id)->clone());
    Layer* live = doc.layer(l->id);
    live->fx.chromatic = true;
    live->fx.chromaShift = 5;
    live->fx.screentone = true;
    live->fx.grain = true;
    live->fx.innerShadow = true;
    live->fx.outerGlow = true;
    live->fx.gradientOverlay = true;
    doc.layerChanged(live);
    QImage img = Compositor::renderImage(doc.root(), doc.bounds());
    CHECK(!img.isNull());
    // red fringe appears outside the square on one side
    CHECK(compositeAt(doc, 8, 50) != 0xffffffffu || compositeAt(doc, 112, 50) != 0xffffffffu);
}

static void testLineartColoring() {
    Document doc(QSize(300, 200));
    auto paper = std::make_shared<RasterLayer>();
    paper->id = doc.newLayerId();
    paper->pixels.fillRect(doc.bounds(), 0xffffffffu);
    doc.root()->insert(0, paper);
    auto color = std::make_shared<RasterLayer>();
    color->id = doc.newLayerId();
    doc.root()->insert(1, color);
    // line art: two boxes, the right one has a 4 px gap in its border
    auto lines = std::make_shared<RasterLayer>();
    lines->id = doc.newLayerId();
    auto box = [&](QRect r) {
        lines->pixels.fillRect(QRect(r.left(), r.top(), r.width(), 3), 0xff000000u);
        lines->pixels.fillRect(QRect(r.left(), r.bottom() - 2, r.width(), 3), 0xff000000u);
        lines->pixels.fillRect(QRect(r.left(), r.top(), 3, r.height()), 0xff000000u);
        lines->pixels.fillRect(QRect(r.right() - 2, r.top(), 3, r.height()), 0xff000000u);
    };
    box(QRect(20, 20, 100, 100));
    box(QRect(160, 20, 100, 100));
    lines->pixels.fillRect(QRect(160, 60, 3, 4), 0u);  // the gap
    lines->reference = true;
    doc.root()->insert(2, lines);
    doc.setCurrentLayer(color->id);

    QString err;
    // without closing gaps the fill leaks out of the right box
    MaskSurface leak = floodFillEx(&doc, QPoint(210, 70), 20, FillSource::ReferenceLayers, true, color.get(), 0, &err);
    CHECK(err.isEmpty());
    CHECK(leak.pixel(5, 5) == 255);
    MaskSurface closed = floodFillEx(&doc, QPoint(210, 70), 20, FillSource::ReferenceLayers, true, color.get(), 6, &err);
    CHECK(closed.pixel(5, 5) == 0);
    CHECK(closed.pixel(210, 70) == 255);
    CHECK(closed.pixel(124, 70) == 0);

    // auto flats: two areas, different colors, background left empty, a layer under the lines
    Layer* flats = ops::autoFlats(doc, 6, true, 0, Qt::red, &err);
    CHECK(flats != nullptr);
    if (flats) {
        auto* fl = static_cast<RasterLayer*>(flats);
        const quint32 a = fl->pixels.pixel(70, 70), b = fl->pixels.pixel(210, 70);
        CHECK((a >> 24) == 255 && (b >> 24) == 255 && a != b);
        CHECK((fl->pixels.pixel(5, 5) >> 24) == 0);
        CHECK((fl->pixels.pixel(21, 70) >> 24) == 255);  // goes under the line
        CHECK(doc.root()->indexOf(flats) < doc.root()->indexOf(doc.layer(lines->id)));
    }
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QTemporaryDir tex;
    TextureLibrary::instance().setRoot(tex.path());
    testTiles();
    testBlend();
    testCompositeAndClipping();
    testUndo();
    testBrush();
    testSelection();
    testVectorAndText();
    testFiles();
    testProjection();
    testCanvasOps();
    testChannels();
    testLineartColoring();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
