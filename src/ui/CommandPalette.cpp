#include "ui/CommandPalette.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "app/Shortcuts.h"
#include "ui/Theme.h"

namespace gx {

namespace {
enum Role { NameRole = Qt::UserRole + 1, PathRole, KeysRole, IdRole, EnabledRole };

QString plain(QString s) {
    s.remove('&');
    if (s.endsWith("...")) s.chop(3);
    return s.trimmed();
}

// name on the left, where it is in the menus in gray, the shortcut on the right
class EntryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const override {
        return QSize(opt.rect.width(), opt.fontMetrics.height() * 2 + 8);
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        const ThemeColors& c = Theme::colors();
        const bool sel = opt.state & QStyle::State_Selected;
        const bool enabled = idx.data(EnabledRole).toBool();
        p->save();
        if (sel) p->fillRect(opt.rect, c.accent);
        const QColor fg = sel ? c.accentText : (enabled ? c.text : c.dim);
        const QColor dim = sel ? c.accentText : c.dim;
        const QRect r = opt.rect.adjusted(34, 3, -8, -3);
        const QIcon icon = qvariant_cast<QIcon>(idx.data(Qt::DecorationRole));
        if (!icon.isNull()) icon.paint(p, QRect(opt.rect.left() + 8, opt.rect.center().y() - 9, 18, 18));
        const int lh = opt.fontMetrics.height();
        const QString keys = idx.data(KeysRole).toString();
        const int kw = keys.isEmpty() ? 0 : opt.fontMetrics.horizontalAdvance(keys) + 12;
        p->setPen(fg);
        p->drawText(QRect(r.left(), r.top(), r.width() - kw, lh), Qt::AlignLeft | Qt::AlignVCenter,
                    opt.fontMetrics.elidedText(idx.data(NameRole).toString(), Qt::ElideRight, r.width() - kw));
        QFont small = opt.font;
        small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1));
        p->setFont(small);
        p->setPen(dim);
        p->drawText(QRect(r.left(), r.top() + lh, r.width() - kw, lh), Qt::AlignLeft | Qt::AlignVCenter, idx.data(PathRole).toString());
        if (!keys.isEmpty()) {
            p->setFont(opt.font);
            const QRect kr(r.right() - kw + 6, r.top(), kw - 6, r.height());
            p->setPen(dim);
            p->drawText(kr, Qt::AlignRight | Qt::AlignVCenter, keys);
        }
        p->restore();
    }
};

void collect(QMenu* m, const QString& path, QHash<QAction*, QString>& out) {
    for (QAction* a : m->actions()) {
        if (a->isSeparator()) continue;
        if (a->menu()) collect(a->menu(), path + QStringLiteral(" › ") + plain(a->text()), out);
        else if (!out.contains(a)) out.insert(a, path);
    }
}
}  // namespace

CommandPalette::CommandPalette(QMenuBar* menus, const QHash<QString, QAction*>& actions, QWidget* parent) : QDialog(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setObjectName("commandPalette");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(4);
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(T("Find a command, filter or setting..."));
    m_edit->setClearButtonEnabled(true);
    v->addWidget(m_edit);
    m_list = new QListWidget(this);
    m_list->setItemDelegate(new EntryDelegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    v->addWidget(m_list, 1);

    // where every command lives in the menus
    QHash<QAction*, QString> where;
    for (QAction* top : menus->actions())
        if (top->menu()) collect(top->menu(), plain(top->text()), where);
    QHash<QString, QString> categories;
    for (const ShortcutInfo& si : Shortcuts::instance().all()) categories.insert(si.id, si.category);
    for (auto it = actions.cbegin(); it != actions.cend(); ++it) {
        QAction* a = it.value();
        if (!a || it.key() == "help.commands") continue;
        Entry e;
        e.id = it.key();
        e.action = a;
        e.name = plain(a->text());
        e.english = plain(a->property("en").toString());  // English names are found in every language
        e.path = where.value(a);
        if (e.path.isEmpty()) e.path = T(categories.value(it.key()));
        e.keys = a->shortcut().toString(QKeySequence::NativeText);
        if (e.name.isEmpty()) continue;
        m_entries << e;
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) { return a.name.localeAwareCompare(b.name) < 0; });

    connect(m_edit, &QLineEdit::textChanged, this, &CommandPalette::refill);
    connect(m_list, &QListWidget::itemActivated, this, [this] { runCurrent(); });
    connect(m_list, &QListWidget::itemClicked, this, [this] { runCurrent(); });
    m_edit->installEventFilter(this);
    refill();
    const QWidget* host = parent ? parent->window() : nullptr;
    const int w = host ? std::min(620, std::max(420, host->width() / 2)) : 520;
    resize(w, 440);
    if (host) move(host->mapToGlobal(QPoint((host->width() - w) / 2, 70)));
    m_edit->setFocus();
}

