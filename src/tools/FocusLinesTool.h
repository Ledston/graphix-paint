#pragma once
#include <QPointer>
#include <QTimer>
#include <functional>

#include "core/FocusLines.h"
#include "tools/Tool.h"

namespace gx {

class RasterLayer;
class ValueBar;

// Manga focus lines and speed lines on their own layer.
// Drag on the canvas: focus lines — from the center outward (the empty middle);
// speed lines — the direction. While the lines layer is selected, its settings
// (and a new drag) change it; painting on the layer fixes it.
class FocusLinesTool : public Tool {
    Q_OBJECT
public:
    explicit FocusLinesTool(int kind, QObject* parent = nullptr);
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_dragging; }

    // The lines layer of this kind that the settings change now (null if none).
    RasterLayer* editedLayer() const;
    // Makes lines with the current settings, or changes the selected lines layer.
    // center/size or direction come from a drag (canvas coordinates).
    void applyDrag(Document* d, QPointF from, QPointF to, bool shift);
    FocusLines& settings() { return m_params; }
    void settingsChanged();  // re-draws the selected lines layer (after a short pause)

private:
    FocusLines fromDrag(Document* d, QPointF from, QPointF to, bool shift) const;
    void rerender();
    void syncOptions();
    void saveDefaults();

    int m_kind;
    FocusLines m_params;
    bool m_dragging = false;
    QPointF m_from, m_to;
    bool m_shift = false;
    QPointer<CanvasView> m_view;
    QTimer m_rerender;
    QPointer<QWidget> m_opts;
    std::function<void()> m_sync;
};

}  // namespace gx
