#include "ui/Widgets.h"

#include <QColorDialog>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Adjustments.h"
#include "ui/Theme.h"

namespace gx {

QBrush checkerBrush(int s) {
    QPixmap pm(s * 2, s * 2);
    pm.fill(QColor(255, 255, 255));
    QPainter p(&pm);
    p.fillRect(0, 0, s, s, QColor(204, 204, 204));
    p.fillRect(s, s, s, s, QColor(204, 204, 204));
    return QBrush(pm);
}

// ============================================================================ ValueBar

ValueBar::ValueBar(const QString& label, double min, double max, int decimals, QWidget* parent)
    : QWidget(parent), m_label(label), m_min(min), m_max(max), m_value(min), m_decimals(decimals) {
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::SizeHorCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(20);
}

void ValueBar::setRange(double min, double max) {
    m_min = min;
    m_max = max;
    m_value = std::clamp(m_value, m_min, m_max);
    update();
}

void ValueBar::setValue(double v) {
    v = std::clamp(v, m_min, m_max);
    if (v == m_value) return;
    m_value = v;
    update();
}

double ValueBar::toT(double v) const {
    if (m_max <= m_min) return 0;
    if (m_log && m_min > 0) return std::log(v / m_min) / std::log(m_max / m_min);
    return (v - m_min) / (m_max - m_min);
}

double ValueBar::fromT(double t) const {
    t = std::clamp(t, 0.0, 1.0);
    if (m_log && m_min > 0) return m_min * std::pow(m_max / m_min, t);
    return m_min + (m_max - m_min) * t;
}

void ValueBar::commit(double v) {
    double f = std::pow(10.0, m_decimals);
    if (m_log && v >= 10 && m_decimals > 0) f = 1;  // whole pixels for big brushes
    v = std::round(v * f) / f;
    v = std::clamp(v, m_min, m_max);
    if (v == m_value) return;
    m_value = v;
    update();
    Q_EMIT valueChanged(v);
}

void ValueBar::setFromX(double x) { commit(fromT((x - 1) / std::max(1.0, width() - 2.0))); }

QString ValueBar::text() const {
    double shown = m_value * m_displayScale;
    int dec = m_decimals;
    if (m_displayScale != 1.0) dec = std::max(0, m_decimals - int(std::log10(m_displayScale)));
    if (m_log && shown >= 10) dec = 0;
    return QString::number(shown, 'f', dec) + m_suffix;
}

void ValueBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const ThemeColors& c = Theme::colors();
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c.border, 1));
    p.setBrush(c.base);
    p.drawRoundedRect(r, 3, 3);
    double t = std::clamp(toT(m_value), 0.0, 1.0);
    QRectF fill = r.adjusted(1, 1, -1, -1);
    fill.setWidth(fill.width() * t);
    QColor a = c.accent;
    a.setAlpha(isEnabled() ? 150 : 60);
    p.setPen(Qt::NoPen);
    p.setBrush(a);
    p.drawRoundedRect(fill, 2, 2);
    p.setPen(isEnabled() ? c.text : c.dim);
    QFont f = font();
    p.setFont(f);
    p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, m_label);
    p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignRight, text());
}

void ValueBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    m_drag = true;
    Q_EMIT dragStarted();
    setFromX(e->position().x());
}

void ValueBar::mouseMoveEvent(QMouseEvent* e) {
    if (m_drag) setFromX(e->position().x());
}

void ValueBar::mouseReleaseEvent(QMouseEvent*) {
    if (!m_drag) return;
    m_drag = false;
    Q_EMIT dragFinished();
}

void ValueBar::mouseDoubleClickEvent(QMouseEvent*) {
    if (m_edit) return;
    m_edit = new QLineEdit(this);
    m_edit->setGeometry(rect());
    m_edit->setAlignment(Qt::AlignRight);
    m_edit->setText(QString::number(m_value * m_displayScale, 'f', std::max(0, m_decimals)));
    m_edit->selectAll();
    m_edit->show();
    m_edit->setFocus();
    auto finish = [this] {
        if (!m_edit) return;
        bool ok = false;
        double v = m_edit->text().replace(',', '.').toDouble(&ok);
        QLineEdit* ed = m_edit;
        m_edit = nullptr;
        ed->deleteLater();
        if (ok) {
            Q_EMIT dragStarted();
            commit(v / m_displayScale);
            Q_EMIT dragFinished();
        }
    };
    connect(m_edit, &QLineEdit::editingFinished, this, finish);
}

