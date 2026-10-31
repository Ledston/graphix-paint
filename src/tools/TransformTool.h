#pragma once
#include <QImage>
#include <QPointer>
#include <QPolygonF>
#include <QTimer>
#include <memory>
#include <vector>

#include "core/Layer.h"
#include "tools/Tool.h"

namespace gx {

class Document;

// Lifts the pixels of a raster layer (inside the selection, or all of them) so a
// tool can show them transformed and paste them back.
struct FloatingPixels {
    bool active = false;
    QPointer<Document> doc;
    quint32 layerId = 0;
    QRect srcRect;
    QImage src;            // premultiplied, srcRect sized
    RasterSurface base;    // layer pixels with the lifted part removed
    MaskSurface selection{0};
    bool hadSelection = false;
    QRect lastPreview;

    bool begin(Document* d, RasterLayer* l, const QString& undoName, bool wholeCanvasIfEmpty, QString* error);
    void showImage(const QImage& img, const QPoint& at);  // preview = base + img
    void commit();
    void cancel();
    RasterLayer* layer() const;
};

class TransformTool : public Tool {
    Q_OBJECT
public:
    explicit TransformTool(QObject* p = nullptr);
    void activate() override;
    void deactivate() override;
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void hover(const PointerEvent& e) override;
    bool keyPress(QKeyEvent* e, CanvasView* v) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView*) const override;
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_dragMode != 0; }
    bool hasPending() const override { return m_active; }
    void commitPending() override { apply(); }
    void cancelPending() override { cancel(); }

    bool smooth = true;

    void flip(bool horizontal);
    void rotate90(bool cw);
    void reset();
    void apply();
    void cancel();

private:
    bool start();
    void updatePreview(bool final);
    QTransform currentTransform() const;
    int hitTest(const QPointF& widgetPos, CanvasView* v) const;  // 1..4 corners, 5..8 edges, 9 inside, 10 rotate, 0 none
    QPointF center() const;

    bool m_active = false;
    bool m_rasterMode = true;
    FloatingPixels m_float;
    // non-raster layers are transformed from a clean copy
    std::vector<std::pair<quint32, LayerPtr>> m_originals;  // one or several layers
    QPointer<Document> m_doc;
    quint32 m_layerId = 0;
    QRectF m_src;
    QPolygonF m_quad, m_startQuad;
    int m_dragMode = 0;
    int m_hover = 0;
    QPointF m_pressPos;
    Qt::KeyboardModifiers m_mods;
    QPointer<CanvasView> m_view;
    QTimer m_throttle;
};

class WarpTool : public Tool {
    Q_OBJECT
public:
    explicit WarpTool(QObject* p = nullptr);
    void activate() override;
    void deactivate() override;
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    bool keyPress(QKeyEvent* e, CanvasView* v) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView*) const override { return Qt::CrossCursor; }
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_drag >= 0; }
    bool hasPending() const override { return m_active; }
    void commitPending() override { apply(); }
    void cancelPending() override { cancel(); }

    int cells = 3;
    void apply();
    void cancel();
    void resetGrid();
    // Like Photoshop: a click on a grid line adds a line through that place (its point can be
    // dragged at once); Ctrl + click on a point removes the line it was added with.
    // u, v: place in the picture, 0..1. Return the index of the new point on (u, v), or -1.
    int addColumn(double u, int onRow);
    int addRow(double v, int onColumn);
    bool removeLineAt(int point);
    int columns() const { return m_nu; }
    int rows() const { return m_nv; }
    QPointF point(int i) const { return m_pts[size_t(i)]; }
    void setPoint(int i, QPointF p) { m_pts[size_t(i)] = p; render(true); }

private:
    bool start();
    void render(bool final);
    QPointF gridPoint(double u, double v) const;  // smooth surface through the control points (index space)
    QPointF sourceAt(double u, double v) const;   // where in the picture an index-space place comes from
    // the nearest place on a grid line to a widget point: row or column, and the index-space place
    bool hitLine(CanvasView* view, QPointF widgetPos, bool* horizontal, double* u, double* v) const;

    bool m_active = false;
    FloatingPixels m_float;
    std::vector<QPointF> m_pts;  // m_nu * m_nv control points, row by row
    int m_nu = 4, m_nv = 4;
    std::vector<double> m_us, m_vs;      // where the columns / rows are in the picture (0..1)
    std::vector<int> m_colAge, m_rowAge;  // 0 = from the grid setting, higher = added later by a click
    int m_drag = -1;
    QPointF m_pressPos, m_startPt;
    std::vector<QPointF> m_startPts;
    bool m_moveAll = false;
    QPointer<CanvasView> m_view;
    QTimer m_throttle;
};

class LiquifyTool : public Tool {
    Q_OBJECT
public:
    enum Mode { Push = 0, TwirlCW, TwirlCCW, Pinch, Bloat, Restore, Smooth };
    explicit LiquifyTool(QObject* p = nullptr);
    void activate() override;
    void deactivate() override;
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    bool keyPress(QKeyEvent* e, CanvasView* v) override;
    bool showsBrushOutline() const override { return true; }
    double outlineDiameter() const override { return size; }
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_drag; }
    bool hasPending() const override { return m_active; }
    void commitPending() override { apply(); }
    void cancelPending() override { cancel(); }

    int mode = Push;
    double size = 120;
    double strength = 0.5;
    void apply();
    void cancel();

private:
    bool start();
    void dab(const QPointF& p, const QPointF& delta, double pressure);
    void renderRect(const QRect& canvasRect);

    bool m_active = false;
    bool m_drag = false;
    FloatingPixels m_float;
    QRect m_region;      // canvas area covered by the displacement field
    int m_scale = 1;     // field cell size in pixels
    int m_fw = 0, m_fh = 0;
    std::vector<float> m_dx, m_dy;
    QImage m_out;        // region sized result
    QPointF m_last;
    QPointer<CanvasView> m_view;
};

}  // namespace gx
