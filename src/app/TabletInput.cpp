#include "app/TabletInput.h"

#include <QGuiApplication>
#include <QPointingDevice>
#include <QTabletEvent>

#include "app/I18n.h"

#ifdef Q_OS_WIN
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformintegration.h>
#endif

namespace gx {

QString TabletInput::s_active = "ink";
QElapsedTimer TabletInput::s_lastWintab;

namespace {
#ifdef Q_OS_WIN
using QWindowsApplication = QNativeInterface::Private::QWindowsApplication;
QWindowsApplication* winApp() { return dynamic_cast<QWindowsApplication*>(QGuiApplicationPrivate::platformIntegration()); }
#endif
bool g_checked = false;
bool g_available = false;
}  // namespace

QString TabletInput::apply(const QString& wanted) {
#ifdef Q_OS_WIN
    if (QWindowsApplication* w = winApp()) {
        if (wanted == "ink") {
            w->setWinTabEnabled(false);
        } else {
            w->setWinTabEnabled(true);
            g_checked = true;
            g_available = w->isWinTabEnabled();
        }
        s_active = w->isWinTabEnabled() ? "wintab" : "ink";
        return s_active;
    }
#else
    Q_UNUSED(wanted);
#endif
    s_active = "ink";
    return s_active;
}

QString TabletInput::active() { return s_active; }

bool TabletInput::wintabAvailable() {
#ifdef Q_OS_WIN
    if (!g_checked) {
        if (QWindowsApplication* w = winApp()) {
            bool was = w->isWinTabEnabled();
            if (!was) w->setWinTabEnabled(true);
            g_available = w->isWinTabEnabled();
            if (!was) w->setWinTabEnabled(false);
        }
        g_checked = true;
    }
    return g_available;
#else
    return false;
#endif
}

QString TabletInput::describe() {
    if (s_active == "wintab") return T("Now in use: Wintab");
    return wintabAvailable() ? T("Now in use: Windows Ink") : T("Now in use: Windows Ink (no Wintab driver found)");
}

bool TabletInput::shouldIgnore(const QTabletEvent* e) {
    const QPointingDevice* d = e ? e->pointingDevice() : nullptr;
    if (!d) return false;
    if (d->name() == QLatin1String("wintab")) {
        s_lastWintab.restart();
        return false;
    }
    // A Windows Ink pen event while Wintab is delivering the same pen: drop the duplicate.
    // Wintab also reports the pen while it hovers, so it is always "recent" during a stroke.
    if (s_active == "wintab" && d->name() == QLatin1String("wmpointer") && s_lastWintab.isValid() &&
        s_lastWintab.elapsed() < 1500)
        return true;
    return false;
}

}  // namespace gx
