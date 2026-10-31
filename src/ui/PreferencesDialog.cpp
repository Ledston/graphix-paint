#include "ui/PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabletEvent>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "app/TabletInput.h"
#include "app/BrushLibrary.h"
#include "tools/ToolManager.h"
#include "ui/ColorPanel.h"
#include "ui/ToolPanel.h"
#include <QDockWidget>
#include <QGridLayout>
#include <QMainWindow>
#include <QTimer>
#include <QToolBar>
#include "ui/ShortcutsDialog.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

namespace {
// Draw here to see the pen pressure on the curve.
class PressureTest : public QWidget {
public:
    PressureTest(CurveEditor* curve, const double* threshold, QWidget* p) : QWidget(p), m_curve(curve), m_threshold(threshold) {
        setMinimumHeight(110);
        setAttribute(Qt::WA_TabletTracking);
        setCursor(Qt::CrossCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Theme::colors().base);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::colors().text);
        for (const auto& d : m_dots) p.drawEllipse(d.first, d.second, d.second);
        p.setPen(Theme::colors().dim);
        p.drawText(rect().adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignLeft,
                   m_dots.isEmpty() ? T("Draw here with the pen to test the pressure curve")
                                    : T("Pressure: %1").arg(QString::number(m_last, 'f', 2)));
    }
    void tabletEvent(QTabletEvent* e) override {
        e->accept();
        if (TabletInput::shouldIgnore(e)) return;
        if (e->type() == QEvent::TabletRelease) return;
        if (e->type() == QEvent::TabletMove && e->buttons() == Qt::NoButton) return;
        double raw = e->pressure();
        const double th = *m_threshold;
        if (th > 0) raw = std::max(0.0, (raw - th) / (1.0 - th));
        PressureCurve c;
        c.set(m_curve->points());
        const float mapped = c.map(float(raw));
        m_last = raw;
        m_curve->setLiveValue(raw);
        m_dots << qMakePair(e->position(), 0.5 + mapped * 6);
        if (m_dots.size() > 600) m_dots.removeFirst();
        update();
    }
    void mousePressEvent(QMouseEvent*) override {
        m_dots.clear();
        update();
    }

private:
    CurveEditor* m_curve;
    const double* m_threshold;
    QList<QPair<QPointF, double>> m_dots;
    double m_last = 0;
};

QLabel* hint(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setObjectName("sectionTitle");
    return l;
}

QCheckBox* check(const QString& text, bool* field, QWidget* parent) {
    auto* c = new QCheckBox(text, parent);
    c->setChecked(*field);
    QObject::connect(c, &QCheckBox::toggled, parent, [field](bool on) { *field = on; });
    return c;
}

// A checkbox per item; a checked box means "shown" (the list keeps what is hidden).
QWidget* showList(QStringList* hidden, const QStringList& keys, const QStringList& labels, QWidget* parent, int columns = 2) {
    auto* box = new QWidget(parent);
    auto* g = new QGridLayout(box);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(16);
    g->setVerticalSpacing(2);
    for (int i = 0; i < keys.size(); ++i) {
        auto* c = new QCheckBox(labels.value(i, keys[i]), box);
        c->setChecked(!hidden->contains(keys[i]));
        const QString key = keys[i];
        QObject::connect(c, &QCheckBox::toggled, box, [hidden, key](bool on) {
            hidden->removeAll(key);
            if (!on) hidden->append(key);
        });
        g->addWidget(c, i / columns, i % columns);
    }
    return box;
}

QSpinBox* spin(int min, int max, int* field, QWidget* parent, const QString& suffix = {}, int step = 1) {
    auto* s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setSuffix(suffix);
    s->setValue(*field);
    QObject::connect(s, &QSpinBox::valueChanged, parent, [field](int v) { *field = v; });
    return s;
}

ColorButton* colorButton(QColor* field, QWidget* parent) {
    auto* b = new ColorButton(*field, parent);
    QObject::connect(b, &ColorButton::colorChanged, parent, [field](const QColor& c) { *field = c; });
    return b;
}

