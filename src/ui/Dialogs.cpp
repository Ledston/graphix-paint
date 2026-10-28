#include "ui/Dialogs.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

// ============================================================================ helpers

int Dialogs::askInt(QWidget* parent, const QString& title, const QString& label, int value, int min, int max) {
    bool ok = false;
    int v = QInputDialog::getInt(parent, title, label, value, min, max, 1, &ok);
    return ok ? v : -1;
}

bool Dialogs::editGradient(QWidget* parent, Gradient& g) {
    QDialog d(parent);
    d.setWindowTitle(T("Edit gradient"));
    auto* v = new QVBoxLayout(&d);
    auto* ed = new GradientEditor(&d);
    ed->setGradient(g);
    v->addWidget(ed);
    auto* row = new QHBoxLayout;
    auto* color = new ColorButton(Qt::black, &d);
    auto* opacity = new ValueBar(T("Opacity"), 0, 1, 2, &d);
    opacity->setDisplayScale(100);
    opacity->setSuffix("%");
    auto* del = new QPushButton(Theme::icon("trash"), T("Delete stop"), &d);
    auto* fg = new QPushButton(T("Main color"), &d);
    row->addWidget(new QLabel(T("Stop color"), &d));
    row->addWidget(color);
    row->addWidget(fg);
    row->addWidget(opacity, 1);
    row->addWidget(del);
    v->addLayout(row);
    auto* hint = new QLabel(T("Click under the bar to add a stop, drag stops to move them, double-click a stop to pick its color."), &d);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    v->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto sync = [=](int i) {
        Gradient gg = ed->gradient();
        if (i < 0 || i >= gg.stops.size()) return;
        color->setColor(gg.stops[i].color);
        opacity->setValue(gg.stops[i].opacity);
    };
    QObject::connect(ed, &GradientEditor::selectionChanged, &d, sync);
    QObject::connect(color, &ColorButton::colorChanged, &d, [ed](const QColor& c) { ed->setStopColor(c); });
    QObject::connect(fg, &QPushButton::clicked, &d, [ed, color] {
        ed->setStopColor(app()->foreground());
        color->setColor(app()->foreground());
    });
    QObject::connect(opacity, &ValueBar::valueChanged, &d, [ed](double o) { ed->setStopOpacity(o); });
    QObject::connect(del, &QPushButton::clicked, &d, [ed] { ed->removeSelected(); });
    sync(0);
    d.resize(460, 180);
    if (d.exec() != QDialog::Accepted) return false;
    g = ed->gradient();
    g.name = "Custom";
    return true;
}

// ============================================================================ GradientEditor

GradientEditor::GradientEditor(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(64);
    m_g = Gradient::twoColor(Qt::black, Qt::white);
}

void GradientEditor::setGradient(const Gradient& g) {
    m_g = g;
    m_g.normalize();
    m_sel = 0;
    update();
    Q_EMIT selectionChanged(0);
}

QRectF GradientEditor::bar() const { return QRectF(10, 6, width() - 20, height() - 30); }

void GradientEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF b = bar();
    p.fillRect(b, checkerBrush(5));
    QLinearGradient lg(b.topLeft(), b.topRight());
    for (const auto& s : m_g.stops) {
        QColor c = s.color;
        c.setAlphaF(float(s.opacity));
        lg.setColorAt(s.pos, c);
    }
    p.fillRect(b, lg);
    p.setPen(Theme::colors().border);
    p.drawRect(b);
    for (int i = 0; i < m_g.stops.size(); ++i) {
        double x = b.left() + b.width() * m_g.stops[i].pos;
        QPolygonF tri({QPointF(x, b.bottom() + 2), QPointF(x - 7, b.bottom() + 14), QPointF(x + 7, b.bottom() + 14)});
        p.setPen(QPen(i == m_sel ? Theme::colors().accent : Theme::colors().text, i == m_sel ? 2 : 1));
        p.setBrush(m_g.stops[i].color);
        p.drawPolygon(tri);
    }
}

