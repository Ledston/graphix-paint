#pragma once
#include <QColor>

#include "tools/Tool.h"

namespace gx {

class HandTool : public Tool {
    Q_OBJECT
public:
    explicit HandTool(QObject* p = nullptr) : Tool("hand", "Hand", "hand", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    QCursor cursor(CanvasView*) const override { return m_drag ? Qt::ClosedHandCursor : Qt::OpenHandCursor; }
    bool isBusy() const override { return m_drag; }

private:
    QPointF m_last;
    bool m_drag = false;
};

class ZoomTool : public Tool {
    Q_OBJECT
public:
    explicit ZoomTool(QObject* p = nullptr) : Tool("zoom", "Zoom", "zoom", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    QCursor cursor(CanvasView*) const override { return Qt::SizeHorCursor; }

private:
    QPointF m_start;
    double m_startZoom = 1;
    bool m_moved = false;
};

class RotateTool : public Tool {
    Q_OBJECT
public:
    explicit RotateTool(QObject* p = nullptr) : Tool("rotate", "Rotate view", "rotate", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void doubleClick(const PointerEvent& e) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView*) const override { return Qt::SizeAllCursor; }

private:
    double m_startAngle = 0, m_startRot = 0;
    bool m_drag = false;
};

class BrushSizeTool : public Tool {
    Q_OBJECT
public:
    explicit BrushSizeTool(QObject* p = nullptr) : Tool("brushsize", "Brush size", "brushsize", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent&) override { m_drag = false; }
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView*) const override { return Qt::BlankCursor; }

private:
    QPointF m_start, m_center;
    double m_startSize = 1;
    bool m_drag = false;
};

class EyedropperTool : public Tool {
    Q_OBJECT
public:
    explicit EyedropperTool(QObject* p = nullptr) : Tool("eyedropper", "Eyedropper", "eyedropper", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView*) const override;
    QWidget* createOptions(QWidget* parent) override;

    bool sampleMerged = true;
    int average = 1;  // 1, 3, 5, 11

private:
    QColor sample(CanvasView* v, const QPointF& pos) const;
    bool m_drag = false;
    bool m_background = false;
    QColor m_prev, m_now;
    QPointF m_pos;
};

}  // namespace gx