// A page = scrollable column of group boxes.
struct PageBuilder {
    QWidget* page;
    QVBoxLayout* col;
    explicit PageBuilder(const QString& title) {
        page = new QWidget;
        auto* outer = new QVBoxLayout(page);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* head = new QLabel(title, page);
        QFont f = head->font();
        f.setPointSizeF(f.pointSizeF() * 1.35);
        f.setBold(true);
        head->setFont(f);
        head->setContentsMargins(12, 10, 12, 4);
        outer->addWidget(head);
        auto* scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* body = new QWidget;
        col = new QVBoxLayout(body);
        col->setContentsMargins(12, 4, 12, 12);
        col->setSpacing(10);
        scroll->setWidget(body);
        outer->addWidget(scroll, 1);
    }
    QFormLayout* group(const QString& title) {
        auto* g = new QGroupBox(title, page);
        auto* f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        f->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        col->addWidget(g);
        return f;
    }
    QWidget* done() {
        col->addStretch();
        return page;
    }
};
}  // namespace

PreferencesDialog::PreferencesDialog(QWidget* parent, Page start) : QDialog(parent), m_p(app()->prefs()), m_original(app()->prefs()) {
    setWindowTitle(T("Settings"));
    QSettings& s = app()->settings();
    m_newW = s.value("newdoc/w", 3000).toInt();
    m_newH = s.value("newdoc/h", 3000).toInt();
    m_newDpi = s.value("newdoc/dpi", 350).toDouble();

    auto* v = new QVBoxLayout(this);
    auto* row = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->setFixedWidth(210);
    m_list->setIconSize(QSize(20, 20));
    m_list->setSpacing(1);
    m_stack = new QStackedWidget(this);
    row->addWidget(m_list);
    row->addWidget(m_stack, 1);
    v->addLayout(row, 1);

    addPage("theme", T("Interface"), interfacePage());
    addPage("image", T("Canvas and view"), canvasPage());
    addPage("cursor", T("Cursor"), cursorPage());
    addPage("tablet", T("Pen and tablet"), penPage());
    addPage("brush", T("Brushes and stabilizer"), brushesPage());
    addPage("folder", T("Files and autosave"), filesPage());
    addPage("speed", T("Performance"), performancePage());
    addPage("keyboard", T("Keyboard shortcuts"), shortcutsPage());
    connect(m_list, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_list->setCurrentRow(int(start));

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    auto* defaults = bb->addButton(T("Reset all settings"), QDialogButtonBox::ResetRole);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &PreferencesDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &PreferencesDialog::apply);
    connect(defaults, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, T("Settings"), T("Return every setting to its default? Brushes and shortcuts are kept.")) !=
            QMessageBox::Yes)
            return;
        Preferences fresh;
        fresh.language = m_p.language;  // keep the language the person reads
        m_p = fresh;
        m_curve->setPoints(m_p.pressureCurve);
        apply();
        // rebuild the pages so every control shows the new values
        int row = m_list->currentRow();
        while (m_stack->count()) {
            QWidget* w = m_stack->widget(0);
            m_stack->removeWidget(w);
            w->deleteLater();
        }
        m_list->clear();
        addPage("theme", T("Interface"), interfacePage());
        addPage("image", T("Canvas and view"), canvasPage());
        addPage("cursor", T("Cursor"), cursorPage());
        addPage("tablet", T("Pen and tablet"), penPage());
        addPage("brush", T("Brushes and stabilizer"), brushesPage());
        addPage("folder", T("Files and autosave"), filesPage());
        addPage("speed", T("Performance"), performancePage());
        addPage("keyboard", T("Keyboard shortcuts"), shortcutsPage());
        m_list->setCurrentRow(row);
    });
    resize(900, 680);
}

void PreferencesDialog::addPage(const QString& icon, const QString& title, QWidget* page) {
    auto* it = new QListWidgetItem(Theme::icon(icon), title, m_list);
    it->setSizeHint(QSize(0, 34));
    m_stack->addWidget(page);
}

// ---------------------------------------------------------------------------- pages

