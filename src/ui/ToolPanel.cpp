#include "ui/ToolPanel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/BrushLibrary.h"
#include "app/I18n.h"
#include "app/Shortcuts.h"
#include "core/Textures.h"
#include "tools/ToolManager.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

// ============================================================================ BrushSlots

BrushSlots::BrushSlots(QWidget* parent) : QWidget(parent) {
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    auto* lib = app()->brushes();
    connect(lib, &BrushLibrary::slotsChanged, this, [this] { updateGeometry(); update(); });
    connect(lib, &BrushLibrary::currentChanged, this, [this] { update(); });
    connect(lib, &BrushLibrary::brushChanged, this, [this] { update(); });
    connect(app()->tools(), &ToolManager::currentChanged, this, [this] { update(); });
    setToolTip(T("Brush slots. Double-click to rename, right-click for more."));
}

int BrushSlots::cols() const { return std::max(2, width() / 74); }

int BrushSlots::heightForWidth(int w) const {
    int c = std::max(2, w / 74);
    int n = app()->brushes()->count() + 1;  // + "add" cell
    return ((n + c - 1) / c) * 40 + 2;
}

QRect BrushSlots::cell(int i) const {
    int c = cols();
    int cw = width() / c;
    return QRect((i % c) * cw + 1, (i / c) * 40 + 1, cw - 2, 38);
}

int BrushSlots::indexAt(const QPoint& p) const {
    int n = app()->brushes()->count();
    for (int i = 0; i <= n; ++i)
        if (cell(i).contains(p)) return i;
    return -1;
}

