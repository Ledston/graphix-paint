#pragma once
#include <QDialog>
#include <QHash>

class QLineEdit;
class QListWidget;
class QMenuBar;
class QAction;

namespace gx {

// Find any command, filter or setting by typing a part of its name (Ctrl+F):
// the list shows where it lives in the menus and its shortcut; Enter runs it.
class CommandPalette : public QDialog {
    Q_OBJECT
public:
    CommandPalette(QMenuBar* menus, const QHash<QString, QAction*>& actions, QWidget* parent);
    QString filterText() const;
    int resultCount() const;
    bool runCurrent();  // runs the selected command (closes the window)
    void setFilterText(const QString& t);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    struct Entry {
        QString id;
        QAction* action;
        QString name, english, path, keys;
    };
    void refill();
    QLineEdit* m_edit;
    QListWidget* m_list;
    QList<Entry> m_entries;
};

}  // namespace gx