QWidget* PreferencesDialog::interfacePage() {
    PageBuilder pb(T("Interface"));
    QWidget* w = pb.page;

    auto* f = pb.group(T("Language"));
    auto* lang = new QComboBox(w);
    lang->addItem(T("Automatic (as Windows)"), "auto");
    lang->addItem("English", "en");
    lang->addItem(QString::fromUtf8("Русский"), "ru");
    lang->setCurrentIndex(std::max(0, lang->findData(m_p.language)));
    connect(lang, &QComboBox::currentIndexChanged, this, [this, lang] { m_p.language = lang->currentData().toString(); });
    f->addRow(T("Language"), lang);
    f->addRow(hint(T("The program restarts to switch the language. Open pictures are asked to be saved first."), w));

    f = pb.group(T("Look"));
    auto* theme = new QComboBox(w);
    theme->addItem(Theme::icon("theme"), T("Dark"), "dark");
    theme->addItem(Theme::icon("theme"), T("Gray"), "gray");
    theme->addItem(Theme::icon("theme"), T("Light"), "light");
    theme->setCurrentIndex(std::max(0, theme->findData(m_p.theme)));
    connect(theme, &QComboBox::currentIndexChanged, this, [this, theme] {
        m_p.theme = theme->currentData().toString();
        Theme::apply(m_p.theme, m_p.uiFontSize);  // preview right away
    });
    f->addRow(T("Theme"), theme);
    auto* fs = new QSpinBox(w);
    fs->setRange(0, 24);
    fs->setSpecialValueText(T("System default"));
    fs->setSuffix(" pt");
    fs->setValue(m_p.uiFontSize);
    connect(fs, &QSpinBox::valueChanged, this, [this](int x) { m_p.uiFontSize = x; });
    f->addRow(T("Interface font size"), fs);
    auto* icons = new QComboBox(w);
    for (int sz : {16, 20, 24, 28, 32}) icons->addItem(QStringLiteral("%1 px").arg(sz), sz);
    icons->setCurrentIndex(std::max(0, icons->findData(m_p.iconSize)));
    connect(icons, &QComboBox::currentIndexChanged, this, [this, icons] { m_p.iconSize = icons->currentData().toInt(); });
    f->addRow(T("Tool icon size"), icons);

    // ---- what is shown: panels, tools, tabs, parts of the color panel
    if (auto* mw = qobject_cast<QMainWindow*>(parentWidget())) {
        f = pb.group(T("Panels"));
        f->addRow(hint(T("Shown right away. You can also fold a panel with the arrow on its title."), w));
        auto* box = new QWidget(w);
        auto* g = new QGridLayout(box);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(16);
        g->setVerticalSpacing(2);
        int i = 0;
        QList<QWidget*> items;
        for (QDockWidget* d : mw->findChildren<QDockWidget*>()) items << d;
        for (QToolBar* t : mw->findChildren<QToolBar*>()) items << t;
        for (QWidget* item : items) {
            auto* c = new QCheckBox(item->windowTitle().isEmpty() ? T("Toolbar") : item->windowTitle(), box);
            c->setChecked(!item->isHidden());
            m_panelsBefore.append(qMakePair(QPointer<QWidget>(item), !item->isHidden()));
            connect(c, &QCheckBox::toggled, item, [item](bool on) { item->setVisible(on); });
            g->addWidget(c, i / 2, i % 2);
            ++i;
        }
        f->addRow(box);
        auto* reset = new QPushButton(Theme::icon("reset"), T("Reset the interface"), w);
        reset->setToolTip(T("Panels back where they were at the start, unfolded; every tool, tab and part of the color panel shown again."));
        connect(reset, &QPushButton::clicked, this, [this, mw] {
            if (QMessageBox::question(this, T("Reset the interface"),
                                      T("Put all panels back where they were at the start, unfold them and show the hidden tools, tabs and "
                                        "parts of the color panel? Brushes, colors and shortcuts stay as they are.")) != QMessageBox::Yes)
                return;
            m_panelsBefore.clear();  // the reset decides what is shown
            reject();
            QPointer<QMainWindow> target(mw);
            QTimer::singleShot(0, [target] {
                if (target) QMetaObject::invokeMethod(target, "resetInterface");
            });
        });
        f->addRow(reset);
    }

    f = pb.group(T("Tools in the tool panel"));
    {
        QStringList keys, labels;
        for (const QString& id : ToolPanel::toolOrder())
            if (Tool* t = app()->tools()->tool(id)) keys << id, labels << T(t->name());
        f->addRow(showList(&m_p.hiddenTools, keys, labels, w, 3));
        f->addRow(hint(T("A hidden tool keeps its shortcut."), w));
    }
    f = pb.group(T("Brush tabs"));
    {
        QStringList keys = BrushSettings::groups();
        BrushLibrary* lib = app()->brushes();
        for (int i = 0; i < lib->count(); ++i)
            if (!keys.contains(lib->at(i).group)) keys << lib->at(i).group;
        QStringList labels;
        for (const QString& k : keys) labels << T(k);
        f->addRow(showList(&m_p.hiddenBrushTabs, keys, labels, w, 3));
    }
    f = pb.group(T("Materials tabs"));
    {
        const QStringList keys = {"Pictures", "Patterns", "Tones", "Gradient maps"};
        QStringList labels;
        for (const QString& k : keys) labels << T(k);
        f->addRow(showList(&m_p.hiddenMaterialTabs, keys, labels, w, 2));
    }
    f = pb.group(T("Color panel"));
    {
        QStringList labels;
        for (const QString& n : ColorPanel::partNames()) labels << T(n);
        f->addRow(showList(&m_p.hiddenColorParts, ColorPanel::partKeys(), labels, w, 2));
    }
    return pb.done();
}