void ValueBar::wheelEvent(QWheelEvent* e) {
    double steps = e->angleDelta().y() / 120.0;
    if (steps == 0) return;
    double t = toT(m_value) + steps * 0.01 * ((e->modifiers() & Qt::ShiftModifier) ? 10 : 1);
    Q_EMIT dragStarted();
    double v = fromT(t);
    if (!m_log) {
        double step = std::pow(10.0, -m_decimals);
        if (m_displayScale > 1) step = 1.0 / m_displayScale;
        v = m_value + (steps > 0 ? step : -step) * ((e->modifiers() & Qt::ShiftModifier) ? 10 : 1);
    }
    commit(v);
    Q_EMIT dragFinished();
    e->accept();
}

// ============================================================================ Section

Section::Section(const QString& title, QWidget* parent, bool open) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    m_head = new QToolButton(this);
    m_head->setText(title);
    m_head->setCheckable(true);
    m_head->setChecked(open);
    m_head->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_head->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    m_head->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_head->setStyleSheet("QToolButton { text-align: left; font-weight: 600; border: none; padding: 2px; }"
                          "QToolButton:checked { background: transparent; color: palette(text); }");
    m_content = new QWidget(this);
    m_body = new QVBoxLayout(m_content);
    m_body->setContentsMargins(4, 0, 0, 4);
    m_body->setSpacing(3);
    v->addWidget(m_head);
    v->addWidget(m_content);
    m_content->setVisible(open);
    connect(m_head, &QToolButton::toggled, this, &Section::setOpen);
}

void Section::setOpen(bool on) {
    m_head->setChecked(on);
    m_head->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    m_content->setVisible(on);
    const QString key = property("stateKey").toString();
    if (!key.isEmpty() && app()) app()->settings().setValue("ui/sections/" + key, on);
}

bool Section::isOpen() const { return m_head->isChecked(); }

void Section::setStateKey(const QString& key) {
    setProperty("stateKey", key);
    if (app() && app()->settings().contains("ui/sections/" + key)) setOpen(app()->settings().value("ui/sections/" + key).toBool());
}

// ============================================================================ ColorButton

ColorButton::ColorButton(const QColor& c, QWidget* parent) : QToolButton(parent), m_color(c) {
    setFixedSize(40, 20);
    connect(this, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_color, this, T("Choose color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            setColor(c);
            Q_EMIT colorChanged(c);
        }
    });
}

void ColorButton::setColor(const QColor& c) {
    m_color = c;
    update();
}

void ColorButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QRect r = rect().adjusted(1, 1, -1, -1);
    p.fillRect(r, checkerBrush(4));
    p.fillRect(r, m_color);
    p.setPen(Theme::colors().border);
    p.drawRect(r.adjusted(0, 0, -1, -1));
}

// ============================================================================ CurveEditor

