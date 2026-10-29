#include "ui/ToolPanel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QPainterPath>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QHelpEvent>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/BrushLibrary.h"
#include "app/I18n.h"
#include "app/Shortcuts.h"
#include "core/Textures.h"
#include "tools/ToolManager.h"
#include "ui/BrushControls.h"
#include "ui/BrushEditor.h"
#include "ui/BrushPreview.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

// ============================================================================ BrushSlots

namespace {
constexpr int kTabH = 20, kCellH = 40, kCellMinW = 92;
}

BrushSlots::BrushSlots(QWidget* parent) : QWidget(parent) {
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    auto* lib = app()->brushes();
    m_group = app()->settings().value("brushes/tab").toString();
    if (!groupsShown().contains(m_group)) m_group = lib->current().group;
    connect(lib, &BrushLibrary::slotsChanged, this, [this] {
        if (!groupsShown().contains(m_group)) m_group = app()->brushes()->current().group;
        updateGeometry();
        update();
    });
    connect(lib, &BrushLibrary::currentChanged, this, [this](int i) {
        // a brush picked with a shortcut shows its tab
        const QString g = app()->brushes()->at(i).group;
        if (g != m_group) setGroup(g);
        update();
    });
    connect(lib, &BrushLibrary::brushChanged, this, [this] {
        updateGeometry();
        update();
    });
    connect(app()->tools(), &ToolManager::currentChanged, this, [this] { update(); });
    connect(app(), &App::colorsChanged, this, [this] { update(); });
    setToolTip(T("Brushes. Click a tab to see its brushes. Double-click a brush for all its settings, right-click for more."));
}

void BrushSlots::setGroup(const QString& g) {
    m_group = g;
    app()->settings().setValue("brushes/tab", g);
    updateGeometry();
    update();
}

QStringList BrushSlots::groupsShown() const {
    auto* lib = app()->brushes();
    QStringList present;
    for (int i = 0; i < lib->count(); ++i)
        if (!present.contains(lib->at(i).group)) present << lib->at(i).group;
    QStringList out;
    for (const QString& g : BrushSettings::groups())
        if (present.contains(g)) out << g;
    for (const QString& g : present)
        if (!out.contains(g)) out << g;  // the user's own tabs, after the built-in ones
    if (!m_group.isEmpty() && !out.contains(m_group)) out << m_group;
    return out;
}

int BrushSlots::cols(int w) const { return std::max(2, w / kCellMinW); }

BrushSlots::Layout BrushSlots::layoutFor(int w) const {
    Layout l;
    QFont f = font();
    f.setPointSizeF(std::max(6.5, f.pointSizeF() - 1.0));
    QFontMetrics fm(f);
    int x = 1, y = 1;
    for (const QString& g : groupsShown()) {
        const int tw = fm.horizontalAdvance(T(g)) + 14;
        if (x > 1 && x + tw > w - 1) {
            x = 1;
            y += kTabH + 2;
        }
        l.tabs << qMakePair(g, QRect(x, y, tw, kTabH));
        x += tw + 3;
    }
    l.top = y + kTabH + 4;
    auto* lib = app()->brushes();
    for (int i = 0; i < lib->count(); ++i)
        if (lib->at(i).group == m_group) l.cells << i;
    const int c = cols(w);
    const int n = int(l.cells.size()) + 1;  // + the "add" cell
    l.height = l.top + ((n + c - 1) / c) * kCellH + 2;
    return l;
}

int BrushSlots::heightForWidth(int w) const { return layoutFor(w).height; }

void BrushSlots::resizeEvent(QResizeEvent*) { updateGeometry(); }

QRect BrushSlots::cell(const Layout& l, int pos, int w) const {
    const int c = cols(w);
    const int cw = w / c;
    return QRect((pos % c) * cw + 1, l.top + (pos / c) * kCellH + 1, cw - 2, kCellH - 2);
}

int BrushSlots::indexAt(const QPoint& p, bool* addCell, QString* tab) const {
    const Layout l = layoutFor(width());
    *addCell = false;
    tab->clear();
    for (const auto& t : l.tabs)
        if (t.second.contains(p)) {
            *tab = t.first;
            return -1;
        }
    for (int k = 0; k <= l.cells.size(); ++k)
        if (cell(l, k, width()).contains(p)) {
            if (k == l.cells.size()) {
                *addCell = true;
                return -1;
            }
            return l.cells[k];
        }
    return -1;
}

