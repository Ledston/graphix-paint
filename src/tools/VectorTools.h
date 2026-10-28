#pragma once
#include <QPointer>

#include "brush/Stabilizer.h"
#include "core/Layer.h"
#include "tools/Tool.h"

namespace gx {

// Settings shared by the linework pens (like SAI's vector pen).
struct LineworkOptions {
    double width = 4;
    double minWidth = 0.15;
    double hardness = 0.9;
    double opacity = 1.0;
    double simplify = 0.6;
};
LineworkOptions& lineworkOptions();

class VectorToolBase : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
protected:
    VectorLayer* vectorLayer(CanvasView* v, bool report = true) const;
    void rerender(CanvasView* v, VectorLayer* l, const QRectF& r);
};

class VectorPenTool : public VectorToolBase {
    Q_OBJECT
public:
    explicit VectorPenTool(QObject* p = nullptr) : VectorToolBase("vpen", "Linework pen", "vector-pen", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    bool showsBrushOutline() const override { return true; }
    double outlineDiameter() const override { return lineworkOptions().width; }
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_active; }

private:
    void addSample(const InputSample& s);
    bool m_active = false;
    Stabilizer m_stab;
    QVector<VectorPoint> m_raw;
    quint32 m_strokeId = 0;
    QRectF m_lastBounds;
    QPointer<CanvasView> m_view;
};

// Click to place points of a smooth curve; double-click or Enter finishes.
class CurveTool : public VectorToolBase {
    Q_OBJECT
public:
    explicit CurveTool(QObject* p = nullptr) : VectorToolBase("curve", "Curve", "curve", p) {}
    void press(const PointerEvent& e) override;
    void hover(const PointerEvent& e) override;
    void doubleClick(const PointerEvent& e) override;
    bool keyPress(QKeyEvent* e, CanvasView* v) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QWidget* createOptions(QWidget* parent) override;
    bool hasPending() const override { return !m_pts.isEmpty(); }
    void commitPending() override { finish(); }
    void cancelPending() override { m_pts.clear(); Q_EMIT overlayChanged(); }

private:
    void finish();
    QVector<VectorPoint> m_pts;
    QPointF m_hover;
    QPointer<CanvasView> m_view;
};

class VectorEditTool : public VectorToolBase {
    Q_OBJECT
public:
    enum Kind { Edit, Pressure, Weight, Color, Eraser };
    explicit VectorEditTool(Kind k = Edit, QObject* p = nullptr);
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void hover(const PointerEvent& e) override;
    void doubleClick(const PointerEvent& e) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_drag; }
    bool showsBrushOutline() const override { return m_kind == Eraser; }
    double outlineDiameter() const override { return eraserSize; }

    static double eraserSize;

private:
    void eraseAt(CanvasView* v, const QPointF& p);
    Kind m_kind;
    bool m_drag = false;
    int m_stroke = -1, m_point = -1;
    QPointF m_press;
    VectorStroke m_orig;
    QPointer<CanvasView> m_view;
    int m_hoverStroke = -1;
    bool m_changed = false;
};

class VectorPressureTool : public VectorEditTool {
    Q_OBJECT
public:
    explicit VectorPressureTool(QObject* p = nullptr) : VectorEditTool(Pressure, p) {}
};
class VectorWeightTool : public VectorEditTool {
    Q_OBJECT
public:
    explicit VectorWeightTool(QObject* p = nullptr) : VectorEditTool(Weight, p) {}
};
class VectorColorTool : public VectorEditTool {
    Q_OBJECT
public:
    explicit VectorColorTool(QObject* p = nullptr) : VectorEditTool(Color, p) {}
};
class VectorEraserTool : public VectorEditTool {
    Q_OBJECT
public:
    explicit VectorEraserTool(QObject* p = nullptr) : VectorEditTool(Eraser, p) {}
};

}  // namespace gx
