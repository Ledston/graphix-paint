#include "ui/ColorPanel.h"

#include <QColorDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

// ============================================================================ ColorWheel

ColorWheel::ColorWheel(QWidget* parent) : QWidget(parent) {
    setMinimumSize(120, 120);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    setCursor(Qt::CrossCursor);
}

double ColorWheel::outerR() const { return std::min(width(), height()) / 2.0 - 2; }
double ColorWheel::innerR() const { return outerR() * 0.82; }

QRectF ColorWheel::square() const {
    double s = innerR() * 1.30;
    return QRectF(width() / 2.0 - s / 2, height() / 2.0 - s / 2, s, s);
}

void ColorWheel::resizeEvent(QResizeEvent*) {
    m_ring = QImage();
    m_sqHue = -1;
}

void ColorWheel::setColor(const QColor& c) {
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    if (h >= 0) m_h = h;  // keep hue for grays
    if (v > 0.001f) m_s = s;
    m_v = v;
    update();
}

void ColorWheel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal dpr = devicePixelRatioF();
    const double R = outerR(), r = innerR();
    const QPointF c(width() / 2.0, height() / 2.0);
    if (m_ring.isNull()) {
        int S = int(std::ceil(R * 2 * dpr)) + 2;
        m_ring = QImage(S, S, QImage::Format_ARGB32_Premultiplied);
        m_ring.fill(Qt::transparent);
        QPainter rp(&m_ring);
        rp.setRenderHint(QPainter::Antialiasing);
        rp.scale(dpr, dpr);
        QPointF cc(S / dpr / 2.0, S / dpr / 2.0);
        QConicalGradient g(cc, 0);
        for (int i = 0; i <= 12; ++i) g.setColorAt(i / 12.0, QColor::fromHsvF(std::fmod(i / 12.0, 1.0) * 0.9999, 1, 1));
        QPainterPath ring;
        ring.addEllipse(cc, R, R);
        ring.addEllipse(cc, r, r);
        rp.fillPath(ring, g);
        m_ring.setDevicePixelRatio(dpr);
    }
    p.drawImage(QPointF(c.x() - m_ring.width() / dpr / 2.0, c.y() - m_ring.height() / dpr / 2.0), m_ring);
    QRectF sq = square();
    if (m_sqHue != m_h || m_sq.isNull()) {
        int w = std::max(1, int(sq.width() * dpr)), h = std::max(1, int(sq.height() * dpr));
        m_sq = QImage(w, h, QImage::Format_RGB32);
        for (int y = 0; y < h; ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(m_sq.scanLine(y));
            float v = 1.f - float(y) / std::max(1, h - 1);
            for (int x = 0; x < w; ++x) row[x] = QColor::fromHsvF(m_h, float(x) / std::max(1, w - 1), v).rgb();
        }
        m_sq.setDevicePixelRatio(dpr);
        m_sqHue = m_h;
    }
    p.drawImage(sq.topLeft(), m_sq);
    // hue marker
    double a = m_h * 2 * M_PI;
    QPointF hm = c + QPointF(std::cos(a), -std::sin(a)) * ((R + r) / 2);
    p.setPen(QPen(Qt::black, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(hm, (R - r) / 2 - 1, (R - r) / 2 - 1);
    p.setPen(QPen(Qt::white, 1));
    p.drawEllipse(hm, (R - r) / 2 - 1, (R - r) / 2 - 1);
    // color harmony marks
    for (float hh : m_harmony) {
        const double ha = hh * 2 * M_PI;
        QPointF pm = c + QPointF(std::cos(ha), -std::sin(ha)) * ((R + r) / 2);
        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(QColor::fromHsvF(hh, 1, 1));
        p.drawEllipse(pm, (R - r) / 2 - 3, (R - r) / 2 - 3);
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(pm, (R - r) / 2 - 3, (R - r) / 2 - 3);
    }
    QPointF sm(sq.left() + m_s * sq.width(), sq.top() + (1 - m_v) * sq.height());
    p.setPen(QPen(m_v > 0.5 && m_s < 0.5 ? Qt::black : Qt::white, 1.6));
    p.drawEllipse(sm, 5, 5);
}

void ColorWheel::pick(const QPointF& pt) {
    QPointF c(width() / 2.0, height() / 2.0);
    if (m_mode == 1) {
        double a = std::atan2(-(pt.y() - c.y()), pt.x() - c.x());
        if (a < 0) a += 2 * M_PI;
        m_h = float(a / (2 * M_PI));
    } else if (m_mode == 2) {
        QRectF sq = square();
        m_s = float(std::clamp((pt.x() - sq.left()) / sq.width(), 0.0, 1.0));
        m_v = float(std::clamp(1.0 - (pt.y() - sq.top()) / sq.height(), 0.0, 1.0));
    }
    update();
    Q_EMIT colorChanged(color());
}

void ColorWheel::mousePressEvent(QMouseEvent* e) {
    QPointF c(width() / 2.0, height() / 2.0);
    double d = QLineF(c, e->position()).length();
    if (square().contains(e->position())) m_mode = 2;
    else if (d >= innerR() - 4 && d <= outerR() + 4) m_mode = 1;
    else { m_mode = 0; return; }
    pick(e->position());
}

void ColorWheel::mouseMoveEvent(QMouseEvent* e) {
    if (m_mode) pick(e->position());
}

void ColorWheel::mouseReleaseEvent(QMouseEvent*) {
    if (m_mode) Q_EMIT colorPicked(color());
    m_mode = 0;
}

// ============================================================================ GradientSlider

GradientSlider::GradientSlider(const QString& label, int max, QWidget* parent) : QWidget(parent), m_label(label), m_max(max) {
    setFixedHeight(18);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QRectF GradientSlider::track() const { return QRectF(18, 3, width() - 18 - 34, height() - 6); }

void GradientSlider::setValue(int v) {
    v = std::clamp(v, 0, m_max);
    if (v == m_value) return;
    m_value = v;
    update();
}

void GradientSlider::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const ThemeColors& c = Theme::colors();
    p.setPen(c.dim);
    p.drawText(QRectF(0, 0, 16, height()), Qt::AlignCenter, m_label);
    QRectF t = track();
    if (m_grad) {
        QLinearGradient g(t.topLeft(), t.topRight());
        for (int i = 0; i <= 12; ++i) g.setColorAt(i / 12.0, m_grad(i / 12.0));
        p.fillRect(t, g);
    }
    p.setPen(c.border);
    p.drawRect(t);
    double x = t.left() + t.width() * m_value / m_max;
    p.setRenderHint(QPainter::Antialiasing);
    QPolygonF tri({QPointF(x, t.bottom() - 4), QPointF(x - 4, t.bottom() + 3), QPointF(x + 4, t.bottom() + 3)});
    p.setBrush(c.text);
    p.setPen(QPen(c.base, 1));
    p.drawPolygon(tri);
    p.setPen(c.text);
    p.drawText(QRectF(width() - 32, 0, 32, height()), Qt::AlignVCenter | Qt::AlignRight, QString::number(m_value));
}

void GradientSlider::setFromX(double x) {
    QRectF t = track();
    int v = int(std::round((x - t.left()) / t.width() * m_max));
    v = std::clamp(v, 0, m_max);
    if (v == m_value) return;
    m_value = v;
    update();
    Q_EMIT valueChanged(v);
}

void GradientSlider::mousePressEvent(QMouseEvent* e) {
    m_drag = true;
    setFromX(e->position().x());
}
void GradientSlider::mouseMoveEvent(QMouseEvent* e) {
    if (m_drag) setFromX(e->position().x());
}
void GradientSlider::mouseReleaseEvent(QMouseEvent*) {
    if (m_drag) Q_EMIT released();
    m_drag = false;
}
void GradientSlider::wheelEvent(QWheelEvent* e) {
    int d = e->angleDelta().y() > 0 ? 1 : -1;
    int v = std::clamp(m_value + d, 0, m_max);
    if (v != m_value) {
        m_value = v;
        update();
        Q_EMIT valueChanged(v);
        Q_EMIT released();
    }
}

// ============================================================================ SwatchGrid

SwatchGrid::SwatchGrid(QWidget* parent, int cell) : QWidget(parent), m_cell(cell) {
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

void SwatchGrid::setColors(const QList<QColor>& c) {
    m_colors = c;
    updateGeometry();
    update();
}

int SwatchGrid::heightForWidth(int w) const {
    int cols = std::max(1, w / (m_cell + 2));
    int rows = std::max(1, int((m_colors.size() + cols - 1) / cols));
    return rows * (m_cell + 2) + 2;
}

int SwatchGrid::indexAt(const QPoint& p) const {
    int cols = std::max(1, width() / (m_cell + 2));
    int c = p.x() / (m_cell + 2), r = p.y() / (m_cell + 2);
    if (c >= cols) return -1;
    int i = r * cols + c;
    return i < m_colors.size() ? i : -1;
}

void SwatchGrid::paintEvent(QPaintEvent*) {
    QPainter p(this);
    int cols = std::max(1, width() / (m_cell + 2));
    const ThemeColors& tc = Theme::colors();
    if (m_colors.isEmpty()) {
        p.setPen(tc.dim);
        p.drawText(rect(), Qt::AlignCenter, T("Right-click to add colors"));
        return;
    }
    for (int i = 0; i < m_colors.size(); ++i) {
        QRect r((i % cols) * (m_cell + 2) + 1, (i / cols) * (m_cell + 2) + 1, m_cell, m_cell);
        p.fillRect(r, m_colors[i]);
        p.setPen(tc.border);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }
}

void SwatchGrid::mousePressEvent(QMouseEvent* e) {
    int i = indexAt(e->pos());
    if (e->button() == Qt::RightButton) {
        Q_EMIT contextRequested(i, e->globalPosition().toPoint());
        return;
    }
    if (i >= 0) Q_EMIT picked(m_colors[i]);
}

// ============================================================================ ColorSwatches

ColorSwatches::ColorSwatches(QWidget* parent) : QWidget(parent) {
    setFixedSize(62, 54);
    connect(app(), &App::colorsChanged, this, [this] { update(); });
    setToolTip(T("Main and sub color. Click the small arrow to swap (X), the corner to reset (D), the checker for the transparent color."));
}

void ColorSwatches::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const ThemeColors& c = Theme::colors();
    QRect bg(20, 18, 30, 30), fg(4, 4, 30, 30);
    p.fillRect(bg, app()->background());
    p.setPen(c.border);
    p.drawRect(bg);
    if (app()->transparentColor()) p.fillRect(fg, checkerBrush(5));
    else p.fillRect(fg, app()->foreground());
    p.setPen(QPen(c.text, 1));
    p.drawRect(fg);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawPixmap(QRect(38, 2, 18, 14), Theme::iconPixmap("swap", 14, c.text));
    // transparent color toggle
    QRect tr(52, 36, 10, 10);
    p.fillRect(tr, checkerBrush(3));
    p.setPen(app()->transparentColor() ? c.accent : c.border);
    p.drawRect(tr);
    // reset
    QRect rr(4, 40, 8, 8);
    p.fillRect(rr.adjusted(2, 2, 2, 2), Qt::white);
    p.fillRect(rr, Qt::black);
}

void ColorSwatches::mousePressEvent(QMouseEvent* e) {
    QPoint p = e->pos();
    if (QRect(38, 2, 18, 16).contains(p)) app()->swapColors();
    else if (QRect(50, 34, 14, 14).contains(p)) app()->setTransparentColor(!app()->transparentColor());
    else if (QRect(2, 38, 12, 12).contains(p)) app()->resetColors();
    else if (QRect(20, 18, 30, 30).contains(p) && !QRect(4, 4, 30, 30).contains(p)) app()->swapColors();
    else if (QRect(4, 4, 30, 30).contains(p) && app()->transparentColor()) app()->setTransparentColor(false);
}

void ColorSwatches::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!QRect(4, 4, 30, 30).contains(e->pos())) return;
    QColor c = QColorDialog::getColor(app()->foreground(), this, T("Main color"));
    if (c.isValid()) {
        app()->setForeground(c);
        app()->pushColorHistory(c);
    }
}

