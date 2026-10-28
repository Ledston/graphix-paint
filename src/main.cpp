#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QTranslator>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#include <exception>
#include <new>
#include <vector>

#include "app/App.h"
#include "app/I18n.h"
#include "app/TabletInput.h"
#include "core/Document.h"
#include "core/Pixel.h"
#include "io/Recovery.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// Keeps the program alive if something throws inside an event handler
// (for example running out of memory on a huge canvas).
class GxApplication : public QApplication {
public:
    using QApplication::QApplication;
    bool notify(QObject* o, QEvent* e) override {
        try {
            return QApplication::notify(o, e);
        } catch (const std::bad_alloc&) {
            report(gx::T("Not enough memory for this operation. Try a smaller brush, canvas or selection."));
        } catch (const std::exception& ex) {
            report(QString::fromLocal8Bit(ex.what()));
        } catch (...) {
            report(gx::T("Unexpected error."));
        }
        return false;
    }

private:
    void report(const QString& msg) {
        static bool showing = false;
        std::fprintf(stderr, "GraphiX error: %s\n", qPrintable(msg));
        if (showing) return;
        showing = true;
        QTimer::singleShot(0, [msg] {
            QMessageBox::warning(nullptr, "GraphiX Paint", msg);
            showing = false;
        });
    }
};

}  // namespace

int main(int argc, char** argv) {
    Q_INIT_RESOURCE(resources);  // resources live in the static core library
    QCoreApplication::setOrganizationName("GraphiX");
    QCoreApplication::setApplicationName("GraphiX Paint");
    QCoreApplication::setApplicationVersion(GX_VERSION);
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, false);
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    GxApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/icons/app.png"));

    gx::App app;
    app.init();
    // Qt's own texts (OK, Cancel, Yes, No, file dialogs) in the chosen language
    static QTranslator qtTranslator;
    if (gx::language() != "en" &&
        qtTranslator.load(QLocale(gx::language()), "qtbase", "_", QCoreApplication::applicationDirPath() + "/translations"))
        a.installTranslator(&qtTranslator);
    gx::Theme::apply(app.prefs().theme, app.prefs().uiFontSize);
    gx::Recovery::instance().start(app.dataDir() + "/recovery", app.prefs().autosaveMinutes);
    gx::TabletInput::apply(app.prefs().tabletApi);  // Wintab like SAI, Windows Ink as fallback

    gx::MainWindow w;
    if (!app.settings().contains("window/geometry")) {
        w.showMaximized();  // first start: use the whole screen
    } else {
        w.show();
    }
#ifdef Q_OS_WIN
    // Turn off Windows Ink press-and-hold right click, tap ripples and flicks on the canvas.
    const DWORD flags = 0x00000001 | 0x00000008 | 0x00000010 | 0x00010000;
    SetPropW(reinterpret_cast<HWND>(w.winId()), L"MicrosoftTabletPenServiceProperty", reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(flags)));
#endif

    QStringList files;
    const QStringList all = QCoreApplication::arguments();
    for (int i = 1; i < all.size(); ++i) {
        if (all[i].startsWith('-')) {
            if (all[i] == "-platform") ++i;
            continue;
        }
        if (QFileInfo::exists(all[i])) files << all[i];
    }
    QTimer::singleShot(0, &w, [&w, files] {
        w.checkRecovery();
        for (const QString& f : files) w.openFile(f);
        if (!w.activeDocument() && gx::app()->prefs().newCanvasOnStart) {
            // Start with a ready canvas so you can draw right away.
            QSettings& s = gx::app()->settings();
            QSize size(s.value("newdoc/w", 3000).toInt(), s.value("newdoc/h", 3000).toInt());
            auto doc = std::make_shared<gx::Document>(size.expandedTo(QSize(16, 16)).boundedTo(QSize(20000, 20000)));
            doc->setDpi(s.value("newdoc/dpi", 350).toDouble());
            auto paper = std::make_shared<gx::RasterLayer>();
            paper->id = doc->newLayerId();
            paper->name = gx::T("Paper");
            paper->pixels.fillRect(doc->bounds(), 0xffffffffu);
            doc->root()->insert(0, paper);
            auto layer = std::make_shared<gx::RasterLayer>();
            layer->id = doc->newLayerId();
            layer->name = gx::T("Layer 1");
            doc->root()->insert(1, layer);
            doc->setCurrentLayer(layer->id);
            doc->setModified(false);
            w.addDocument(doc);
        }
    });

    int r = a.exec();
    app.shutdown();
    gx::Recovery::instance().shutdown();
    return r;
}
