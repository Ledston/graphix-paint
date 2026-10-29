#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPolygonF>
#include <QRect>
#include <QVector>

namespace gx {

class Document;
class RasterLayer;

// Manga focus lines (from the edges toward a center) and speed lines (parallel streaks).
// The settings are kept on the layer, so the lines can be changed later.
struct FocusLines {
    enum Kind { Focus = 0, Speed = 1 };
    int kind = Focus;
    QRect area;                // where the lines are drawn (the selection or the whole picture)
    QPointF center;            // focus: the middle of the empty ellipse
    double rx = 150, ry = 100; // focus: the empty ellipse
    double angle = 0;          // speed: direction in degrees
    int count = 120;           // number of lines
    double width = 6;          // px at the wide end
    double widthJitter = 0.5;  // 0..1
    double lengthJitter = 0.5; // 0..1: how unevenly the lines end
    double spread = 0.6;       // 0..1: how unevenly the lines are spaced
    double gaps = 0.2;         // 0..1: share of empty gaps between groups of lines
    double length = 0.6;       // speed: line length as a share of the area
    QColor color = Qt::black;
    quint32 seed = 1;

    QJsonObject toJson() const;
    static FocusLines fromJson(const QJsonObject& o);
    static bool isFocusLines(const QJsonObject& generator) { return generator.value("type").toString() == "lines"; }

    // Every line as a filled polygon (canvas coordinates), the same for the same settings.
    QVector<QPolygonF> shapes() const;
    // Draws the lines into the layer (replacing its pixels), inside the document selection
    // if there is one. No undo step.
    void renderInto(Document& doc, RasterLayer& layer) const;
};

}  // namespace gx