// ============================================================================ ColorPanel

ColorPanel::ColorPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(4);
    m_wheel = new ColorWheel(this);
    v->addWidget(m_wheel, 1);

    auto* row = new QHBoxLayout;
    row->addWidget(new ColorSwatches(this));
    auto* hexBox = new QVBoxLayout;
    m_hex = new QLineEdit(this);
    m_hex->setMaxLength(7);
    m_hex->setPlaceholderText("#rrggbb");
    m_hex->setFixedWidth(84);
    hexBox->addWidget(m_hex);
    hexBox->addStretch();
    row->addLayout(hexBox);
    row->addStretch();
    v->addLayout(row);

    const char* names[6] = {"H", "S", "V", "R", "G", "B"};
    const int maxes[6] = {359, 100, 100, 255, 255, 255};
    for (int i = 0; i < 6; ++i) {
        m_sl[i] = new GradientSlider(names[i], maxes[i], this);
        v->addWidget(m_sl[i]);
        connect(m_sl[i], &GradientSlider::valueChanged, this, [this, i](int) {
            if (m_updating) return;
            QColor c;
            if (i < 3) c = QColor::fromHsv(m_sl[0]->value(), m_sl[1]->value() * 255 / 100, m_sl[2]->value() * 255 / 100);
            else c = QColor(m_sl[3]->value(), m_sl[4]->value(), m_sl[5]->value());
            setColor(c, m_sl[i]);
        });
        connect(m_sl[i], &GradientSlider::released, this, [] { app()->pushColorHistory(app()->foreground()); });
    }

    auto* hrow = new QHBoxLayout;
    auto* hlab = new QLabel(T("Harmony"), this);
    hlab->setObjectName("sectionTitle");
    hrow->addWidget(hlab);
    m_harmonyBox = new QComboBox(this);
    m_harmonyBox->addItems({T("Off"), T("Complementary"), T("Split complementary"), T("Triad"), T("Analogous"), T("Square"),
                            T("Light and dark")});
    m_harmonyBox->setToolTip(T("Shows colors that go well with the main color"));
    m_harmonyBox->setCurrentIndex(app()->settings().value("colors/harmony", 0).toInt());
    hrow->addWidget(m_harmonyBox, 1);
    v->addLayout(hrow);
    m_harmonySw = new SwatchGrid(this, 18);
    m_harmonySw->setToolTip(T("Click a color to use it"));
    v->addWidget(m_harmonySw);
    connect(m_harmonyBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        app()->settings().setValue("colors/harmony", i);
        updateHarmony();
    });
    connect(m_harmonySw, &SwatchGrid::picked, this, [](const QColor& c) {
        app()->setForeground(c);
        app()->pushColorHistory(c);
    });

    auto* hl = new QLabel(T("Recent colors"), this);
    hl->setObjectName("sectionTitle");
    v->addWidget(hl);
    m_history = new SwatchGrid(this, 14);
    v->addWidget(m_history);
    auto* pl = new QLabel(T("Palette"), this);
    pl->setObjectName("sectionTitle");
    v->addWidget(pl);
    m_palette = new SwatchGrid(this, 16);
    v->addWidget(m_palette);
    v->addStretch(0);

    connect(m_wheel, &ColorWheel::colorChanged, this, [this](const QColor& c) { setColor(c, m_wheel); });
    connect(m_wheel, &ColorWheel::colorPicked, this, [](const QColor& c) { app()->pushColorHistory(c); });
    connect(m_hex, &QLineEdit::editingFinished, this, [this] {
        QColor c(m_hex->text().startsWith('#') ? m_hex->text() : "#" + m_hex->text());
        if (c.isValid()) {
            setColor(c, nullptr);
            app()->pushColorHistory(c);
        }
    });
    connect(m_history, &SwatchGrid::picked, this, [](const QColor& c) { app()->setForeground(c); });
    connect(m_palette, &SwatchGrid::picked, this, [](const QColor& c) {
        app()->setForeground(c);
        app()->pushColorHistory(c);
    });
    connect(m_palette, &SwatchGrid::contextRequested, this, &ColorPanel::paletteMenu);
    connect(app(), &App::colorsChanged, this, &ColorPanel::syncFromApp);
    connect(app(), &App::colorHistoryChanged, this, [this] { m_history->setColors(app()->colorHistory().mid(0, 24)); });
    m_history->setColors(app()->colorHistory().mid(0, 24));
    loadPalette();
    syncFromApp();
}

