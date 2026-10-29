#include "ui/BrushEditor.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/BrushLibrary.h"
#include "app/I18n.h"
#include "tools/ToolManager.h"
#include "ui/BrushControls.h"
#include "ui/BrushPreview.h"
#include "ui/Theme.h"

namespace gx {

namespace {
QPointer<BrushEditorDialog> g_open;
}

void BrushEditorDialog::open(int slot, QWidget* parent) {
    if (g_open) g_open->close();  // one window at a time, for the brush asked for
    auto* d = new BrushEditorDialog(slot, parent);
    d->setAttribute(Qt::WA_DeleteOnClose);
    g_open = d;
    d->show();
    d->raise();
    d->activateWindow();
}

BrushEditorDialog::BrushEditorDialog(int slot, QWidget* parent) : QDialog(parent), m_slot(slot) {
    BrushLibrary* lib = app()->brushes();
    m_original = lib->at(slot);
    setWindowTitle(T("Brush settings") + " - " + T(m_original.name));
    setObjectName("brushEditor");
    // the canvas stays usable: try the brush there too
    setModal(false);

    m_controls = new BrushControls(this, [this] { apply(); });
    QList<BrushControls::Page> pages = m_controls->build(this, false);

    auto* root = new QVBoxLayout(this);
    auto* main = new QHBoxLayout;
    root->addLayout(main, 1);

    // ---- sections
    m_list = new QListWidget(this);
    m_list->setObjectName("prefsSidebar");
    m_list->setFixedWidth(170);
    m_list->setIconSize(QSize(18, 18));
    m_stack = new QStackedWidget(this);
    for (const BrushControls::Page& p : pages) {
        auto* it = new QListWidgetItem(Theme::icon(p.icon), p.title, m_list);
        it->setData(Qt::UserRole, p.id);
        auto* scroll = new QScrollArea(m_stack);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* holder = new QWidget(scroll);
        auto* hv = new QVBoxLayout(holder);
        hv->setContentsMargins(10, 6, 10, 6);
        auto* title = new QLabel(p.title, holder);
        QFont f = title->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() + 1);
        title->setFont(f);
        hv->addWidget(title);
        hv->addWidget(p.widget);
        hv->addStretch();
        scroll->setWidget(holder);
        m_stack->addWidget(scroll);
    }
    connect(m_list, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    main->addWidget(m_list);
    auto* mid = new QWidget(this);
    mid->setMinimumWidth(300);
    auto* mv = new QVBoxLayout(mid);
    mv->setContentsMargins(0, 0, 0, 0);
    // name and palette tab
    auto* form = new QFormLayout;
    m_name = new QLineEdit(T(m_original.name), mid);
    form->addRow(T("Name"), m_name);
    m_group = new QComboBox(mid);
    QStringList groups = BrushSettings::groups();
    for (int i = 0; i < lib->count(); ++i)
        if (!groups.contains(lib->at(i).group) && !lib->at(i).group.isEmpty()) groups << lib->at(i).group;
    for (const QString& g : groups) m_group->addItem(T(g), g);
    m_group->setCurrentIndex(std::max(0, m_group->findData(m_original.group)));
    form->addRow(T("Brush tab"), m_group);
    mv->addLayout(form);
    mv->addWidget(m_stack, 1);
    main->addWidget(mid, 1);

    // ---- sample stroke and test sheet
    auto* right = new QVBoxLayout;
    auto* pl = new QLabel(T("Preview"), this);
    pl->setObjectName("sectionTitle");
    right->addWidget(pl);
    m_preview = new BrushPreviewWidget(this);
    m_preview->setFixedHeight(120);
    m_preview->setMinimumWidth(360);
    right->addWidget(m_preview);
    auto* tl = new QHBoxLayout;
    auto* tlab = new QLabel(T("Try it here"), this);
    tlab->setObjectName("sectionTitle");
    tl->addWidget(tlab);
    tl->addStretch();
    auto* clear = new QToolButton(this);
    clear->setIcon(Theme::icon("trash"));
    clear->setToolTip(T("Clear the sheet"));
    clear->setAutoRaise(true);
    tl->addWidget(clear);
    right->addLayout(tl);
    m_pad = new BrushTestPad(this);
    m_pad->setMinimumSize(360, 220);
    right->addWidget(m_pad, 1);
    connect(clear, &QToolButton::clicked, m_pad, &BrushTestPad::clear);
    main->addLayout(right, 1);

    // ---- buttons
    auto* bb = new QDialogButtonBox(this);
    QPushButton* reset = bb->addButton(T("Reset to default"), QDialogButtonBox::ResetRole);
    bool isDefault = false;
    for (const BrushSettings& d : BrushSettings::defaults())
        if (d.name == m_original.name) isDefault = true;
    reset->setEnabled(isDefault);
    reset->setToolTip(T("The settings this brush came with"));
    bb->addButton(QDialogButtonBox::Cancel);
    QPushButton* ok = bb->addButton(QDialogButtonBox::Ok);
    ok->setText(T("Done"));
    ok->setDefault(true);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &BrushEditorDialog::reject);
    connect(reset, &QPushButton::clicked, this, [this] {
        for (const BrushSettings& d : BrushSettings::defaults())
            if (d.name == m_original.name) {
                BrushSettings s = d;
                s.group = m_controls->settings.group;
                m_controls->load(s);
                apply();
                updatePages();
            }
    });
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString& t) {
        if (t.trimmed().isEmpty()) return;
        m_controls->settings.name = t.trimmed();
        apply();
    });
    connect(m_group, &QComboBox::activated, this, [this](int i) {
        m_controls->settings.group = m_group->itemData(i).toString();
        apply();
    });
    connect(m_controls, &BrushControls::visibilityChanged, this, [this] {
        m_controls->load(m_controls->settings);  // enables / disables dependent controls
        updatePages();
    });
    // the palette changed under us (slot removed or moved): the window closes
    connect(lib, &BrushLibrary::slotsChanged, this, [this] {
        if (m_slot >= app()->brushes()->count()) close();
    });
    // colors are part of the preview
    connect(app(), &App::colorsChanged, this, [this] { m_preview->setBrush(m_controls->settings); });

    m_controls->load(m_original);
    m_preview->setBrush(m_original);
    m_pad->setBrush(m_original);
    updatePages();
    m_list->setCurrentRow(0);
    resize(1080, 640);
}

void BrushEditorDialog::updatePages() {
    // pages that do not apply to this kind of brush are hidden (mixing for a pen...)
    for (int i = 0; i < m_list->count(); ++i) {
        const QString id = m_list->item(i)->data(Qt::UserRole).toString();
        bool vis = true;
        if (id == "mixing") vis = m_controls->settings.engine == BrushEngine::Mix || m_controls->settings.engine == BrushEngine::Smudge;
        m_list->item(i)->setHidden(!vis);
        if (!vis && m_list->currentRow() == i) m_list->setCurrentRow(0);
    }
}

void BrushEditorDialog::apply() {
    BrushLibrary* lib = app()->brushes();
    if (m_slot < 0 || m_slot >= lib->count()) return;
    lib->update(m_slot, m_controls->settings);
    m_preview->setBrush(m_controls->settings);
    m_pad->setBrush(m_controls->settings);
}

void BrushEditorDialog::reject() {
    BrushLibrary* lib = app()->brushes();
    if (m_slot >= 0 && m_slot < lib->count()) lib->update(m_slot, m_original);
    QDialog::reject();
}

}  // namespace gx