QImage BrushSlots::strokePreview(const BrushSettings& b, const QSize& size) {
    // a tiny sample stroke, cached until the brush or the colors change
    BrushSettings small = b;
    small.size = std::min(b.size, size.height() * 0.8);
    small.taperIn = std::min(b.taperIn, 6.0);
    small.taperOut = std::min(b.taperOut, 8.0);
    const QByteArray key = QJsonDocument(small.toJson()).toJson(QJsonDocument::Compact) + QByteArray::number(size.width()) +
                           QByteArray::number(size.height()) + app()->foreground().name().toLatin1() + app()->background().name().toLatin1() +
                           QByteArray::number(devicePixelRatioF());
    auto it = m_previews.constFind(key);
    if (it != m_previews.cend()) return it.value();
    if (m_previews.size() > 300) m_previews.clear();
    QImage img = renderBrushPreview(small, size, app()->foreground(), app()->background(), false, devicePixelRatioF());
    m_previews.insert(key, img);
    return img;
}

void BrushSlots::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const ThemeColors& c = Theme::colors();
    auto* lib = app()->brushes();
    const Layout l = layoutFor(width());
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1.0));
    p.setFont(small);
    // tabs
    for (const auto& t : l.tabs) {
        const bool cur = t.first == m_group;
        p.setPen(cur ? c.accent : c.border);
        p.setBrush(cur ? c.accent : c.base);
        p.drawRoundedRect(QRectF(t.second).adjusted(0.5, 0.5, -0.5, -0.5), kTabH / 2.0, kTabH / 2.0);
        p.setPen(cur ? c.accentText : c.text);
        p.drawText(t.second, Qt::AlignCenter, T(t.first));
    }
    const bool brushActive = app()->tools()->current() && app()->tools()->current()->id() == "brush";
    for (int k = 0; k < l.cells.size(); ++k) {
        const int i = l.cells[k];
        QRect r = cell(l, k, width());
        bool cur = brushActive && i == lib->currentIndex();
        p.setPen(cur ? c.accent : c.border);
        p.setBrush(cur ? c.accent : c.button);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        QColor fg = cur ? c.accentText : c.text;
        const BrushSettings& b = lib->at(i);
        p.drawPixmap(QRect(r.left() + 3, r.top() + 2, 16, 16), Theme::iconPixmap(b.iconKey, 16, fg));
        p.setPen(fg);
        QString name = p.fontMetrics().elidedText(T(b.name), Qt::ElideRight, r.width() - 24);
        p.drawText(QRect(r.left() + 21, r.top() + 1, r.width() - 23, 18), Qt::AlignLeft | Qt::AlignVCenter, name);
        // size and a small sample stroke
        QString size = QString::number(b.size, 'f', b.size < 10 ? 1 : 0);
        p.setPen(cur ? c.accentText : c.dim);
        const int sw = p.fontMetrics().horizontalAdvance("0000") + 2;
        p.drawText(QRect(r.left() + 3, r.top() + 20, sw, 16), Qt::AlignLeft | Qt::AlignVCenter, size);
        const QRect pr(r.left() + sw + 3, r.top() + 21, r.width() - sw - 6, 15);
        if (pr.width() > 12) {
            QImage img = strokePreview(b, pr.size());
            p.save();
            QPainterPath clip;
            clip.addRoundedRect(QRectF(pr), 2, 2);
            p.setClipPath(clip);
            p.drawImage(pr.topLeft(), img);
            p.restore();
        }
    }
    QRect add = cell(l, int(l.cells.size()), width());
    p.setPen(QPen(c.border, 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(add).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    p.drawPixmap(QRect(add.center().x() - 8, add.center().y() - 8, 16, 16), Theme::iconPixmap("plus", 16, c.dim));
}

void BrushSlots::mousePressEvent(QMouseEvent* e) {
    bool addCell = false;
    QString tab;
    const int i = indexAt(e->pos(), &addCell, &tab);
    if (!tab.isEmpty()) {
        if (e->button() == Qt::LeftButton) setGroup(tab);
        return;
    }
    if (e->button() == Qt::RightButton) {
        if (i >= 0 || addCell) menu(i, e->globalPosition().toPoint());
        return;
    }
    if (addCell) {
        menu(-1, e->globalPosition().toPoint());
        return;
    }
    if (i >= 0) app()->tools()->selectBrushSlot(i);
}

bool BrushSlots::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        // the full name of the brush under the mouse (names are cut in narrow panels)
        auto* he = static_cast<QHelpEvent*>(e);
        bool addCell = false;
        QString tab;
        const int i = indexAt(he->pos(), &addCell, &tab);
        if (i >= 0) {
            const BrushSettings& b = app()->brushes()->at(i);
            QToolTip::showText(he->globalPos(), QStringLiteral("<b>%1</b><br>%2 px<br><i>%3</i>")
                                                    .arg(T(b.name).toHtmlEscaped())
                                                    .arg(b.size, 0, 'f', b.size < 10 ? 1 : 0)
                                                    .arg(T("Double-click for all settings").toHtmlEscaped()),
                               this);
            return true;
        }
    }
    return QWidget::event(e);
}