void ColorPanel::updateHarmony() {
    if (!m_harmonyBox) return;
    const int kind = m_harmonyBox->currentIndex();
    m_harmonySw->setVisible(kind > 0);
    QColor c = app()->foreground();
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    if (h < 0) h = m_wheel->color().hsvHueF() >= 0 ? m_wheel->color().hsvHueF() : 0.f;
    QList<float> offsets;
    switch (kind) {
    case 1: offsets = {0.5f}; break;
    case 2: offsets = {150.f / 360.f, 210.f / 360.f}; break;
    case 3: offsets = {1.f / 3.f, 2.f / 3.f}; break;
    case 4: offsets = {-30.f / 360.f, 30.f / 360.f, -60.f / 360.f, 60.f / 360.f}; break;
    case 5: offsets = {0.25f, 0.5f, 0.75f}; break;
    default: break;
    }
    QList<float> hues;
    for (float o : offsets) hues << float(std::fmod(h + o + 1.f, 1.f));
    m_wheel->setHarmony(hues);
    QList<QColor> sw;
    if (kind == 6) {
        // the same color in light and dark steps (for shading)
        for (int i = 0; i < 8; ++i) {
            const float t = i / 7.f;
            sw << QColor::fromHsvF(std::fmod(h + (t - 0.5f) * 0.06f + 1.f, 1.f), std::clamp(s * (0.55f + 0.7f * t), 0.f, 1.f),
                                   std::clamp(1.f - t * 0.82f, 0.f, 1.f));
        }
    } else if (kind > 0) {
        QList<float> all = {h};
        all << hues;
        for (float hh : all) {
            sw << QColor::fromHsvF(hh, s, v);
            sw << QColor::fromHsvF(hh, s * 0.55f, std::min(1.f, v + 0.2f));  // lighter
            sw << QColor::fromHsvF(hh, std::min(1.f, s * 1.1f), v * 0.62f);   // darker
        }
    }
    m_harmonySw->setColors(sw);
}

