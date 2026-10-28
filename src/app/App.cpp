#include "app/App.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "app/BrushLibrary.h"
#include "app/I18n.h"
#include "app/Shortcuts.h"
#include "core/Textures.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"

namespace gx {

namespace {
App* g_app = nullptr;

bool dirWritable(const QString& d) {
    QDir().mkpath(d);
    QFile f(d + "/.write_test");
    bool ok = f.open(QIODevice::WriteOnly);
    if (ok) {
        f.close();
        f.remove();
    }
    return ok;
}
}  // namespace

App::App(QObject* parent) : QObject(parent) { g_app = this; }

App::~App() {
    if (g_app == this) g_app = nullptr;
}

App* App::instance() { return g_app; }

void App::init() {
    // Portable mode: a "portable" file next to the program keeps everything beside it.
    QString exeDir = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(exeDir + "/portable") || QFileInfo::exists(exeDir + "/portable.txt"))
        m_dataDir = exeDir + "/data";
    else
        m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(m_dataDir);
    m_settings = std::make_unique<QSettings>(m_dataDir + "/settings.ini", QSettings::IniFormat);
    m_prefs.load(*m_settings);
    setLanguage(m_prefs.language);

    // Textures live next to the program when possible (easy to find and add to),
    // otherwise in the user data folder.
    m_texturesDir = dirWritable(exeDir + "/textures") ? exeDir + "/textures" : m_dataDir + "/textures";
    TextureLibrary::instance().setRoot(m_texturesDir);

    m_curve.set(m_prefs.pressureCurve);
    m_brushes = std::make_unique<BrushLibrary>(m_dataDir + "/brushes.json");

    m_settings->beginGroup("colors");
    m_fg = QColor(m_settings->value("fg", "#000000").toString());
    m_bg = QColor(m_settings->value("bg", "#ffffff").toString());
    for (const QString& c : m_settings->value("history").toStringList()) m_history << QColor(c);
    m_settings->endGroup();

    m_tools = new ToolManager(this);
}

void App::shutdown() {
    savePrefs();
    if (m_brushes) m_brushes->save();
    m_settings->beginGroup("colors");
    m_settings->setValue("fg", m_fg.name());
    m_settings->setValue("bg", m_bg.name());
    QStringList h;
    for (const QColor& c : m_history) h << c.name();
    m_settings->setValue("history", h);
    m_settings->endGroup();
    Shortcuts::instance().save(*m_settings);
    m_settings->sync();
}

void App::savePrefs() {
    m_prefs.save(*m_settings);
    m_settings->sync();
}

void App::preferencesChanged() {
    savePrefs();
    Q_EMIT prefsChanged();
}

void App::setForeground(const QColor& c) {
    if (c == m_fg && !m_transparent) return;
    m_fg = c;
    m_transparent = false;
    Q_EMIT colorsChanged();
}

void App::setBackground(const QColor& c) {
    if (c == m_bg) return;
    m_bg = c;
    Q_EMIT colorsChanged();
}

void App::swapColors() {
    std::swap(m_fg, m_bg);
    m_transparent = false;
    Q_EMIT colorsChanged();
}

void App::resetColors() {
    m_fg = Qt::black;
    m_bg = Qt::white;
    m_transparent = false;
    Q_EMIT colorsChanged();
}

void App::setTransparentColor(bool on) {
    if (m_transparent == on) return;
    m_transparent = on;
    Q_EMIT colorsChanged();
}

void App::pushColorHistory(const QColor& c) {
    if (!m_history.isEmpty() && m_history.front() == c) return;
    m_history.removeAll(c);
    m_history.prepend(c);
    while (m_history.size() > 32) m_history.removeLast();
    Q_EMIT colorHistoryChanged();
}

void App::setPressureCurve(const QVector<QPointF>& pts) {
    m_prefs.pressureCurve = pts;
    m_curve.set(pts);
    Q_EMIT pressureCurveChanged();
}

void App::setStabilizer(int mode, int level) {
    m_prefs.stabilizerMode = std::clamp(mode, 0, 3);
    m_prefs.stabilizerLevel = std::clamp(level, 0, 30);
    Q_EMIT stabilizerChanged();
}

void App::setPressureSmoothing(int level) {
    m_prefs.pressureSmoothing = std::clamp(level, 0, 20);
    Q_EMIT stabilizerChanged();
}

void App::setActiveView(CanvasView* v) {
    if (m_view == v) return;
    m_view = v;
    Q_EMIT activeViewChanged(v);
}

CanvasView* App::activeView() const { return m_view.data(); }

Document* App::activeDocument() const { return m_view ? m_view->doc() : nullptr; }

void App::showStatus(const QString& text, int ms) { Q_EMIT statusMessage(text, ms); }

}  // namespace gx