QString CommandPalette::filterText() const { return m_edit->text(); }
int CommandPalette::resultCount() const { return m_list->count(); }
void CommandPalette::setFilterText(const QString& t) { m_edit->setText(t); }

void CommandPalette::refill() {
    const QString q = m_edit->text().trimmed();
    const QStringList words = q.split(' ', Qt::SkipEmptyParts);
    struct Hit {
        int score;
        const Entry* e;
    };
    QList<Hit> hits;
    const QStringList recent = app()->settings().value("commands/recent").toStringList();
    for (const Entry& e : m_entries) {
        int score = 0;
        if (words.isEmpty()) {
            const int r = int(recent.indexOf(e.id));
            score = r >= 0 ? 1000 - r : 0;  // recently used first
        } else {
            const QString hay = e.name + ' ' + e.english + ' ' + e.path;
            bool all = true;
            for (const QString& w : words)
                if (!hay.contains(w, Qt::CaseInsensitive)) all = false;
            if (!all) continue;
            score = 10;
            if (e.name.startsWith(q, Qt::CaseInsensitive) || e.english.startsWith(q, Qt::CaseInsensitive)) score = 30;
            else if (e.name.contains(q, Qt::CaseInsensitive) || e.english.contains(q, Qt::CaseInsensitive)) score = 20;
            if (recent.contains(e.id)) score += 5;
        }
        hits << Hit{score, &e};
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    m_list->clear();
    for (const Hit& h : hits) {
        auto* it = new QListWidgetItem(h.e->action->icon(), h.e->name, m_list);
        it->setData(NameRole, h.e->name);
        it->setData(PathRole, h.e->path);
        it->setData(KeysRole, h.e->keys);
        it->setData(IdRole, h.e->id);
        it->setData(EnabledRole, h.e->action->isEnabled());
    }
    if (m_list->count()) m_list->setCurrentRow(0);
}

bool CommandPalette::runCurrent() {
    QListWidgetItem* it = m_list->currentItem();
    if (!it) return false;
    const QString id = it->data(IdRole).toString();
    QAction* a = nullptr;
    for (const Entry& e : m_entries)
        if (e.id == id) a = e.action;
    if (!a || !a->isEnabled()) return false;
    QStringList recent = app()->settings().value("commands/recent").toStringList();
    recent.removeAll(id);
    recent.prepend(id);
    while (recent.size() > 12) recent.removeLast();
    app()->settings().setValue("commands/recent", recent);
    close();
    // after the window is gone, so dialogs the command opens get the focus
    QMetaObject::invokeMethod(a, [a] { a->trigger(); }, Qt::QueuedConnection);
    return true;
}

bool CommandPalette::eventFilter(QObject* o, QEvent* e) {
    if (o == m_edit && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        const int row = m_list->currentRow();
        switch (k->key()) {
        case Qt::Key_Down:
            if (row < m_list->count() - 1) m_list->setCurrentRow(row + 1);
            return true;
        case Qt::Key_Up:
            if (row > 0) m_list->setCurrentRow(row - 1);
            return true;
        case Qt::Key_PageDown:
            m_list->setCurrentRow(std::min(m_list->count() - 1, row + 8));
            return true;
        case Qt::Key_PageUp:
            m_list->setCurrentRow(std::max(0, row - 8));
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            runCurrent();
            return true;
        case Qt::Key_Escape:
            close();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(o, e);
}

}  // namespace gx