void BrushSlots::mouseDoubleClickEvent(QMouseEvent* e) {
    bool addCell = false;
    QString tab;
    const int i = indexAt(e->pos(), &addCell, &tab);
    if (i < 0) return;
    app()->tools()->selectBrushSlot(i);
    BrushEditorDialog::open(i, window());
}

void BrushSlots::menu(int i, const QPoint& global) {
    auto* lib = app()->brushes();
    QMenu m(this);
    if (i >= 0 && i < lib->count()) {
        m.addAction(Theme::icon("settings"), T("Brush settings..."), this, [this, i] { BrushEditorDialog::open(i, window()); });
        m.addSeparator();
    }
    QMenu* add = m.addMenu(Theme::icon("plus"), T("New brush from"));
    QString lastGroup;
    for (const BrushSettings& d : BrushSettings::defaults()) {
        if (d.group != lastGroup) {
            if (!lastGroup.isEmpty()) add->addSeparator();
            add->addSection(T(d.group));
            lastGroup = d.group;
        }
        add->addAction(Theme::icon(d.iconKey), T(d.name), this, [this, lib, d] {
            BrushSettings b = d;
            b.group = m_group;  // goes into the tab that is open
            int idx = lib->add(b);
            app()->tools()->selectBrushSlot(idx);
        });
    }
    if (i >= 0 && i < lib->count()) {
        m.addAction(Theme::icon("duplicate"), T("Duplicate"), this, [lib, i] { app()->tools()->selectBrushSlot(lib->duplicate(i)); });
        m.addAction(T("Rename..."), this, [this, lib, i] {
            bool ok = false;
            QString n = QInputDialog::getText(this, T("Rename brush"), T("Name:"), QLineEdit::Normal, T(lib->at(i).name), &ok);
            if (ok && !n.trimmed().isEmpty()) {
                BrushSettings s = lib->at(i);
                s.name = n.trimmed();
                lib->update(i, s);
            }
        });
        QMenu* icon = m.addMenu(T("Icon"));
        for (const char* k : {"pen", "ink", "pencil", "airbrush", "brush", "watercolor", "marker", "chalk", "bristle", "binary",
                              "eraser", "softeraser", "blur", "smudge", "hatching", "stipple", "charcoal", "blender", "glow",
                              "highlight", "droplet", "ribbon", "stamp", "leaf", "rain", "snow", "fur"})
            icon->addAction(Theme::icon(k), QString(), this, [lib, i, k] {
                BrushSettings s = lib->at(i);
                s.iconKey = k;
                lib->update(i, s);
            });
        QMenu* tabs = m.addMenu(T("Move to tab"));
        QStringList all = BrushSettings::groups();
        for (const QString& g : groupsShown())
            if (!all.contains(g)) all << g;
        for (const QString& g : all) {
            QAction* a = tabs->addAction(T(g), this, [this, lib, i, g] {
                BrushSettings s = lib->at(i);
                s.group = g;
                lib->update(i, s);
                setGroup(g);
            });
            a->setCheckable(true);
            a->setChecked(lib->at(i).group == g);
        }
        tabs->addSeparator();
        tabs->addAction(T("New tab..."), this, [this, lib, i] {
            bool ok = false;
            QString n = QInputDialog::getText(this, T("New tab"), T("Name:"), QLineEdit::Normal, QString(), &ok);
            if (ok && !n.trimmed().isEmpty()) {
                BrushSettings s = lib->at(i);
                s.group = n.trimmed();
                lib->update(i, s);
                setGroup(s.group);
            }
        });
        int pos = -1;
        const Layout l = layoutFor(width());
        pos = int(l.cells.indexOf(i));
        m.addAction(T("Move left"), this, [lib, i, l, pos] { lib->move(i, l.cells[pos - 1]); })->setEnabled(pos > 0);
        m.addAction(T("Move right"), this, [lib, i, l, pos] { lib->move(i, l.cells[pos + 1]); })->setEnabled(pos >= 0 && pos < l.cells.size() - 1);
        m.addAction(T("Reset to default"), this, [lib, i] { lib->resetSlot(i); });
        m.addSeparator();
        m.addAction(T("Export brush..."), this, [this, lib, i] {
            QString f = QFileDialog::getSaveFileName(this, T("Export brush"), lib->at(i).name + ".gxbrush", "GraphiX brush (*.gxbrush)");
            QString err;
            if (!f.isEmpty() && !lib->exportFile(f, {i}, &err)) QMessageBox::warning(this, T("Export brush"), err);
        });
        m.addAction(Theme::icon("trash"), T("Delete"), this, [this, lib, i] {
            if (QMessageBox::question(this, T("Delete brush"), T("Delete the brush \"%1\"?").arg(T(lib->at(i).name))) == QMessageBox::Yes)
                lib->remove(i);
        })->setEnabled(lib->count() > 1);
    }
    m.addSeparator();
    m.addAction(T("Import brushes..."), this, [this, lib] {
        QString f = QFileDialog::getOpenFileName(this, T("Import brushes"), {}, "GraphiX brushes (*.gxbrush *.json);;All files (*)");
        QString err;
        if (!f.isEmpty() && !lib->importFile(f, &err)) QMessageBox::warning(this, T("Import brushes"), err);
    });
    m.addAction(T("Export all brushes..."), this, [this, lib] {
        QString f = QFileDialog::getSaveFileName(this, T("Export brushes"), "brushes.gxbrush", "GraphiX brushes (*.gxbrush)");
        QList<int> all;
        for (int k = 0; k < lib->count(); ++k) all << k;
        QString err;
        if (!f.isEmpty() && !lib->exportFile(f, all, &err)) QMessageBox::warning(this, T("Export brushes"), err);
    });
    m.addAction(T("Restore default brushes"), this, [this, lib] {
        if (QMessageBox::question(this, T("Restore default brushes"), T("Replace all brush slots with the defaults?")) == QMessageBox::Yes)
            lib->resetToDefaults();
    });
    m.exec(global);
}

