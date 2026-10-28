#pragma once
#include <QPointer>
#include <QTimer>
#include <memory>

#include "brush/BrushStroke.h"
#include "brush/Stabilizer.h"
#include "tools/Tool.h"

namespace gx {

// Painting with the current brush slot. Handles the stabilizer, pressure,
// Shift-click straight lines, perspective snapping, symmetry and the eraser end of the pen.
class BrushTool : public Tool {
    Q_OBJECT
public:
    explicit BrushTool(QObject* p = nullptr);
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void hover(const PointerEvent& e) override;
    bool keyPress(QKeyEvent* e, CanvasView* v) override;
    bool showsBrushOutline() const override { return true; }
    double outlineDiameter() const override;
    bool isBusy() const override { return m_stroke != nullptr || m_forward; }
    QCursor cursor(CanvasView*) const override { return Qt::CrossCursor; }

    // Editing the layer mask instead of pixels (toggled from the layer panel)
    static bool editMask;

private:
    InputSample toSample(const PointerEvent& e) const;
    bool constrain(InputSample& s);
    void feed(const std::vector<InputSample>& v);

    std::unique_ptr<BrushStroke> m_stroke;
    Stabilizer m_stab;
    QPointer<CanvasView> m_view;
    QTimer m_air;
    bool m_hoverEraser = false;
    bool m_usingEraser = false;
    // straight line memory (Shift + click)
    QPointF m_lastEnd;
    double m_lastPressure = 1;
    bool m_hasLastEnd = false;
    void* m_lastDoc = nullptr;
    // perspective snapping
    bool m_snap = false;
    int m_snapTarget = -1;
    QPointF m_snapStart;
    std::vector<InputSample> m_snapHold;
    // forward to the linework pen on vector layers
    Tool* m_forward = nullptr;
};

}  // namespace gx
