#pragma once
#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace gx {

class CanvasView;
class ValueBar;

class NavigatorView : public QWidget {
    Q_OBJECT
public:
    explicit NavigatorView(QWidget* parent = nullptr);
    void setView(CanvasView* v);
    QSize sizeHint() const override { return QSize(220, 150); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
private:
    void refresh();
    QRectF imageRect() const;
    void jump(const QPointF& p);
    QPointer<CanvasView> m_view;
    QImage m_thumb;
    QTimer m_timer;
    bool m_incomplete = false;
};

class NavigatorPanel : public QWidget {
    Q_OBJECT
public:
    explicit NavigatorPanel(QWidget* parent = nullptr);
private:
    void setView(CanvasView* v);
    void sync();
    NavigatorView* m_nav;
    ValueBar* m_zoom;
    ValueBar* m_rot;
    QPointer<CanvasView> m_view;
    bool m_updating = false;
};

}  // namespace gx