QWidget* PreferencesDialog::canvasPage() {
    PageBuilder pb(T("Canvas and view"));
    QWidget* w = pb.page;

    auto* f = pb.group(T("Transparency"));
    f->addRow(check(T("Show transparency as a checkerboard"), &m_p.checkerboard, w));
    auto* cr = new QHBoxLayout;
    cr->addWidget(colorButton(&m_p.checker1, w));
    cr->addWidget(colorButton(&m_p.checker2, w));
    cr->addStretch();
    f->addRow(T("Checker colors"), cr);
    f->addRow(T("Checker size"), spin(2, 64, &m_p.checkerSize, w, " px"));
    f->addRow(T("Background without checkerboard"), colorButton(&m_p.paperColor, w));

    f = pb.group(T("Workspace"));
    auto* wr = new QHBoxLayout;
    wr->addWidget(new QLabel(T("Dark"), w));
    wr->addWidget(colorButton(&m_p.workspaceDark, w));
    wr->addSpacing(10);
    wr->addWidget(new QLabel(T("Gray"), w));
    wr->addWidget(colorButton(&m_p.workspaceGray, w));
    wr->addSpacing(10);
    wr->addWidget(new QLabel(T("Light"), w));
    wr->addWidget(colorButton(&m_p.workspaceLight, w));
    wr->addStretch();
    f->addRow(T("Around the canvas"), wr);
    f->addRow(check(T("Show sharp pixels when zoomed in (like SAI)"), &m_p.pixelatedZoomIn, w));
    f->addRow(check(T("Pixel grid at high zoom"), &m_p.showPixelGrid, w));

    f = pb.group(T("Zoom and rotation"));
    f->addRow(check(T("Mouse wheel zooms (otherwise scrolls)"), &m_p.wheelZooms, w));
    f->addRow(check(T("Reverse the mouse wheel"), &m_p.invertWheel, w));
    f->addRow(T("Zoom step"), spin(5, 100, &m_p.zoomStepPercent, w, " %"));
    f->addRow(T("Rotation step"), spin(1, 90, &m_p.rotateStepDeg, w, QString(QChar(0xB0))));

    f = pb.group(T("Grid and guides"));
    f->addRow(check(T("Show grid"), &m_p.showGrid, w));
    f->addRow(T("Grid size"), spin(2, 10000, &m_p.gridSize, w, " px"));
    f->addRow(T("Subdivisions"), spin(1, 20, &m_p.gridSubdivisions, w));
    f->addRow(T("Grid color"), colorButton(&m_p.gridColor, w));
    f->addRow(check(T("Rule of thirds"), &m_p.showThirds, w));
    f->addRow(check(T("Golden ratio"), &m_p.showGoldenRatio, w));
    f->addRow(check(T("Center lines"), &m_p.showCenterLines, w));
    f->addRow(hint(T("Grid and guides are only shown on screen. They are never painted or saved into the picture."), w));
    return pb.done();
}

