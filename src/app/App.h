#pragma once
#include <QColor>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <memory>

#include "app/Preferences.h"
#include "brush/BrushSettings.h"
#include "brush/Stabilizer.h"

namespace gx {

class BrushLibrary;
class ToolManager;
class CanvasView;
class Document;

// Application-wide state shared by all windows, views and tools.
class App : public QObject {
    Q_OBJECT
public:
    explicit App(QObject* parent = nullptr);
    ~App() override;
    static App* instance();

    void init();
    void shutdown();

    QString dataDir() const { return m_dataDir; }
    QString texturesDir() const { return m_texturesDir; }
    QString defaultTexturesDir() const;
    QSettings& settings() { return *m_settings; }

    Preferences& prefs() { return m_prefs; }
    void savePrefs();
    void preferencesChanged();  // re-reads prefs everywhere

    // ---- colors ----
    QColor foreground() const { return m_fg; }
    QColor background() const { return m_bg; }
    void setForeground(const QColor& c);
    void setBackground(const QColor& c);
    void swapColors();
    void resetColors();
    bool transparentColor() const { return m_transparent; }
    void setTransparentColor(bool on);
    QList<QColor> colorHistory() const { return m_history; }
    void pushColorHistory(const QColor& c);

    // ---- brushes and tools ----
    BrushLibrary* brushes() const { return m_brushes.get(); }
    ToolManager* tools() const { return m_tools; }

    // ---- input ----
    PressureCurve& pressureCurve() { return m_curve; }
    void setPressureCurve(const QVector<QPointF>& pts);
    void setStabilizer(int mode, int level);
    void setPressureSmoothing(int level);
    int stabilizerMode() const { return m_prefs.stabilizerMode; }
    int stabilizerLevel() const { return m_prefs.stabilizerLevel; }
    int pressureSmoothing() const { return m_prefs.pressureSmoothing; }

    // ---- views ----
    CanvasView* activeView() const;
    void setActiveView(CanvasView* v);
    Document* activeDocument() const;

    void showStatus(const QString& text, int ms = 4000);

Q_SIGNALS:
    void colorsChanged();
    void colorHistoryChanged();
    void stabilizerChanged();
    void pressureCurveChanged();
    void prefsChanged();
    void activeViewChanged(CanvasView* view);
    void statusMessage(const QString& text, int ms);

private:
    void applyTexturesDir();
    void applyThreads();
    QString m_dataDir, m_texturesDir;
    std::unique_ptr<QSettings> m_settings;
    Preferences m_prefs;
    QColor m_fg = Qt::black, m_bg = Qt::white;
    bool m_transparent = false;
    QList<QColor> m_history;
    std::unique_ptr<BrushLibrary> m_brushes;
    ToolManager* m_tools = nullptr;
    PressureCurve m_curve;
    QPointer<CanvasView> m_view;
};

inline App* app() { return App::instance(); }

}  // namespace gx
