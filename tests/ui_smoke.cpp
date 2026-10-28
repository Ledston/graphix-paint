// UI smoke test: opens the main window, uses every tool on the canvas and triggers
// every menu action (dialogs are captured and closed automatically).
// Usage: gx_uismoke <output dir>    (run with QT_QPA_PLATFORM=offscreen)
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/Layer.h"
#include "io/Recovery.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

using namespace gx;

static QString g_out;
static int g_shot = 0;
static QString g_current;

static QString safeName(QString s) {
    s.replace(QRegularExpression("[^A-Za-z0-9]+"), "_");
    return s.left(40);
}

static void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

static void drag(QWidget* v, QPointF a, QPointF b, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    auto send = [&](QEvent::Type t, QPointF p, Qt::MouseButtons bs) {
        QMouseEvent e(t, p, v->mapToGlobal(p), Qt::LeftButton, bs, mods);
        QCoreApplication::sendEvent(v, &e);
    };
    send(QEvent::MouseButtonPress, a, Qt::LeftButton);
    for (int i = 1; i <= 30; ++i) {
        double t = i / 30.0;
        QPointF p = a + (b - a) * t + QPointF(0, std::sin(t * 6.283) * 40);
        send(QEvent::MouseMove, p, Qt::LeftButton);
    }
    send(QEvent::MouseButtonRelease, b, Qt::NoButton);
}

static void key(QWidget* v, int k) {
    QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier);
    QCoreApplication::sendEvent(v, &press);
    QKeyEvent rel(QEvent::KeyRelease, k, Qt::NoModifier);
    QCoreApplication::sendEvent(v, &rel);
}

static void collectActions(QMenu* m, QList<QAction*>& out) {
    for (QAction* a : m->actions()) {
        if (a->isSeparator()) continue;
        if (a->menu()) collectActions(a->menu(), out);
        else out << a;
    }
}

static std::shared_ptr<Document> makeDoc() {
    auto doc = std::make_shared<Document>(QSize(1200, 900));
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

int main(int argc, char** argv) {
    Q_INIT_RESOURCE(resources);
    QCoreApplication::setOrganizationName("GraphiXTest");
    QCoreApplication::setApplicationName("GraphiX Paint Test");
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);  // so the test can close every dialog
    QApplication a(argc, argv);
    g_out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + "/uismoke";
    QDir().mkpath(g_out);

    App application;
    application.init();
    if (qEnvironmentVariableIsSet("GX_LANG")) setLanguage(qEnvironmentVariable("GX_LANG"));
    Theme::apply(application.prefs().theme, application.prefs().uiFontSize);
    Recovery::instance().start(application.dataDir() + "/recovery", 0);

    MainWindow w;
    w.resize(1600, 1000);
    w.show();
    w.addDocument(makeDoc());
    pump(300);
    w.grab().save(g_out + "/start.png");

    // Close every modal dialog or popup that appears, keeping a screenshot of it.
    QTimer killer;
    killer.setInterval(150);
    QObject::connect(&killer, &QTimer::timeout, [] {
        if (QWidget* p = QApplication::activePopupWidget()) {
            p->close();
            return;
        }
        if (QWidget* m = QApplication::activeModalWidget()) {
            QString name = QString("%1/dlg_%2_%3.png").arg(g_out).arg(++g_shot, 3, 10, QChar('0')).arg(safeName(g_current));
            m->grab().save(name);
            std::fprintf(stderr, "  dialog '%s' closed\n", qPrintable(m->windowTitle()));
            if (auto* d = qobject_cast<QDialog*>(m)) d->reject();
            else m->close();
        }
    });
    killer.start();

    auto view = [] { return static_cast<QWidget*>(app()->activeView()); };
    int failures = 0;

    // ---- every tool on the canvas ----
    for (Tool* t : app()->tools()->tools()) {
        g_current = "tool " + t->id();
        std::fprintf(stderr, "%s\n", qPrintable(g_current));
        app()->tools()->setCurrent(t->id());
        pump(30);
        QWidget* v = view();
        if (!v) {
            std::fprintf(stderr, "  no active view!\n");
            ++failures;
            break;
        }
        QPointF c(v->width() / 2.0, v->height() / 2.0);
        drag(v, c - QPointF(120, 60), c + QPointF(120, 60));
        pump(60);
        drag(v, c - QPointF(60, -60), c + QPointF(80, -40));
        pump(60);
        if (t->id() == "text") key(v, Qt::Key_A);
        app()->tools()->commitPending();
        pump(60);
        if (t->id() == "brush" || t->id() == "transform" || t->id() == "liquify" || t->id() == "gradient")
            w.grab().save(QString("%1/tool_%2.png").arg(g_out, safeName(t->id())));
    }
    app()->tools()->setCurrent("brush");

    // ---- each theme ----
    for (const QString th : {"gray", "light", "dark"}) {
        Theme::apply(th, 0);
        pump(150);
        w.grab().save(QString("%1/theme_%2.png").arg(g_out, th));
    }

    // ---- every menu action ----
    QList<QAction*> actions;
    for (QAction* m : w.menuBar()->actions())
        if (m->menu()) collectActions(m->menu(), actions);
    const QStringList skip = {"quit", "exit", "close", "restart"};
    for (QAction* act : actions) {
        QString text = act->text().remove('&');
        bool skipIt = false;
        for (const QString& s : skip)
            if (text.contains(s, Qt::CaseInsensitive) || act->objectName().contains(s, Qt::CaseInsensitive)) skipIt = true;
        if (skipIt || !act->isEnabled()) continue;
        g_current = "action " + text;
        std::fprintf(stderr, "%s\n", qPrintable(g_current));
        act->trigger();
        pump(40);
        if (!view()) {
            // An action closed the document: open a fresh one.
            w.addDocument(makeDoc());
            pump(50);
        }
    }
    g_current = "final";
    pump(200);
    w.grab().save(g_out + "/final.png");
    std::fprintf(stderr, "UI smoke done, %d failures\n", failures);
    std::fflush(nullptr);
    std::_Exit(failures ? 1 : 0);
}
