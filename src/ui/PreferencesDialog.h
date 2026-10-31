#pragma once
#include <QDialog>
#include <QPointer>

#include "app/Preferences.h"

class QListWidget;
class QStackedWidget;
class QLabel;

namespace gx {

class CurveEditor;

// Settings window: a list of sections on the left, the section on the right.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    enum Page { Interface, Canvas, Cursor, Pen, Brushes, Files, Performance, Shortcuts, PageCount };
    explicit PreferencesDialog(QWidget* parent = nullptr, Page start = Interface);
    void accept() override;
    void reject() override;

Q_SIGNALS:
    void restartRequested();

private:
    QWidget* interfacePage();
    QWidget* canvasPage();
    QWidget* cursorPage();
    QWidget* penPage();
    QWidget* brushesPage();
    QWidget* filesPage();
    QWidget* performancePage();
    QWidget* shortcutsPage();
    void addPage(const QString& icon, const QString& title, QWidget* page);
    void apply();

    Preferences m_p;
    Preferences m_original;
    QList<QPair<QPointer<QWidget>, bool>> m_panelsBefore;  // panels shown before the dialog (Cancel puts them back)
    CurveEditor* m_curve = nullptr;
    QListWidget* m_list = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_tabletState = nullptr;
    QLabel* m_texturesPath = nullptr;
    int m_newW = 3000, m_newH = 3000;
    double m_newDpi = 350;
};

}  // namespace gx
