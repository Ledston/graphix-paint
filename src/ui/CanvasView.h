#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QPainterPath>
#include <QPointer>
#include <QTimer>
#include <QTransform>
#include <QWidget>
#include <memory>

#include "tools/Tool.h"

namespace gx {

class Document;
class Projection;

// Shows a document and turns pen / mouse input into tool events.
// Rendering is done on the CPU with all cores (like SAI): the screen is drawn
// from the projection pyramid, and only changed parts are redrawn while painting.
class CanvasView : public QWidget {
    Q_OBJECT
public:
    explicit CanvasView(std::shared_ptr<Document> doc, QWidget* parent = nullptr, bool scratchpad = false);
    ~CanvasView() override;

    Document* doc() const { return m_doc.get(); }
    std::shared_ptr<Document> docPtr() const { return m_doc; }
    bool isScratchpad() const { return m_scratch; }

    // ---- view transform ----
    QTransform canvasToWidget() const;
    QTransform widgetToCanvas() const { return canvasToWidget().inverted(); }
    QPointF toCanvas(const QPointF& w) const { return widgetToCanvas().map(w); }
    QPointF toWidget(const QPointF& c) const { return canvasToWidget().map(c); }
    double zoom() const { return m_zoom; }
    double rotation() const { return m_rot; }
    bool isFlipped() const { return m_flip; }
    QPointF focus() const { return m_focus; }
    void setZoom(double z, const QPointF& anchorWidget);
    void setZoom(double z) { setZoom(z, QPointF(width() / 2.0, height() / 2.0)); }
    void zoomBy(double factor, const QPointF& anchorWidget);
    void zoomIn();
    void zoomOut();
    void setRotation(double deg);
    void rotateBy(double deg) { setRotation(m_rot + deg); }
    void setFlipped(bool f);
    void fitToWindow();
    void actualPixels();
    void resetView();
    void panBy(const QPointF& widgetDelta);
    void centerOn(const QPointF& canvasPos);
    QPolygonF visibleCanvas() const;

    QPointF cursorCanvasPos() const { return toCanvas(m_hover); }
    bool cursorInside() const { return m_inside; }
    void refreshCursor();
    void invalidateAll();

    // While true, views paint the picture for a screen recording (layers hidden from
    // recordings are left out). Used around MainWindow::grab() by the recorder.
    static void setRecordingPaint(bool on) { s_recordingPaint = on; }
    // Frees the second picture cache once recording stops.
    void dropRecordingCache() { m_rec = ViewCache(); }

Q_SIGNALS:
    void viewChanged();
    void cursorMoved(const QPointF& canvasPos);
    void activated(CanvasView* self);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void tabletEvent(QTabletEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    bool event(QEvent* e) override;

private:
    PointerEvent makeEvent(const QPointF& wpos, double pressure, Qt::MouseButton b, Qt::MouseButtons bs,
                           Qt::KeyboardModifiers m, double timeMs, bool tablet) const;
    struct ViewCache {
        QImage img;
        QTransform xf;
        bool valid = false;
        bool complete = false;
        QRect dirty;  // physical pixels
    };
    QTransform physicalTransform() const;
    void ensureCache(ViewCache& vc, Projection* proj);
    bool renderRect(ViewCache& vc, const Projection* proj, const QRect& physRect, int level);
    void markCanvasDirty(const QRect& canvasRect);
    void paintOverlays(QPainter& p);
    void drawSelection(QPainter& p);
    void drawGuides(QPainter& p);
    void drawCompositionGrid(QPainter& p);
    void drawBrushOutline(QPainter& p);
    void rebuildSelectionPath();
    // the selection shown in color (selection pen), at most 2048 px on its long side
    void updateSelectionTint(const QRect& canvasRect);
    bool wantsSelectionTint() const;
    void viewTransformChanged();

    std::shared_ptr<Document> m_doc;
    bool m_scratch = false;

    QPointF m_focus;
    double m_zoom = 1.0, m_rot = 0.0;
    bool m_flip = false;

    ViewCache m_main;  // what the screen shows
    ViewCache m_rec;   // the same for screen recordings (only while recording the window)
    static bool s_recordingPaint;
    bool m_moreWork = false;
    quint32 m_frame = 0;

    QPointF m_hover;
    bool m_inside = false;
    bool m_tabletDown = false;
    bool m_mouseDown = false;
    QElapsedTimer m_sinceTablet;
    Qt::MouseButton m_tabletButton = Qt::LeftButton;

    QPainterPath m_selPath;
    bool m_selDirty = true;
    bool m_selPainting = false;  // a selection pen stroke is going on: the ants wait for its end
    QImage m_selTint;
    double m_selTintScale = 1;
    bool m_selTintValid = false;
    QTimer m_ants;
    int m_antsPhase = 0;
};

}  // namespace gx
