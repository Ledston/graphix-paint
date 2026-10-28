#include "ui/PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QSpinBox>
#include <QTabWidget>
#include <QTabletEvent>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

namespace {
// Draw here to see the pen pressure on the curve.
class PressureTest : public QWidget {
public:
    PressureTest(CurveEditor* curve, QWidget* p) : QWidget(p), m_curve(curve) {
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
                   m_dots.isEmpty() ? T("Draw here with the pen to test the pressure curve") : QString::number(m_last, 'f', 2));
    }
    void tabletEvent(QTabletEvent* e) override {
        e->accept();
        if (e->type() == QEvent::TabletRelease) return;
        double raw = e->pressure();
        if (e->type() == QEvent::TabletMove && e->buttons() == Qt::NoButton) return;
        float mapped = 0;
        {
            PressureCurve c;
            c.set(m_curve->points());
            mapped = c.map(float(raw));
        }
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
    QList<QPair<QPointF, double>> m_dots;
    double m_last = 0;
};
}  // namespace

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent), m_p(app()->prefs()) {
    setWindowTitle(T("Preferences"));
    auto* v = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    v->addWidget(tabs);

    // ---- General
    auto* g = new QWidget;
    auto* gf = new QFormLayout(g);
    auto* theme = new QComboBox(g);
    theme->addItem(T("Dark"), "dark");
    theme->addItem(T("Gray"), "gray");
    theme->addItem(T("Light"), "light");
    theme->setCurrentIndex(std::max(0, theme->findData(m_p.theme)));
    connect(theme, &QComboBox::currentIndexChanged, this, [this, theme] {
        m_p.theme = theme->currentData().toString();
        Theme::apply(m_p.theme, m_p.uiFontSize);
    });
    gf->addRow(T("Theme"), theme);
    auto* lang = new QComboBox(g);
    lang->addItem(T("Automatic"), "auto");
    lang->addItem("English", "en");
    lang->addItem(QString::fromUtf8("Русский"), "ru");
    lang->setCurrentIndex(std::max(0, lang->findData(m_p.language)));
    connect(lang, &QComboBox::currentIndexChanged, this, [this, lang] { m_p.language = lang->currentData().toString(); });
    gf->addRow(T("Language (restart needed)"), lang);
    auto* fs = new QSpinBox(g);
    fs->setRange(0, 24);
    fs->setSpecialValueText(T("System default"));
    fs->setValue(m_p.uiFontSize);
    connect(fs, &QSpinBox::valueChanged, this, [this](int x) { m_p.uiFontSize = x; });
    gf->addRow(T("Interface font size"), fs);
    auto* undo = new QSpinBox(g);
    undo->setRange(10, 2000);
    undo->setValue(m_p.undoLimit);
    connect(undo, &QSpinBox::valueChanged, this, [this](int x) { m_p.undoLimit = x; });
    gf->addRow(T("Undo steps"), undo);
    auto* as = new QSpinBox(g);
    as->setRange(0, 120);
    as->setSuffix(T(" min"));
    as->setSpecialValueText(T("Off"));
    as->setValue(m_p.autosaveMinutes);
    connect(as, &QSpinBox::valueChanged, this, [this](int x) { m_p.autosaveMinutes = x; });
    gf->addRow(T("Recovery copy every"), as);
    auto* cache = new QSpinBox(g);
    cache->setRange(128, 16384);
    cache->setSingleStep(128);
    cache->setSuffix(" MB");
    cache->setValue(m_p.projectionCacheMB);
    connect(cache, &QSpinBox::valueChanged, this, [this](int x) { m_p.projectionCacheMB = x; });
    gf->addRow(T("Display cache"), cache);
    tabs->addTab(g, T("General"));

    // ---- Canvas
    auto* c = new QWidget;
    auto* cf = new QFormLayout(c);
    auto* checker = new QCheckBox(T("Show transparency as a checkerboard"), c);
    checker->setChecked(m_p.checkerboard);
    connect(checker, &QCheckBox::toggled, this, [this](bool x) { m_p.checkerboard = x; });
    cf->addRow(checker);
    auto* c1 = new ColorButton(m_p.checker1, c);
    auto* c2 = new ColorButton(m_p.checker2, c);
    connect(c1, &ColorButton::colorChanged, this, [this](const QColor& x) { m_p.checker1 = x; });
    connect(c2, &ColorButton::colorChanged, this, [this](const QColor& x) { m_p.checker2 = x; });
    auto* cr = new QHBoxLayout;
    cr->addWidget(c1);
    cr->addWidget(c2);
    cr->addStretch();
    cf->addRow(T("Checker colors"), cr);
    auto* cs = new QSpinBox(c);
    cs->setRange(2, 64);
    cs->setValue(m_p.checkerSize);
    connect(cs, &QSpinBox::valueChanged, this, [this](int x) { m_p.checkerSize = x; });
    cf->addRow(T("Checker size"), cs);
    auto* paper = new ColorButton(m_p.paperColor, c);
    connect(paper, &ColorButton::colorChanged, this, [this](const QColor& x) { m_p.paperColor = x; });
    cf->addRow(T("Background without checkerboard"), paper);
    auto* wd = new ColorButton(m_p.workspaceDark, c);
    auto* wg = new ColorButton(m_p.workspaceGray, c);
    auto* wl = new ColorButton(m_p.workspaceLight, c);
    connect(wd, &ColorButton::colorChanged, this, [this](const QColor& x) { m_p.workspaceDark = x; });
    connect(wg, &ColorButton::colorChanged, this, [this](const QColor& x) { m_p.workspaceGray = x; });
    connect(wl, &ColorButton::colorChanged, this, [this](const QColor& x) { m_p.workspaceLight = x; });
    auto* wr = new QHBoxLayout;
    wr->addWidget(wd);
    wr->addWidget(wg);
    wr->addWidget(wl);
    wr->addStretch();
    cf->addRow(T("Workspace (dark / gray / light)"), wr);
    auto* pix = new QCheckBox(T("Show sharp pixels when zoomed in (like SAI)"), c);
    pix->setChecked(m_p.pixelatedZoomIn);
    connect(pix, &QCheckBox::toggled, this, [this](bool x) { m_p.pixelatedZoomIn = x; });
    cf->addRow(pix);
    auto* grid = new QCheckBox(T("Pixel grid at high zoom"), c);
    grid->setChecked(m_p.showPixelGrid);
    connect(grid, &QCheckBox::toggled, this, [this](bool x) { m_p.showPixelGrid = x; });
    cf->addRow(grid);
    auto* outline = new QCheckBox(T("Show brush outline"), c);
    outline->setChecked(m_p.showBrushOutline);
    connect(outline, &QCheckBox::toggled, this, [this](bool x) { m_p.showBrushOutline = x; });
    cf->addRow(outline);
    auto* cross = new QCheckBox(T("Crosshair in the brush outline"), c);
    cross->setChecked(m_p.crosshair);
    connect(cross, &QCheckBox::toggled, this, [this](bool x) { m_p.crosshair = x; });
    cf->addRow(cross);
    tabs->addTab(c, T("Canvas"));

    // ---- Pen
    auto* p = new QWidget;
    auto* pv = new QVBoxLayout(p);
    auto* pf = new QFormLayout;
    auto* api = new QComboBox(p);
    api->addItem(T("Windows Ink (recommended)"), "ink");
    api->addItem(T("Wintab (older drivers)"), "wintab");
    api->setCurrentIndex(std::max(0, api->findData(m_p.tabletApi)));
    connect(api, &QComboBox::currentIndexChanged, this, [this, api] { m_p.tabletApi = api->currentData().toString(); });
    pf->addRow(T("Tablet driver (restart needed)"), api);
    auto* mp = new ValueBar(T("Mouse pressure"), 0.05, 1, 2, p);
    mp->setDisplayScale(100);
    mp->setSuffix("%");
    mp->setValue(m_p.mousePressure);
    connect(mp, &ValueBar::valueChanged, this, [this](double x) { m_p.mousePressure = x; });
    pf->addRow(mp);
    auto* rc = new QCheckBox(T("Right click / pen side button picks colors"), p);
    rc->setChecked(m_p.rightClickEyedropper);
    connect(rc, &QCheckBox::toggled, this, [this](bool x) { m_p.rightClickEyedropper = x; });
    pf->addRow(rc);
    auto* wz = new QCheckBox(T("Mouse wheel zooms (otherwise scrolls)"), p);
    wz->setChecked(m_p.wheelZooms);
    connect(wz, &QCheckBox::toggled, this, [this](bool x) { m_p.wheelZooms = x; });
    pf->addRow(wz);
    pv->addLayout(pf);
    auto* lbl = new QLabel(T("Pressure curve (all brushes without their own curve)"), p);
    lbl->setObjectName("sectionTitle");
    pv->addWidget(lbl);
    m_curve = new CurveEditor(p);
    m_curve->setPoints(m_p.pressureCurve);
    pv->addWidget(m_curve, 1);
    pv->addWidget(new PressureTest(m_curve, p));
    auto* presets = new QComboBox(p);
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
    pv->addWidget(presets);
    tabs->addTab(p, T("Pen"));

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, [this] {
        Theme::apply(app()->prefs().theme, app()->prefs().uiFontSize);
        reject();
    });
    resize(520, 620);
}

void PreferencesDialog::accept() {
    Preferences old = app()->prefs();
    m_p.pressureCurve = m_curve->points();
    app()->prefs() = m_p;
    app()->setPressureCurve(m_p.pressureCurve);
    Theme::apply(m_p.theme, m_p.uiFontSize);
    app()->preferencesChanged();
    if (old.language != m_p.language || old.tabletApi != m_p.tabletApi)
        QMessageBox::information(this, T("Preferences"), T("Restart GraphiX Paint to use the new language or tablet driver."));
    QDialog::accept();
}

}  // namespace gx
