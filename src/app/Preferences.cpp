#include "app/Preferences.h"

#include <QJsonDocument>
#include <QJsonArray>

#include "core/Adjustments.h"

namespace gx {

void Preferences::load(QSettings& s) {
    s.beginGroup("prefs");
    theme = s.value("theme", theme).toString();
    language = s.value("language", language).toString();
    uiFontSize = s.value("uiFontSize", uiFontSize).toInt();
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
    showBrushOutline = s.value("showBrushOutline", showBrushOutline).toBool();
    crosshair = s.value("crosshair", crosshair).toBool();
    hideCursorWhilePainting = s.value("hideCursorWhilePainting", hideCursorWhilePainting).toBool();
    tabletApi = s.value("tabletApi", tabletApi).toString();
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
    s.endGroup();
}

void Preferences::save(QSettings& s) const {
    s.beginGroup("prefs");
    s.setValue("theme", theme);
    s.setValue("language", language);
    s.setValue("uiFontSize", uiFontSize);
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
    s.setValue("showBrushOutline", showBrushOutline);
    s.setValue("crosshair", crosshair);
    s.setValue("hideCursorWhilePainting", hideCursorWhilePainting);
    s.setValue("tabletApi", tabletApi);
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
    s.endGroup();
}

}  // namespace gx
