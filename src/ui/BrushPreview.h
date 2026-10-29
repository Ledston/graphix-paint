#pragma once
#include <QColor>
#include <QImage>
#include <QTimer>
#include <QWidget>
#include <memory>

#include "brush/BrushSettings.h"

namespace gx {

class Document;
class BrushStroke;
class RasterLayer;

// A sample stroke of a brush: an S curve, light -> full -> light pressure.
// 'colors' adds stripes of color under the right half (to show mixing brushes).
// Big brushes are drawn smaller so the stroke fits.
QImage renderBrushPreview(const BrushSettings& b, const QSize& size, const QColor& fg, const QColor& bg, bool colors, double dpr = 1.0);

// The sample stroke as a widget; redraws shortly after the brush changes.
class BrushPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit BrushPreviewWidget(QWidget* parent = nullptr);
    void setBrush(const BrushSettings& b);
    QSize sizeHint() const override { return QSize(360, 120); }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    BrushSettings m_brush;
    QImage m_img;
    QTimer m_timer;
};

// A small sheet to try the brush with the pen, next to its settings.
class BrushTestPad : public QWidget {
    Q_OBJECT
public:
    explicit BrushTestPad(QWidget* parent = nullptr);
    ~BrushTestPad() override;
    void setBrush(const BrushSettings& b) { m_brush = b; }
    void clear();
    QSize sizeHint() const override { return QSize(360, 220); }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;

private:
    void ensureDoc();
    void begin(const QPointF& p, double pressure, double tx, double ty);
    void add(const QPointF& p, double pressure, double tx, double ty);
    void finish();
    QPointF toDoc(const QPointF& w) const;

    BrushSettings m_brush;
    std::shared_ptr<Document> m_doc;
    std::shared_ptr<RasterLayer> m_layer;
    std::unique_ptr<BrushStroke> m_stroke;
    QImage m_view;
    QRect m_dirty;
    bool m_tablet = false;
    double m_time = 0;
};

}  // namespace gx
