// Scenario tests that drive the real UI: draw, then use tools and check the pixels.
// Usage: gx_uiscenarios   (run with QT_QPA_PLATFORM=offscreen)
#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "app/App.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/Layer.h"
#include "io/Recovery.h"
#include "tools/ToolManager.h"
#include "core/LayerOps.h"
#include "ui/CanvasView.h"
#include "ui/LayerPanel.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

using namespace gx;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (cond) ++g_pass;                                                         \
        else {                                                                      \
            ++g_fail;                                                               \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                           \
    } while (0)

static void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static CanvasView* view() { return app()->activeView(); }

static void mouse(QEvent::Type t, QPointF canvasPos, Qt::MouseButtons bs, Qt::KeyboardModifiers m = Qt::NoModifier) {
    CanvasView* v = view();
    QPointF p = v->toWidget(canvasPos);
    QMouseEvent e(t, p, v->mapToGlobal(p), Qt::LeftButton, bs, m);
    QCoreApplication::sendEvent(v, &e);
}

static void dragCanvas(QPointF a, QPointF b, Qt::KeyboardModifiers m = Qt::NoModifier, int steps = 20) {
    mouse(QEvent::MouseButtonPress, a, Qt::LeftButton, m);
    for (int i = 1; i <= steps; ++i) mouse(QEvent::MouseMove, a + (b - a) * (double(i) / steps), Qt::LeftButton, m);
    mouse(QEvent::MouseButtonRelease, b, Qt::NoButton, m);
    pump(30);
}

static void key(int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
    QKeyEvent p(QEvent::KeyPress, k, m);
    QCoreApplication::sendEvent(view(), &p);
    QKeyEvent r(QEvent::KeyRelease, k, m);
    QCoreApplication::sendEvent(view(), &r);
    pump(20);
}

// Opaque pixels a layer currently shows (including a tool preview).
static long long opaquePixels(Layer* l) {
    if (!l) return -1;
    const RasterSurface* s = l->displaySurface();
    if (!s) return -1;
    QRect r = s->exactBounds();
    if (r.isEmpty()) return 0;
    QImage img = surfaceToImage(*s, r);
    long long n = 0;
    for (int y = 0; y < img.height(); ++y) {
        const quint32* row = reinterpret_cast<const quint32*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) n += (row[x] >> 24) > 128;
    }
    return n;
}

static std::shared_ptr<Document> makeDoc() {
    auto doc = std::make_shared<Document>(QSize(800, 600));
    auto paper = std::make_shared<RasterLayer>();
    paper->id = doc->newLayerId();
    paper->name = "Paper";
    paper->pixels.fillRect(doc->bounds(), 0xffffffffu);
    doc->root()->insert(0, paper);
    auto layer = std::make_shared<RasterLayer>();
    layer->id = doc->newLayerId();
    layer->name = "Layer 1";
    doc->root()->insert(1, layer);
    doc->setCurrentLayer(layer->id);
    doc->setModified(false);
    return doc;
}

static Layer* paintSomething(Document* d) {
    app()->tools()->setCurrent("brush");
    Layer* l = d->currentLayer();
    // a thick filled block made of strokes
    for (int y = 200; y <= 400; y += 8) dragCanvas(QPointF(250, y), QPointF(550, y));
    return l;
}

static void clickLayer(MainWindow& w, quint32 id, Qt::KeyboardModifiers m = Qt::NoModifier) {
    auto* panel = w.findChild<LayerPanel*>();
    QTreeView* tree = panel->tree();
    auto* model = static_cast<LayerModel*>(tree->model());
    QRect r = tree->visualRect(model->indexOf(id));
    QPointF p(r.right() - 40, r.center().y());  // on the name, away from eye and thumbnail
    QMouseEvent press(QEvent::MouseButtonPress, p, tree->viewport()->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, m);
    QCoreApplication::sendEvent(tree->viewport(), &press);
    QMouseEvent rel(QEvent::MouseButtonRelease, p, tree->viewport()->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, m);
    QCoreApplication::sendEvent(tree->viewport(), &rel);
    pump(30);
}

static QRect px(Document* d, quint32 id) {
    auto* l = dynamic_cast<RasterLayer*>(d->layer(id));
    return l ? l->pixels.exactBounds() : QRect();
}

static QList<quint32> addPaintedLayers(Document* d, int n) {
    QList<quint32> ids;
    for (int i = 0; i < n; ++i) {
        RasterLayer* l = ops::addRasterLayer(*d, QStringLiteral("L%1").arg(i + 1));
        l->pixels.fillRect(QRect(100 + i * 60, 100, 50, 50), 0xff2040a0u);
        ids << l->id;
    }
    d->structureChanged();
    pump(50);
    return ids;
}

static void scenario(const char* name, const std::function<void(MainWindow&)>& fn, MainWindow& w) {
    std::fprintf(stderr, "== %s\n", name);
    w.addDocument(makeDoc());
    pump(80);
    view()->fitToWindow();
    pump(30);
    fn(w);
    Document* d = view()->doc();
    app()->tools()->setCurrent("brush");
    pump(30);
    d->setModified(false);
}