int GradientEditor::stopAt(const QPointF& pt) const {
    QRectF b = bar();
    for (int i = 0; i < m_g.stops.size(); ++i) {
        double x = b.left() + b.width() * m_g.stops[i].pos;
        if (std::fabs(pt.x() - x) < 8 && pt.y() > b.bottom() - 2) return i;
    }
    return -1;
}

void GradientEditor::mousePressEvent(QMouseEvent* e) {
    int i = stopAt(e->position());
    QRectF b = bar();
    if (i < 0) {
        double t = std::clamp((e->position().x() - b.left()) / b.width(), 0.0, 1.0);
        float c[4];
        m_g.eval(t, c);
        GradientStop s{t, QColor::fromRgbF(c[0], c[1], c[2]), c[3]};
        m_g.stops.push_back(s);
        m_g.normalize();
        for (int k = 0; k < m_g.stops.size(); ++k)
            if (std::fabs(m_g.stops[k].pos - t) < 1e-9) i = k;
        Q_EMIT changed();
    }
    m_sel = std::max(0, i);
    m_drag = true;
    Q_EMIT selectionChanged(m_sel);
    update();
}

void GradientEditor::mouseMoveEvent(QMouseEvent* e) {
    if (!m_drag || m_sel < 0 || m_sel >= m_g.stops.size()) return;
    QRectF b = bar();
    double t = std::clamp((e->position().x() - b.left()) / b.width(), 0.0, 1.0);
    m_g.stops[m_sel].pos = t;
    GradientStop keep = m_g.stops[m_sel];
    std::stable_sort(m_g.stops.begin(), m_g.stops.end(), [](const GradientStop& a, const GradientStop& c) { return a.pos < c.pos; });
    for (int k = 0; k < m_g.stops.size(); ++k)
        if (m_g.stops[k].pos == keep.pos && m_g.stops[k].color == keep.color) m_sel = k;
    update();
    Q_EMIT changed();
}

void GradientEditor::mouseReleaseEvent(QMouseEvent*) { m_drag = false; }

void GradientEditor::mouseDoubleClickEvent(QMouseEvent* e) {
    int i = stopAt(e->position());
    if (i < 0) return;
    QColor c = QColorDialog::getColor(m_g.stops[i].color, this, T("Stop color"));
    if (c.isValid()) {
        m_sel = i;
        setStopColor(c);
        Q_EMIT selectionChanged(i);
    }
}

void GradientEditor::setStopColor(const QColor& c) {
    if (m_sel < 0 || m_sel >= m_g.stops.size()) return;
    m_g.stops[m_sel].color = c;
    update();
    Q_EMIT changed();
}

void GradientEditor::setStopOpacity(double o) {
    if (m_sel < 0 || m_sel >= m_g.stops.size()) return;
    m_g.stops[m_sel].opacity = o;
    update();
    Q_EMIT changed();
}

void GradientEditor::removeSelected() {
    if (m_g.stops.size() <= 2 || m_sel < 0 || m_sel >= m_g.stops.size()) return;
    m_g.stops.removeAt(m_sel);
    m_sel = 0;
    update();
    Q_EMIT changed();
    Q_EMIT selectionChanged(0);
}

// ============================================================================ new document

namespace {
struct Preset {
    const char* name;
    int w, h;
    double dpi;
};
const Preset kPresets[] = {
    {"Custom", 0, 0, 0},
    {"Square 2000 x 2000", 2000, 2000, 350},
    {"Square 4000 x 4000", 4000, 4000, 350},
    {"A4 portrait, 300 dpi", 2480, 3508, 300},
    {"A4 portrait, 350 dpi", 2894, 4093, 350},
    {"A4 landscape, 300 dpi", 3508, 2480, 300},
    {"A5 portrait, 350 dpi", 2039, 2894, 350},
    {"B5 comic page, 600 dpi", 4299, 6071, 600},
    {"Full HD 1920 x 1080", 1920, 1080, 72},
    {"QHD 2560 x 1440", 2560, 1440, 72},
    {"4K UHD 3840 x 2160", 3840, 2160, 72},
    {"Phone wallpaper 1080 x 2400", 1080, 2400, 72},
    {"Twitter / X post 1600 x 900", 1600, 900, 72},
    {"Instagram 1080 x 1350", 1080, 1350, 72},
    {"Game texture 2048 x 2048", 2048, 2048, 72},
    {"Sprite sheet 1024 x 1024", 1024, 1024, 72},
};
double unitFactor(int unit, double dpi) {  // pixels per unit
    switch (unit) {
    case 1: return dpi / 2.54;   // cm
    case 2: return dpi / 25.4;   // mm
    case 3: return dpi;          // inch
    default: return 1.0;
    }
}
}  // namespace

