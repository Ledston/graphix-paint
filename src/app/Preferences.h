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
    int iconSize = 20;           // tool and toolbar icons, px

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
    int zoomStepPercent = 20;        // one mouse wheel step
    int rotateStepDeg = 15;          // rotate view keys / Alt + wheel
    bool invertWheel = false;

    // Grid and guides (View > Grid and guides)
    bool showGrid = false;
    int gridSize = 100;              // px
    int gridSubdivisions = 4;
    QColor gridColor = QColor(80, 160, 255);
    bool showThirds = false;
    bool showGoldenRatio = false;
    bool showCenterLines = false;

    // Cursor
    bool showBrushOutline = true;
    bool crosshair = true;
    bool hideCursorWhilePainting = false;

    // Input
    QString tabletApi = "wintab";    // wintab (like SAI) or ink (Windows Ink)
    double pressureThreshold = 0.0;  // lighter touches count as no pressure
    double mousePressure = 1.0;
    bool rightClickEyedropper = true;  // right mouse button / pen side button
    bool eraserEndUsesEraser = true;   // the back of the pen erases
    bool wheelZooms = true;
    bool ctrlWheelBrushSize = true;
    int brushStepPercent = 15;       // [ and ] change the size by this much
    QVector<QPointF> pressureCurve{QPointF(0, 0), QPointF(1, 1)};

    // Stabilizer
    int stabilizerMode = 1;          // see Stabilizer::Mode
    int stabilizerLevel = 3;
    int pressureSmoothing = 0;

    // Files
    QString defaultFormat = "gxp";   // gxp or psd for "Save"
    int jpegQuality = 92;
    int recentCount = 12;
    bool newCanvasOnStart = true;
    QString texturesDir;             // empty = default place

    // Performance & safety
    int undoLimit = 200;
    int autosaveMinutes = 3;         // 0 = off
    int projectionCacheMB = 1024;
    int threads = 0;                 // 0 = all processor cores

    void load(QSettings& s);
    void save(QSettings& s) const;
};

}  // namespace gx
