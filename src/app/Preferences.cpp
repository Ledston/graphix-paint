#include "app/Preferences.h"

#include <QJsonDocument>
#include <QJsonArray>

#include "core/Adjustments.h"

namespace gx {

void Preferences::load(QSettings& s) {
    s.beginGroup("prefs");
    theme = s.value("theme", theme).toString();
    language = s.value("language", language).toString();
    const int version = s.value("version", 1).toInt();
    uiFontSize = s.value("uiFontSize", uiFontSize).toInt();
    iconSize = std::clamp(s.value("iconSize", iconSize).toInt(), 16, 40);
    checkerboard = s.value("checkerboard", checkerboard).toBool();
    checker1 = QColor(s.value("checker1", checker1.name()).toString());
    checker2 = QColor(s.value("checker2", checker2.name()).toString());
    checkerSize = std::clamp(s.value("checkerSize", checkerSize).toInt(), 2, 64);
    paperColor = QColor(s.value("paperColor", paperColor.name()).toString());
    workspaceDark = QColor(s.value("workspaceDark", workspaceDark.name()).toString());
    workspaceGray = QColor(s.value("workspaceGray", workspaceGray.name()).toString());
    workspaceLight = QColor(s.value("workspaceLight", workspaceLight.name()).toString());
    pixelatedZoomIn = s.value("pixelatedZoomIn", pixelatedZoomIn).toBool();
    showPixelGrid = s.value("showPixelGrid", showPixelGrid).toBool();
    zoomStepPercent = std::clamp(s.value("zoomStepPercent", zoomStepPercent).toInt(), 5, 100);
    rotateStepDeg = std::clamp(s.value("rotateStepDeg", rotateStepDeg).toInt(), 1, 90);
    invertWheel = s.value("invertWheel", invertWheel).toBool();
    showGrid = s.value("showGrid", showGrid).toBool();
    gridSize = std::clamp(s.value("gridSize", gridSize).toInt(), 2, 10000);
    gridSubdivisions = std::clamp(s.value("gridSubdivisions", gridSubdivisions).toInt(), 1, 20);
    gridColor = QColor(s.value("gridColor", gridColor.name()).toString());
    showThirds = s.value("showThirds", showThirds).toBool();
    showGoldenRatio = s.value("showGoldenRatio", showGoldenRatio).toBool();
    showCenterLines = s.value("showCenterLines", showCenterLines).toBool();
    showBrushOutline = s.value("showBrushOutline", showBrushOutline).toBool();
    crosshair = s.value("crosshair", crosshair).toBool();
    hideCursorWhilePainting = s.value("hideCursorWhilePainting", hideCursorWhilePainting).toBool();
    tabletApi = s.value("tabletApi", tabletApi).toString();
    if (version < 2) tabletApi = "wintab";  // 1.0 defaulted to Windows Ink; Wintab works like SAI
    if (tabletApi != "ink") tabletApi = "wintab";
    pressureThreshold = std::clamp(s.value("pressureThreshold", pressureThreshold).toDouble(), 0.0, 0.5);
    eraserEndUsesEraser = s.value("eraserEndUsesEraser", eraserEndUsesEraser).toBool();
    ctrlWheelBrushSize = s.value("ctrlWheelBrushSize", ctrlWheelBrushSize).toBool();
    brushStepPercent = std::clamp(s.value("brushStepPercent", brushStepPercent).toInt(), 2, 100);
    mousePressure = std::clamp(s.value("mousePressure", mousePressure).toDouble(), 0.05, 1.0);
    rightClickEyedropper = s.value("rightClickEyedropper", rightClickEyedropper).toBool();
    wheelZooms = s.value("wheelZooms", wheelZooms).toBool();
    QByteArray pc = s.value("pressureCurve").toByteArray();
    if (!pc.isEmpty()) pressureCurve = pointsFromJson(QJsonDocument::fromJson(pc).array());
    stabilizerMode = std::clamp(s.value("stabilizerMode", stabilizerMode).toInt(), 0, 3);
    stabilizerLevel = std::clamp(s.value("stabilizerLevel", stabilizerLevel).toInt(), 0, 30);
    pressureSmoothing = std::clamp(s.value("pressureSmoothing", pressureSmoothing).toInt(), 0, 20);
    undoLimit = std::clamp(s.value("undoLimit", undoLimit).toInt(), 10, 2000);
    autosaveMinutes = std::clamp(s.value("autosaveMinutes", autosaveMinutes).toInt(), 0, 120);
    projectionCacheMB = std::clamp(s.value("projectionCacheMB", projectionCacheMB).toInt(), 128, 16384);
    threads = std::clamp(s.value("threads", threads).toInt(), 0, 256);
    defaultFormat = s.value("defaultFormat", defaultFormat).toString() == "psd" ? "psd" : "gxp";
    jpegQuality = std::clamp(s.value("jpegQuality", jpegQuality).toInt(), 1, 100);
    recentCount = std::clamp(s.value("recentCount", recentCount).toInt(), 0, 40);
    newCanvasOnStart = s.value("newCanvasOnStart", newCanvasOnStart).toBool();
    texturesDir = s.value("texturesDir", texturesDir).toString();
    s.endGroup();
}

void Preferences::save(QSettings& s) const {
    s.beginGroup("prefs");
    s.setValue("theme", theme);
    s.setValue("language", language);
    s.setValue("version", 2);
    s.setValue("uiFontSize", uiFontSize);
    s.setValue("iconSize", iconSize);
    s.setValue("checkerboard", checkerboard);
    s.setValue("checker1", checker1.name());
    s.setValue("checker2", checker2.name());
    s.setValue("checkerSize", checkerSize);
    s.setValue("paperColor", paperColor.name());
    s.setValue("workspaceDark", workspaceDark.name());
    s.setValue("workspaceGray", workspaceGray.name());
    s.setValue("workspaceLight", workspaceLight.name());
    s.setValue("pixelatedZoomIn", pixelatedZoomIn);
    s.setValue("showPixelGrid", showPixelGrid);
    s.setValue("zoomStepPercent", zoomStepPercent);
    s.setValue("rotateStepDeg", rotateStepDeg);
    s.setValue("invertWheel", invertWheel);
    s.setValue("showGrid", showGrid);
    s.setValue("gridSize", gridSize);
    s.setValue("gridSubdivisions", gridSubdivisions);
    s.setValue("gridColor", gridColor.name());
    s.setValue("showThirds", showThirds);
    s.setValue("showGoldenRatio", showGoldenRatio);
    s.setValue("showCenterLines", showCenterLines);
    s.setValue("showBrushOutline", showBrushOutline);
    s.setValue("crosshair", crosshair);
    s.setValue("hideCursorWhilePainting", hideCursorWhilePainting);
    s.setValue("tabletApi", tabletApi);
    s.setValue("pressureThreshold", pressureThreshold);
    s.setValue("eraserEndUsesEraser", eraserEndUsesEraser);
    s.setValue("ctrlWheelBrushSize", ctrlWheelBrushSize);
    s.setValue("brushStepPercent", brushStepPercent);
    s.setValue("mousePressure", mousePressure);
    s.setValue("rightClickEyedropper", rightClickEyedropper);
    s.setValue("wheelZooms", wheelZooms);
    s.setValue("pressureCurve", QJsonDocument(pointsToJson(pressureCurve).toArray()).toJson(QJsonDocument::Compact));
    s.setValue("stabilizerMode", stabilizerMode);
    s.setValue("stabilizerLevel", stabilizerLevel);
    s.setValue("pressureSmoothing", pressureSmoothing);
    s.setValue("undoLimit", undoLimit);
    s.setValue("autosaveMinutes", autosaveMinutes);
    s.setValue("projectionCacheMB", projectionCacheMB);
    s.setValue("threads", threads);
    s.setValue("defaultFormat", defaultFormat);
    s.setValue("jpegQuality", jpegQuality);
    s.setValue("recentCount", recentCount);
    s.setValue("newCanvasOnStart", newCanvasOnStart);
    s.setValue("texturesDir", texturesDir);
    s.endGroup();
}

}  // namespace gx