QWidget* PreferencesDialog::cursorPage() {
    PageBuilder pb(T("Cursor"));
    QWidget* w = pb.page;
    auto* f = pb.group(T("Brush cursor"));
    f->addRow(check(T("Show brush outline"), &m_p.showBrushOutline, w));
    f->addRow(check(T("Crosshair in the brush outline"), &m_p.crosshair, w));
    f->addRow(check(T("Hide the outline while painting"), &m_p.hideCursorWhilePainting, w));
    f->addRow(hint(T("Very small brushes always show a crosshair so the cursor never disappears."), w));
    return pb.done();
}

QWidget* PreferencesDialog::penPage() {
    PageBuilder pb(T("Pen and tablet"));
    QWidget* w = pb.page;

    auto* f = pb.group(T("Tablet connection"));
    auto* api = new QComboBox(w);
    api->addItem(T("Wintab (like SAI, recommended)"), "wintab");
    api->addItem(T("Windows Ink"), "ink");
    api->setCurrentIndex(std::max(0, api->findData(m_p.tabletApi)));
    m_tabletState = new QLabel(TabletInput::describe(), w);
    connect(api, &QComboBox::currentIndexChanged, this, [this, api] {
        m_p.tabletApi = api->currentData().toString();
        TabletInput::apply(m_p.tabletApi);  // switches right away, no restart
        m_tabletState->setText(TabletInput::describe());
    });
    f->addRow(T("Pen input"), api);
    f->addRow(QString(), m_tabletState);
    f->addRow(hint(T("Wintab works with Windows Ink switched off in the tablet driver. If the pen has no pressure, try the other option: "
                     "the change works immediately."),
                   w));

    f = pb.group(T("Pressure"));
    auto* thr = new ValueBar(T("Ignore light touches below"), 0, 0.5, 2, w);
    thr->setDisplayScale(100);
    thr->setSuffix("%");
    thr->setValue(m_p.pressureThreshold);
    connect(thr, &ValueBar::valueChanged, this, [this](double x) { m_p.pressureThreshold = x; });
    f->addRow(thr);
    auto* lbl = new QLabel(T("Pressure curve (all brushes without their own curve)"), w);
    f->addRow(lbl);
    m_curve = new CurveEditor(w);
    m_curve->setPoints(m_p.pressureCurve);
    m_curve->setFixedHeight(240);
    f->addRow(m_curve);
    auto* presets = new QComboBox(w);
    presets->addItems({T("Curve presets..."), T("Linear"), T("Soft (light touch)"), T("Hard (strong touch)"), T("S-curve")});
    connect(presets, &QComboBox::activated, this, [this, presets](int i) {
        QVector<QPointF> pts;
        switch (i) {
        case 1: pts = {{0, 0}, {1, 1}}; break;
        case 2: pts = {{0, 0}, {0.25, 0.5}, {1, 1}}; break;
        case 3: pts = {{0, 0}, {0.6, 0.35}, {1, 1}}; break;
        case 4: pts = {{0, 0}, {0.3, 0.15}, {0.7, 0.85}, {1, 1}}; break;
        default: return;
        }
        m_curve->setPoints(pts);
        presets->setCurrentIndex(0);
    });
    f->addRow(presets);
    f->addRow(new PressureTest(m_curve, &m_p.pressureThreshold, w));

    f = pb.group(T("Buttons"));
    f->addRow(check(T("Right click / pen side button picks colors"), &m_p.rightClickEyedropper, w));
    f->addRow(check(T("The back of the pen erases"), &m_p.eraserEndUsesEraser, w));
    f->addRow(hint(T("The middle mouse button always moves the canvas."), w));

    f = pb.group(T("Mouse"));
    auto* mp = new ValueBar(T("Mouse pressure"), 0.05, 1, 2, w);
    mp->setDisplayScale(100);
    mp->setSuffix("%");
    mp->setValue(m_p.mousePressure);
    connect(mp, &ValueBar::valueChanged, this, [this](double x) { m_p.mousePressure = x; });
    f->addRow(mp);
    return pb.done();
}

