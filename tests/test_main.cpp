// Engine tests. Run headless: QT_QPA_PLATFORM=offscreen ./gx_tests
#include <QDir>
#include <QElapsedTimer>
#include <QPainterPath>
#include <QGuiApplication>
#include "app/BrushLibrary.h"
#include "app/MaterialActions.h"
#include "app/Materials.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "brush/BrushStroke.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/ReplaceColor.h"
#include "tools/FramesTool.h"
#include "app/Templates.h"
#include "core/FocusLines.h"
#include "core/Guides.h"
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

static void testMultiLayerOps() {
    Document doc(QSize(64, 64));
    QList<quint32> ids;
    for (int i = 0; i < 4; ++i) {
        auto l = std::make_shared<RasterLayer>();
        l->id = doc.newLayerId();
        l->name = QString("L%1").arg(i + 1);
        l->pixels.fillRect(QRect(i * 10, 0, 8, 8), 0xff000000u | quint32(i * 40));
        doc.root()->insert(i, l);
        ids << l->id;
    }
    auto order = [&] {
        QStringList n;
        for (auto& c : doc.root()->children) n << c->name;
        return n.join(',');
    };
    CHECK(order() == "L1,L2,L3,L4");
    ops::moveLayers(doc, {ids[0], ids[1]}, doc.root(), 4);  // to the top
    CHECK(order() == "L3,L4,L1,L2");
    ops::moveLayers(doc, {ids[0], ids[1]}, doc.root(), 1);  // between L3 and L4
    CHECK(order() == "L3,L1,L2,L4");
    doc.undo();
    doc.undo();
    CHECK(order() == "L1,L2,L3,L4");
    ops::duplicateLayers(doc, {ids[1], ids[2]});
    CHECK(doc.root()->children.size() == 6);
    CHECK(doc.selectedCount() == 2);
    doc.undo();
    GroupLayer* g = ops::groupLayers(doc, {ids[0], ids[2]});
    CHECK(g && g->children.size() == 2 && order() == "L2,Folder 1,L4");
    doc.undo();
    CHECK(order() == "L1,L2,L3,L4");
}

static void testNewBrushes() {
    // every default brush draws something and keeps to its settings when saved and loaded
    for (const BrushSettings& d : BrushSettings::defaults()) {
        BrushSettings back = BrushSettings::fromJson(d.toJson());
        CHECK(back.bundleLines == d.bundleLines && back.particles == d.particles && back.group == d.group &&
              std::abs(back.subColorShare - d.subColorShare) < 1e-9 && back.textureFollowsStroke == d.textureFollowsStroke);
        CHECK(!d.group.isEmpty());
        Document doc(QSize(300, 120));
        auto* l = ops::addRasterLayer(doc);
        l->pixels.fillRect(QRect(150, 0, 150, 120), premulFromColor(QColor(200, 60, 60)));  // something to mix, blur or erase
        StrokeContext ctx;
        ctx.doc = &doc;
        ctx.layer = l;
        ctx.brush = d;
        ctx.brush.size = std::min(d.size, 40.0);
        ctx.color = Qt::black;
        ctx.subColor = Qt::white;
        PressureCurve curve;
        ctx.pressureCurve = &curve;
        const QImage before = surfaceToImage(l->pixels, doc.bounds());
        {
            BrushStroke s(ctx);
            for (int i = 0; i <= 80; ++i) {
                InputSample p;
                p.pos = QPointF(20 + i * 3.3, 60 + std::sin(i * 0.1) * 15);
                p.pressure = 0.4 + 0.6 * std::sin(i / 80.0 * M_PI);
                if (i == 0) s.begin(p);
                else s.add(p);
            }
            s.end();
        }
        const QImage after = surfaceToImage(l->pixels, doc.bounds());
        CHECK(after != before);
        if (after == before) std::fprintf(stderr, "  brush '%s' changed nothing\n", qPrintable(d.name));
    }
    // a palette saved by an older version gets the new brushes once
    QTemporaryDir dir;
    const QString file = dir.path() + "/brushes.json";
    {
        QFile f(file);
        f.open(QIODevice::WriteOnly);
        QJsonArray arr;
        BrushSettings pen = BrushSettings::defaults()[0];
        pen.name = "My pen";
        arr.append(pen.toJson());
        f.write(QJsonDocument(QJsonObject{{"version", 1}, {"brushes", arr}}).toJson());
    }
    {
        BrushLibrary lib(file);
        CHECK(lib.indexOfName("My pen") == 0);
        CHECK(lib.indexOfName("Hatching") > 0 && lib.indexOfName("Stipple") > 0);
        CHECK(lib.indexOfName("Pencil (SAI)") > 0 && lib.indexOfName("b blur") > 0);  // the owner's SAI brushes (version 4)
        const int n = lib.count();
        lib.save();
        BrushLibrary again(file);
        CHECK(again.count() == n);  // only once
    }
}

