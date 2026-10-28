#include "ui/ShortcutsDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "app/I18n.h"
#include "app/Shortcuts.h"

namespace gx {

ShortcutsDialog::ShortcutsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(T("Keyboard shortcuts"));
    auto* v = new QVBoxLayout(this);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(T("Search..."));
    v->addWidget(m_filter);
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({T("Command"), T("Shortcut")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setRootIsDecorated(true);
    v->addWidget(m_tree, 1);
    auto* row = new QHBoxLayout;
    m_edit = new QKeySequenceEdit(this);
    m_edit->setMaximumSequenceLength(1);
    auto* setB = new QPushButton(T("Assign"), this);
    auto* clearB = new QPushButton(T("Clear"), this);
    auto* defB = new QPushButton(T("Default"), this);
    row->addWidget(new QLabel(T("New shortcut:"), this));
    row->addWidget(m_edit, 1);
    row->addWidget(setB);
    row->addWidget(clearB);
    row->addWidget(defB);
    v->addLayout(row);
    m_info = new QLabel(this);
    m_info->setObjectName("sectionTitle");
    m_info->setWordWrap(true);
    v->addWidget(m_info);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    auto* resetAll = bb->addButton(T("Reset all"), QDialogButtonBox::ResetRole);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(resetAll, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, T("Reset all"), T("Restore every shortcut to its default?")) == QMessageBox::Yes) {
            Shortcuts::instance().resetAll();
            populate();
        }
    });
    connect(setB, &QPushButton::clicked, this, &ShortcutsDialog::assign);
    connect(clearB, &QPushButton::clicked, this, [this] {
        auto* it = m_tree->currentItem();
        if (!it || it->data(0, Qt::UserRole).toString().isEmpty()) return;
        Shortcuts::instance().set(it->data(0, Qt::UserRole).toString(), {});
        populate();
    });
    connect(defB, &QPushButton::clicked, this, [this] {
        auto* it = m_tree->currentItem();
        if (!it) return;
        QString id = it->data(0, Qt::UserRole).toString();
        for (const auto& s : Shortcuts::instance().all())
            if (s.id == id) Shortcuts::instance().set(id, s.defaultSeq);
        populate();
    });
    connect(m_filter, &QLineEdit::textChanged, this, &ShortcutsDialog::populate);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        m_edit->clear();
        if (it && !it->data(0, Qt::UserRole).toString().isEmpty()) m_edit->setKeySequence(QKeySequence(it->text(1), QKeySequence::NativeText));
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this] { m_edit->setFocus(); });
    populate();
    m_info->setText(T("Select a command, press the keys in the box and click Assign. Held keys (pan, zoom, rotate, eyedropper) work while the key is held down."));
    resize(620, 620);
}

void ShortcutsDialog::populate() {
    QString sel = m_tree->currentItem() ? m_tree->currentItem()->data(0, Qt::UserRole).toString() : QString();
    m_tree->clear();
    QHash<QString, QTreeWidgetItem*> cats;
    QString filter = m_filter->text().trimmed();
    for (const ShortcutInfo& s : Shortcuts::instance().all()) {
        QString name = T(s.text);
        QString keys = s.seq.toString(QKeySequence::NativeText);
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive) && !keys.contains(filter, Qt::CaseInsensitive)) continue;
        QTreeWidgetItem* cat = cats.value(s.category);
        if (!cat) {
            cat = new QTreeWidgetItem(m_tree, {T(s.category)});
            cat->setFirstColumnSpanned(true);
            QFont f = cat->font(0);
            f.setBold(true);
            cat->setFont(0, f);
            cats.insert(s.category, cat);
        }
        auto* it = new QTreeWidgetItem(cat, {name, keys});
        it->setData(0, Qt::UserRole, s.id);
        if (s.seq != s.defaultSeq) {
            QFont f = it->font(1);
            f.setItalic(true);
            it->setFont(1, f);
        }
        if (s.id == sel) m_tree->setCurrentItem(it);
    }
    m_tree->expandAll();
}

void ShortcutsDialog::assign() {
    auto* it = m_tree->currentItem();
    if (!it) return;
    QString id = it->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) return;
    QKeySequence seq = m_edit->keySequence();
    auto& sc = Shortcuts::instance();
    QString other = sc.conflict(id, seq);
    if (!other.isEmpty()) {
        QString otherName;
        for (const auto& s : sc.all())
            if (s.id == other) otherName = T(s.text);
        if (QMessageBox::question(this, T("Shortcut in use"),
                                  T("%1 is already used by \"%2\". Move it here?").arg(seq.toString(QKeySequence::NativeText), otherName)) !=
            QMessageBox::Yes)
            return;
        sc.set(other, {});
    }
    sc.set(id, seq);
    populate();
}

}  // namespace gx
