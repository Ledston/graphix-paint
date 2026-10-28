#pragma once
#include <QPointer>
#include <QTimer>

#include "core/Gradient.h"
#include "tools/SelectTools.h"

namespace gx {

// Paint bucket. Samples all visible layers by default (fills under line art),
// with tolerance, expansion and soft edges.
class FillTool : public Tool {
    Q_OBJECT
public:
    explicit FillTool(QObject* p = nullptr) : Tool("fill", "Fill", "fill", p) {}
    void press(const PointerEvent& e) override;
    QWidget* createOptions(QWidget* parent) override;

    int tolerance = 40;
    bool sampleMerged = true;
    bool contiguous = true;
    int expand = 1;
    bool antialias = true;
    double opacity = 1.0;
};

// Draw a shape with the lasso and it is filled with the main color (Alt: erases).
class LassoFillTool : public LassoSelectTool {
    Q_OBJECT
public:
    explicit LassoFillTool(QObject* p = nullptr);
    QWidget* createOptions(QWidget* parent) override;
    bool antialias = true;
    bool eraseMode = false;
    double opacity = 1.0;

protected:
    void apply(CanvasView* v, const QPolygonF& poly, Qt::KeyboardModifiers mods) override;
};

// Fills the layer (or selection) with a gradient. Drag to set direction.
class GradientTool : public Tool {
    Q_OBJECT
public:
    enum Shape { Linear = 0, Radial, Reflected, Diamond, Angle };
    explicit GradientTool(QObject* p = nullptr);
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    bool keyPress(QKeyEvent* e, CanvasView* v) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_drag; }

    int shape = Linear;
    int preset = 0;
    int repeat = 0;  // 0 none, 1 repeat, 2 mirror
    bool reverse = false;
    double opacity = 1.0;
    BlendMode mode = BlendMode::Normal;
    Gradient custom;
    bool useCustom = false;

private:
    Gradient currentGradient() const;
    void render();

    bool m_drag = false;
    QPointF m_a, m_b;
    QPointer<CanvasView> m_view;
    QTimer m_throttle;
    bool m_pending = false;
};

}  // namespace gx