void ColorPanel::setColor(const QColor& c, QObject* source) {
    m_updating = true;
    app()->setForeground(c);
    if (source != m_wheel) m_wheel->setColor(c);
    m_updating = false;
}

void ColorPanel::syncFromApp() {
    QColor c = app()->foreground();
    m_updating = true;
    if (m_wheel->color().rgb() != c.rgb()) m_wheel->setColor(c);
    int h = m_wheel->color().hsvHue();
    if (h < 0) h = 0;
    float hf, sf, vf;
    c.getHsvF(&hf, &sf, &vf);
    m_sl[0]->setValue(c.hsvHue() >= 0 ? c.hsvHue() : m_sl[0]->value());
    m_sl[1]->setValue(int(std::round(sf * 100)));
    m_sl[2]->setValue(int(std::round(vf * 100)));
    m_sl[3]->setValue(c.red());
    m_sl[4]->setValue(c.green());
    m_sl[5]->setValue(c.blue());
    int hh = m_sl[0]->value();
    double s = m_sl[1]->value() / 100.0, v = m_sl[2]->value() / 100.0;
    m_sl[0]->setGradient([s, v](double t) { return QColor::fromHsvF(float(std::min(t, 0.9999)), float(std::max(s, 0.6)), float(std::max(v, 0.7))); });
    m_sl[1]->setGradient([hh, v](double t) { return QColor::fromHsvF(hh / 360.f, float(t), float(v)); });
    m_sl[2]->setGradient([hh, s](double t) { return QColor::fromHsvF(hh / 360.f, float(s), float(t)); });
    m_sl[3]->setGradient([c](double t) { return QColor(int(t * 255), c.green(), c.blue()); });
    m_sl[4]->setGradient([c](double t) { return QColor(c.red(), int(t * 255), c.blue()); });
    m_sl[5]->setGradient([c](double t) { return QColor(c.red(), c.green(), int(t * 255)); });
    if (!m_hex->hasFocus()) m_hex->setText(c.name());
    m_updating = false;
    updateHarmony();
}