static void testDecorBrushes() {
    auto& lib = TextureLibrary::instance();
    CHECK(lib.stampSets().contains("Leaves") && lib.stampSets().contains("Stars"));
    CHECK(lib.names(TextureKind::Ribbon).contains("Bricks") && lib.names(TextureKind::Ribbon).contains("Lace"));
    auto draw = [](const QString& name, Document& doc, RasterLayer* l) {
        BrushSettings b;
        for (const BrushSettings& d : BrushSettings::defaults())
            if (d.name == name) b = d;
        StrokeContext ctx;
        ctx.doc = &doc;
        ctx.layer = l;
        ctx.brush = b;
        ctx.color = Qt::black;
        PressureCurve curve;
        ctx.pressureCurve = &curve;
        BrushStroke s(ctx);
        for (int i = 0; i <= 100; ++i) {
            InputSample p;
            p.pos = QPointF(20 + i * 5.6, 100);
            p.pressure = 1;
            if (i == 0) s.begin(p);
            else s.add(p);
        }
        s.end();
    };
    {   // a brick ribbon: bricks with mortar gaps between them, all across the band
        Document doc(QSize(600, 200));
        auto* l = ops::addRasterLayer(doc);
        draw("Bricks", doc, l);
        QImage img = surfaceToImage(l->pixels, QRect(100, 100 - 10, 400, 1));
        int gaps = 0, ink = 0;
        bool in = false;
        for (int x = 0; x < img.width(); ++x) {
            const bool on = qAlpha(img.pixel(x, 0)) > 128;
            ink += on;
            if (!on && in) ++gaps;
            in = on;
        }
        CHECK(ink > 200 && gaps >= 3);
    }
    {   // stamps: separate pictures, not one line
        Document doc(QSize(600, 200));
        auto* l = ops::addRasterLayer(doc);
        draw("Stars", doc, l);
        const QRect eb = l->pixels.exactBounds();
        CHECK(eb.width() > 300);
        QImage img = surfaceToImage(l->pixels, doc.bounds());
        int ink = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) ink += qAlpha(img.pixel(x, y)) > 128;
        CHECK(ink > 200 && ink < 600 * 200 / 4);
    }
}

