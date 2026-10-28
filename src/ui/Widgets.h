#pragma once
#include <QColor>
#include <QPointF>
#include <QToolButton>
#include <QVector>
#include <QWidget>
#include <functional>

class QLineEdit;
class QVBoxLayout;

namespace gx {

// SAI-style value bar: a slim bar with the label and value drawn inside.
// Click or drag to set, wheel to step, double-click to type a number.
class ValueBar : public QWidget {
    Q_OBJECT
public:
    ValueBar(const QString& label, double min, double max, int decimals = 0, QWidget* parent = nullptr);
    void setValue(double v);
    double value() const { return m_value; }
    void setRange(double min, double max);
    void setLogarithmic(bool on) { m_log = on; update(); }
    void setSuffix(const QString& s) { m_suffix = s; update(); }
    void setDisplayScale(double s) { m_displayScale = s; update(); }  // e.g. 100 for percent of 0..1
    void setLabel(const QString& l) { m_label = l; update(); }
    QSize sizeHint() const override { return QSize(160, 20); }
    QSize minimumSizeHint() const override { return QSize(60, 18); }

Q_SIGNALS:
    void valueChanged(double v);
    void dragStarted();
    void dragFinished();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    double toT(double v) const;
    double fromT(double t) const;
    void setFromX(double x);
    void commit(double v);
    QString text() const;

    QString m_label, m_suffix;
    double m_min, m_max, m_value;
    int m_decimals;
    bool m_log = false;
    double m_displayScale = 1.0;
    bool m_drag = false;
    QLineEdit* m_edit = nullptr;
};

// Collapsible section with a header button.
class Section : public QWidget {
    Q_OBJECT
public:
    Section(const QString& title, QWidget* parent = nullptr, bool open = true);
    QVBoxLayout* body() const { return m_body; }
    void setOpen(bool on);

private:
    QToolButton* m_head;
    QWidget* m_content;
    QVBoxLayout* m_body;
};

class ColorButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorButton(const QColor& c = Qt::black, QWidget* parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor& c);
Q_SIGNALS:
    void colorChanged(const QColor& c);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QColor m_color;
};

// Editable curve (pressure curve, Curves adjustment). Points are 0..1.
class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget* parent = nullptr);
    void setPoints(const QVector<QPointF>& p);
    QVector<QPointF> points() const { return m_pts; }
    void setHistogram(const QVector<float>& h) { m_hist = h; update(); }
    void setCurveColor(const QColor& c) { m_curveColor = c; update(); }
    void setLiveValue(double x) { m_live = x; update(); }  // shows the current pen pressure
    QSize sizeHint() const override { return QSize(220, 220); }
    int heightForWidth(int w) const override { return w; }
    bool hasHeightForWidth() const override { return true; }

Q_SIGNALS:
    void pointsChanged(const QVector<QPointF>& p);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    QRectF plot() const;
    QPointF toWidget(const QPointF& p) const;
    QPointF fromWidget(const QPointF& w) const;
    QVector<QPointF> m_pts{QPointF(0, 0), QPointF(1, 1)};
    QVector<float> m_hist;
    QColor m_curveColor;
    int m_drag = -1;
    double m_live = -1;
};

// Checkerboard brush for transparency previews
QBrush checkerBrush(int size = 6);

}  // namespace gx
