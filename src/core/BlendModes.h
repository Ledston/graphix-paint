#pragma once
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace gx {

// Values are stored in files: never renumber, only append.
enum class BlendMode : int {
    Normal = 0,
    Multiply = 1,
    Screen = 2,
    Overlay = 3,
    Shine = 4,       // SAI "Luminosity": additive glow
    Shade = 5,       // SAI "Shade": subtractive darkening
    ShineShade = 6,  // SAI "Lum & Shade"
    Darken = 7,
    Lighten = 8,
    ColorDodge = 9,
    ColorBurn = 10,
    LinearDodge = 11,
    LinearBurn = 12,
    HardLight = 13,
    SoftLight = 14,
    VividLight = 15,
    LinearLight = 16,
    PinLight = 17,
    HardMix = 18,
    Difference = 19,
    Exclusion = 20,
    Subtract = 21,
    Divide = 22,
    Hue = 23,
    Saturation = 24,
    Color = 25,
    Luminosity = 26,
    DarkerColor = 27,
    LighterColor = 28,
    Dissolve = 29,   // random pixels: partly transparent paint becomes a speckle of solid pixels
    Count
};

struct BlendModeInfo {
    BlendMode mode;
    const char* name;     // English UI name (translated by T())
    const char* psdKey;   // 4-char PSD key
    bool separator;       // draw a separator before this item in menus
};

const BlendModeInfo* blendModeInfos();  // array of int(BlendMode::Count)
QString blendModeName(BlendMode m);
// Dissolve: turns the partly transparent pixels of a premultiplied row starting at canvas (x, y)
// into fully opaque or fully transparent ones (repeatable random pattern).
void dissolveRow(float* p, int n, int x, int y);
QByteArray blendModePsdKey(BlendMode m);
BlendMode blendModeFromPsdKey(const QByteArray& key, bool* passThrough = nullptr);
// Order modes appear in the UI (SAI modes first, then Photoshop groups)
QList<BlendMode> blendModeUiOrder();

// Blends n premultiplied RGBA float pixels of src onto dst.
// src alpha must already contain opacity and masks.
void blendRow(BlendMode mode, float* dst, const float* src, int n);

// Same, but keeps dst alpha (used for clipping onto a base / alpha lock).
void blendRowAtop(BlendMode mode, float* dst, const float* src, int n);

}  // namespace gx
