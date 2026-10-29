#pragma once
#include <QPointer>
#include <QTimer>

#include "core/Gradient.h"
#include "tools/SelectTools.h"

namespace gx {

// Paints the main color (or the current pattern of the materials panel) through a mask,
// inside the selection, as one undo step.
void paintThroughMask(Document* d, RasterLayer* l, const MaskSurface& mask, const QColor& color, double opacity, bool erase,
                      const QString& undoName, bool pattern = false, bool clipToSelection = true);
// The fill options for "fill with: main color / pattern"
QWidget* patternOptions(QWidget* parent, bool* usePattern);

// Paint bucket. Samples all visible layers by default (fills under line art),
// with tolerance, expansion and soft edges.
class FillTool : public Tool {
    Q_OBJECT
public:
    explicit FillTool(QObject* p = nullptr) : Tool("fill", "Fill", "fill", p) {}
    void press(const PointerEvent& e) override;
    QWidget* createOptions(QWidget* parent) override;

    int tolerance = 40;
    bool sampleMerged = true;  // (old setting, see source)
    int source = 0;            // FillSource: all visible, current layer, reference layers
    int gapClose = 0;          // px
    bool contiguous = true;
    int expand = 1;
    bool antialias = true;
    double opacity = 1.0;
    bool usePattern = false;
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
    // 0 = fill the whole shape, 1 = fill only the closed areas of the line art inside the shape
    int mode = 0;
    int source = 2;    // FillSource: lines from all visible layers, the current layer or the reference layers
    int gapClose = 3;  // px
    int under = 2;     // the fill goes this far under the lines
    bool usePattern = false;

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