QWidget* PreferencesDialog::brushesPage() {
    PageBuilder pb(T("Brushes and stabilizer"));
    QWidget* w = pb.page;
    auto* f = pb.group(T("Stabilizer"));
    auto* mode = new QComboBox(w);
    mode->addItem(T("Off"), 0);
    mode->addItem(T("Average"), 1);
    mode->addItem(T("Speed (S)"), 2);
    mode->addItem(T("Rope"), 3);
    mode->setCurrentIndex(std::max(0, mode->findData(m_p.stabilizerMode)));
    connect(mode, &QComboBox::currentIndexChanged, this, [this, mode] { m_p.stabilizerMode = mode->currentData().toInt(); });
    f->addRow(T("Mode"), mode);
    f->addRow(T("Strength"), spin(0, 30, &m_p.stabilizerLevel, w));
    f->addRow(T("Pressure smoothing"), spin(0, 20, &m_p.pressureSmoothing, w));
    f->addRow(hint(T("Stabilizer: Average = SAI 1-15 levels, Speed = SAI S-levels, Rope = pulled string"), w));

    f = pb.group(T("Brush size"));
    f->addRow(T("[ and ] change the size by"), spin(2, 100, &m_p.brushStepPercent, w, " %"));
    f->addRow(check(T("Ctrl + mouse wheel changes the brush size"), &m_p.ctrlWheelBrushSize, w));
    return pb.done();
}

QWidget* PreferencesDialog::filesPage() {
    PageBuilder pb(T("Files and autosave"));
    QWidget* w = pb.page;
    auto* f = pb.group(T("Saving"));
    auto* fmt = new QComboBox(w);
    fmt->addItem(T("GraphiX (.gxp) - keeps everything"), "gxp");
    fmt->addItem(T("Photoshop (.psd)"), "psd");
    fmt->setCurrentIndex(std::max(0, fmt->findData(m_p.defaultFormat)));
    connect(fmt, &QComboBox::currentIndexChanged, this, [this, fmt] { m_p.defaultFormat = fmt->currentData().toString(); });
    f->addRow(T("Save new pictures as"), fmt);
    f->addRow(T("JPEG / WebP quality"), spin(1, 100, &m_p.jpegQuality, w));
    f->addRow(T("Recent files in the menu"), spin(0, 40, &m_p.recentCount, w));

    f = pb.group(T("Recover work"));
    auto* as = spin(0, 120, &m_p.autosaveMinutes, w, T(" min"));
    as->setSpecialValueText(T("Off"));
    f->addRow(T("Recovery copy every"), as);
    f->addRow(hint(T("Copies are made in the background and never replace your files. After a crash the program offers to restore them."), w));

    f = pb.group(T("Start"));
    f->addRow(check(T("Open a new canvas when the program starts"), &m_p.newCanvasOnStart, w));
    auto* size = new QHBoxLayout;
    auto* nw = new QSpinBox(w);
    nw->setRange(16, 20000);
    nw->setValue(m_newW);
    nw->setSuffix(" px");
    auto* nh = new QSpinBox(w);
    nh->setRange(16, 20000);
    nh->setValue(m_newH);
    nh->setSuffix(" px");
    auto* dpi = new QDoubleSpinBox(w);
    dpi->setRange(1, 2400);
    dpi->setDecimals(0);
    dpi->setValue(m_newDpi);
    dpi->setSuffix(" dpi");
    connect(nw, &QSpinBox::valueChanged, this, [this](int x) { m_newW = x; });
    connect(nh, &QSpinBox::valueChanged, this, [this](int x) { m_newH = x; });
    connect(dpi, &QDoubleSpinBox::valueChanged, this, [this](double x) { m_newDpi = x; });
    size->addWidget(nw);
    size->addWidget(new QLabel("x", w));
    size->addWidget(nh);
    size->addWidget(dpi);
    f->addRow(T("New canvas size"), size);

    f = pb.group(T("Folders"));
    m_texturesPath = new QLabel(m_p.texturesDir.isEmpty() ? app()->defaultTexturesDir() : m_p.texturesDir, w);
    m_texturesPath->setWordWrap(true);
    m_texturesPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    f->addRow(T("Textures folder:"), m_texturesPath);
    auto* tr = new QHBoxLayout;
    auto* openT = new QPushButton(Theme::icon("folder"), T("Open"), w);
    auto* changeT = new QPushButton(T("Change..."), w);
    auto* defT = new QPushButton(T("Default"), w);
    tr->addWidget(openT);
    tr->addWidget(changeT);
    tr->addWidget(defT);
    tr->addStretch();
    f->addRow(QString(), tr);
    connect(openT, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_texturesPath->text())); });
    connect(changeT, &QPushButton::clicked, this, [this] {
        QString d = QFileDialog::getExistingDirectory(this, T("Textures folder:"), m_texturesPath->text());
        if (d.isEmpty()) return;
        m_p.texturesDir = d;
        m_texturesPath->setText(d);
    });
    connect(defT, &QPushButton::clicked, this, [this] {
        m_p.texturesDir.clear();
        m_texturesPath->setText(app()->defaultTexturesDir());
    });
    f->addRow(hint(T("Inside are three folders: paper, brush and shapes. Put PNG or JPG pictures there and use Help > Reload textures."), w));
    auto* openS = new QPushButton(Theme::icon("folder"), T("Open settings folder"), w);
    connect(openS, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(app()->dataDir())); });
    f->addRow(T("Settings and brushes"), openS);
    return pb.done();
}

