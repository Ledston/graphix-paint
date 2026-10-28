#pragma once
#include <QPointer>
#include <QVector>

#include "core/Tile.h"
#include "tools/Tool.h"

namespace gx {

// Moves the current layer (folders move everything inside). With a selection on a
// pixel layer, only the selected pixels move. Arrow keys nudge by 1 px (Shift: 10 px).
class MoveTool : public Tool {
    Q_OBJECT
public:
    explicit MoveTool(QObject* p = nullptr) : Tool("move", "Move", "move", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    bool keyPress(QKeyEvent* e, CanvasView* v) override;
    QCursor cursor(CanvasView*) const override { return Qt::SizeAllCursor; }
    bool isBusy() const override { return m_active; }

    static void translateLayer(Layer* l, const QPoint& d);

private:
    void setOffsets(const QPoint& d);
    QRect targetsBounds() const;
    void rebuildSelectionPreview(const QPoint& d);

    QPointer<CanvasView> m_view;
    QVector<quint32> m_targets;
    QPointF m_start;
    QPoint m_delta;
    bool m_active = false;
    bool m_selMode = false;
    RasterSurface m_float, m_base;
    MaskSurface m_selOrig{0};
    QRect m_selRect, m_lastPreview;
};

}  // namespace gx
