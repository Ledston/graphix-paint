#pragma once
#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;
class QKeySequenceEdit;
class QLineEdit;
class QLabel;

namespace gx {

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);

private:
    void populate();
    void assign();
    QTreeWidget* m_tree;
    QKeySequenceEdit* m_edit;
    QLineEdit* m_filter;
    QLabel* m_info;
};

}  // namespace gx
