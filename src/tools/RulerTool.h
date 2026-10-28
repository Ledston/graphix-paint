#pragma once
#include <QPointer>

#include "tools/Tool.h"

namespace gx {

// Edits the perspective ruler (vanishing points) and the symmetry axis on the canvas.
class RulerTool : public Tool {
    Q_OBJECT
public:
    explicit RulerTool(QObject* p = nullptr) : Tool("ruler", "Rulers", "perspective", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_drag >= 0; }

private:
    int m_drag = -1;  // 0..2 vanishing points, 10 symmetry center, 11 symmetry angle
    QPointF m_off;
    QPointer<CanvasView> m_view;
    QPointer<QWidget> m_opts;
};

}  // namespace gx