NewDocumentDialog::NewDocumentDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(T("New canvas"));
    auto* f = new QFormLayout(this);
    m_preset = new QComboBox(this);
    for (const Preset& p : kPresets) m_preset->addItem(T(p.name));
    f->addRow(T("Preset"), m_preset);
    m_unit = new QComboBox(this);
    m_unit->addItems({T("pixels"), T("cm"), T("mm"), T("inches")});
    m_w = new QDoubleSpinBox(this);
    m_h = new QDoubleSpinBox(this);
    for (auto* s : {m_w, m_h}) {
        s->setRange(1, 100000);
        s->setDecimals(0);
    }
    auto* swap = new QToolButton(this);
    swap->setIcon(Theme::icon("swap"));
    swap->setToolTip(T("Swap width and height"));
    auto* wr = new QHBoxLayout;
    wr->addWidget(m_w, 1);
    wr->addWidget(new QLabel("x", this));
    wr->addWidget(m_h, 1);
    wr->addWidget(m_unit);
    wr->addWidget(swap);
    f->addRow(T("Size"), wr);
    m_dpi = new QDoubleSpinBox(this);
    m_dpi->setRange(1, 4800);
    m_dpi->setDecimals(0);
    f->addRow(T("Resolution (dpi)"), m_dpi);
    m_bg = new QComboBox(this);
    m_bg->addItems({T("White paper"), T("Transparent"), T("Custom color...")});
    f->addRow(T("Background"), m_bg);
    m_info = new QLabel(this);
    m_info->setObjectName("sectionTitle");
    f->addRow(m_info);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QSettings& s = app()->settings();
    m_dpi->setValue(s.value("newdoc/dpi", 350).toDouble());
    setPixels(s.value("newdoc/w", 3000).toInt(), s.value("newdoc/h", 3000).toInt());
    m_bg->setCurrentIndex(s.value("newdoc/bg", 0).toInt() == 2 ? 0 : s.value("newdoc/bg", 0).toInt());

    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i <= 0) return;
        m_dpi->setValue(kPresets[i].dpi);
        m_unit->setCurrentIndex(0);
        m_lastUnit = 0;
        setPixels(kPresets[i].w, kPresets[i].h);
    });
    connect(m_unit, &QComboBox::currentIndexChanged, this, [this](int u) {
        QSize px = canvasSize();
        m_lastUnit = u;
        setPixels(px.width(), px.height());
    });
    connect(swap, &QToolButton::clicked, this, [this] {
        double w = m_w->value();
        m_w->setValue(m_h->value());
        m_h->setValue(w);
    });
    connect(m_bg, &QComboBox::activated, this, [this](int i) {
        if (i == 2) {
            QColor c = QColorDialog::getColor(m_custom, this, T("Background color"));
            if (c.isValid()) m_custom = c;
        }
    });
    for (auto* sb : {m_w, m_h, m_dpi}) connect(sb, &QDoubleSpinBox::valueChanged, this, [this] { updateInfo(); });
    updateInfo();
}

void NewDocumentDialog::setPixels(int w, int h) {
    double f = unitFactor(m_unit->currentIndex(), m_dpi->value());
    int dec = m_unit->currentIndex() == 0 ? 0 : 2;
    m_w->setDecimals(dec);
    m_h->setDecimals(dec);
    m_w->setRange(dec ? 0.01 : 1, 100000.0 / f);
    m_h->setRange(dec ? 0.01 : 1, 100000.0 / f);
    m_w->setValue(w / f);
    m_h->setValue(h / f);
    updateInfo();
}