static void testMaterials() {
    QTemporaryDir root;
    Materials& m = Materials::instance();
    m.setRoot(root.path());
    CHECK(m.patterns().contains("Checker") && m.patterns().contains("Polka dots"));
    CHECK(Materials::tones().size() >= 6 && m.gradients().size() >= 8);
    const QImage checker = m.pattern("Checker");
    CHECK(!checker.isNull() && checker.width() >= 4);
    {   // a pattern on a new layer: covers the selection only, with holes where the tile is empty
        Document doc(QSize(200, 200));
        ops::addRasterLayer(doc);
        QPainterPath p;
        p.addRect(20, 20, 100, 100);
        ops::selectPath(doc, p, ops::SelectMode::Replace, false, "sel");
        m.patternScale = 1;
        m.patternTint = true;
        const int before = int(doc.root()->children.size());
        auto* l = static_cast<RasterLayer*>(materials::placePattern(doc, "Checker", Qt::red, nullptr));
        CHECK(l && int(doc.root()->children.size()) == before + 1);
        CHECK(m.currentPattern() == "Checker");
        if (l) {
            int on = 0, off = 0;
            for (int y = 20; y < 120; ++y)
                for (int x = 20; x < 120; ++x) ((l->pixels.pixel(x, y) >> 24) > 128 ? on : off)++;
            CHECK(on > 2000 && off > 2000);
            CHECK((l->pixels.pixel(150, 150) >> 24) == 0);  // outside the selection
            const quint32 px = l->pixels.pixel(20, 20) ? l->pixels.pixel(20, 20) : l->pixels.pixel(20 + checker.width() / 2, 20);
            CHECK(((px >> 16) & 0xff) > 200 && ((px >> 8) & 0xff) < 30);  // in the main color
        }
        doc.undo();
        CHECK(int(doc.root()->children.size()) == before);
    }
    {   // Edit > Fill with pattern on the current layer, then a stroke around the selection
        Document doc(QSize(200, 200));
        auto* l = ops::addRasterLayer(doc);
        QString err;
        m.setCurrentPattern("Checker");
        CHECK(materials::fillWithPattern(doc, Qt::black, &err));
        l = static_cast<RasterLayer*>(doc.currentLayer());
        int on = 0;
        for (int x = 0; x < 200; ++x) on += (l->pixels.pixel(x, 100) >> 24) > 128;
        CHECK(on > 40 && on < 160);
        ops::clearLayer(doc, l->id);
        QPainterPath p;
        p.addRect(50, 50, 100, 100);
        ops::selectPath(doc, p, ops::SelectMode::Replace, false, "sel");
        CHECK(materials::strokeSelection(doc, 6, materials::StrokePosition::Outside, Qt::black, false, &err));
        l = static_cast<RasterLayer*>(doc.currentLayer());
        auto a = [&](int x, int y) { return int(l->pixels.pixel(x, y) >> 24); };
        CHECK(a(47, 100) == 255 && a(45, 100) == 255 && a(52, 100) == 0 && a(40, 100) == 0 && a(100, 100) == 0);
        l->pixels.fillRect(doc.bounds(), 0);  // (Clear works only inside the selection)
        CHECK(materials::strokeSelection(doc, 6, materials::StrokePosition::Inside, Qt::black, false, &err));
        l = static_cast<RasterLayer*>(doc.currentLayer());
        CHECK(a(52, 100) == 255 && a(47, 100) == 0 && a(60, 100) == 0);
        l->pixels.fillRect(doc.bounds(), 0);
        CHECK(materials::strokeSelection(doc, 8, materials::StrokePosition::Center, Qt::black, false, &err));
        l = static_cast<RasterLayer*>(doc.currentLayer());
        CHECK(a(48, 100) == 255 && a(51, 100) == 255 && a(40, 100) == 0 && a(60, 100) == 0);
        ops::deselect(doc);
        CHECK(!materials::strokeSelection(doc, 4, materials::StrokePosition::Inside, Qt::black, false, &err) && !err.isEmpty());
    }
    {   // a screentone layer and a gradient map layer
        Document doc(QSize(120, 120));
        ops::addRasterLayer(doc);
        auto* t = static_cast<RasterLayer*>(materials::placeTone(doc, 1, Qt::black, nullptr));
        CHECK(t && t->fx.screentone && (t->pixels.pixel(60, 60) >> 24) == 255);
        auto* g = dynamic_cast<AdjustmentLayer*>(materials::placeGradient(doc, 2, nullptr));
        CHECK(g && g->adjustment.type == AdjustmentType::GradientMap);
        if (g) CHECK(Gradient::fromJson(g->adjustment.params.value("gradient").toObject()).stops.size() >= 2);
        // the gradient goes into the user's set and can be removed again
        const int n = int(m.gradients().size());
        CHECK(materials::saveLayerGradient(doc, "My map", nullptr));
        CHECK(int(m.gradients().size()) == n + 1 && m.isUserGradient("My map"));
        m.removeGradient("My map");
        CHECK(int(m.gradients().size()) == n);
    }
    {   // part of a drawing saved as a picture and as a pattern, then placed again
        Document doc(QSize(100, 100));
        auto* l = ops::addRasterLayer(doc);
        l->pixels.fillRect(QRect(10, 10, 20, 30), 0xff00ff00u);
        QString err;
        const QString pic = materials::saveSelection(doc, false, "Leaf", &err);
        CHECK(!pic.isEmpty());
        QString file;
        for (const QString& f : m.images())
            if (QFileInfo(f).completeBaseName() == pic) file = f;
        CHECK(!file.isEmpty() && QImage(file).size() == QSize(20, 30));
        auto* placed = static_cast<RasterLayer*>(materials::placeImage(doc, file, QPointF(70, 70), &err));
        CHECK(placed && (placed->pixels.pixel(70, 70) >> 24) == 255 && (placed->pixels.pixel(55, 70) >> 24) == 0);
        CHECK(m.removeFile(file) && !m.images().contains(file));
        doc.setCurrentLayer(l->id);
        QPainterPath p;
        p.addRect(0, 0, 40, 40);
        ops::selectPath(doc, p, ops::SelectMode::Replace, false, "sel");
        const QString pat = materials::saveSelection(doc, true, "Square", &err);
        CHECK(pat == "Square" && m.patterns().contains("Square") && m.pattern("Square").size() == QSize(40, 40));
        CHECK(m.currentPattern() == "Square");
    }
}