CurveEditor::CurveEditor(QWidget* parent) : QWidget(parent) {
    setMinimumSize(140, 140);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

void CurveEditor::setPoints(const QVector<QPointF>& p) {
    m_pts = p.size() >= 2 ? p : QVector<QPointF>{QPointF(0, 0), QPointF(1, 1)};
    std::sort(m_pts.begin(), m_pts.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    update();
}

QRectF CurveEditor::plot() const {
    double s = std::min(width(), height()) - 12.0;
    return QRectF((width() - s) / 2, (height() - s) / 2, s, s);
}

QPointF CurveEditor::toWidget(const QPointF& p) const {
    QRectF r = plot();
    return QPointF(r.left() + p.x() * r.width(), r.bottom() - p.y() * r.height());
}

QPointF CurveEditor::fromWidget(const QPointF& w) const {
    QRectF r = plot();
    return QPointF(std::clamp((w.x() - r.left()) / r.width(), 0.0, 1.0), std::clamp((r.bottom() - w.y()) / r.height(), 0.0, 1.0));
}

void CurveEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const ThemeColors& c = Theme::colors();
    QRectF r = plot();
    p.fillRect(r, c.base);
    if (!m_hist.isEmpty()) {
        float mx = *std::max_element(m_hist.begin(), m_hist.end());
        if (mx > 0) {
            QPainterPath h;
            h.moveTo(r.bottomLeft());
            for (int i = 0; i < m_hist.size(); ++i)
                h.lineTo(r.left() + r.width() * i / (m_hist.size() - 1), r.bottom() - r.height() * std::sqrt(m_hist[i] / mx));
            h.lineTo(r.bottomRight());
            QColor hc = c.dim;
            hc.setAlpha(90);
            p.fillPath(h, hc);
        }
    }
    p.setPen(QPen(c.border, 1));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(r.left() + r.width() * i / 4, r.top()), QPointF(r.left() + r.width() * i / 4, r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4), QPointF(r.right(), r.top() + r.height() * i / 4));
    }
    p.drawRect(r);
    p.setPen(QPen(c.dim, 1, Qt::DashLine));
    p.drawLine(r.bottomLeft(), r.topRight());
    std::vector<float> lut = curveLut(m_pts, 256);
    QPainterPath path;
    for (int i = 0; i < 256; ++i) {
        QPointF w = toWidget(QPointF(i / 255.0, lut[size_t(i)]));
        if (i == 0) path.moveTo(w);
        else path.lineTo(w);
    }
    p.setPen(QPen(m_curveColor.isValid() ? m_curveColor : c.text, 2));
    p.drawPath(path);
    if (m_live >= 0) {
        QColor lc = c.accent;
        p.setPen(QPen(lc, 1));
        double x = r.left() + r.width() * m_live;
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
    for (int i = 0; i < m_pts.size(); ++i) {
        QPointF w = toWidget(m_pts[i]);
        p.setPen(QPen(c.text, 1.5));
        p.setBrush(i == m_drag ? c.accent : c.panel);
        p.drawEllipse(w, 4.5, 4.5);
    }
}

void CurveEditor::mousePressEvent(QMouseEvent* e) {
    QPointF w = e->position();
    m_drag = -1;
    for (int i = 0; i < m_pts.size(); ++i)
        if (QLineF(toWidget(m_pts[i]), w).length() < 8) { m_drag = i; break; }
    if (e->button() == Qt::RightButton) {
        if (m_drag > 0 && m_drag < m_pts.size() - 1) {
            m_pts.remove(m_drag);
            Q_EMIT pointsChanged(m_pts);
        }
        m_drag = -1;
        update();
        return;
    }
    if (m_drag < 0) {
        QPointF np = fromWidget(w);
        int at = 0;
        while (at < m_pts.size() && m_pts[at].x() < np.x()) ++at;
        m_pts.insert(at, np);
        m_drag = at;
        Q_EMIT pointsChanged(m_pts);
    }
    update();
}

void CurveEditor::mouseMoveEvent(QMouseEvent* e) {
    if (m_drag < 0) return;
    QPointF np = fromWidget(e->position());
    double lo = m_drag > 0 ? m_pts[m_drag - 1].x() + 0.01 : 0.0;
    double hi = m_drag < m_pts.size() - 1 ? m_pts[m_drag + 1].x() - 0.01 : 1.0;
    if (m_drag == 0) np.setX(std::min(np.x(), hi));
    else if (m_drag == m_pts.size() - 1) np.setX(std::max(np.x(), lo));
    else np.setX(std::clamp(np.x(), lo, hi));
    m_pts[m_drag] = np;
    update();
    Q_EMIT pointsChanged(m_pts);
}

void CurveEditor::mouseReleaseEvent(QMouseEvent*) {
    m_drag = -1;
    update();
}

void CurveEditor::mouseDoubleClickEvent(QMouseEvent*) {
    m_pts = {QPointF(0, 0), QPointF(1, 1)};
    update();
    Q_EMIT pointsChanged(m_pts);
}

}  // namespace gx