// ============================================================================ BrushSettingsWidget

BrushSettingsWidget::BrushSettingsWidget(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(3);

    auto* head = new QHBoxLayout;
    m_title = new QLabel(this);
    m_title->setObjectName("sectionTitle");
    head->addWidget(m_title, 1);
    auto* all = new QToolButton(this);
    all->setIcon(Theme::icon("settings"));
    all->setText(T("All settings"));
    all->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    all->setAutoRaise(true);
    all->setToolTip(T("All settings of this brush, with a preview (or double-click the brush)"));
    connect(all, &QToolButton::clicked, this, [this] { BrushEditorDialog::open(app()->brushes()->currentIndex(), window()); });
    head->addWidget(all);
    v->addLayout(head);

    m_controls = new BrushControls(this, [this] { store(); });
    const QList<BrushControls::Page> pages = m_controls->build(this, true);
    for (const BrushControls::Page& pg : pages) {
        if (pg.id == "basic") {
            v->addWidget(pg.widget);  // the main settings are always shown
            continue;
        }
        auto* sec = new Section(pg.title, this, pg.id == "mixing");
        sec->body()->addWidget(pg.widget);
        sec->setStateKey("brush/" + pg.id);
        v->addWidget(sec);
        m_sections << qMakePair(static_cast<QWidget*>(sec), pg.visible);
    }
    v->addStretch();
    connect(m_controls, &BrushControls::visibilityChanged, this, [this] {
        m_controls->load(m_controls->settings);
        updateVisibility();
    });

    connect(app()->brushes(), &BrushLibrary::currentChanged, this, &BrushSettingsWidget::load);
    connect(app()->brushes(), &BrushLibrary::brushChanged, this, [this](int i) {
        if (i == app()->brushes()->currentIndex() && !m_controls->loading() && !m_storing) load();
    });
    connect(app()->brushes(), &BrushLibrary::slotsChanged, this, &BrushSettingsWidget::load);
    load();
}

