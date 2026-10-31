// Scenario tests that drive the real UI: draw, then use tools and check the pixels.
// Usage: gx_uiscenarios   (run with QT_QPA_PLATFORM=offscreen)
#include <QApplication>
#include <QComboBox>
#include <QPainter>
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
#include "ui/ColorRangeDialog.h"
#include "ui/LineartDialog.h"
#include "ui/PreferencesDialog.h"
#include <QToolButton>
#include "ui/MainWindow.h"
#include "ui/ReferenceOverlay.h"
#include "ui/CommandPalette.h"
#include <QMenuBar>
#include "tools/FillTools.h"
#include <QPainterPath>
#include "ui/EffectFilterDialog.h"
#include "ui/BrushEditor.h"
#include "ui/BrushPreview.h"
#include "ui/ToolPanel.h"
#include "ui/Widgets.h"
#include "app/BrushLibrary.h"
#include "ui/ReferencePanel.h"
#include "app/I18n.h"
#include "app/Recorder.h"
#include "io/VideoWriter.h"
#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QThreadPool>
#include "ui/Theme.h"
#include "ui/MaterialsPanel.h"
#include "tools/FocusLinesTool.h"
#include "tools/SelectTools.h"
#include "tools/TransformTool.h"
#include "ui/LiquifyDialog.h"
#include "ui/ThemeEditor.h"
#include "ui/ReplaceColorDialog.h"
#include "app/Templates.h"
#include "ui/Dialogs.h"
#include "ui/NavigatorPanel.h"
#include <QTemporaryDir>
#include <QWheelEvent>
#include "core/LiquifyField.h"
#include "app/Shortcuts.h"
#include "core/Textures.h"
#include "ui/ColorPanel.h"
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include "core/FocusLines.h"
#include <QMenu>
#include "app/Materials.h"
#include <QDialog>
#include <QDockWidget>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>

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
    if (qEnvironmentVariableIsSet("GX_LANG")) setLanguage(qEnvironmentVariable("GX_LANG"));
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

    scenario("make line art from a painting", [](MainWindow& w) {
        Document* d = view()->doc();
        paintSomething(d);
        const int before = int(d->root()->children.size());
        {
            LineartDialog dlg(d, &w);
            pump(400);  // the preview runs in the background
            dlg.accept();
        }
        pump(50);
        CHECK(int(d->root()->children.size()) == before + 1);
        Layer* la = d->currentLayer();
        CHECK(la && la->reference);
        CHECK(opaquePixels(la) > 100);
        d->undo();
        CHECK(int(d->root()->children.size()) == before);
    }, w);

    scenario("color range selects the painted color", [](MainWindow& w) {
        Document* d = view()->doc();
        app()->setForeground(QColor(20, 40, 160));
        paintSomething(d);
        {
            ColorRangeDialog dlg(d, &w);
            pump(50);
            dlg.accept();
        }
        CHECK(d->hasSelection());
        CHECK(d->selection().pixel(400, 296) > 128);
        CHECK(d->selection().pixel(10, 10) == 0);
        app()->setForeground(Qt::black);
    }, w);

    scenario("settings window applies right away", [](MainWindow& w) {
        const int oldIcon = app()->prefs().iconSize;
        {
            PreferencesDialog dlg(&w);
            pump(30);
            // the settings window edits a copy; change it the way the controls do
            app()->prefs().iconSize = 28;
            app()->preferencesChanged();
        }
        pump(50);
        bool found = false;
        for (QToolButton* b : w.findChildren<QToolButton*>())
            if (b->iconSize() == QSize(28, 28)) found = true;
        CHECK(found);
        app()->prefs().iconSize = oldIcon;
        app()->preferencesChanged();
        // grid / guides toggle draws without problems
        app()->prefs().showGrid = app()->prefs().showThirds = true;
        app()->preferencesChanged();
        view()->repaint();
        app()->prefs().showGrid = app()->prefs().showThirds = false;
        app()->preferencesChanged();
        app()->setGrayscaleView(true);
        view()->repaint();
        app()->setGrayscaleView(false);
    }, w);

    scenario("video recording: record, pause, continue, save", [](MainWindow& w) {
        Document* d = view()->doc();
        Layer* l = d->currentLayer();
        auto* r = static_cast<RasterLayer*>(l);
        r->pixels.fillRect(QRect(100, 100, 200, 200), 0xffff0000u);  // the "tracing" layer
        auto blue = std::make_shared<RasterLayer>();
        blue->id = d->newLayerId();
        blue->name = "Blue";
        blue->pixels.fillRect(QRect(500, 100, 200, 200), 0xff0000ffu);
        d->root()->insert(2, blue);
        d->structureChanged();
        l->hideInTimelapse = true;
        pump(50);
        Recorder& rec = Recorder::instance();
        rec.setNewMode(Recorder::Mode::Canvas);
        CHECK(Recorder::hiddenLayerCount(d) == 1);
        QString err;
        const bool started = rec.start(d, &err);
        if (!started) std::fprintf(stderr, "NOTE recording did not start: %s\n", qPrintable(err));
        CHECK(started);
        CHECK(rec.state(d) == Recorder::State::Recording);
        CHECK(w.windowTitle().startsWith(QString::fromUtf8("● REC")));
        // some drawing: the green block grows
        for (int i = 0; i < 8; ++i) {
            blue->pixels.fillRect(QRect(100, 400, 60 * (i + 1), 80), 0xff00a000u);
            d->pixelsChanged(QRect(100, 400, 480, 80), blue.get());
            pump(40);
            rec.tick();
        }
        if (qEnvironmentVariableIsSet("GX_SHOTS")) w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/recording.png");
        rec.pause();
        CHECK(rec.state(d) == Recorder::State::Paused);
        if (qEnvironmentVariableIsSet("GX_SHOTS")) {
            pump(30);
            w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/paused.png");
        }
        CHECK(!w.windowTitle().startsWith(QString::fromUtf8("●")));
        const qint64 len1 = rec.length(d);
        CHECK(len1 > 0);
        Recorder::Job job = rec.job(d);
        CHECK(job.files.size() == 1);
        // the video: the hidden red layer is not there, the rest is
        {
            VideoReader vr;
            QString rerr;
            const bool opened = !job.files.isEmpty() && vr.open(job.files.first(), &rerr);
            if (!opened) std::fprintf(stderr, "NOTE could not read %s: %s\n", qPrintable(job.files.value(0)), qPrintable(rerr));
            CHECK(opened);
            QImage frame, last;
            qint64 dur = 0, total = 0;
            int n = 0;
            while (vr.readFrame(frame, &dur)) {
                last = frame;
                total += dur;
                ++n;
            }
            if (n < 5 || std::abs(total - len1) >= kVideoSecond / 10)
                std::fprintf(stderr, "NOTE frames %d, read length %lld, recorded %lld\n", n, (long long)total, (long long)len1);
            CHECK(n >= 5);
            CHECK(std::abs(total - len1) < kVideoSecond / 10);
            CHECK(!last.isNull() && last.width() % 16 == 0 && last.height() % 16 == 0);
            if (!last.isNull()) {
                const double k = std::min(last.width() / 800.0, last.height() / 600.0);
                const QPointF off((last.width() - 800 * k) / 2, (last.height() - 600 * k) / 2);
                auto at = [&](int x, int y) { return QColor(last.pixel(int(off.x() + x * k), int(off.y() + y * k))); };
                CHECK(at(200, 200).red() > 220 && at(200, 200).green() > 220);  // white paper, no red
                CHECK(at(600, 200).blue() > 180 && at(600, 200).red() < 80);    // blue
                CHECK(at(500, 440).green() > 100 && at(500, 440).red() < 80);   // the last green block
            }
        }
        // continue: a second piece, then save sped up and at real speed
        CHECK(rec.start(d, &err));
        for (int i = 0; i < 5; ++i) {
            d->pixelsChanged(QRect(0, 0, 50, 50), blue.get());
            pump(40);
            rec.tick();
        }
        rec.pause();
        job = rec.job(d);
        CHECK(job.files.size() == 2);
        CHECK(rec.length(d) > len1);
        const QString out1 = QDir::tempPath() + "/gx_rec_real.mp4", out2 = QDir::tempPath() + "/gx_rec_fast.mp4";
        Recorder::ExportOptions opt;
        opt.holdEnd = true;
        const bool ex1 = Recorder::exportVideo(job, out1, opt, {}, &err);
        if (!ex1) std::fprintf(stderr, "NOTE export failed: %s\n", qPrintable(err));
        CHECK(ex1);
        auto lengthOf = [](const QString& f, int* frames) {
            VideoReader vr;
            qint64 t = 0, dur = 0;
            QImage img;
            *frames = 0;
            if (!vr.open(f, nullptr)) return qint64(-1);
            while (vr.readFrame(img, &dur)) {
                t += dur;
                ++*frames;
            }
            return t;
        };
        int frames = 0;
        const qint64 realLen = lengthOf(out1, &frames);
        std::fprintf(stderr, "INFO real-speed video %lld (recorded %lld), %d frames\n", (long long)realLen, (long long)job.length, frames);
        CHECK(std::abs(realLen - (job.length + 2 * kVideoSecond)) < kVideoSecond / 10);
        opt.targetLength = job.length / 3;  // three times faster
        opt.holdEnd = false;
        double lastProgress = 0;
        const bool ex2 = Recorder::exportVideo(job, out2, opt, [&](double p) { lastProgress = p; return true; }, &err);
        if (!ex2) std::fprintf(stderr, "NOTE fast export failed: %s\n", qPrintable(err));
        CHECK(ex2);
        CHECK(lastProgress > 0.99);
        const qint64 fastLen = lengthOf(out2, &frames);
        std::fprintf(stderr, "INFO fast video %lld (target %lld), %d frames\n", (long long)fastLen, (long long)opt.targetLength, frames);
        CHECK(fastLen > 0 && std::abs(fastLen - opt.targetLength) < kVideoSecond / 10 + job.length / 30);
        // cancel
        CHECK(!Recorder::exportVideo(job, out2 + ".x", opt, [](double) { return false; }, &err) && err.isEmpty());
        CHECK(!QFileInfo::exists(out2 + ".x"));
        QFile::remove(out1);
        QFile::remove(out2);
        rec.discard(d);
        CHECK(rec.state(d) == Recorder::State::None);
        CHECK(rec.length(d) == 0);
    }, w);

    scenario("video recording: the program window without hidden layers", [](MainWindow& w) {
        Document* d = view()->doc();
        auto* l = static_cast<RasterLayer*>(d->currentLayer());
        l->pixels.fillRect(d->bounds(), 0xffff0000u);  // all red, hidden from recordings
        l->hideInTimelapse = true;
        d->pixelsChanged(d->bounds(), l);
        pump(80);
        CanvasView* v = view();
        const QPoint c(v->width() / 2, v->height() / 2);
        QImage normal = v->grab().toImage();
        CanvasView::setRecordingPaint(true);
        QImage forRec = v->grab().toImage();
        CanvasView::setRecordingPaint(false);
        const double k = normal.devicePixelRatio();
        CHECK(QColor(normal.pixel(int(c.x() * k), int(c.y() * k))).red() > 200 && QColor(normal.pixel(int(c.x() * k), int(c.y() * k))).green() < 60);
        CHECK(QColor(forRec.pixel(int(c.x() * k), int(c.y() * k))).green() > 200);  // white paper in the recording
        // the screen itself still shows the layer afterwards
        QImage again = v->grab().toImage();
        CHECK(QColor(again.pixel(int(c.x() * k), int(c.y() * k))).green() < 60);

        Recorder& rec = Recorder::instance();
        rec.setNewMode(Recorder::Mode::Window);
        QString err;
        CHECK(rec.start(d, &err));
        CHECK(rec.mode(d) == Recorder::Mode::Window);
        for (int i = 0; i < 4; ++i) {
            QMouseEvent move(QEvent::MouseMove, QPointF(c) + QPointF(i, i), v->mapToGlobal(QPointF(c)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(v, &move);
            pump(40);
            rec.tick();
        }
        rec.pause();
        Recorder::Job job = rec.job(d);
        CHECK(job.files.size() == 1 && job.mode == Recorder::Mode::Window);
        VideoReader vr;
        QImage frame;
        qint64 dur = 0;
        CHECK(!job.files.isEmpty() && vr.open(job.files.first(), nullptr) && vr.readFrame(frame, &dur));
        CHECK(frame.width() == job.width && frame.height() == job.height);
        // the canvas in the middle of the window is white, not red
        if (!frame.isNull()) {
            const QPoint vc = v->mapTo(&w, c);
            const double kx = double(frame.width()) / w.width(), ky = double(frame.height()) / w.height();
            const double kk = std::min(kx, ky);
            const QPointF off((frame.width() - w.width() * kk) / 2, (frame.height() - w.height() * kk) / 2);
            const QColor px(frame.pixel(int(off.x() + vc.x() * kk), int(off.y() + vc.y() * kk)));
            CHECK(px.green() > 180 && px.red() > 180);
        }
        rec.discard(d);
        rec.setNewMode(Recorder::Mode::Canvas);
        l->hideInTimelapse = false;
    }, w);

    if (qEnvironmentVariableIsSet("GX_BENCH")) {
        // how long one recorded frame takes on a big picture
        auto big = std::make_shared<Document>(QSize(4000, 3000));
        auto paper = std::make_shared<RasterLayer>();
        paper->id = big->newLayerId();
        paper->pixels.fillRect(big->bounds(), 0xffffffffu);
        big->root()->insert(0, paper);
        auto lay = std::make_shared<RasterLayer>();
        lay->id = big->newLayerId();
        for (int i = 0; i < 40; ++i) lay->pixels.fillRect(QRect(i * 90, i * 60, 300, 200), 0xff000000u | quint32(i * 6000));
        big->root()->insert(1, lay);
        big->setCurrentLayer(lay->id);
        w.addDocument(big);
        pump(300);
        for (auto m : {Recorder::Mode::Canvas, Recorder::Mode::Window}) {
            Recorder& rec = Recorder::instance();
            rec.setNewMode(m);
            QElapsedTimer t;
            t.start();
            rec.start(big.get(), nullptr);
            const qint64 first = t.elapsed();
            t.restart();
            for (int i = 0; i < 60; ++i) {
                lay->pixels.fillRect(QRect(1000 + i * 20, 1500, 30, 30), 0xff336699u);
                big->pixelsChanged(QRect(1000 + i * 20, 1500, 30, 30), lay.get());
                rec.tick();
            }
            std::fprintf(stderr, "BENCH %s: start %lld ms, %.1f ms per frame\n", m == Recorder::Mode::Canvas ? "canvas" : "window", first, t.elapsed() / 60.0);
            rec.pause();
            rec.discard(big.get());
        }
        Recorder::instance().setNewMode(Recorder::Mode::Canvas);
    }

    scenario("brushes: hatching, stipple, two colors, settings window", [](MainWindow& w) {
        Document* d = view()->doc();
        BrushLibrary* lib = app()->brushes();
        const int hatch = lib->indexOfName("Hatching");
        const int white = lib->indexOfName("Hatching with white");
        const int stip = lib->indexOfName("Stipple");
        CHECK(hatch >= 0 && white >= 0 && stip >= 0);
        if (hatch < 0 || white < 0 || stip < 0) return;
        // hatching: one stroke makes several separate lines across the stroke
        app()->tools()->selectBrushSlot(hatch);
        pump(250);  // a new view ignores the mouse for a moment (pen events come first)
        app()->setForeground(Qt::black);
        app()->setBackground(Qt::white);
        dragCanvas(QPointF(100, 300), QPointF(700, 300), Qt::NoModifier, 60);
        Layer* l = d->currentLayer();
        {
            QRect eb = l->displaySurface() ? l->displaySurface()->exactBounds() : QRect();
            std::fprintf(stderr, "INFO hatching stroke bounds %d,%d %dx%d, brush %s lines %d\n", eb.x(), eb.y(), eb.width(), eb.height(),
                         qPrintable(app()->brushes()->current().name), app()->brushes()->current().bundleLines);
        }
        int runs = 0;
        {
            QImage img = surfaceToImage(*l->displaySurface(), QRect(400, 250, 1, 100));
            bool in = false;
            for (int y = 0; y < img.height(); ++y) {
                const bool ink = qAlpha(img.pixel(0, y)) > 100;
                if (ink && !in) ++runs;
                in = ink;
            }
        }
        std::fprintf(stderr, "INFO hatching lines across: %d\n", runs);
        CHECK(runs >= 4);
        // the second color: white lines over a black block
        auto* r = static_cast<RasterLayer*>(l);
        r->pixels.fillRect(QRect(0, 400, 800, 120), 0xff000000u);
        d->pixelsChanged(QRect(0, 400, 800, 120), l);
        app()->tools()->selectBrushSlot(white);
        dragCanvas(QPointF(100, 460), QPointF(700, 460), Qt::NoModifier, 60);
        {
            QImage img = surfaceToImage(*l->displaySurface(), QRect(100, 400, 600, 120));
            int whites = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) whites += qGray(img.pixel(x, y)) > 120 && qAlpha(img.pixel(x, y)) > 200;
            std::fprintf(stderr, "INFO white pixels from the second color: %d\n", whites);
            CHECK(whites > 150);
        }
        // stipple: separate dots
        app()->tools()->selectBrushSlot(stip);
        dragCanvas(QPointF(100, 150), QPointF(700, 150), Qt::NoModifier, 60);
        {
            QImage img = surfaceToImage(*l->displaySurface(), QRect(100, 100, 600, 100));
            int ink = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) ink += qAlpha(img.pixel(x, y)) > 128;
            const double share = double(ink) / (img.width() * img.height());
            std::fprintf(stderr, "INFO stipple covers %.3f\n", share);
            CHECK(share > 0.005 && share < 0.4);
        }
        // the settings window: a change applies to the brush, Cancel brings it back
        const BrushSettings before = lib->at(hatch);
        {
            BrushEditorDialog dlg(hatch, &w);
            dlg.show();
            pump(150);
            auto* pad = dlg.findChild<BrushTestPad*>();
            CHECK(pad != nullptr);
            if (pad) {
                auto send = [pad](QEvent::Type t, QPointF p, Qt::MouseButtons bs) {
                    QMouseEvent e(t, p, pad->mapToGlobal(p), Qt::LeftButton, bs, Qt::NoModifier);
                    QCoreApplication::sendEvent(pad, &e);
                };
                send(QEvent::MouseButtonPress, QPointF(30, 100), Qt::LeftButton);
                for (int i = 1; i <= 30; ++i) send(QEvent::MouseMove, QPointF(30 + i * 10, 100 + std::sin(i * 0.3) * 30), Qt::LeftButton);
                send(QEvent::MouseButtonRelease, QPointF(330, 100), Qt::NoButton);
                pump(120);
            }
            const QList<ValueBar*> bars = dlg.findChildren<ValueBar*>();
            ValueBar* linesBar = nullptr;
            for (ValueBar* b : bars)
                if (b->toolTip() == T("Lines in one stroke (hatching). 1 = an ordinary brush.")) linesBar = b;
            CHECK(linesBar != nullptr);
            if (linesBar) {
                linesBar->setValue(12);
                Q_EMIT linesBar->valueChanged(12);
            }
            pump(150);
            CHECK(lib->at(hatch).bundleLines == 12);
            if (qEnvironmentVariableIsSet("GX_SHOTS")) dlg.grab().save(qEnvironmentVariable("GX_SHOTS") + "/brush_editor.png");
            dlg.reject();
        }
        CHECK(lib->at(hatch).bundleLines == before.bundleLines);
        // the preview draws something
        QImage pv = renderBrushPreview(lib->at(hatch), QSize(200, 60), Qt::black, Qt::white, true);
        int dark = 0;
        for (int y = 0; y < pv.height(); ++y)
            for (int x = 0; x < pv.width() / 2; ++x) dark += qGray(pv.pixel(x, y)) < 100;
        CHECK(dark > 30);
        if (qEnvironmentVariableIsSet("GX_SHOTS")) {
            if (auto* tp = w.findChild<ToolPanel*>()) tp->grab().save(qEnvironmentVariable("GX_SHOTS") + "/tool_panel.png");
        }
        app()->tools()->selectBrushSlot(0);
    }, w);

    scenario("effects as filters: preview, apply, cancel, undo", [](MainWindow& w) {
        Document* d = view()->doc();
        auto* l = static_cast<RasterLayer*>(d->currentLayer());
        l->pixels.fillRect(QRect(300, 200, 200, 150), 0xff2050c0u);
        d->pixelsChanged(QRect(300, 200, 200, 150), l);
        const long long before = opaquePixels(l);
        auto waitPreview = [](EffectFilterDialog&) {
            QThreadPool::globalInstance()->waitForDone(5000);
            pump(400);
            QThreadPool::globalInstance()->waitForDone(5000);
            pump(200);
        };
        {   // outline: more pixels around the shape
            EffectFilterDialog dlg(d, l->id, "outline", &w);
            dlg.show();
            waitPreview(dlg);
            CHECK(l->preview != nullptr);  // the canvas shows the result before OK
            CHECK(opaquePixels(l) > before);
            dlg.setNewLayer(true);  // the default: the effect on its own layer above
            dlg.accept();
        }
        CHECK(!l->preview);
        CHECK(opaquePixels(l) == before);  // the layer itself is not changed
        auto* fxLayer = dynamic_cast<RasterLayer*>(d->currentLayer());
        CHECK(fxLayer && fxLayer != l && d->root()->indexOf(fxLayer) == d->root()->indexOf(l) + 1);
        const long long outlineOnly = fxLayer ? opaquePixels(fxLayer) : 0;
        std::fprintf(stderr, "INFO outline layer %lld pixels\n", outlineOnly);
        CHECK(outlineOnly > 0 && fxLayer && (fxLayer->pixels.pixel(400, 275) >> 24) == 0);  // the inside stays on the old layer
        const quint32 fxId = fxLayer ? fxLayer->id : 0;
        d->undo();
        CHECK(!d->layer(fxId));
        l = static_cast<RasterLayer*>(d->currentLayer());
        CHECK(opaquePixels(l) == before);
        {   // cancel: nothing changes
            EffectFilterDialog dlg(d, l->id, "chromatic", &w);
            dlg.show();
            waitPreview(dlg);
            dlg.reject();
        }
        CHECK(!l->preview && opaquePixels(l) == before);
        {   // chromatic aberration into the layer itself: the edges get colored fringes
            EffectFilterDialog dlg(d, l->id, "chromatic", &w);
            dlg.show();
            waitPreview(dlg);
            dlg.setNewLayer(false);
            dlg.accept();
        }
        QImage img = surfaceToImage(l->pixels, QRect(290, 190, 220, 170));
        bool fringe = false;
        for (int y = 0; y < img.height() && !fringe; ++y)
            for (int x = 0; x < img.width(); ++x) {
                QColor c = QColor::fromRgba(img.pixel(x, y));
                if (c.alpha() > 40 && c.red() > c.blue() + 40) { fringe = true; break; }
            }
        CHECK(fringe);
    }, w);

    scenario("lasso fill: closed areas inside the shape", [](MainWindow&) {
        Document* d = view()->doc();
        pump(250);
        // line art on a reference layer: a circle with a small gap, and a line through the background
        auto lines = std::make_shared<RasterLayer>();
        lines->id = d->newLayerId();
        lines->name = "Lines";
        lines->reference = true;
        {
            QImage img(800, 600, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::black, 4));
            p.drawArc(QRectF(250, 150, 300, 300), 5 * 16, 350 * 16);  // a gap of about 10 degrees (~25 px)... too wide
            p.drawEllipse(QRectF(270, 170, 260, 260));                 // inner closed circle
            p.drawLine(QPointF(0, 520), QPointF(800, 520));
            p.end();
            imageToSurface(img, lines->pixels, QPoint(0, 0));
        }
        d->root()->insert(2, lines);
        d->structureChanged();
        Layer* fillLayer = d->currentLayer();  // "Layer 1", under the lines
        auto* lf = static_cast<LassoFillTool*>(app()->tools()->tool("lasso-fill"));
        CHECK(lf != nullptr);
        if (!lf) return;
        lf->mode = 1;
        lf->source = int(FillSource::ReferenceLayers);
        lf->gapClose = 3;
        lf->under = 2;
        app()->tools()->setCurrent("lasso-fill");
        app()->setForeground(QColor(220, 60, 60));
        // a rough loop around the inner circle (it also cuts through the ring between the circles)
        QVector<QPointF> loop;
        for (int i = 0; i <= 48; ++i) {
            const double a = i * 2 * M_PI / 48;
            loop << QPointF(400 + std::cos(a) * 145, 300 + std::sin(a) * 145);
        }
        mouse(QEvent::MouseButtonPress, loop.first(), Qt::LeftButton);
        for (const QPointF& p : loop) mouse(QEvent::MouseMove, p, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, loop.last(), Qt::NoButton);
        pump(100);
        auto at = [&](int x, int y) { return QColor::fromRgba(surfaceToImage(*fillLayer->displaySurface(), QRect(x, y, 1, 1)).pixel(0, 0)); };
        CHECK(at(400, 300).alpha() > 200 && at(400, 300).red() > 180);  // the middle of the closed circle
        CHECK(at(400, 172).alpha() > 200);                               // under the line of the circle
        CHECK(at(400, 100).alpha() == 0);                                // the background stays empty
        CHECK(at(100, 300).alpha() == 0);
        app()->tools()->setCurrent("brush");
    }, w);

    scenario("find a command", [](MainWindow& w) {
        QHash<QString, QAction*> acts;
        for (QAction* a : w.findChildren<QAction*>())
            if (!a->property("en").toString().isEmpty()) acts.insert(a->property("en").toString(), a);
        CommandPalette pal(w.menuBar(), acts, &w);
        pal.show();
        pump(50);
        CHECK(pal.resultCount() > 50);  // everything when nothing is typed
        pal.setFilterText("chromatic");
        pump(20);
        CHECK(pal.resultCount() >= 1);
        pal.setFilterText("zzzz-nothing");
        pump(20);
        CHECK(pal.resultCount() == 0);
        // running a command: "Rule of thirds" turns the guide on
        const bool thirds = app()->prefs().showThirds;
        pal.setFilterText("Rule of thirds");
        pump(20);
        CHECK(pal.resultCount() >= 1);
        CHECK(pal.runCurrent());
        pump(50);
        CHECK(app()->prefs().showThirds != thirds);
        app()->prefs().showThirds = thirds;
        app()->preferencesChanged();
    }, w);

    scenario("materials: drag onto the canvas, fill with pattern, stroke selection", [](MainWindow& w) {
        Document* d = view()->doc();
        auto* panel = w.findChild<MaterialsPanel*>();
        CHECK(panel && panel->tabs()->count() == 4);
        if (!panel) return;
        for (QListWidget* l : panel->findChildren<QListWidget*>()) CHECK(l->count() > 0 || l->parentWidget() == panel->tabs()->widget(0));
        QHash<QString, QAction*> acts;
        for (QAction* a : w.findChildren<QAction*>())
            if (!a->property("en").toString().isEmpty()) acts.insert(a->property("en").toString(), a);
        CHECK(acts.contains("Fill with pattern") && acts.contains("Stroke selection..."));
        auto drop = [&](const QString& kind, const QString& value, QPointF canvasPos) {
            auto* md = new QMimeData;
            md->setData(QString::fromLatin1(kMaterialMime), QJsonDocument(QJsonObject{{"kind", kind}, {"value", value}}).toJson());
            const QPointF at = view()->mapTo(&w, view()->toWidget(canvasPos).toPoint());
            QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, md, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&w, &enter);
            CHECK(enter.isAccepted());
            QDropEvent ev(at, Qt::CopyAction, md, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&w, &ev);
            pump(30);
            delete md;
        };
        // a pattern dropped on the canvas: a new layer filled with it
        size_t layers = d->root()->children.size();
        drop("pattern", "Polka dots", QPointF(400, 300));
        CHECK(d->root()->children.size() == layers + 1);
        const long long dots = opaquePixels(d->currentLayer());
        CHECK(dots > 800 * 600 / 20 && dots < 800 * 600 * 9 / 10);
        // a picture lands where it is dropped
        QImage pic(40, 40, QImage::Format_ARGB32_Premultiplied);
        pic.fill(Qt::blue);
        const QString file = Materials::instance().addImage(pic, "Blue square");
        drop("image", file, QPointF(200, 150));
        CHECK(d->root()->children.size() == layers + 2);
        auto* pl = dynamic_cast<RasterLayer*>(d->currentLayer());
        CHECK(pl && (pl->pixels.pixel(200, 150) >> 24) == 255 && (pl->pixels.pixel(260, 150) >> 24) == 0);
        Materials::instance().removeFile(file);
        // a screentone and a gradient map from the panel (double-click does the same)
        Q_EMIT panel->place("tone", "0");
        pump(20);
        auto* tl = dynamic_cast<RasterLayer*>(d->currentLayer());
        CHECK(tl && tl->fx.screentone);
        Q_EMIT panel->place("gradient", "0");
        pump(20);
        auto* gl = dynamic_cast<AdjustmentLayer*>(d->currentLayer());
        CHECK(gl && gl->adjustment.type == AdjustmentType::GradientMap);
        // Edit > Fill with pattern on a fresh layer
        ops::addRasterLayer(*d);
        Materials::instance().setCurrentPattern("Stripes");
        acts["Fill with pattern"]->trigger();
        pump(20);
        const long long stripes = opaquePixels(d->currentLayer());
        CHECK(stripes > 800 * 600 / 10 && stripes < 800 * 600 * 9 / 10);
        // Edit > Stroke selection: an outline around a rectangle
        ops::addRasterLayer(*d);
        QPainterPath r;
        r.addRect(300, 200, 200, 150);
        ops::selectPath(*d, r, ops::SelectMode::Replace, false, "sel");
        QTimer::singleShot(150, [] {
            if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                if (auto* bb = dlg->findChild<QDialogButtonBox*>()) bb->button(QDialogButtonBox::Ok)->click();
        });
        acts["Stroke selection..."]->trigger();
        pump(30);
        auto* sl = dynamic_cast<RasterLayer*>(d->currentLayer());
        CHECK(sl && opaquePixels(sl) > 600 * 2 && (sl->pixels.pixel(400, 275) >> 24) == 0);
        std::fprintf(stderr, "INFO stroke pixels %lld\n", sl ? opaquePixels(sl) : 0LL);
        ops::deselect(*d);
        // undo takes the stroke back (deselect and select are steps too)
        const quint32 sid = sl ? sl->id : 0;
        bool gone = false;
        for (int i = 0; i < 4 && !gone; ++i) {
            d->undo();
            Layer* l = d->layer(sid);
            gone = l && opaquePixels(l) == 0;
        }
        CHECK(gone);
        if (qEnvironmentVariableIsSet("GX_SHOTS")) {
            for (QDockWidget* dk : w.findChildren<QDockWidget*>())
                if (dk->objectName() == "materialsDock") dk->raise();
            for (int t = 0; t < 4; ++t) {
                panel->tabs()->setCurrentIndex(t);
                pump(40);
                panel->grab().save(qEnvironmentVariable("GX_SHOTS") + QString("/materials_%1.png").arg(t));
            }
            w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/materials_window.png");
        }
    }, w);

    scenario("rulers lead the brush; focus and speed lines", [](MainWindow& w) {
        Document* d = view()->doc();
        Layer* l = d->currentLayer();
        app()->tools()->setCurrent("brush");
        pump(250);
        // a wobbly stroke along a straight ruler stays on the ruler line
        d->ruler.kind = Ruler::Straight;
        d->ruler.snap = true;
        d->ruler.a = QPointF(100, 300);
        d->ruler.b = QPointF(700, 300);
        d->guidesModified();
        mouse(QEvent::MouseButtonPress, QPointF(150, 320), Qt::LeftButton);
        for (int i = 1; i <= 40; ++i) mouse(QEvent::MouseMove, QPointF(150 + i * 12, 300 + ((i % 2) ? 25 : -25)), Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, QPointF(630, 300), Qt::NoButton);
        pump(30);
        QRect eb = px(d, l->id);
        std::fprintf(stderr, "INFO straight ruler stroke %d,%d %dx%d\n", eb.x(), eb.y(), eb.width(), eb.height());
        CHECK(eb.width() > 400 && eb.height() < 30 && std::abs(eb.center().y() - 300) < 4);
        // around an ellipse: every painted pixel is close to the ellipse
        d->undo();
        d->ruler.kind = Ruler::Ellipse;
        d->ruler.a = QPointF(400, 300);
        d->ruler.rx = 200;
        d->ruler.ry = 120;
        d->ruler.angle = 0;
        mouse(QEvent::MouseButtonPress, QPointF(620, 300), Qt::LeftButton);
        for (int i = 1; i <= 60; ++i) {
            const double t = i * 2 * M_PI / 60, k = (i % 3) ? 0.85 : 1.15;
            mouse(QEvent::MouseMove, QPointF(400 + 200 * k * std::cos(t), 300 + 120 * k * std::sin(t)), Qt::LeftButton);
        }
        mouse(QEvent::MouseButtonRelease, QPointF(600, 300), Qt::NoButton);
        pump(30);
        {
            auto* rl = static_cast<RasterLayer*>(d->currentLayer());
            int far = 0, inked = 0;
            for (int y = 150; y < 450; y += 2)
                for (int x = 180; x < 620; x += 2)
                    if ((rl->pixels.pixel(x, y) >> 24) > 128) {
                        ++inked;
                        const double e = std::hypot((x - 400) / 200.0, (y - 300) / 120.0);
                        far += std::abs(e - 1) > 0.06;
                    }
            std::fprintf(stderr, "INFO ellipse ruler: %d inked, %d off the ellipse\n", inked, far);
            CHECK(inked > 200 && far == 0);
        }
        d->undo();
        // the toolbar button turns snapping off; the menu chooses the ruler
        QToolButton* rb = nullptr;
        for (QToolButton* b : w.findChildren<QToolButton*>())
            if (b->menu() && b->menu()->actions().size() == 7) rb = b;
        CHECK(rb && rb->isChecked());
        if (rb) {
            rb->click();
            pump(20);
            CHECK(!d->ruler.snap && d->ruler.isOn());
            rb->menu()->actions()[Ruler::Fan]->trigger();
            pump(20);
            CHECK(d->ruler.kind == Ruler::Fan && d->ruler.snap && rb->isChecked() && app()->tools()->current()->id() == "ruler");
            if (qEnvironmentVariableIsSet("GX_SHOTS")) {
                rb->menu()->actions()[Ruler::Concentric]->trigger();
                pump(250);
                w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/ruler_concentric.png");
                rb->menu()->actions()[Ruler::Curve]->trigger();
                pump(50);
                w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/ruler_curve.png");
            }
            rb->menu()->actions()[Ruler::None]->trigger();
            pump(20);
            CHECK(!d->ruler.isOn() && !rb->isChecked());
        }
        // focus lines: a drag makes a lines layer, the panel changes it, undo takes it back
        const size_t layers = d->root()->children.size();
        app()->tools()->setCurrent("focuslines");
        pump(250);
        auto* ft = dynamic_cast<FocusLinesTool*>(app()->tools()->current());
        CHECK(ft != nullptr);
        if (!ft) return;
        ft->settings().gaps = 0;
        ft->settings().count = 100;
        dragCanvas(QPointF(400, 300), QPointF(520, 380));
        pump(30);
        CHECK(d->root()->children.size() == layers + 1);
        auto* fl = dynamic_cast<RasterLayer*>(d->currentLayer());
        CHECK(fl && fl->generatorEditable() && ft->editedLayer() == fl);
        if (fl) {
            CHECK((fl->pixels.pixel(400, 300) >> 24) == 0 && (fl->pixels.pixel(460, 330) >> 24) == 0);
            const long long before = opaquePixels(fl);
            CHECK(before > 800 * 600 / 30);
            ft->settings().width = 30;  // as the panel does
            ft->settingsChanged();
            pump(200);
            fl = dynamic_cast<RasterLayer*>(d->currentLayer());
            const long long thick = opaquePixels(fl);
            std::fprintf(stderr, "INFO focus lines pixels %lld -> %lld after a thicker setting\n", before, thick);
            CHECK(thick > before * 2);
            CHECK(d->root()->children.size() == layers + 1);  // the same layer
            if (qEnvironmentVariableIsSet("GX_SHOTS")) w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/focuslines.png");
        }
        // speed lines on a new layer (another kind does not change the focus layer)
        app()->tools()->setCurrent("speedlines");
        pump(250);
        dragCanvas(QPointF(600, 300), QPointF(200, 300));
        pump(30);
        CHECK(d->root()->children.size() == layers + 2);
        auto* sl = dynamic_cast<RasterLayer*>(d->currentLayer());
        CHECK(sl && FocusLines::fromJson(sl->generator).kind == FocusLines::Speed);
        if (sl) CHECK(std::abs(std::abs(FocusLines::fromJson(sl->generator).angle) - 180) < 1);
        if (qEnvironmentVariableIsSet("GX_SHOTS")) w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/speedlines.png");
        d->undo();
        d->undo();
        d->undo();
        pump(20);
        CHECK(d->root()->children.size() == layers);
    }, w);

    scenario("selection: wand adds and subtracts, selection pen, panels fold and scroll", [](MainWindow& w) {
        Document* d = view()->doc();
        // three separate red squares on the layer
        auto* l = static_cast<RasterLayer*>(d->currentLayer());
        for (int k = 0; k < 3; ++k) l->pixels.fillRect(QRect(100 + k * 200, 200, 100, 100), 0xffdd2222u);
        d->pixelsChanged(d->bounds(), l);
        app()->tools()->setCurrent("magic-wand");
        pump(250);
        auto click = [](QPointF p, Qt::KeyboardModifiers m) {
            mouse(QEvent::MouseButtonPress, p, Qt::LeftButton, m);
            mouse(QEvent::MouseButtonRelease, p, Qt::NoButton, m);
            pump(20);
        };
        auto sel = [&](int x, int y) { return d->hasSelection() ? int(d->selection().pixel(x, y)) : 0; };
        click(QPointF(150, 250), Qt::NoModifier);
        CHECK(sel(150, 250) == 255 && sel(350, 250) == 0);
        click(QPointF(350, 250), Qt::ShiftModifier);  // add a square next to the selection
        std::fprintf(stderr, "INFO wand after shift: %d %d %d %d\n", sel(150, 250), sel(350, 250), sel(550, 250), sel(50, 50));
        CHECK(sel(150, 250) == 255 && sel(350, 250) == 255 && sel(550, 250) == 0);
        click(QPointF(550, 250), Qt::ShiftModifier);
        CHECK(sel(550, 250) == 255);
        click(QPointF(350, 250), Qt::ControlModifier);  // Ctrl takes one away
        CHECK(sel(150, 250) == 255 && sel(350, 250) == 0 && sel(550, 250) == 255);
        click(QPointF(150, 250), Qt::AltModifier);  // so does Alt
        CHECK(sel(150, 250) == 0 && sel(550, 250) == 255);
        // the soft edge of the square is taken by "Expand"
        CHECK(sel(500, 250) >= 128 && sel(497, 250) == 0);
        // the selection pen: paints exactly its circle, Ctrl takes away, the color tint is shown
        ops::deselect(*d);
        app()->tools()->setCurrent("select-brush");
        pump(250);
        auto* pen = dynamic_cast<SelectionBrushTool*>(app()->tools()->current());
        CHECK(pen != nullptr);
        if (!pen) return;
        pen->size = 40;
        dragCanvas(QPointF(100, 450), QPointF(500, 450));
        pump(30);
        CHECK(sel(300, 450) == 255 && sel(300, 466) > 128 && sel(300, 480) == 0);
        dragCanvas(QPointF(300, 420), QPointF(300, 480), Qt::ControlModifier);
        pump(30);
        CHECK(sel(300, 450) == 0 && sel(200, 450) == 255);
        if (qEnvironmentVariableIsSet("GX_SHOTS")) w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/selection_pen.png");
        d->undo();
        CHECK(sel(300, 450) == 255);
        ops::deselect(*d);
        app()->tools()->setCurrent("brush");
        pump(50);

        // the brush tab scrolls when it holds many brushes
        BrushLibrary* lib = app()->brushes();
        const int n0 = lib->count();
        for (int i = 0; i < 24; ++i) {
            BrushSettings b = lib->at(0);
            b.name = QString("Test brush %1").arg(i);
            b.group = "Basic";
            lib->add(b);
        }
        pump(80);
        auto* bs = w.findChild<QScrollArea*>("brushSlotsScroll");
        CHECK(bs != nullptr);
        if (bs) {
            if (auto* slots_ = bs->widget()) {
                static_cast<BrushSlots*>(slots_)->setGroup("Basic");
                pump(80);
                std::fprintf(stderr, "INFO brush slots %d high in a %d view, scroll max %d\n", slots_->height(), bs->viewport()->height(),
                             bs->verticalScrollBar()->maximum());
                CHECK(slots_->height() > bs->viewport()->height() && bs->verticalScrollBar()->maximum() > 0);
            }
        }
        while (lib->count() > n0) lib->remove(lib->count() - 1);
        pump(30);

        // panels: fold with the arrow, shrink by dragging, reset
        auto* colorDock = w.findChild<QDockWidget*>("colorDock");
        CHECK(colorDock != nullptr);
        if (!colorDock) return;
        const int full = colorDock->height();
        QToolButton* fold = colorDock->titleBarWidget()->findChild<QToolButton*>("dockFold");
        CHECK(fold != nullptr);
        if (fold) fold->click();
        pump(80);
        std::fprintf(stderr, "INFO color panel %d high, folded %d\n", full, colorDock->height());
        CHECK(colorDock->height() < 40 && colorDock->property("folded").toBool());
        if (fold) fold->click();
        pump(80);
        CHECK(colorDock->height() > 150);
        w.resizeDocks({colorDock}, {140}, Qt::Vertical);  // as when the line under the panel is dragged
        pump(80);
        std::fprintf(stderr, "INFO color panel shrunk to %d\n", colorDock->height());
        CHECK(colorDock->height() < 170);
        // parts switched off in the settings
        Preferences& p = app()->prefs();
        p.hiddenColorParts = {"rgb", "palette"};
        p.hiddenTools = {"veraser"};
        p.hiddenMaterialTabs = {"Tones"};
        p.hiddenBrushTabs = {"Decor"};
        app()->preferencesChanged();
        pump(50);
        auto* cp = w.findChild<ColorPanel*>();
        int visibleSliders = 0;
        if (cp)
            for (QWidget* c : cp->findChildren<QWidget*>())
                if (QString(c->metaObject()->className()).contains("GradientSlider") && c->isVisibleTo(cp)) ++visibleSliders;
        CHECK(visibleSliders == 3);
        if (qEnvironmentVariableIsSet("GX_SHOTS")) w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/panels_hidden.png");
        auto* mp = w.findChild<MaterialsPanel*>();
        CHECK(mp && !mp->tabs()->isTabVisible(2) && mp->tabs()->isTabVisible(1));
        w.resetInterface();
        pump(80);
        CHECK(p.hiddenColorParts.isEmpty() && p.hiddenTools.isEmpty() && mp && mp->tabs()->isTabVisible(2));
        visibleSliders = 0;
        if (cp)
            for (QWidget* c : cp->findChildren<QWidget*>())
                if (QString(c->metaObject()->className()).contains("GradientSlider") && c->isVisibleTo(cp)) ++visibleSliders;
        CHECK(visibleSliders == 6);
    }, w);

    scenario("SAI keys and the owner's brushes", [](MainWindow& w) {
        BrushLibrary* lib = app()->brushes();
        // the six brushes from SAI 2, in the "Custom brushes" tab
        const QStringList mine = {"Pencil (SAI)", "111", "333", "blur", "AirBrush (SAI)", "b blur"};
        for (const QString& n : mine) {
            const int i = lib->indexOfName(n);
            CHECK(i >= 0 && lib->at(i).group == BrushSettings::customGroup());
        }
        CHECK(TextureLibrary::instance().get(TextureKind::Brush, "Paper") && TextureLibrary::instance().get(TextureKind::Paper, "Fabric 01"));
        // every key is on exactly one command
        QHash<QString, QAction*> byId;
        for (const ShortcutInfo& i : Shortcuts::instance().all())
            if (i.action) byId.insert(i.id, i.action);
        const QList<QPair<QString, QKeySequence>> want = {
            {"view.zoomin", QKeySequence(Qt::Key_W)},  {"view.zoomout", QKeySequence(Qt::Key_Q)}, {"view.rotr", QKeySequence(Qt::Key_R)},
            {"view.rotl", QKeySequence(Qt::Key_E)},    {"view.flip", QKeySequence(Qt::Key_H)},    {"brush.smaller", QKeySequence(Qt::Key_BracketLeft)},
            {"brush.bigger", QKeySequence(Qt::Key_BracketRight)}, {"tool.eraser", QKeySequence(Qt::Key_F)},
            {"brush.quick1", QKeySequence(Qt::Key_A)}, {"brush.quick2", QKeySequence(Qt::Key_S)}, {"brush.quick3", QKeySequence(Qt::Key_D)},
            {"brush.quick4", QKeySequence(Qt::Key_Z)}, {"brush.quick5", QKeySequence(Qt::Key_C)}, {"brush.quick6", QKeySequence(Qt::Key_B)},
            {"brush.opacity40", QKeySequence(Qt::Key_4)}, {"brush.opacity100", QKeySequence(Qt::Key_0)}};
        for (const auto& k : want) {
            QAction* a = byId.value(k.first);
            CHECK(a && a->shortcut() == k.second);
            int same = 0;
            for (QAction* o : w.findChildren<QAction*>())
                if (o->shortcut() == k.second) ++same;
            if (same != 1) std::fprintf(stderr, "NOTE key %s is on %d commands\n", qPrintable(k.second.toString()), same);
            CHECK(same == 1);
        }
        auto run = [&](const char* id) {
            if (QAction* a = byId.value(id)) a->trigger();
            pump(20);
        };
        // brushes on A S D Z C B
        run("brush.quick1");
        CHECK(lib->current().name == "Pencil (SAI)" && app()->tools()->current()->id() == "brush");
        run("brush.quick2");
        CHECK(lib->current().name == "111");
        run("brush.quick6");
        CHECK(lib->current().name == "b blur");
        // digits: opacity
        run("brush.opacity40");
        CHECK(std::abs(lib->current().opacity - 0.4) < 1e-9);
        run("brush.opacity100");
        CHECK(std::abs(lib->current().opacity - 1.0) < 1e-9);
        // [ ]: SAI's sizes
        run("brush.quick1");
        CHECK(std::abs(lib->current().size - 9) < 1e-9);
        run("brush.bigger");
        CHECK(std::abs(lib->current().size - 10) < 1e-9);
        run("brush.bigger");
        CHECK(std::abs(lib->current().size - 12) < 1e-9);
        run("brush.smaller");
        run("brush.smaller");
        CHECK(std::abs(lib->current().size - 9) < 1e-9);
        // F: eraser and back
        run("tool.eraser");
        CHECK(lib->current().engine == BrushEngine::Erase);
        run("tool.eraser");
        CHECK(lib->current().name == "Pencil (SAI)");
        // W Q R E H: the view
        const double z = view()->zoom();
        run("view.zoomin");
        const double z1 = view()->zoom();
        CHECK(z1 > z);
        run("view.zoomout");
        CHECK(view()->zoom() < z1);
        const double r0 = view()->rotation();
        run("view.rotr");
        CHECK(view()->rotation() > r0);
        run("view.rotl");
        CHECK(std::abs(view()->rotation() - r0) < 1e-6);
        const bool f0 = view()->isFlipped();
        run("view.flip");
        CHECK(view()->isFlipped() != f0);
        run("view.flip");
        // the brush tab of the owner's brushes, and a quick key moved to another brush
        if (auto* bs = w.findChild<BrushSlots*>()) {
            bs->setGroup(BrushSettings::customGroup());
            pump(40);
            if (qEnvironmentVariableIsSet("GX_SHOTS")) w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/custom_brushes.png");
        }
        setQuickBrush(0, "333");
        CHECK(quickBrushName(0) == "333" && quickBrushName(2).isEmpty());
        setQuickBrush(0, "Pencil (SAI)");
        setQuickBrush(2, "333");
        // the owner's brushes paint: the smearing brush moves color without washing it out
        Document* d = view()->doc();
        auto* l = static_cast<RasterLayer*>(d->currentLayer());
        l->pixels.fillRect(QRect(100, 100, 300, 200), 0xffdd2222u);
        l->pixels.fillRect(QRect(400, 100, 300, 200), 0xff2244ddu);
        d->pixelsChanged(d->bounds(), l);
        run("brush.quick2");
        dragCanvas(QPointF(300, 200), QPointF(500, 200));
        pump(30);
        int holes = 0;
        for (int x = 280; x <= 520; x += 4)
            if ((l->pixels.pixel(x, 200) >> 24) < 250) ++holes;
        const quint32 mid = l->pixels.pixel(420, 200);
        std::fprintf(stderr, "INFO after smearing: %d see-through pixels, at 420: %08x\n", holes, mid);
        CHECK(holes == 0);
        CHECK(((mid >> 16) & 0xff) > 0x30);  // red carried into the blue
        d->undo();
        if (QAction* a = byId.value("brush.opacity100")) a->trigger();
    }, w);

    scenario("smart selection stays inside the object; mesh warp lines added and removed", [](MainWindow& w) {
        Document* d = view()->doc();
        auto* l = static_cast<RasterLayer*>(d->currentLayer());
        {   // two red discs with black outlines, on the white paper
            QImage img(800, 600, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::black, 4));
            p.setBrush(QColor(220, 40, 40));
            p.drawEllipse(QPointF(250, 300), 120, 120);
            p.drawEllipse(QPointF(560, 300), 80, 80);
            p.end();
            imageToSurface(img, l->pixels, QPoint(0, 0));
            d->pixelsChanged(d->bounds(), l);
        }
        app()->tools()->setCurrent("smart-select");
        pump(250);
        auto* smart = dynamic_cast<SmartSelectTool*>(app()->tools()->current());
        CHECK(smart != nullptr);
        if (!smart) return;
        smart->size = 60;
        auto sel = [&](int x, int y) { return d->hasSelection() ? int(d->selection().pixel(x, y)) : 0; };
        // a stroke across the middle of the big disc, its brush runs over the outline
        dragCanvas(QPointF(180, 300), QPointF(330, 300));
        dragCanvas(QPointF(250, 220), QPointF(250, 380));
        pump(30);
        int inside = 0, total = 0;
        for (int y = 200; y <= 400; y += 10)
            for (int x = 150; x <= 350; x += 10)
                if (std::hypot(x - 250, y - 300) < 100) ++total, inside += sel(x, y) >= 128;
        int spilled = 0;
        for (int a = 0; a < 360; a += 10) {
            const double t = a * M_PI / 180;
            spilled += sel(int(250 + 132 * std::cos(t)), int(300 + 132 * std::sin(t))) > 0;  // just outside the outline
        }
        std::fprintf(stderr, "INFO smart selection: %d of %d inside, %d outside the outline\n", inside, total, spilled);
        CHECK(inside > total * 7 / 10 && spilled == 0);
        CHECK(sel(560, 300) == 0);  // the other disc is left alone
        if (qEnvironmentVariableIsSet("GX_SHOTS")) w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/smart_selection.png");
        dragCanvas(QPointF(250, 300), QPointF(250, 310), Qt::ControlModifier);  // take away around the middle
        pump(30);
        CHECK(sel(250, 300) == 0 && sel(145, 300) >= 128);  // taken away as far as the reach, the rest stays
        ops::deselect(*d);

        // mesh warp: a click on a line adds a line, Ctrl + click removes it
        app()->tools()->setCurrent("warp");
        pump(250);
        auto* warp = dynamic_cast<WarpTool*>(app()->tools()->current());
        CHECK(warp && warp->columns() == 4 && warp->rows() == 4);
        if (!warp) return;
        const QPointF a = warp->point(0), b = warp->point(1);
        const QPointF mid = (a + b) / 2;
        mouse(QEvent::MouseButtonPress, mid, Qt::LeftButton);
        for (int i = 1; i <= 10; ++i) mouse(QEvent::MouseMove, mid + QPointF(0, i * 3), Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, mid + QPointF(0, 30), Qt::NoButton);
        pump(40);
        CHECK(warp->columns() == 5 && warp->rows() == 4);
        CHECK(std::abs(warp->point(1).y() - (mid.y() + 30)) < 1.0 && std::abs(warp->point(1).x() - mid.x()) < 2.0);  // dragged at once
        // a click on a vertical line adds a row
        const QPointF c = warp->point(0), e2 = warp->point(warp->columns());
        mouse(QEvent::MouseButtonPress, (c + e2) / 2, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, (c + e2) / 2, Qt::NoButton);
        pump(40);
        CHECK(warp->rows() == 5);
        if (qEnvironmentVariableIsSet("GX_SHOTS")) w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/warp_points.png");
        // Ctrl + click the new point of the column: the column goes, a corner stays
        mouse(QEvent::MouseButtonPress, warp->point(1), Qt::LeftButton, Qt::ControlModifier);
        mouse(QEvent::MouseButtonRelease, warp->point(1), Qt::NoButton, Qt::ControlModifier);
        pump(40);
        CHECK(warp->columns() == 4 && warp->rows() == 5);
        mouse(QEvent::MouseButtonPress, warp->point(0), Qt::LeftButton, Qt::ControlModifier);
        mouse(QEvent::MouseButtonRelease, warp->point(0), Qt::NoButton, Qt::ControlModifier);
        pump(40);
        CHECK(warp->columns() == 4 && warp->rows() == 5);
        warp->cancel();
        app()->tools()->setCurrent("brush");
        pump(30);
    }, w);

    scenario("liquify window: tools, freeze, undo, OK and Cancel", [](MainWindow& w) {
        Document* d = view()->doc();
        auto* l = static_cast<RasterLayer*>(d->currentLayer());
        // vertical stripes: any warp shows as bent stripes
        for (int x = 100; x < 700; x += 40) l->pixels.fillRect(QRect(x, 100, 20, 400), 0xff202020u);
        d->pixelsChanged(d->bounds(), l);
        const QImage before = surfaceToImage(l->pixels, d->bounds());
        auto diff = [](const QImage& a, const QImage& b, const QRect& r) {
            int n = 0;
            for (int y = r.top(); y <= r.bottom(); ++y)
                for (int x = r.left(); x <= r.right(); ++x) n += a.pixel(x, y) != b.pixel(x, y);
            return n;
        };
        {   // the field on its own: every tool moves pixels (freeze and thaw do not)
            LiquifyField f;
            f.begin(before);
            LiquifyField::Brush b;
            b.size = 120;
            b.pressure = 0.8;
            for (int m = 0; m < LiquifyField::ModeCount; ++m) {
                if (m == LiquifyField::Reconstruct || m == LiquifyField::Smooth || m == LiquifyField::Freeze || m == LiquifyField::Thaw) continue;
                f.begin(before);
                // push left moves sideways to the pen's way: go down so the vertical stripes bend
                const bool side = m == LiquifyField::PushLeft;
                for (int i = 0; i < 8; ++i)
                    f.dab(LiquifyField::Mode(m), side ? QPointF(300, 280 + i * 5) : QPointF(300 + i * 5, 300), side ? QPointF(0, 5) : QPointF(5, 0), 1.0, b);
                const int changed = diff(f.output(), before, QRect(200, 200, 200, 200));
                if (changed < 50) std::fprintf(stderr, "NOTE liquify mode %d changed %d pixels\n", m, changed);
                CHECK(changed >= 50);
            }
            // reconstruct brings it back; frozen places do not move
            f.begin(before);
            for (int i = 0; i < 8; ++i) f.dab(LiquifyField::Forward, QPointF(300 + i * 5, 300), QPointF(5, 0), 1.0, b);
            f.saveUndo();
            f.reconstructAll(1.0);
            CHECK(diff(f.output(), before, QRect(200, 200, 200, 200)) == 0 && !f.changed());
            CHECK(f.undo() && f.changed());
            f.begin(before);
            f.dab(LiquifyField::Freeze, QPointF(300, 300), {}, 1.0, LiquifyField::Brush{400, 1.0, 1.0, 0.5});
            CHECK(f.frozenAt(300, 300) > 0.99f);
            for (int i = 0; i < 8; ++i) f.dab(LiquifyField::Forward, QPointF(300 + i * 5, 300), QPointF(5, 0), 1.0, b);
            CHECK(diff(f.output(), before, QRect(280, 280, 40, 40)) == 0);
        }
        {   // the window: painting with the mouse, then Cancel leaves the layer as it was
            LiquifyDialog dlg(before, nullptr, &w);
            dlg.show();
            pump(80);
            QWidget* c = dlg.canvasWidget();
            auto paint = [&](QPointF a, QPointF b) {
                QMouseEvent p(QEvent::MouseButtonPress, dlg.canvasPoint(a), c->mapToGlobal(dlg.canvasPoint(a)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(c, &p);
                for (int i = 1; i <= 20; ++i) {
                    const QPointF q = dlg.canvasPoint(a + (b - a) * (i / 20.0));
                    QMouseEvent m(QEvent::MouseMove, q, c->mapToGlobal(q), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(c, &m);
                }
                const QPointF e = dlg.canvasPoint(b);
                QMouseEvent r(QEvent::MouseButtonRelease, e, c->mapToGlobal(e), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(c, &r);
                pump(20);
            };
            dlg.setMode(LiquifyField::Forward);
            dlg.brush().size = 150;
            paint(QPointF(250, 300), QPointF(400, 300));
            CHECK(dlg.field().changed() && diff(dlg.result(), before, QRect(150, 150, 400, 300)) > 200);
            if (qEnvironmentVariableIsSet("GX_SHOTS")) dlg.grab().save(qEnvironmentVariable("GX_SHOTS") + "/liquify.png");
            dlg.reject();
        }
        CHECK(diff(surfaceToImage(l->pixels, d->bounds()), before, d->bounds()) == 0);
        // OK through the menu command: the layer gets the result, one undo step
        QTimer::singleShot(200, [] {
            auto* dlg = qobject_cast<LiquifyDialog*>(QApplication::activeModalWidget());
            if (!dlg) return;
            dlg->setMode(LiquifyField::TwirlCW);
            for (int i = 0; i < 20; ++i) dlg->field().dab(LiquifyField::TwirlCW, QPointF(400, 300), {}, 1.0, LiquifyField::Brush{200, 1.0, 0.5, 1.0});
            dlg->accept();
        });
        QAction* act = nullptr;
        for (QAction* a : w.findChildren<QAction*>())
            if (a->property("en").toString() == "Liquify...") act = a;
        CHECK(act != nullptr);
        if (act) act->trigger();
        pump(40);
        l = static_cast<RasterLayer*>(d->currentLayer());
        const int twisted = diff(surfaceToImage(l->pixels, d->bounds()), before, QRect(250, 150, 300, 300));
        std::fprintf(stderr, "INFO liquify OK changed %d pixels\n", twisted);
        CHECK(twisted > 500);
        d->undo();
        l = static_cast<RasterLayer*>(d->currentLayer());
        CHECK(diff(surfaceToImage(l->pixels, d->bounds()), before, d->bounds()) == 0);
    }, w);

    scenario("themes: the classic three stay, new ones, my theme with a picture; navigator wheel", [](MainWindow& w) {
        const QStringList names = Theme::names();
        for (const char* n : {"dark", "gray", "light", "soft", "lavender", "midnight", "custom"}) CHECK(names.contains(n));
        // every theme applies, has readable text, and the menu has it
        for (const QString& n : names) {
            Theme::apply(n, 0);
            pump(30);
            CHECK(Theme::current() == n);
            const ThemeColors& c = Theme::colors();
            CHECK(c.text != c.panel && c.workspace.isValid());
            bool inMenu = false;
            for (QAction* a : w.findChildren<QAction*>())
                if (a->property("en").toString() != "" && a->isCheckable() && a->actionGroup() && a->text() == T(n == "dark" ? "Dark theme" : n == "gray" ? "Gray theme" : n == "light" ? "Light theme" : Theme::displayName(n)))
                    inMenu = true;
            CHECK(inMenu);
            if (qEnvironmentVariableIsSet("GX_SHOTS") && (n == "soft" || n == "lavender" || n == "midnight")) {
                app()->prefs().theme = n;
                app()->preferencesChanged();
                pump(80);
                w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/theme_" + n + ".png");
            }
        }
        CHECK(qApp->styleSheet().contains("border-radius: 5px"));  // midnight is round 5
        // my theme: colors, roundness, a picture, unreadable text fixed by itself
        QTemporaryDir dir;
        QImage tex(64, 64, QImage::Format_RGB32);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) tex.setPixel(x, y, ((x / 8 + y / 8) % 2) ? 0xff806040u : 0xff403020u);
        const QString texFile = dir.filePath("wood.png");
        tex.save(texFile);
        {
            ThemeEditorDialog dlg(&w);
            ThemeColors& c = dlg.colors();
            c.panel = QColor("#3a2a20");
            c.window = QColor("#2a1d16");
            c.text = QColor("#3b2b21");  // almost the panel color: cannot be read
            c.accent = QColor("#e0a050");
            c.radius = 12;
            c.texture = texFile;
            c.textureOpacity = 0.3;
            dlg.preview();
            pump(50);
            CHECK(Theme::colors().text != QColor("#3b2b21"));  // made readable
            CHECK(qApp->styleSheet().contains("border-radius: 12px") && qApp->styleSheet().contains("background-image"));
            if (qEnvironmentVariableIsSet("GX_SHOTS")) w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/theme_custom.png");
            // export and import keep everything, the picture inside the file
            const QString f = dir.filePath("mine.gxtheme");
            QString err;
            CHECK(ThemeEditorDialog::exportTheme(c, f, &err));
            ThemeColors back;
            CHECK(ThemeEditorDialog::importTheme(f, &back, &err));
            CHECK(back.panel == c.panel && back.radius == 12 && !back.texture.isEmpty() && QImage(back.texture).size() == QSize(64, 64));
            dlg.accept();
        }
        CHECK(app()->prefs().theme == "custom" && Theme::current() == "custom" && Theme::customColors().radius == 12);
        {   // Cancel puts the theme back
            ThemeEditorDialog dlg(&w);
            dlg.colors().radius = 2;
            dlg.preview();
            dlg.reject();
        }
        CHECK(Theme::current() == "custom" && qApp->styleSheet().contains("border-radius: 12px"));
        app()->prefs().theme = "dark";
        Theme::apply("dark", 0);
        app()->preferencesChanged();
        pump(30);
        // the navigator: the wheel over it zooms the canvas
        if (auto* nav = w.findChild<NavigatorView*>()) {
            const double z = view()->zoom();
            QWheelEvent we(QPointF(20, 20), nav->mapToGlobal(QPointF(20, 20)), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(nav, &we);
            pump(20);
            CHECK(view()->zoom() > z);
        } else {
            CHECK(false);
        }
    }, w);

    scenario("seamless mode, replace color, canvas templates", [](MainWindow& w) {
        Document* d = view()->doc();
        auto* l = static_cast<RasterLayer*>(d->currentLayer());
        l->pixels.fillRect(QRect(0, 0, 800, 600), 0xffdd2222u);  // red everywhere: copies show red too
        d->pixelsChanged(d->bounds(), l);
        QHash<QString, QAction*> acts;
        for (QAction* a : w.findChildren<QAction*>())
            if (!a->property("en").toString().isEmpty()) acts.insert(a->property("en").toString(), a);
        for (const char* n : {"Seamless mode", "Offset by half", "Save as pattern...", "Replace color...", "Save as template..."}) CHECK(acts.contains(n));
        // zoom out so the canvas does not fill the view, then look left of it
        view()->setZoom(0.4);
        pump(100);
        auto leftOfCanvas = [&] {
            const QPointF c = view()->toWidget(QPointF(-100, 300));
            const QImage img = view()->grab().toImage();
            const QPoint p = (c * img.devicePixelRatio()).toPoint();
            return img.rect().contains(p) ? QColor(img.pixel(p)) : QColor();
        };
        const QColor off = leftOfCanvas();
        acts["Seamless mode"]->trigger();
        pump(200);
        CHECK(d->seamless && acts["Seamless mode"]->isChecked());
        const QColor on = leftOfCanvas();
        std::fprintf(stderr, "INFO left of the canvas: %s off, %s on\n", qPrintable(off.name()), qPrintable(on.name()));
        CHECK(off.isValid() && on.isValid() && on.red() > on.blue() + 60 && !(off.red() > off.blue() + 60));
        if (qEnvironmentVariableIsSet("GX_SHOTS")) w.grab().save(qEnvironmentVariable("GX_SHOTS") + "/seamless.png");
        acts["Seamless mode"]->trigger();
        pump(50);
        CHECK(!d->seamless);
        view()->fitToWindow();
        // replace color: the red becomes the second color
        app()->setForeground(QColor(0xdd, 0x22, 0x22));
        app()->setBackground(QColor(0x22, 0x44, 0xdd));
        {
            ReplaceColorDialog dlg(d, l->id, &w);
            dlg.show();
            pump(200);
            CHECK(l->preview != nullptr);  // the canvas shows it before OK
            dlg.accept();
        }
        l = static_cast<RasterLayer*>(d->currentLayer());
        const quint32 c = l->pixels.pixel(400, 300);
        CHECK(!l->preview && (c & 0xff) > 0xb0 && ((c >> 16) & 0xff) < 0x50);
        d->undo();
        l = static_cast<RasterLayer*>(d->currentLayer());
        CHECK(((l->pixels.pixel(400, 300) >> 16) & 0xff) > 0xb0);
        // templates in File > New, and a saved one comes back
        {
            NewDocumentDialog dlg(&w);
            auto* combo = dlg.findChildren<QComboBox*>().value(0);
            CHECK(combo && combo->count() >= 5);
        }
        QString err;
        CHECK(templates::saveAsTemplate(*d, "Test template", &err));
        bool listed = false;
        for (const templates::Info& t : templates::all()) listed |= t.id == "user:Test template.gxp";
        CHECK(listed);
        auto fromMine = templates::make("user:Test template.gxp", QSize(), 0, Qt::white, &err);
        CHECK(fromMine && fromMine->size() == d->size() && fromMine->root()->children.size() == d->root()->children.size());
        QFile::remove(templates::folder() + "/Test template.gxp");
    }, w);

    scenario("reference over everything", [](MainWindow& w) {
        auto* panel = w.findChild<ReferencePanel*>();
        CHECK(panel);
        if (!panel) return;
        QImage img(300, 200, QImage::Format_RGB32);
        img.fill(Qt::green);
        QApplication::clipboard()->setImage(img);
        QToolButton* paste = nullptr;
        QToolButton* trash = nullptr;
        for (QToolButton* b : panel->findChildren<QToolButton*>()) {
            if (b->toolTip() == T("Paste from the clipboard")) paste = b;
            if (b->toolTip() == T("Remove")) trash = b;
        }
        CHECK(paste && trash);
        if (!paste || !trash) return;
        paste->click();
        pump(50);
        panel->setOverlayVisible(true);
        pump(50);
        auto* ov = w.findChild<ReferenceOverlay*>();
        CHECK(ov && ov->isVisible() && ov->hasImage());
        panel->setClickThrough(true);
        pump(80);
        CHECK(ov && ov->isVisible() && ov->clickThrough() && (ov->windowFlags() & Qt::WindowTransparentForInput));
        CHECK(panel->clickThrough());
        panel->setClickThrough(false);
        pump(80);
        CHECK(ov && ov->isVisible() && !(ov->windowFlags() & Qt::WindowTransparentForInput));
        ov->setOpacityValue(0.3);
        CHECK(std::abs(ov->opacity() - 0.3) < 1e-6);
        ov->repaint();
        if (qEnvironmentVariableIsSet("GX_SHOTS")) {
            QEnterEvent enter(QPointF(20, 20), QPointF(20, 20), ov->mapToGlobal(QPointF(20, 20)));
            QCoreApplication::sendEvent(ov, &enter);
            pump(30);
            ov->grab().save(qEnvironmentVariable("GX_SHOTS") + "/overlay.png");
        }
        panel->setOverlayVisible(false);
        pump(80);
        CHECK(!ov->isVisible());
        trash->click();
        pump(50);
    }, w);

    std::fprintf(stderr, "%d passed, %d failed\n", g_pass, g_fail);
    std::fflush(nullptr);
    std::_Exit(g_fail ? 1 : 0);
}