static void testRulersAndLines() {
    auto near = [](QPointF a, QPointF b, double tol = 0.6) { return std::hypot(a.x() - b.x(), a.y() - b.y()) <= tol; };
    Ruler r;
    r.kind = Ruler::Straight;
    r.a = QPointF(0, 100);
    r.b = QPointF(200, 100);
    CHECK(near(r.constrain({}, QPointF(50, 140)), QPointF(50, 100)));
    r.kind = Ruler::Parallel;  // parallel to a-b through where the stroke started
    CHECK(near(r.constrain(QPointF(10, 300), QPointF(90, 320)), QPointF(90, 300)));
    r.kind = Ruler::Fan;
    r.a = QPointF(0, 0);
    CHECK(near(r.constrain(QPointF(100, 100), QPointF(200, 180)), QPointF(190, 190)));
    r.kind = Ruler::Ellipse;
    r.a = QPointF(500, 400);
    r.rx = 200;
    r.ry = 100;
    r.angle = 30;
    for (QPointF p : {QPointF(900, 400), QPointF(500, 410), QPointF(320, 250), QPointF(505, 380)}) {
        const QPointF q = r.constrain({}, p) - r.a;
        const double an = -30 * M_PI / 180;
        const double x = q.x() * std::cos(an) - q.y() * std::sin(an), y = q.x() * std::sin(an) + q.y() * std::cos(an);
        CHECK(std::abs(std::hypot(x / 200, y / 100) - 1) < 1e-6);
    }
    r.kind = Ruler::Concentric;  // the ellipse through the start point
    {
        const QPointF start = r.a + QPointF(std::cos(M_PI / 6), std::sin(M_PI / 6)) * 100;  // on the axis, half of rx
        const QPointF q = r.constrain(start, r.a + QPointF(0, 300)) - r.a;
        const double an = -30 * M_PI / 180;
        const double x = q.x() * std::cos(an) - q.y() * std::sin(an), y = q.x() * std::sin(an) + q.y() * std::cos(an);
        CHECK(std::abs(std::hypot(x / 200, y / 100) - 0.5) < 1e-6);
    }
    r.kind = Ruler::Curve;
    r.a = QPointF(0, 0);
    r.b = QPointF(100, 100);
    r.c = QPointF(200, 0);
    CHECK(near(r.constrain({}, QPointF(100, 160)), QPointF(100, 100)));  // the curve goes through b
    CHECK(near(r.constrain({}, QPointF(-50, -20)), QPointF(0, 0)));
    // handles: dragging the second handle of an ellipse sets its size and angle
    r.kind = Ruler::Ellipse;
    r.a = QPointF(0, 0);
    r.setHandle(1, QPointF(0, 150), false);
    CHECK(std::abs(r.rx - 150) < 1e-6 && std::abs(r.angle - 90) < 1e-6);
    const Ruler back = Ruler::fromJson(r.toJson());
    CHECK(back.kind == r.kind && near(back.a, r.a) && std::abs(back.rx - r.rx) < 1e-9 && std::abs(back.angle - r.angle) < 1e-9);
    r.transform(QTransform::fromTranslate(10, 20));
    CHECK(near(r.a, QPointF(10, 20)) && std::abs(r.rx - 150) < 1e-6);

    // focus lines: empty in the middle, dense at the edges, the same every time
    Document doc(QSize(400, 300));
    auto* l = ops::addRasterLayer(doc);
    FocusLines f;
    f.area = doc.bounds();
    f.center = QPointF(200, 150);
    f.rx = 80;
    f.ry = 50;
    f.count = 90;
    f.width = 8;
    f.gaps = 0;
    CHECK(f.shapes().size() == 90 && f.shapes() == f.shapes());
    f.renderInto(doc, *l);
    auto alpha = [&](int x, int y) { return int(l->pixels.pixel(x, y) >> 24); };
    int middle = 0, edge = 0;
    for (int y = 120; y < 180; ++y)
        for (int x = 160; x < 240; ++x) middle += alpha(x, y) > 0;
    for (int y = 0; y < 300; ++y) edge += alpha(2, y) > 128;
    CHECK(middle == 0);
    CHECK(edge > 40 && edge < 280);
    CHECK(l->generatorEditable() && FocusLines::isFocusLines(l->generator));
    const FocusLines g = FocusLines::fromJson(l->generator);
    CHECK(g.count == 90 && g.center == f.center && g.shapes() == f.shapes());
    // painting on the layer fixes the lines
    doc.pixelsChanged(QRect(0, 0, 1, 1), l);
    CHECK(!l->generatorEditable());
    // speed lines inside a selection, along the x axis
    QPainterPath sel;
    sel.addRect(50, 50, 200, 100);
    ops::selectPath(doc, sel, ops::SelectMode::Replace, false, "sel");
    auto* l2 = ops::addRasterLayer(doc);
    FocusLines s;
    s.kind = FocusLines::Speed;
    s.area = doc.selectionBounds();
    s.count = 40;
    s.width = 3;
    s.length = 0.8;
    s.gaps = 0;
    s.renderInto(doc, *l2);
    const QRect eb = l2->pixels.exactBounds();
    CHECK(!eb.isEmpty() && QRect(50, 50, 200, 100).contains(eb));
    int rows = 0;
    for (int y = 50; y < 150; ++y) {
        bool any = false;
        for (int x = 50; x < 250 && !any; ++x) any = (l2->pixels.pixel(x, y) >> 24) > 100;
        rows += any;
    }
    CHECK(rows > 40);
    // the ruler and the lines settings are saved in the picture
    doc.ruler.kind = Ruler::Concentric;
    doc.ruler.a = QPointF(12, 34);
    QTemporaryDir dir;
    const QString file = dir.filePath("lines.gxp");
    QString err;
    CHECK(saveNative(takeSnapshot(doc), file, &err));
    auto loaded = loadNative(file, &err);
    CHECK(loaded && loaded->ruler.kind == Ruler::Concentric && loaded->ruler.a == QPointF(12, 34));
    if (loaded) {
        auto* ll = dynamic_cast<RasterLayer*>(loaded->layer(l2->id));
        CHECK(ll && ll->generatorEditable() && FocusLines::fromJson(ll->generator).kind == FocusLines::Speed);
        auto* l1 = dynamic_cast<RasterLayer*>(loaded->layer(l->id));
        CHECK(l1 && l1->generator.isEmpty());  // painted on: kept as plain pixels
    }
}