QSize NewDocumentDialog::canvasSize() const {
    double f = unitFactor(m_lastUnit, m_dpi->value());
    return QSize(std::clamp(int(std::lround(m_w->value() * f)), 1, 100000), std::clamp(int(std::lround(m_h->value() * f)), 1, 100000));
}

double NewDocumentDialog::dpi() const { return m_dpi->value(); }

QColor NewDocumentDialog::background() const {
    switch (m_bg->currentIndex()) {
    case 1: return Qt::transparent;
    case 2: return m_custom;
    default: return Qt::white;
    }
}

void NewDocumentDialog::updateInfo() {
    QSize s = canvasSize();
    double mp = double(s.width()) * s.height() / 1e6;
    double cm = 2.54 / m_dpi->value();
    m_info->setText(T("%1 x %2 px · %3 MP · %4 x %5 cm")
                        .arg(s.width())
                        .arg(s.height())
                        .arg(mp, 0, 'f', 1)
                        .arg(s.width() * cm, 0, 'f', 1)
                        .arg(s.height() * cm, 0, 'f', 1));
}

void NewDocumentDialog::accept() {
    QSize s = canvasSize();
    double mp = double(s.width()) * s.height() / 1e6;
    if (mp > 400 && QMessageBox::question(this, T("Very large canvas"),
                                          T("This canvas has %1 megapixels. Empty areas cost no memory, but painting all of it needs a lot of RAM. Continue?")
                                              .arg(int(mp))) != QMessageBox::Yes)
        return;
    QSettings& st = app()->settings();
    st.setValue("newdoc/w", s.width());
    st.setValue("newdoc/h", s.height());
    st.setValue("newdoc/dpi", dpi());
    st.setValue("newdoc/bg", m_bg->currentIndex());
    QDialog::accept();
}

// ============================================================================ canvas size

CanvasSizeDialog::CanvasSizeDialog(const QSize& cur, QWidget* parent) : QDialog(parent), m_cur(cur) {
    setWindowTitle(T("Canvas size"));
    auto* f = new QFormLayout(this);
    f->addRow(new QLabel(T("Current: %1 x %2 px").arg(cur.width()).arg(cur.height()), this));
    m_w = new QSpinBox(this);
    m_h = new QSpinBox(this);
    for (auto* s : {m_w, m_h}) {
        s->setRange(1, 100000);
        s->setSuffix(" px");
    }
    m_w->setValue(cur.width());
    m_h->setValue(cur.height());
    f->addRow(T("Width"), m_w);
    f->addRow(T("Height"), m_h);
    auto* grid = new QGridLayout;
    m_anchor = new QButtonGroup(this);
    for (int i = 0; i < 9; ++i) {
        auto* b = new QToolButton(this);
        b->setCheckable(true);
        b->setFixedSize(26, 26);
        m_anchor->addButton(b, i);
        grid->addWidget(b, i / 3, i % 3);
    }
    m_anchor->button(4)->setChecked(true);
    auto* gw = new QWidget(this);
    gw->setLayout(grid);
    f->addRow(T("Anchor"), gw);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QSize CanvasSizeDialog::newSize() const { return QSize(m_w->value(), m_h->value()); }

QPoint CanvasSizeDialog::offset() const {
    int a = m_anchor->checkedId();
    int ax = a % 3, ay = a / 3;
    int dx = newSize().width() - m_cur.width(), dy = newSize().height() - m_cur.height();
    return QPoint(ax == 0 ? 0 : ax == 1 ? dx / 2 : dx, ay == 0 ? 0 : ay == 1 ? dy / 2 : dy);
}

// ============================================================================ image size

ImageSizeDialog::ImageSizeDialog(const QSize& cur, double dpi, QWidget* parent) : QDialog(parent), m_cur(cur) {
    setWindowTitle(T("Image size"));
    auto* f = new QFormLayout(this);
    m_w = new QSpinBox(this);
    m_h = new QSpinBox(this);
    for (auto* s : {m_w, m_h}) {
        s->setRange(1, 100000);
        s->setSuffix(" px");
    }
    m_w->setValue(cur.width());
    m_h->setValue(cur.height());
    m_pct = new QDoubleSpinBox(this);
    m_pct->setRange(0.1, 10000);
    m_pct->setSuffix(" %");
    m_pct->setValue(100);
    m_keep = new QCheckBox(T("Keep proportions"), this);
    m_keep->setChecked(true);
    m_dpi = new QDoubleSpinBox(this);
    m_dpi->setRange(1, 4800);
    m_dpi->setDecimals(0);
    m_dpi->setValue(dpi);
    m_quality = new QComboBox(this);
    m_quality->addItems({T("Smooth (best for photos and paintings)"), T("Nearest neighbour (pixel art)")});
    f->addRow(T("Width"), m_w);
    f->addRow(T("Height"), m_h);
    f->addRow(T("Scale"), m_pct);
    f->addRow(m_keep);
    f->addRow(T("Resolution (dpi)"), m_dpi);
    f->addRow(T("Resampling"), m_quality);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_w, &QSpinBox::valueChanged, this, [this](int w) {
        if (m_updating) return;
        m_updating = true;
        if (m_keep->isChecked()) m_h->setValue(std::max(1, int(std::lround(double(w) * m_cur.height() / m_cur.width()))));
        m_pct->setValue(100.0 * w / m_cur.width());
        m_updating = false;
    });
    connect(m_h, &QSpinBox::valueChanged, this, [this](int h) {
        if (m_updating) return;
        m_updating = true;
        if (m_keep->isChecked()) m_w->setValue(std::max(1, int(std::lround(double(h) * m_cur.width() / m_cur.height()))));
        m_pct->setValue(100.0 * h / m_cur.height());
        m_updating = false;
    });
    connect(m_pct, &QDoubleSpinBox::valueChanged, this, [this](double p) {
        if (m_updating) return;
        m_updating = true;
        m_w->setValue(std::max(1, int(std::lround(m_cur.width() * p / 100))));
        m_h->setValue(std::max(1, int(std::lround(m_cur.height() * p / 100))));
        m_updating = false;
    });
}

