#pragma once
#include <QColor>
#include <QJsonObject>
#include <QPointer>
#include <QPolygonF>
#include <QVector>

#include "tools/Tool.h"

namespace gx {

class Document;
class RasterLayer;

// Comic panel frames on their own layer (like Clip Studio's frame border tools).
// Drag a rectangle: a new frame. Cut mode: drag a line across frames to split them with a
// gutter. "Page" makes a whole page of frames inside the margins. The frames stay
// editable while the frames layer is not painted on.
struct FrameSet {
    QVector<QPolygonF> frames;
    double width = 6;     // border, px
    QColor color = Qt::black;
    QJsonObject toJson() const;
    static FrameSet fromJson(const QJsonObject& o);
    static bool isFrames(const QJsonObject& generator) { return generator.value("type").toString() == "frames"; }
    void renderInto(Document& doc, RasterLayer& layer) const;
    // splits every frame the line crosses, leaving `gutter` px between the halves
    int cut(QPointF a, QPointF b, double gutter);
    // a page: rows of frames inside margins; columns[i] = frames in row i
    static FrameSet page(const QRectF& area, const QVector<int>& columns, double gutter);
};

class FramesTool : public Tool {
    Q_OBJECT
public:
    explicit FramesTool(QObject* parent = nullptr) : Tool("frames", "Panel frames", "frames", parent) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_dragging; }

    int mode = 0;          // 0 draw a frame, 1 cut frames
    double width = 6;
    double gutter = 40;
    double margin = 0.07;  // page margins, part of the smaller side
    // Adds a frame / cuts / makes a page on the current frames layer (or a new one).
    void addFrame(Document* d, const QRectF& r);
    void cutFrames(Document* d, QPointF a, QPointF b);
    void makePage(Document* d, const QVector<int>& columns);
    RasterLayer* framesLayer(Document* d) const;  // the current layer when it holds editable frames

private:
    void commit(Document* d, const FrameSet& fs, const QString& undo);
    bool m_dragging = false;
    QPointF m_a, m_b;
    QPointer<CanvasView> m_view;
};

}  // namespace gx
