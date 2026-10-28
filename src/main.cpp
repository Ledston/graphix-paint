#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
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

QString readTabletApi(const char* argv0) {
    // The platform plugin options must be chosen before the application object exists.
    QString exeDir = QFileInfo(QString::fromLocal8Bit(argv0)).absolutePath();
    QString ini;
    if (QFileInfo::exists(exeDir + "/portable") || QFileInfo::exists(exeDir + "/portable.txt")) ini = exeDir + "/data/settings.ini";
    else ini = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.ini";
    if (!QFileInfo::exists(ini)) return "ink";
    QSettings s(ini, QSettings::IniFormat);
    return s.value("prefs/tabletApi", "ink").toString();
}

}  // namespace

int main(int argc, char** argv) {
    Q_INIT_RESOURCE(resources);  // resources live in the static core library
    QCoreApplication::setOrganizationName("GraphiX");
    QCoreApplication::setApplicationName("GraphiX Paint");
    QCoreApplication::setApplicationVersion(GX_VERSION);
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, false);
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    std::vector<char*> args(argv, argv + argc);
    static char platformFlag[] = "-platform";
    static char wintab[] = "windows:wintab";
#ifdef Q_OS_WIN
    bool hasPlatform = false;
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "-platform") hasPlatform = true;
    if (!hasPlatform && readTabletApi(argv[0]) == "wintab") {
        args.push_back(platformFlag);
        args.push_back(wintab);
    }
#else
    (void)platformFlag;
    (void)wintab;
    (void)readTabletApi;
#endif
    int ac = int(args.size());
    args.push_back(nullptr);
    GxApplication a(ac, args.data());
    a.setWindowIcon(QIcon(":/icons/app.png"));

    gx::App app;
    app.init();
    gx::Theme::apply(app.prefs().theme, app.prefs().uiFontSize);
    gx::Recovery::instance().start(app.dataDir() + "/recovery", app.prefs().autosaveMinutes);

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
        if (!w.activeDocument()) {
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
