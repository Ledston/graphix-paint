#pragma once
#include <QImage>
#include <QWidget>

class QSlider;
class QToolButton;
class QLabel;

namespace gx {

// A reference picture floating over everything (like a sheet of tracing paper on the screen).
// - opacity can be changed;
// - "draw through" lets the pen and mouse pass through it to the canvas below;
// - on Windows 10 (2004) and later it is not shown in screen recordings, streams and screen
//   sharing: the people watching see only the canvas.
class ReferenceOverlay : public QWidget {
    Q_OBJECT
public:
    explicit ReferenceOverlay(QWidget* owner);
    ~ReferenceOverlay() override;

    void setImage(const QImage& img);
    bool hasImage() const { return !m_img.isNull(); }

    double opacity() const { return m_opacity; }
    void setOpacityValue(double o);
    bool clickThrough() const { return m_through; }
    void setClickThrough(bool on);
    bool gray() const { return m_gray; }
    void setGray(bool on);
    bool flip() const { return m_flip; }
    void setFlip(bool on);
    bool hideFromCapture() const { return m_hideCapture; }
    void setHideFromCapture(bool on);

    // What the hiding from capture actually does on this computer, for tooltips.
    enum class CaptureState { Hidden, BlackBox, Off, Failed, NotSupported };
    CaptureState captureState() const { return m_captureState; }
    static QString describe(CaptureState s);

Q_SIGNALS:
    void stateChanged();  // opacity, click-through, visibility, capture state

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool event(QEvent* e) override;

private:
    void applyCaptureHiding();
    void updateBar();
    void saveState() const;
    QRectF imageRect() const;
    Qt::Edges edgesAt(const QPoint& p) const;

    QImage m_img, m_grayImg;
    double m_opacity = 0.5;
    bool m_through = false, m_gray = false, m_flip = false, m_hideCapture = true;
    CaptureState m_captureState = CaptureState::NotSupported;

    QWidget* m_bar = nullptr;
    QSlider* m_opSlider = nullptr;
    QToolButton *m_grayBtn = nullptr, *m_flipBtn = nullptr, *m_throughBtn = nullptr;

    bool m_moving = false;
    Qt::Edges m_resizing;
    QPoint m_pressGlobal;
    QRect m_pressGeom;
};

}  // namespace gx
