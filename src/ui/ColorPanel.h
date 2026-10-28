#pragma once
#include <QColor>
#include <QImage>
#include <QWidget>
#include <functional>

class QLineEdit;
class QStackedWidget;
class QToolButton;

namespace gx {

// Hue ring with a saturation/value square (SAI style).
class ColorWheel : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheel(QWidget* parent = nullptr);
    void setColor(const QColor& c);
    QColor color() const { return QColor::fromHsvF(m_h, m_s, m_v); }
    QSize sizeHint() const override { return QSize(220, 220); }
    int heightForWidth(int w) const override { return w; }
    bool hasHeightForWidth() const override { return true; }
Q_SIGNALS:
    void colorChanged(const QColor& c);
    void colorPicked(const QColor& c);  // mouse released
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent*) override;
private:
    void pick(const QPointF& p);
    QRectF square() const;
    double outerR() const;
    double innerR() const;
    float m_h = 0, m_s = 0, m_v = 0;
    int m_mode = 0;  // 1 ring, 2 square
    QImage m_ring, m_sq;
    float m_sqHue = -1;
};

// Horizontal slider with a painted gradient track
class GradientSlider : public QWidget {
    Q_OBJECT
public:
    GradientSlider(const QString& label, int max, QWidget* parent = nullptr);
    void setValue(int v);
    int value() const { return m_value; }
    void setGradient(const std::function<QColor(double)>& f) { m_grad = f; update(); }
    QSize sizeHint() const override { return QSize(180, 18); }
Q_SIGNALS:
    void valueChanged(int v);
    void released();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
private:
    QRectF track() const;
    void setFromX(double x);
    QString m_label;
    int m_max, m_value = 0;
    std::function<QColor(double)> m_grad;
    bool m_drag = false;
};

class SwatchGrid : public QWidget {
    Q_OBJECT
public:
    explicit SwatchGrid(QWidget* parent = nullptr, int cell = 16);
    void setColors(const QList<QColor>& c);
    QList<QColor> colors() const { return m_colors; }
    int heightForWidth(int w) const override;
    bool hasHeightForWidth() const override { return true; }
    QSize sizeHint() const override { return QSize(200, heightForWidth(200)); }
Q_SIGNALS:
    void picked(const QColor& c);
    void contextRequested(int index, const QPoint& globalPos);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
private:
    int indexAt(const QPoint& p) const;
    QList<QColor> m_colors;
    int m_cell;
};

// Foreground / background swatches with swap, reset and the transparent color
class ColorSwatches : public QWidget {
    Q_OBJECT
public:
    explicit ColorSwatches(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(60, 54); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
};

class ColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPanel(QWidget* parent = nullptr);
    void savePalette();

private:
    void syncFromApp();
    void setColor(const QColor& c, QObject* source);
    void loadPalette();
    void paletteMenu(int index, const QPoint& pos);

    ColorWheel* m_wheel;
    GradientSlider* m_sl[6];  // H S V R G B
    QLineEdit* m_hex;
    SwatchGrid* m_history;
    SwatchGrid* m_palette;
    bool m_updating = false;
};

}  // namespace gx