static void testSaiBrushes() {
    const QVector<BrushSettings> mine = BrushSettings::customDefaults();
    CHECK(mine.size() == 6);
    for (const BrushSettings& b : mine) CHECK(b.group == BrushSettings::customGroup());
    CHECK(BrushSettings::groups().first() == BrushSettings::customGroup());
    {
        BrushSettings b = mine[1];
        const BrushSettings back = BrushSettings::fromJson(b.toJson());
        CHECK(back.sizeSoftness == b.sizeSoftness && back.densitySoftness == b.densitySoftness && back.blendSoftness == b.blendSoftness);
    }
    // SAI's size list for [ and ]
    CHECK(brushSizeStep(9, +1) == 10 && brushSizeStep(9, -1) == 8 && brushSizeStep(10.5, +1) == 12 && brushSizeStep(10.5, -1) == 10);
    CHECK(brushSizeStep(5000, +1) == 5000 && brushSizeStep(0.5, -1) == 0.5);
    // the softness of pressure: at half pressure a soft brush is wider than a hard one
    auto width = [](double softness) {
        Document doc(QSize(400, 120));
        auto* l = ops::addRasterLayer(doc);
        BrushSettings b;
        b.size = 40;
        b.minSize = 0;
        b.sizePressure = true;
        b.hardness = 1;
        b.sizeSoftness = softness;
        StrokeContext ctx;
        ctx.doc = &doc;
        ctx.layer = l;
        ctx.brush = b;
        ctx.color = Qt::black;
        BrushStroke st(ctx);
        for (int i = 0; i <= 50; ++i) {
            InputSample p;
            p.pos = QPointF(50 + i * 6, 60);
            p.pressure = 0.5;
            if (i == 0) st.begin(p);
            else st.add(p);
        }
        st.end();
        return l->pixels.exactBounds().height();
    };
    const int hard = width(0), plain = width(100), soft = width(200);
    std::printf("pressure softness at half pressure: %d / %d / %d px\n", hard, plain, soft);
    CHECK(hard < plain && plain < soft);
    // full blending with full dilution moves the colors and does not wash them out (as in SAI)
    {
        Document doc(QSize(300, 100));
        auto* l = ops::addRasterLayer(doc);
        l->pixels.fillRect(QRect(0, 0, 150, 100), 0xffdd2222u);
        l->pixels.fillRect(QRect(150, 0, 150, 100), 0xff2244ddu);
        StrokeContext ctx;
        ctx.doc = &doc;
        ctx.layer = l;
        ctx.brush = mine[1];  // "111"
        ctx.color = Qt::green;
        BrushStroke st(ctx);
        for (int i = 0; i <= 40; ++i) {
            InputSample p;
            p.pos = QPointF(60 + i * 4, 50);
            p.pressure = 0.8;
            if (i == 0) st.begin(p);
            else st.add(p);
        }
        st.end();
        int holes = 0, green = 0;
        for (int x = 40; x < 260; x += 3) {
            const quint32 c = l->pixels.pixel(x, 50);
            holes += (c >> 24) < 250;
            green += ((c >> 8) & 0xff) > 0x80;
        }
        CHECK(holes == 0 && green == 0);
        CHECK(((l->pixels.pixel(190, 50) >> 16) & 0xff) > 0x30);  // red pulled into the blue
    }
}

