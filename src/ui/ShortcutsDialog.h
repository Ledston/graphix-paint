#pragma once
#include <QDialog>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QKeySequenceEdit;
class QLineEdit;
class QLabel;

namespace gx {

// The shortcut list with search and editing. Used in its own window and inside Settings.
class ShortcutsEditor : public QWidget {
    Q_OBJECT
public:
    explicit ShortcutsEditor(QWidget* parent = nullptr);
    void resetAll();

private:
    void populate();
    void assign();
    QTreeWidget* m_tree;
    QKeySequenceEdit* m_edit;
    QLineEdit* m_filter;
    QLabel* m_info;
};

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);
};

}  // namespace gx
