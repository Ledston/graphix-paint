#include "app/Shortcuts.h"

namespace gx {

Shortcuts& Shortcuts::instance() {
    static Shortcuts s;
    return s;
}

void Shortcuts::registerAction(QAction* a, const QString& id, const QString& category, const QKeySequence& def) {
    ShortcutInfo& i = m_items[id];
    if (!m_order.contains(id)) m_order << id;
    i.id = id;
    i.category = category;
    i.text = a->text().remove('&');
    i.defaultSeq = def;
    if (!i.loaded && !i.action) i.seq = def;
    i.action = a;
    a->setShortcut(i.seq);
    a->setShortcutContext(Qt::ApplicationShortcut);
}

void Shortcuts::registerHold(const QString& id, const QString& category, const QString& text, const QKeySequence& def) {
    ShortcutInfo& i = m_items[id];
    if (!m_order.contains(id)) m_order << id;
    i.id = id;
    i.category = category;
    i.text = text;
    i.defaultSeq = def;
    i.hold = true;
    if (!i.loaded) i.seq = def;
}

QKeySequence Shortcuts::get(const QString& id) const { return m_items.value(id).seq; }

void Shortcuts::set(const QString& id, const QKeySequence& seq) {
    auto it = m_items.find(id);
    if (it == m_items.end()) return;
    it->seq = seq;
    if (it->action) it->action->setShortcut(seq);
    Q_EMIT changed();
}

void Shortcuts::resetAll() {
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        it->seq = it->defaultSeq;
        if (it->action) it->action->setShortcut(it->seq);
    }
    Q_EMIT changed();
}

QList<ShortcutInfo> Shortcuts::all() const {
    QList<ShortcutInfo> l;
    for (const QString& id : m_order) l << m_items.value(id);
    return l;
}

QString Shortcuts::conflict(const QString& id, const QKeySequence& seq) const {
    if (seq.isEmpty()) return {};
    const ShortcutInfo self = m_items.value(id);
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        if (it.key() == id) continue;
        if (it->hold != self.hold) continue;  // hold keys and commands live side by side
        if (it->seq == seq) return it.key();
    }
    return {};
}

void Shortcuts::load(QSettings& s) {
    s.beginGroup("shortcuts");
    for (const QString& k : s.childKeys()) {
        auto it = m_items.find(k);
        QKeySequence seq = QKeySequence::fromString(s.value(k).toString(), QKeySequence::PortableText);
        if (it == m_items.end()) {
            ShortcutInfo i;
            i.id = k;
            i.seq = seq;
            i.loaded = true;
            m_items.insert(k, i);  // remembered until the action registers
            continue;
        }
        it->seq = seq;
        it->loaded = true;
        if (it->action) it->action->setShortcut(seq);
    }
    s.endGroup();
}

void Shortcuts::save(QSettings& s) const {
    s.beginGroup("shortcuts");
    s.remove("");
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
        if (it->seq != it->defaultSeq) s.setValue(it.key(), it->seq.toString(QKeySequence::PortableText));
    s.endGroup();
}

bool Shortcuts::holdActive(const QString& id, const QSet<int>& keys, Qt::KeyboardModifiers mods) const {
    QKeySequence seq = get(id);
    if (seq.isEmpty()) return false;
    QKeyCombination c = seq[0];
    Qt::Key key = c.key();
    Qt::KeyboardModifiers want = c.keyboardModifiers();
    mods &= (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    auto modFor = [](Qt::Key k) -> Qt::KeyboardModifiers {
        switch (k) {
        case Qt::Key_Control: return Qt::ControlModifier;
        case Qt::Key_Alt: return Qt::AltModifier;
        case Qt::Key_Shift: return Qt::ShiftModifier;
        case Qt::Key_Meta: return Qt::MetaModifier;
        default: return Qt::NoModifier;
        }
    };
    Qt::KeyboardModifiers km = modFor(key);
    if (km != Qt::NoModifier) return mods == (want | km);
    return keys.contains(int(key)) && mods == want;
}

}  // namespace gx