void BrushSlots::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const ThemeColors& c = Theme::colors();
    auto* lib = app()->brushes();
    const bool brushActive = app()->tools()->current() && app()->tools()->current()->id() == "brush";
    for (int i = 0; i < lib->count(); ++i) {
        QRect r = cell(i);
        bool cur = brushActive && i == lib->currentIndex();
        p.setPen(cur ? c.accent : c.border);
        p.setBrush(cur ? c.accent : c.button);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        QColor fg = cur ? c.accentText : c.text;
        const BrushSettings& b = lib->at(i);
        p.drawPixmap(QRect(r.left() + 3, r.top() + 3, 18, 18), Theme::iconPixmap(b.iconKey, 18, fg));
        p.setPen(fg);
        QFont f = font();
        f.setPointSizeF(std::max(6.5, f.pointSizeF() - 1.0));
        p.setFont(f);
        QString name = p.fontMetrics().elidedText(T(b.name), Qt::ElideRight, r.width() - 26);
        p.drawText(QRect(r.left() + 23, r.top() + 2, r.width() - 25, 20), Qt::AlignLeft | Qt::AlignVCenter, name);
        QString size = QString::number(b.size, 'f', b.size < 10 ? 1 : 0);
        p.setPen(cur ? c.accentText : c.dim);
        p.drawText(QRect(r.left() + 4, r.top() + 20, r.width() - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, size);
    }
    QRect add = cell(lib->count());
    p.setPen(QPen(c.border, 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(add).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    p.drawPixmap(QRect(add.center().x() - 8, add.center().y() - 8, 16, 16), Theme::iconPixmap("plus", 16, c.dim));
}

void BrushSlots::mousePressEvent(QMouseEvent* e) {
    int i = indexAt(e->pos());
    auto* lib = app()->brushes();
    if (i < 0) return;
    if (e->button() == Qt::RightButton) {
        menu(i, e->globalPosition().toPoint());
        return;
    }
    if (i == lib->count()) {
        menu(-1, e->globalPosition().toPoint());
        return;
    }
    app()->tools()->selectBrushSlot(i);
}

void BrushSlots::mouseDoubleClickEvent(QMouseEvent* e) {
    int i = indexAt(e->pos());
    auto* lib = app()->brushes();
    if (i < 0 || i >= lib->count()) return;
    bool ok = false;
    QString n = QInputDialog::getText(this, T("Rename brush"), T("Name:"), QLineEdit::Normal, lib->at(i).name, &ok);
    if (ok && !n.trimmed().isEmpty()) {
        BrushSettings s = lib->at(i);
        s.name = n.trimmed();
        lib->update(i, s);
    }
}

void BrushSlots::menu(int i, const QPoint& global) {
    auto* lib = app()->brushes();
    QMenu m(this);
    QMenu* add = m.addMenu(Theme::icon("plus"), T("New brush from"));
    for (const BrushSettings& d : BrushSettings::defaults()) {
        add->addAction(Theme::icon(d.iconKey), T(d.name), this, [lib, d] {
            int idx = lib->add(d);
            app()->tools()->selectBrushSlot(idx);
        });
    }
    if (i >= 0 && i < lib->count()) {
        m.addAction(Theme::icon("duplicate"), T("Duplicate"), this, [lib, i] { app()->tools()->selectBrushSlot(lib->duplicate(i)); });
        m.addAction(T("Rename..."), this, [this, lib, i] {
            bool ok = false;
            QString n = QInputDialog::getText(this, T("Rename brush"), T("Name:"), QLineEdit::Normal, lib->at(i).name, &ok);
            if (ok && !n.trimmed().isEmpty()) {
                BrushSettings s = lib->at(i);
                s.name = n.trimmed();
                lib->update(i, s);
            }
        });
        QMenu* icon = m.addMenu(T("Icon"));
        for (const char* k : {"pen", "ink", "pencil", "airbrush", "brush", "watercolor", "marker", "chalk", "bristle", "binary",
                              "eraser", "softeraser", "blur", "smudge"})
            icon->addAction(Theme::icon(k), QString(), this, [lib, i, k] {
                BrushSettings s = lib->at(i);
                s.iconKey = k;
                lib->update(i, s);
            });
        m.addAction(T("Move left"), this, [lib, i] { lib->move(i, i - 1); })->setEnabled(i > 0);
        m.addAction(T("Move right"), this, [lib, i] { lib->move(i, i + 1); })->setEnabled(i < lib->count() - 1);
        m.addAction(T("Reset to default"), this, [lib, i] { lib->resetSlot(i); });
        m.addSeparator();
        m.addAction(T("Export brush..."), this, [this, lib, i] {
            QString f = QFileDialog::getSaveFileName(this, T("Export brush"), lib->at(i).name + ".gxbrush", "GraphiX brush (*.gxbrush)");
            QString err;
            if (!f.isEmpty() && !lib->exportFile(f, {i}, &err)) QMessageBox::warning(this, T("Export brush"), err);
        });
        m.addAction(Theme::icon("trash"), T("Delete"), this, [this, lib, i] {
            if (QMessageBox::question(this, T("Delete brush"), T("Delete the brush \"%1\"?").arg(lib->at(i).name)) == QMessageBox::Yes)
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

// ============================================================================ quick sizes

namespace {
class QuickSizes : public QWidget {
public:
    explicit QuickSizes(std::function<void(double)> pick, QWidget* parent) : QWidget(parent), m_pick(std::move(pick)) {
        setFixedHeight(2 * 22 + 2);
        setToolTip(T("Quick brush sizes"));
    }
    void setCurrent(double s) { m_cur = s; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const ThemeColors& c = Theme::colors();
        int n = int(sizes().size()), per = (n + 1) / 2;
        double cw = width() / double(per);
        for (int i = 0; i < n; ++i) {
            QRectF r((i % per) * cw, (i / per) * 22, cw - 1, 21);
            double s = sizes()[size_t(i)];
            bool cur = std::fabs(s - m_cur) < 0.01;
            p.setPen(cur ? c.accent : c.border);
            p.setBrush(cur ? c.accent : c.base);
            p.drawRoundedRect(r, 2, 2);
            const bool room = cw >= 40;  // narrow panel: numbers only
            if (room) {
                double dot = std::clamp(std::log2(s + 1) * 1.6, 1.5, 9.0);
                p.setPen(Qt::NoPen);
                p.setBrush(cur ? c.accentText : c.text);
                p.drawEllipse(QPointF(r.left() + 8, r.center().y()), dot / 2, dot / 2);
            }
            p.setPen(cur ? c.accentText : c.text);
            QFont f = font();
            f.setPointSizeF(std::max(6.0, f.pointSizeF() - 1.5));
            p.setFont(f);
            if (room) p.drawText(r.adjusted(14, 0, -2, 0), Qt::AlignVCenter | Qt::AlignRight, QString::number(s));
            else p.drawText(r, Qt::AlignCenter, QString::number(s));
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        int n = int(sizes().size()), per = (n + 1) / 2;
        double cw = width() / double(per);
        int i = int(e->position().y() / 22) * per + int(e->position().x() / cw);
        if (i >= 0 && i < n) m_pick(sizes()[size_t(i)]);
    }

private:
    static const std::vector<double>& sizes() {
        static const std::vector<double> s = {1, 2, 3, 5, 7, 10, 15, 20, 30, 50, 70, 100, 150, 250, 400, 700};
        return s;
    }
    std::function<void(double)> m_pick;
    double m_cur = 0;
};
}  // namespace

// ============================================================================ BrushSettingsWidget

ValueBar* BrushSettingsWidget::bar(QWidget* parent, const QString& label, double min, double max, int dec, double* field,
                                   double scale, const QString& suffix, bool log) {
    auto* b = new ValueBar(label, min, max, dec, parent);
    b->setDisplayScale(scale);
    b->setSuffix(suffix);
    b->setLogarithmic(log);
    connect(b, &ValueBar::valueChanged, this, [this, field](double v) {
        if (m_loading) return;
        *field = v;
        store();
    });
    m_loaders << [b, field] { b->setValue(*field); };
    return b;
}

QCheckBox* BrushSettingsWidget::check(QWidget* parent, const QString& label, bool* field) {
    auto* c = new QCheckBox(label, parent);
    connect(c, &QCheckBox::toggled, this, [this, field](bool on) {
        if (m_loading) return;
        *field = on;
        store();
        updateVisibility();
    });
    m_loaders << [c, field] { c->setChecked(*field); };
    return c;
}

BrushSettingsWidget::BrushSettingsWidget(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(3);

    m_title = new QLabel(this);
    m_title->setObjectName("sectionTitle");
    v->addWidget(m_title);

    auto* row = new QHBoxLayout;
    m_engine = new QComboBox(this);
    for (int e = 0; e <= 4; ++e) m_engine->addItem(T(brushEngineName(BrushEngine(e))), e);
    m_engine->setToolTip(T("How the brush puts paint on the canvas"));
    m_mode = new QComboBox(this);
    for (BlendMode bm : blendModeUiOrder()) m_mode->addItem(T(blendModeName(bm)), int(bm));
    m_mode->setToolTip(T("Brush blending mode"));
    row->addWidget(m_engine, 1);
    row->addWidget(m_mode, 1);
    v->addLayout(row);
    connect(m_engine, &QComboBox::activated, this, [this](int i) {
        m_s.engine = BrushEngine(m_engine->itemData(i).toInt());
        store();
        updateVisibility();
    });
    connect(m_mode, &QComboBox::activated, this, [this](int i) {
        m_s.paintMode = BlendMode(m_mode->itemData(i).toInt());
        store();
    });

    auto* sizeBar = bar(this, T("Size"), 0.5, 5000, 1, &m_s.size, 1, " px", true);
    v->addWidget(sizeBar);
    auto* quick = new QuickSizes([this](double s) { m_s.size = s; store(); load(); }, this);
    m_loaders << [this, quick] { quick->setCurrent(m_s.size); };
    v->addWidget(quick);
    v->addWidget(bar(this, T("Min size"), 0, 1, 2, &m_s.minSize, 100, "%"));
    v->addWidget(bar(this, T("Opacity"), 0, 1, 2, &m_s.opacity, 100, "%"));
    v->addWidget(bar(this, T("Density"), 0, 1, 2, &m_s.density, 100, "%"));
    v->addWidget(bar(this, T("Min density"), 0, 1, 2, &m_s.minDensity, 100, "%"));
    v->addWidget(bar(this, T("Hardness"), 0, 1, 2, &m_s.hardness, 100, "%"));
    v->addWidget(bar(this, T("Spacing"), 0.01, 2, 2, &m_s.spacing, 100, "%"));

    auto* pr = new QHBoxLayout;
    pr->addWidget(new QLabel(T("Pressure:"), this));
    pr->addWidget(check(this, T("Size"), &m_s.sizePressure));
    pr->addWidget(check(this, T("Density"), &m_s.densityPressure));
    pr->addStretch();
    v->addLayout(pr);

    m_mixSection = new Section(T("Mixing"), this, true);
    m_mixSection->body()->addWidget(bar(this, T("Blending"), 0, 1, 2, &m_s.blending, 100, "%"));
    m_mixSection->body()->addWidget(bar(this, T("Dilution"), 0, 1, 2, &m_s.dilution, 100, "%"));
    m_mixSection->body()->addWidget(bar(this, T("Persistence"), 0, 1, 2, &m_s.persistence, 100, "%"));
    m_mixSection->body()->addWidget(bar(this, T("Fringe"), 0, 1, 2, &m_s.fringe, 100, "%"));
    m_mixSection->body()->addWidget(check(this, T("Light pressure mixes more"), &m_s.blendingPressure));
    v->addWidget(m_mixSection);

    auto* shape = new Section(T("Tip shape"), this, false);
    m_shape = new QComboBox(this);
    shape->body()->addWidget(m_shape);
    shape->body()->addWidget(bar(this, T("Shape strength"), 0, 1, 2, &m_s.shapeStrength, 100, "%"));
    shape->body()->addWidget(check(this, T("Shape is the whole tip"), &m_s.shapeAsTip));
    shape->body()->addWidget(bar(this, T("Roundness"), 0.02, 1, 2, &m_s.roundness, 100, "%"));
    shape->body()->addWidget(bar(this, T("Angle"), -180, 180, 0, &m_s.angle, 1, QString(QChar(0xB0))));
    shape->body()->addWidget(check(this, T("Angle follows the stroke"), &m_s.angleFollowsStroke));
    shape->body()->addWidget(check(this, T("Angle from pen tilt"), &m_s.angleFromTilt));
    shape->body()->addWidget(check(this, T("Anti-aliasing"), &m_s.antialias));
    v->addWidget(shape);
    connect(m_shape, &QComboBox::activated, this, [this](int i) {
        m_s.shape = m_shape->itemData(i).toString();
        store();
    });

    auto* tex = new Section(T("Texture"), this, false);
    m_texture = new QComboBox(this);
    tex->body()->addWidget(m_texture);
    tex->body()->addWidget(bar(this, T("Strength"), 0, 1, 2, &m_s.textureStrength, 100, "%"));
    tex->body()->addWidget(bar(this, T("Scale"), 0.05, 20, 2, &m_s.textureScale, 100, "%", true));
    tex->body()->addWidget(check(this, T("Invert"), &m_s.textureInvert));
    tex->body()->addWidget(check(this, T("Texture every dab"), &m_s.textureEachDab));
    auto* hint = new QLabel(T("Add your own textures: Help > Open textures folder."), this);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    tex->body()->addWidget(hint);
    v->addWidget(tex);
    connect(m_texture, &QComboBox::activated, this, [this](int i) {
        m_s.texture = m_texture->itemData(i).toString();
        if (!m_s.texture.isEmpty() && m_s.textureStrength <= 0) m_s.textureStrength = 0.5;
        store();
        load();
    });

    auto* dyn = new Section(T("Dynamics"), this, false);
    dyn->body()->addWidget(bar(this, T("Size jitter"), 0, 1, 2, &m_s.sizeJitter, 100, "%"));
    dyn->body()->addWidget(bar(this, T("Opacity jitter"), 0, 1, 2, &m_s.opacityJitter, 100, "%"));
    dyn->body()->addWidget(bar(this, T("Angle jitter"), 0, 1, 2, &m_s.angleJitter, 100, "%"));
    dyn->body()->addWidget(bar(this, T("Scatter"), 0, 5, 2, &m_s.scatter, 100, "%"));
    dyn->body()->addWidget(bar(this, T("Hue jitter"), 0, 1, 2, &m_s.hueJitter, 100, "%"));
    dyn->body()->addWidget(bar(this, T("Saturation jitter"), 0, 1, 2, &m_s.saturationJitter, 100, "%"));
    dyn->body()->addWidget(bar(this, T("Value jitter"), 0, 1, 2, &m_s.valueJitter, 100, "%"));
    dyn->body()->addWidget(bar(this, T("Taper start"), 0, 500, 0, &m_s.taperIn, 1, " px"));
    dyn->body()->addWidget(bar(this, T("Taper end"), 0, 500, 0, &m_s.taperOut, 1, " px"));
    v->addWidget(dyn);

    auto* adv = new Section(T("Behaviour"), this, false);
    adv->body()->addWidget(check(this, T("Build up (airbrush style)"), &m_s.buildup));
    adv->body()->addWidget(check(this, T("Spray while pen is still"), &m_s.airbrush));
    adv->body()->addWidget(bar(this, T("Spray rate"), 1, 200, 0, &m_s.airbrushRate, 1, "/s"));
    adv->body()->addWidget(check(this, T("Keep opacity"), &m_s.keepOpacity));
    adv->body()->addWidget(check(this, T("Own pressure curve"), &m_s.customPressureCurve));
    auto* curveBtn = new QPushButton(T("Edit brush pressure curve..."), this);
    adv->body()->addWidget(curveBtn);
    connect(curveBtn, &QPushButton::clicked, this, [this] {
        QDialog d(this);
        d.setWindowTitle(T("Brush pressure curve"));
        auto* l = new QVBoxLayout(&d);
        auto* ce = new CurveEditor(&d);
        ce->setPoints(m_s.pressureCurve);
        l->addWidget(ce);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        l->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() == QDialog::Accepted) {
            m_s.pressureCurve = ce->points();
            m_s.customPressureCurve = true;
            store();
            load();
        }
    });
    v->addWidget(adv);
    v->addStretch();

    // Keep the panel narrow: combo boxes shrink, long labels elide instead of widening the dock.
    for (QComboBox* cb : findChildren<QComboBox*>()) {
        cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        cb->setMinimumContentsLength(4);
    }

    connect(app()->brushes(), &BrushLibrary::currentChanged, this, &BrushSettingsWidget::load);
    connect(app()->brushes(), &BrushLibrary::brushChanged, this, [this](int i) {
        if (i == app()->brushes()->currentIndex() && !m_loading) load();
    });
    connect(app()->brushes(), &BrushLibrary::slotsChanged, this, &BrushSettingsWidget::load);
    load();
}

void BrushSettingsWidget::load() {
    m_loading = true;
    m_s = app()->brushes()->current();
    m_title->setText(T(m_s.name));
    m_engine->setCurrentIndex(std::max(0, m_engine->findData(int(m_s.engine))));
    m_mode->setCurrentIndex(std::max(0, m_mode->findData(int(m_s.paintMode))));
    auto& lib = TextureLibrary::instance();
    m_shape->clear();
    m_shape->addItem(T("Round"), QString());
    for (const QString& n : lib.names(TextureKind::Shape)) {
        auto t = lib.get(TextureKind::Shape, n);
        m_shape->addItem(QIcon(QPixmap::fromImage(t->preview)), n, n);
    }
    m_shape->setCurrentIndex(std::max(0, m_shape->findData(m_s.shape)));
    m_texture->clear();
    m_texture->addItem(T("None"), QString());
    for (TextureKind k : {TextureKind::Brush, TextureKind::Paper})
        for (const QString& n : lib.names(k)) {
            auto t = lib.get(k, n);
            if (m_texture->findData(n) < 0) m_texture->addItem(QIcon(QPixmap::fromImage(t->preview)), n, n);
        }
    m_texture->setCurrentIndex(std::max(0, m_texture->findData(m_s.texture)));
    for (auto& f : m_loaders) f();
    m_loading = false;
    updateVisibility();
}

void BrushSettingsWidget::store() {
    if (m_loading) return;
    m_loading = true;
    app()->brushes()->update(app()->brushes()->currentIndex(), m_s);
    m_loading = false;
}

void BrushSettingsWidget::updateVisibility() {
    m_mixSection->setVisible(m_s.engine == BrushEngine::Mix || m_s.engine == BrushEngine::Smudge);
    m_mode->setVisible(m_s.engine == BrushEngine::Paint);
}

// ============================================================================ ToolPanel

ToolPanel::ToolPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);
    auto* grid = new QGridLayout;
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
        b->setIconSize(QSize(20, 20));
        b->setCheckable(true);
        b->setAutoRaise(true);
        QKeySequence ks = Shortcuts::instance().get("tool." + id);
        b->setToolTip(ks.isEmpty() ? T(t->name()) : QStringLiteral("%1  (%2)").arg(T(t->name()), ks.toString(QKeySequence::NativeText)));
        m_group->addButton(b);
        m_buttons.insert(id, b);
        grid->addWidget(b, i / 8, i % 8);
        connect(b, &QToolButton::clicked, this, [id] { app()->tools()->setCurrent(id); });
        ++i;
    }
    v->addLayout(grid);
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
        m_options.insert(t, w);
        m_stack->addWidget(w);
    }
    m_stack->setCurrentWidget(w);
}

}  // namespace gx