QWidget* PreferencesDialog::performancePage() {
    PageBuilder pb(T("Performance"));
    QWidget* w = pb.page;
    auto* f = pb.group(T("Memory"));
    f->addRow(T("Undo steps"), spin(10, 2000, &m_p.undoLimit, w));
    f->addRow(T("Display cache"), spin(128, 16384, &m_p.projectionCacheMB, w, " MB", 128));
    f->addRow(hint(T("Undo steps share unchanged parts of the picture, so even many steps use little memory."), w));
    f = pb.group(T("Processor"));
    auto* th = spin(0, 256, &m_p.threads, w);
    th->setSpecialValueText(T("All cores (%1)").arg(QThread::idealThreadCount()));
    f->addRow(T("Threads for filters and drawing"), th);
    return pb.done();
}

QWidget* PreferencesDialog::shortcutsPage() {
    PageBuilder pb(T("Keyboard shortcuts"));
    auto* ed = new ShortcutsEditor(pb.page);
    pb.col->addWidget(ed, 1);
    auto* reset = new QPushButton(T("Reset all shortcuts"), pb.page);
    connect(reset, &QPushButton::clicked, ed, &ShortcutsEditor::resetAll);
    auto* r = new QHBoxLayout;
    r->addWidget(reset);
    r->addStretch();
    pb.col->addLayout(r);
    return pb.page;
}

// ---------------------------------------------------------------------------- apply

void PreferencesDialog::apply() {
    if (m_curve) m_p.pressureCurve = m_curve->points();
    const QString oldLanguage = app()->prefs().language;
    app()->prefs() = m_p;
    app()->setPressureCurve(m_p.pressureCurve);
    app()->setStabilizer(m_p.stabilizerMode, m_p.stabilizerLevel);
    app()->setPressureSmoothing(m_p.pressureSmoothing);
    QSettings& s = app()->settings();
    s.setValue("newdoc/w", m_newW);
    s.setValue("newdoc/h", m_newH);
    s.setValue("newdoc/dpi", m_newDpi);
    Theme::apply(m_p.theme, m_p.uiFontSize);
    app()->preferencesChanged();
    if (m_tabletState) m_tabletState->setText(TabletInput::describe());
    m_original = m_p;  // Cancel after Apply keeps what was applied
    auto effective = [](const QString& code) {
        if (code == "en" || code == "ru") return code;
        return QLocale::system().language() == QLocale::Russian ? QStringLiteral("ru") : QStringLiteral("en");
    };
    if (oldLanguage != m_p.language && effective(m_p.language) != language()) {
        if (QMessageBox::question(this, T("Language"), T("Restart GraphiX Paint now to switch the language?")) == QMessageBox::Yes)
            Q_EMIT restartRequested();
    }
}

void PreferencesDialog::accept() {
    apply();
    QDialog::accept();
}

void PreferencesDialog::reject() {
    // undo the live previews (theme, tablet, panels)
    Theme::apply(m_original.theme, m_original.uiFontSize);
    for (const auto& pv : m_panelsBefore)
        if (pv.first) pv.first->setVisible(pv.second);
    if (TabletInput::active() != app()->prefs().tabletApi) TabletInput::apply(app()->prefs().tabletApi);
    QDialog::reject();
}

}  // namespace gx
