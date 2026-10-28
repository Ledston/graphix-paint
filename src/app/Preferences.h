#pragma once
#include <QColor>
#include <QPointF>
#include <QSettings>
#include <QString>
#include <QVector>

namespace gx {

struct Preferences {
    // Appearance
    QString theme = "dark";      // dark, gray, light
    QString language = "auto";   // auto, en, ru
    int uiFontSize = 0;          // 0 = system default

    // Canvas
    bool checkerboard = true;
    QColor checker1 = QColor(204, 204, 204);
    QColor checker2 = QColor(255, 255, 255);
    int checkerSize = 8;
    QColor paperColor = Qt::white;   // when the checkerboard is off
    QColor workspaceDark = QColor(48, 49, 54);
    QColor workspaceGray = QColor(128, 128, 128);
    QColor workspaceLight = QColor(222, 223, 226);
    bool pixelatedZoomIn = true;     // nearest-neighbour above 200% like SAI
    bool showPixelGrid = false;      // at very high zoom

    // Cursor
    bool showBrushOutline = true;
    bool crosshair = true;
    bool hideCursorWhilePainting = false;

    // Input
    QString tabletApi = "ink";       // ink (Windows Ink) or wintab
    double mousePressure = 1.0;
    bool rightClickEyedropper = true;
    bool wheelZooms = true;
    QVector<QPointF> pressureCurve{QPointF(0, 0), QPointF(1, 1)};

    // Stabilizer
    int stabilizerMode = 1;          // see Stabilizer::Mode
    int stabilizerLevel = 3;
    int pressureSmoothing = 0;

    // Performance & safety
    int undoLimit = 200;
    int autosaveMinutes = 3;         // 0 = off
    int projectionCacheMB = 1024;

    void load(QSettings& s);
    void save(QSettings& s) const;
};

}  // namespace gx
