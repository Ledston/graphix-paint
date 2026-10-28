#pragma once
// Pixel conversions. Layer storage is 8-bit premultiplied ARGB (QImage ARGB32_Premultiplied);
// all mixing math happens in premultiplied float RGBA.

#include <QColor>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace gx {

constexpr float kInv255 = 1.0f / 255.0f;

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// quint32 ARGB (premultiplied) -> float RGBA (premultiplied)
inline void unpack(quint32 p, float* o) {
    o[0] = float((p >> 16) & 255u) * kInv255;
    o[1] = float((p >> 8) & 255u) * kInv255;
    o[2] = float(p & 255u) * kInv255;
    o[3] = float(p >> 24) * kInv255;
}

inline quint32 pack(const float* f) {
    float a = clamp01(f[3]);
    float r = std::min(clamp01(f[0]), a), g = std::min(clamp01(f[1]), a), b = std::min(clamp01(f[2]), a);
    return (quint32(a * 255.f + 0.5f) << 24) | (quint32(r * 255.f + 0.5f) << 16) | (quint32(g * 255.f + 0.5f) << 8) |
           quint32(b * 255.f + 0.5f);
}

// Pack with ordered dithering to avoid banding in gradients and soft brushes.
inline quint32 packDither(const float* f, int x, int y) {
    static const float bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    float d = (bayer[((y & 3) << 2) | (x & 3)] + 0.5f) / 16.f;  // 0..1
    float a = clamp01(f[3]);
    auto q = [&](float v) { return quint32(std::min(255.f, std::floor(v * 255.f + d))); };
    quint32 qa = q(a);
    float fa = qa * kInv255;
    float r = std::min(clamp01(f[0]), fa), g = std::min(clamp01(f[1]), fa), b = std::min(clamp01(f[2]), fa);
    return (qa << 24) | (std::min(q(r), qa) << 16) | (std::min(q(g), qa) << 8) | std::min(q(b), qa);
}

inline quint32 premulFromColor(const QColor& c, float alpha = 1.f) {
    float f[4] = {float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF()) * alpha};
    f[0] *= f[3]; f[1] *= f[3]; f[2] *= f[3];
    return pack(f);
}

inline QColor colorFromPremul(quint32 p) {
    int a = int(p >> 24);
    if (a == 0) return QColor(0, 0, 0, 0);
    auto un = [a](quint32 v) { return std::min(255, int((v * 255u + quint32(a) / 2) / quint32(a))); };
    return QColor(un((p >> 16) & 255u), un((p >> 8) & 255u), un(p & 255u), a);
}

inline float luminance(float r, float g, float b) { return 0.3f * r + 0.59f * g + 0.11f * b; }

// Straight color as float rgb
struct Rgb { float r, g, b; };
inline Rgb rgbOf(const QColor& c) { return {float(c.redF()), float(c.greenF()), float(c.blueF())}; }

}  // namespace gx
