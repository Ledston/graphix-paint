#pragma once
#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSettings>

namespace gx {

struct ShortcutInfo {
    QString id;
    QString category;  // English, shown through T()
    QString text;      // English
    QKeySequence defaultSeq;
    QKeySequence seq;
    bool hold = false;  // held key (pan, zoom, eyedropper...) rather than a command
    bool loaded = false;  // value came from the settings file
    QPointer<QAction> action;
};

// Every keyboard shortcut of the program, user-editable and saved in the settings.
class Shortcuts : public QObject {
    Q_OBJECT
public:
    static Shortcuts& instance();

    void registerAction(QAction* a, const QString& id, const QString& category, const QKeySequence& def = {});
    void registerHold(const QString& id, const QString& category, const QString& text, const QKeySequence& def);

    QKeySequence get(const QString& id) const;
    void set(const QString& id, const QKeySequence& seq);
    void resetAll();
    QList<ShortcutInfo> all() const;
    QString conflict(const QString& id, const QKeySequence& seq) const;  // id of the other binding or empty

    void load(QSettings& s);
    void save(QSettings& s) const;

    // Hold keys: called by the canvas with the currently pressed keys.
    bool holdActive(const QString& id, const QSet<int>& keys, Qt::KeyboardModifiers mods) const;

Q_SIGNALS:
    void changed();

private:
    QHash<QString, ShortcutInfo> m_items;
    QStringList m_order;
};

}  // namespace gx
