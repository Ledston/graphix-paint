#pragma once
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <memory>

#include "brush/BrushStroke.h"
#include "core/LayerOps.h"
#include "tools/Tool.h"

namespace gx {

// Shared options of the selection tools. Shift adds, Alt subtracts, Shift+Alt intersects.
struct SelectionOptions {
    int mode = 0;  // ops::SelectMode
    bool antialias = true;
    double feather = 0;
};
SelectionOptions& selectionOptions();
QWidget* createSelectionOptions(QWidget* parent, bool showAntialias = true);
ops::SelectMode selectModeFor(Qt::KeyboardModifiers m);

class ShapeSelectTool : public Tool {
    Q_OBJECT
public:
    ShapeSelectTool(const QString& id, const QString& name, const QString& icon, bool ellipse, QObject* p = nullptr)
        : Tool(id, name, icon, p), m_ellipse(ellipse) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QWidget* createOptions(QWidget* parent) override { return createSelectionOptions(parent); }
    bool isBusy() const override { return m_drag; }

private:
    QRectF rectNow() const;
    bool m_ellipse;
    bool m_drag = false;
    QPointF m_a, m_b;
    Qt::KeyboardModifiers m_mods;
    QPointer<CanvasView> m_view;
};

class RectSelectTool : public ShapeSelectTool {
    Q_OBJECT
public:
    explicit RectSelectTool(QObject* p = nullptr) : ShapeSelectTool("select-rect", "Rectangle selection", "select-rect", false, p) {}
};
class EllipseSelectTool : public ShapeSelectTool {
    Q_OBJECT
public:
    explicit EllipseSelectTool(QObject* p = nullptr) : ShapeSelectTool("select-ellipse", "Ellipse selection", "select-ellipse", true, p) {}
};

// Freehand lasso; click without dragging adds polygon points (double-click or Enter to close).
class LassoSelectTool : public Tool {
    Q_OBJECT
public:
    explicit LassoSelectTool(QObject* p = nullptr) : Tool("lasso", "Lasso selection", "lasso", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void hover(const PointerEvent& e) override;
    void doubleClick(const PointerEvent& e) override;
    bool keyPress(QKeyEvent* e, CanvasView* v) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QWidget* createOptions(QWidget* parent) override { return createSelectionOptions(parent); }
    bool isBusy() const override { return m_drag; }
    bool hasPending() const override { return m_poly.size() > 0; }
    void commitPending() override { finish(); }
    void cancelPending() override { m_poly.clear(); Q_EMIT overlayChanged(); }

protected:
    virtual void apply(CanvasView* v, const QPolygonF& poly, Qt::KeyboardModifiers mods);
    void finish();
    QPolygonF m_poly;
    QPointF m_hover;
    bool m_drag = false;
    bool m_dragged = false;
    Qt::KeyboardModifiers m_mods;
    QPointer<CanvasView> m_view;
};

class MagicWandTool : public Tool {
    Q_OBJECT
public:
    explicit MagicWandTool(QObject* p = nullptr) : Tool("magic-wand", "Magic wand", "magic-wand", p) {}
    void press(const PointerEvent& e) override;
    QWidget* createOptions(QWidget* parent) override;

    int tolerance = 32;
    bool sampleMerged = true;
    bool contiguous = true;
};

// Paints the selection with a brush (Alt erases it).
class SelectionBrushTool : public Tool {
    Q_OBJECT
public:
    explicit SelectionBrushTool(QObject* p = nullptr) : Tool("select-brush", "Selection pen", "select-brush", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    bool showsBrushOutline() const override { return true; }
    double outlineDiameter() const override { return size; }
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_drag; }

    double size = 30;
    double hardness = 0.8;

private:
    void dab(const QPointF& p, double pressure);
    bool m_drag = false;
    bool m_erase = false;
    QPointF m_last;
    double m_residual = 0;
    QPointer<CanvasView> m_view;
    QRect m_dirty;
};

// Flood fill helper shared by the wand and the bucket.
MaskSurface floodFill(Document* doc, const QPoint& seed, int tolerance, bool sampleMerged, bool contiguous, const Layer* layer,
                      QString* error);

// Where the fill looks for borders.
enum class FillSource { AllVisible = 0, CurrentLayer = 1, ReferenceLayers = 2 };
// gapClose: breaks in the borders up to this many px are treated as closed.
MaskSurface floodFillEx(Document* doc, const QPoint& seed, int tolerance, FillSource source, bool contiguous, const Layer* layer,
                        int gapClose, QString* error);

}  // namespace gx
