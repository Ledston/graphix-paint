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
    Qt::KeyboardModifiers m_mods;  // at the press: they choose add / subtract
    bool m_square = false;
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
    bool sampleMerged = true;  // (old setting, kept for the scenarios) see source
    int source = 0;            // FillSource: all visible, current layer, reference layers
    bool contiguous = true;
    int gapClose = 0;          // px: small breaks in the line art count as closed
    int expand = 1;            // px: grows under the soft edge of the line art
};

// Grows (px > 0) or shrinks (px < 0) a mask by a distance, inside `limit`.
void growMask(MaskSurface& m, int px, const QRect& limit);

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
    double hardness = 0.9;
    bool pressureSize = false;  // the pen pressure changes the size (off: exactly the circle shown)
    bool showTint = true;       // the selection shown in color while this tool is used

private:
    void dab(const QPointF& p, double pressure);
    double m_lastPressure = 1;
    bool m_drag = false;
    bool m_erase = false;
    QPointF m_last;
    double m_residual = 0;
    QPointer<CanvasView> m_view;
    QRect m_dirty;
};

// Smart selection (like Photoshop's quick selection): paint over an object and the selection
// grows over the colors painted on, but stops at its edges and at the lines of the line art.
// Strokes add to the selection; Ctrl or Alt take away.
class SmartSelectTool : public Tool {
    Q_OBJECT
public:
    explicit SmartSelectTool(QObject* p = nullptr) : Tool("smart-select", "Smart selection", "smartselect", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    bool showsBrushOutline() const override { return true; }
    double outlineDiameter() const override { return size; }
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_drag; }

    double size = 40;
    int tolerance = 36;   // how different a color may be from the colors painted on
    double reach = 3;     // how far from the brush the selection may grow (times the brush radius)
    int source = 0;       // FillSource: all visible, current layer, reference layers
    int expand = 1;       // px under the soft edge of the lines
    bool showTint = true;

private:
    void dab(const QPointF& c);
    bool m_drag = false;
    bool m_subtract = false;
    QPointF m_last;
    double m_residual = 0;
    QPointer<CanvasView> m_view;
    QRect m_srcRect;                  // the pixels read once per stroke
    std::vector<quint32> m_src;
    std::vector<quint32> m_samples;   // colors painted on so far
    MaskSurface m_before{0};          // the selection when the stroke began (for subtracting / combining)
    MaskSurface m_stroke{0};          // what this stroke found
};

// Flood fill helper shared by the wand and the bucket.
MaskSurface floodFill(Document* doc, const QPoint& seed, int tolerance, bool sampleMerged, bool contiguous, const Layer* layer,
                      QString* error);

// Where the fill looks for borders.
enum class FillSource { AllVisible = 0, CurrentLayer = 1, ReferenceLayers = 2 };
// gapClose: breaks in the borders up to this many px are treated as closed.
// The pixels the fill looks at (premultiplied ARGB, area.width() per row). False on error.
bool readFillSource(Document* doc, const QRect& area, FillSource source, const Layer* layer, std::vector<quint32>& px, QString* error);
MaskSurface floodFillEx(Document* doc, const QPoint& seed, int tolerance, FillSource source, bool contiguous, const Layer* layer,
                        int gapClose, QString* error);

}  // namespace gx