static void testSeamlessAndReplace() {
    // replace color: red becomes blue, a darker red a darker blue, green stays
    {
        ReplaceColorParams p;
        p.from = QColor(220, 30, 30);
        p.to = QColor(30, 60, 220);
        p.tolerance = 60;
        p.softness = 20;
        p.keepShading = true;
        float px[12] = {220 / 255.f, 30 / 255.f, 30 / 255.f, 1, 110 / 255.f, 15 / 255.f, 15 / 255.f, 1, 30 / 255.f, 200 / 255.f, 30 / 255.f, 1};
        replaceColorPixels(px, 3, p);
        CHECK(px[2] > 0.8f && px[0] < 0.2f);                 // blue
        CHECK(px[6] > px[4] && px[6] < px[2]);               // a darker blue
        CHECK(std::abs(px[9] - 200 / 255.f) < 1e-4f);        // green untouched
    }
    // offset by half wraps the pixels and undoes in one step
    {
        Document doc(QSize(200, 100));
        auto* l = ops::addRasterLayer(doc);
        l->pixels.fillRect(QRect(0, 0, 10, 10), 0xffff0000u);
        offsetWrap(doc, 100, 50);
        l = static_cast<RasterLayer*>(doc.currentLayer());
        CHECK((l->pixels.pixel(105, 55) >> 24) == 255 && (l->pixels.pixel(5, 5) >> 24) == 0);
        offsetWrap(doc, 150, 75);  // a stroke over the corner shows on all four sides
        l = static_cast<RasterLayer*>(doc.currentLayer());
        CHECK((l->pixels.pixel(55, 25) >> 24) == 255);
        doc.undo();
        doc.undo();
        l = static_cast<RasterLayer*>(doc.currentLayer());
        CHECK((l->pixels.pixel(5, 5) >> 24) == 255);
    }
    // wrap painting: a stroke over the right edge comes back on the left
    {
        Document doc(QSize(300, 100));
        doc.seamless = true;
        auto* l = ops::addRasterLayer(doc);
        StrokeContext ctx;
        ctx.doc = &doc;
        ctx.layer = l;
        ctx.brush.size = 20;
        ctx.brush.hardness = 1;
        ctx.color = Qt::black;
        ctx.wrap = true;
        BrushStroke st(ctx);
        for (int i = 0; i <= 20; ++i) {
            InputSample p;
            p.pos = QPointF(250 + i * 4, 50);  // to x = 330: 30 px past the edge
            p.pressure = 1;
            if (i == 0) st.begin(p);
            else st.add(p);
        }
        st.end();
        CHECK((l->pixels.pixel(20, 50) >> 24) > 200 && (l->pixels.pixel(295, 50) >> 24) > 200 && (l->pixels.pixel(150, 50) >> 24) == 0);
        // the flag is kept in the file
        QTemporaryDir dir;
        QString err;
        CHECK(saveNative(takeSnapshot(doc), dir.filePath("s.gxp"), &err));
        auto back = loadNative(dir.filePath("s.gxp"), &err);
        CHECK(back && back->seamless);
    }
    // templates: layer sets
    {
        auto ill = templates::make("illustration", QSize(800, 600), 350, Qt::white);
        CHECK(ill && ill->root()->children.size() == 7);  // paper + 6
        if (ill) CHECK(ill->currentLayer() && ill->currentLayer()->opacity < 0.5f);  // starts on the sketch
        auto comic = templates::make("comic", QSize(1000, 1400), 350, Qt::white);
        bool frames = false;
        if (comic)
            for (auto& c : comic->root()->children)
                if (auto* r = dynamic_cast<RasterLayer*>(c.get()); r && r->reference && !r->pixels.exactBounds().isEmpty()) frames = true;
        CHECK(frames);
        auto pat = templates::make("pattern", QSize(256, 256), 350, Qt::transparent);
        CHECK(pat && pat->seamless && pat->root()->children.size() == 1);
    }
}