void ColorPanel::loadPalette() {
    QList<QColor> l;
    for (const QString& s : app()->settings().value("palette/colors").toStringList()) l << QColor(s);
    if (l.isEmpty() && !app()->settings().contains("palette/colors")) {
        for (const char* s : {"#000000", "#3c3c3c", "#7f7f7f", "#c3c3c3", "#ffffff", "#7a2e2e", "#d24a3c", "#f08a4b", "#f2c94c",
                              "#a3c24c", "#3f8f5a", "#2f6f8f", "#3c5ad2", "#7a4fc2", "#c24f9a", "#f2d0b8", "#e0a98a", "#b5745a",
                              "#7a4a3a", "#3a2a24"})
            l << QColor(s);
    }
    m_palette->setColors(l);
}

void ColorPanel::savePalette() {
    QStringList s;
    for (const QColor& c : m_palette->colors()) s << c.name();
    app()->settings().setValue("palette/colors", s);
}

void ColorPanel::paletteMenu(int index, const QPoint& pos) {
    QMenu m(this);
    QAction* add = m.addAction(T("Add main color"));
    QAction* del = index >= 0 ? m.addAction(T("Remove color")) : nullptr;
    QAction* rep = index >= 0 ? m.addAction(T("Replace with main color")) : nullptr;
    m.addSeparator();
    QAction* imp = m.addAction(T("Import palette (.gpl)..."));
    QAction* exp = m.addAction(T("Export palette (.gpl)..."));
    QAction* clr = m.addAction(T("Clear palette"));
    QAction* r = m.exec(pos);
    QList<QColor> l = m_palette->colors();
    if (r == add) l << app()->foreground();
    else if (r && r == del) l.removeAt(index);
    else if (r && r == rep) l[index] = app()->foreground();
    else if (r == clr) l.clear();
    else if (r == imp) {
        QString f = QFileDialog::getOpenFileName(this, T("Import palette"), {}, "GIMP palette (*.gpl);;All files (*)");
        QFile file(f);
        if (!f.isEmpty() && file.open(QIODevice::ReadOnly)) {
            QTextStream ts(&file);
            while (!ts.atEnd()) {
                QStringList parts = ts.readLine().simplified().split(' ');
                if (parts.size() >= 3) {
                    bool a, b, c;
                    int R = parts[0].toInt(&a), G = parts[1].toInt(&b), B = parts[2].toInt(&c);
                    if (a && b && c) l << QColor(R, G, B);
                }
            }
        }
    } else if (r == exp) {
        QString f = QFileDialog::getSaveFileName(this, T("Export palette"), "palette.gpl", "GIMP palette (*.gpl)");
        QFile file(f);
        if (!f.isEmpty() && file.open(QIODevice::WriteOnly)) {
            QTextStream ts(&file);
            ts << "GIMP Palette\nName: GraphiX\n#\n";
            for (const QColor& c : l) ts << c.red() << ' ' << c.green() << ' ' << c.blue() << '\t' << c.name() << '\n';
        }
        return;
    } else return;
    m_palette->setColors(l);
    savePalette();
}

}  // namespace gx