void BrushSettingsWidget::load() {
    const BrushSettings& b = app()->brushes()->current();
    m_title->setText(T(b.name));
    // the texture lists only change when textures are added or reloaded
    auto& tl = TextureLibrary::instance();
    const qsizetype n = tl.names(TextureKind::Shape).size() * 1000 + tl.names(TextureKind::Brush).size() * 31 + tl.names(TextureKind::Paper).size() +
                        tl.names(TextureKind::Ribbon).size() * 100000 + tl.stampSets().size() * 10000000;
    if (n != m_textureCount) {
        m_textureCount = n;
        m_controls->refreshLists();
    }
    m_controls->load(b);
    updateVisibility();
}

void BrushSettingsWidget::store() {
    if (m_controls->loading()) return;
    BrushLibrary* lib = app()->brushes();
    m_storing = true;
    lib->update(lib->currentIndex(), m_controls->settings);
    m_storing = false;
}

void BrushSettingsWidget::updateVisibility() {
    for (const auto& s : m_sections) s.first->setVisible(s.second(m_controls->settings));
}

// ============================================================================ ToolPanel

ToolPanel::ToolPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);
    auto* grid = new QGridLayout;
    m_grid = grid;
    grid->setSpacing(1);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    const QStringList order = {"select-rect", "select-ellipse", "lasso", "magic-wand", "select-brush", "move", "transform",
                               "warp", "liquify", "fill", "lasso-fill", "gradient", "eyedropper", "hand", "zoom", "rotate",
                               "text", "vpen", "curve", "vedit", "vpressure", "vweight", "vcolor", "veraser", "ruler"};
    int i = 0;
    for (const QString& id : order) {
        Tool* t = app()->tools()->tool(id);
        if (!t) continue;
        auto* b = new QToolButton(this);
        b->setIcon(Theme::icon(t->iconName()));
        b->setIconSize(QSize(app()->prefs().iconSize, app()->prefs().iconSize));
        b->setCheckable(true);
        b->setAutoRaise(true);
        QKeySequence ks = Shortcuts::instance().get("tool." + id);
        b->setToolTip(ks.isEmpty() ? T(t->name()) : QStringLiteral("%1  (%2)").arg(T(t->name()), ks.toString(QKeySequence::NativeText)));
        m_group->addButton(b);
        m_buttons.insert(id, b);
        m_buttonOrder << b;
        connect(b, &QToolButton::clicked, this, [id] { app()->tools()->setCurrent(id); });
        ++i;
    }
    v->addLayout(grid);
    layoutToolButtons();
    connect(app(), &App::prefsChanged, this, &ToolPanel::layoutToolButtons);
    v->addWidget(new BrushSlots(this));

    m_stack = new QStackedWidget(this);
    m_brush = new BrushSettingsWidget(m_stack);
    m_stack->addWidget(m_brush);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidget(m_stack);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    v->addWidget(m_scroll, 1);

    connect(app()->tools(), &ToolManager::currentChanged, this, &ToolPanel::showOptionsFor);
    showOptionsFor(app()->tools()->current());
}

void ToolPanel::layoutToolButtons() {
    const int s = app()->prefs().iconSize;
    const int cols = s <= 22 ? 8 : s <= 28 ? 6 : 5;  // keep the panel narrow with big icons
    int i = 0;
    for (QToolButton* b : m_buttonOrder) {
        b->setIconSize(QSize(s, s));
        m_grid->addWidget(b, i / cols, i % cols);
        ++i;
    }
}

void ToolPanel::showOptionsFor(Tool* t) {
    m_group->setExclusive(false);
    for (QToolButton* b : m_buttons) b->setChecked(false);
    if (t)
        if (QToolButton* b = m_buttons.value(t->id())) b->setChecked(true);
    m_group->setExclusive(true);
    if (!t) return;
    if (t->id() == "brush") {
        m_stack->setCurrentWidget(m_brush);
        return;
    }
    QWidget* w = m_options.value(t);
    if (!w) {
        w = t->createOptions(m_stack);
        if (!w) {
            w = new QWidget(m_stack);
            auto* l = new QVBoxLayout(w);
            auto* lab = new QLabel(T(t->name()), w);
            lab->setObjectName("sectionTitle");
            l->addWidget(lab);
            l->addStretch();
        }
        for (QComboBox* cb : w->findChildren<QComboBox*>()) {
            cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            cb->setMinimumContentsLength(4);
        }
        // the same margins and spacing for every tool, like the brush settings
        if (auto* lay = qobject_cast<QBoxLayout*>(w->layout())) {
            lay->setContentsMargins(4, 4, 4, 4);
            lay->setSpacing(3);
        }
        m_options.insert(t, w);
        m_stack->addWidget(w);
    }
    m_stack->setCurrentWidget(w);
}

}  // namespace gx