static void testFramesAndBalloons() {
    // a page of frames and a cut
    FrameSet fs = FrameSet::page(QRectF(100, 100, 800, 1200), {1, 2, 1}, 40);
    CHECK(fs.frames.size() == 4);
    const int cut = fs.cut(QPointF(50, 300), QPointF(950, 300), 30);  // across the top frame
    CHECK(cut == 1 && fs.frames.size() == 5);
    double gapTop = 1e9, gapBottom = -1e9;
    for (const QPolygonF& f : fs.frames) {
        const QRectF r = f.boundingRect();
        if (r.top() < 150 && r.bottom() <= 300) gapBottom = std::max(gapBottom, r.bottom());
        if (r.top() >= 300 && r.top() < 400) gapTop = std::min(gapTop, r.top());
    }
    CHECK(std::abs((gapTop - gapBottom) - 30) < 0.5);
    const FrameSet back = FrameSet::fromJson(fs.toJson());
    CHECK(back.frames.size() == 5 && FrameSet::isFrames(fs.toJson()));
    {
        Document doc(QSize(1000, 1400));
        auto* l = ops::addRasterLayer(doc);
        fs.renderInto(doc, *l);
        CHECK((l->pixels.pixel(100, 700) >> 24) > 200 && (l->pixels.pixel(500, 700) >> 24) == 0 && l->generatorEditable());
    }
    // speech balloons: every shape draws around the text, with its tail
    TextParams t;
    t.text = "Hello!";
    t.sizePx = 40;
    t.origin = QPointF(200, 200);
    const QRectF plain = textBounds(t);
    for (int b = 1; b <= 4; ++b) {
        t.balloon = b;
        const QPainterPath path = balloonPath(t);
        CHECK(!path.isEmpty());
        const QRectF withBalloon = textBounds(t);
        CHECK(withBalloon.contains(plain) && withBalloon.height() > plain.height() * 2);
    }
    t.balloon = TextParams::Oval;
    t.tailTip = QPointF(-150, 300);
    CHECK(textBounds(t).left() < 200 - 140);
    const TextParams tb = TextParams::fromJson(t.toJson());
    CHECK(tb.balloon == TextParams::Oval && tb.tailTip == t.tailTip && tb.tail);
    TextLayer tl;
    tl.text = t;
    renderTextLayer(tl);
    const QPointF in = QPointF(200, 200) + QPointF(-10, 10);  // inside the oval, outside the letters: white fill
    CHECK(tl.cache.pixel(int(in.x()), int(in.y())) == 0xffffffffu);
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
    testMultiLayerOps();
    testNewBrushes();
    testDecorBrushes();
    testMaterials();
    testRulersAndLines();
    testSaiBrushes();
    testSeamlessAndReplace();
    testFramesAndBalloons();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
