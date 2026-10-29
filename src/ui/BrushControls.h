#pragma once
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

#include "brush/BrushSettings.h"

class QCheckBox;
class QComboBox;
class QVBoxLayout;
class QWidget;

namespace gx {

class ValueBar;

// The controls of a brush, built once and shared by the tool panel and the brush settings
// window. Every control edits 'settings' and calls the change callback.
class BrushControls : public QObject {
    Q_OBJECT
public:
    struct Page {
        QString id;       // stable key (remembers whether its section is open)
        QString title;    // translated
        QString icon;
        QWidget* widget;
        std::function<bool(const BrushSettings&)> visible;  // e.g. mixing only for mixing brushes
    };

    BrushControls(QWidget* parent, std::function<void()> changed);
    BrushSettings settings;

    // compact = the tool panel (no engine row on the first page, it has its own header)
    QList<Page> build(QWidget* parent, bool compact);
    void load(const BrushSettings& b);  // shows b in the controls
    bool loading() const { return m_loading; }
    void refreshLists();                // texture and tip shape lists (after reloading textures)

Q_SIGNALS:
    void visibilityChanged();  // a page may have to appear or disappear (engine changed...)

private:
    ValueBar* bar(QWidget* parent, const QString& label, double min, double max, int dec, double* field, double scale = 1,
                  const QString& suffix = {}, bool log = false, const QString& tip = {});
    ValueBar* intBar(QWidget* parent, const QString& label, int min, int max, int* field, const QString& tip = {});
    QCheckBox* check(QWidget* parent, const QString& label, bool* field, const QString& tip = {});
    QWidget* page(QWidget* parent, QVBoxLayout** layout);
    void changed();

    std::function<void()> m_changed;
    bool m_loading = false;
    QList<std::function<void()>> m_loaders;
    QList<QComboBox*> m_shapeCombos, m_textureCombos;
};

}  // namespace gx