int main(int argc, char** argv) {
    Q_INIT_RESOURCE(resources);
    QCoreApplication::setOrganizationName("GraphiXTest");
    QCoreApplication::setApplicationName("GraphiX Paint Scenarios");
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication a(argc, argv);
    App application;
    application.init();
    Theme::apply("dark", 0);
    Recovery::instance().start(application.dataDir() + "/recovery", 0);
    MainWindow w;
    w.resize(1400, 900);
    w.show();
    pump(200);

    scenario("mesh warp keeps the picture", [](MainWindow&) {
        Document* d = view()->doc();
        Layer* l = paintSomething(d);
        long long before = opaquePixels(l);
        CHECK(before > 20000);
        app()->tools()->setCurrent("warp");
        pump(100);
        l = d->currentLayer();
        long long during = opaquePixels(l);
        std::fprintf(stderr, "   before %lld, warp on %lld\n", before, during);
        CHECK(std::llabs(during - before) < before / 50);
        // pull the middle of the mesh
        dragCanvas(QPointF(400, 300), QPointF(430, 320));
        pump(60);
        app()->tools()->commitPending();
        pump(60);
        l = d->currentLayer();
        long long after = opaquePixels(l);
        std::fprintf(stderr, "   after apply %lld\n", after);
        CHECK(after > before / 2);
        // a second time
        app()->tools()->setCurrent("brush");
        app()->tools()->setCurrent("warp");
        pump(100);
        l = d->currentLayer();
        CHECK(opaquePixels(l) > before / 2);
        app()->tools()->commitPending();
    }, w);

    scenario("transform keeps the picture", [](MainWindow&) {
        Document* d = view()->doc();
        Layer* l = paintSomething(d);
        long long before = opaquePixels(l);
        app()->tools()->setCurrent("transform");
        pump(100);
        l = d->currentLayer();
        CHECK(std::llabs(opaquePixels(l) - before) < before / 50);
        dragCanvas(QPointF(400, 300), QPointF(420, 310));  // move
        app()->tools()->commitPending();
        pump(60);
        CHECK(std::llabs(opaquePixels(d->currentLayer()) - before) < before / 20);
    }, w);

    scenario("liquify keeps the picture", [](MainWindow&) {
        Document* d = view()->doc();
        Layer* l = paintSomething(d);
        long long before = opaquePixels(l);
        app()->tools()->setCurrent("liquify");
        pump(100);
        l = d->currentLayer();
        CHECK(std::llabs(opaquePixels(l) - before) < before / 50);
        dragCanvas(QPointF(300, 300), QPointF(360, 300));
        app()->tools()->commitPending();
        pump(60);
        CHECK(opaquePixels(d->currentLayer()) > before / 2);
    }, w);

    scenario("several layers: ctrl / shift click and group operations", [](MainWindow& w) {
        Document* d = view()->doc();
        QList<quint32> ids = addPaintedLayers(d, 4);
        clickLayer(w, ids[0]);
        CHECK(d->selectedCount() == 1);
        clickLayer(w, ids[2], Qt::ControlModifier);
        CHECK(d->selectedCount() == 2);
        CHECK(d->isLayerSelected(ids[0]) && d->isLayerSelected(ids[2]) && !d->isLayerSelected(ids[1]));
        clickLayer(w, ids[2], Qt::ControlModifier);  // ctrl-click again removes it
        CHECK(d->selectedCount() == 1);
        clickLayer(w, ids[3], Qt::ShiftModifier);    // range L1..L4
        CHECK(d->selectedCount() == 4);
        clickLayer(w, ids[1]);                        // plain click: just one again
        CHECK(d->selectedCount() == 1 && d->currentLayerId() == ids[1]);

        // move two layers together with the move tool
        clickLayer(w, ids[0]);
        clickLayer(w, ids[1], Qt::ControlModifier);
        QRect b0 = px(d, (ids[0])), b1 = px(d, (ids[1])), b2 = px(d, (ids[2]));
        app()->tools()->setCurrent("move");
        dragCanvas(QPointF(300, 300), QPointF(320, 330));
        CHECK(px(d, (ids[0])) == b0.translated(20, 30));
        CHECK(px(d, (ids[1])) == b1.translated(20, 30));
        CHECK(px(d, (ids[2])) == b2);
        app()->tools()->setCurrent("brush");

        // group them into a folder
        ops::groupLayers(*d, d->selectedLayerIds());
        Layer* g = d->currentLayer();
        CHECK(g && g->isGroup() && static_cast<GroupLayer*>(g)->children.size() == 2);
        d->undo();
        CHECK(d->layer(ids[0]) && d->layer(ids[0])->parent == d->root());

        // merge three layers into one
        d->setLayerSelection({ids[0], ids[1], ids[2]}, ids[2]);
        const int before = int(d->root()->children.size());
        Layer* m = ops::mergeLayers(*d, d->selectedLayerIds());
        CHECK(m && int(d->root()->children.size()) == before - 2);
        d->undo();

        // delete two
        d->setLayerSelection({ids[1], ids[3]}, ids[3]);
        ops::deleteLayers(*d, d->selectedLayerIds());
        CHECK(!d->layer(ids[1]) && !d->layer(ids[3]) && d->layer(ids[0]) && d->layer(ids[2]));
        d->undo();
        CHECK(d->layer(ids[1]) && d->layer(ids[3]));

        // transform two at once
        d->setLayerSelection({ids[0], ids[1]}, ids[1]);
        b0 = px(d, (ids[0]));
        b1 = px(d, (ids[1]));
        app()->tools()->setCurrent("transform");
        pump(50);
        QRectF both = QRectF(b0 | b1);
        dragCanvas(both.center(), both.center() + QPointF(10, 0));
        app()->tools()->commitPending();
        pump(50);
        CHECK(std::abs(px(d, (ids[0])).left() - (b0.left() + 10)) <= 1);
        CHECK(std::abs(px(d, (ids[1])).left() - (b1.left() + 10)) <= 1);
    }, w);

    std::fprintf(stderr, "%d passed, %d failed\n", g_pass, g_fail);
    std::fflush(nullptr);
    std::_Exit(g_fail ? 1 : 0);
}