QSize ImageSizeDialog::newSize() const { return QSize(m_w->value(), m_h->value()); }
double ImageSizeDialog::dpi() const { return m_dpi->value(); }
bool ImageSizeDialog::smooth() const { return m_quality->currentIndex() == 0; }

// ============================================================================ recovery

RecoveryDialog::RecoveryDialog(const QList<Recovery::Item>& items, QWidget* parent) : QDialog(parent), m_items(items) {
    setWindowTitle(T("Recover work"));
    auto* v = new QVBoxLayout(this);
    auto* l = new QLabel(T("GraphiX did not close normally last time. These pictures can be restored:"), this);
    l->setWordWrap(true);
    v->addWidget(l);
    m_list = new QListWidget(this);
    for (const auto& it : items) {
        auto* li = new QListWidgetItem(QStringLiteral("%1   (%2)").arg(it.title, it.time.toString("yyyy-MM-dd HH:mm")), m_list);
        li->setFlags(li->flags() | Qt::ItemIsUserCheckable);
        li->setCheckState(Qt::Checked);
        li->setToolTip(it.originalPath);
    }
    v->addWidget(m_list);
    auto* bb = new QDialogButtonBox(this);
    auto* restore = bb->addButton(T("Restore"), QDialogButtonBox::AcceptRole);
    auto* discard = bb->addButton(T("Discard"), QDialogButtonBox::DestructiveRole);
    auto* later = bb->addButton(T("Decide later"), QDialogButtonBox::RejectRole);
    v->addWidget(bb);
    connect(restore, &QPushButton::clicked, this, &QDialog::accept);
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    connect(discard, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, T("Discard"), T("Delete the recovery copies? This cannot be undone.")) == QMessageBox::Yes) {
            m_discard = true;
            reject();
        }
    });
    resize(480, 300);
}

QList<Recovery::Item> RecoveryDialog::selected() const {
    QList<Recovery::Item> out;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->checkState() == Qt::Checked) out << m_items[i];
    return out;
}

}  // namespace gx
