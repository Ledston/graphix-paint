#pragma once
#include <QImage>
#include <QRect>
#include <functional>
#include <vector>

#include "core/Tile.h"

namespace gx {

// Gaussian blur approximated by three box blurs. Interleaved channels, in place.
void gaussianBlur(float* data, int w, int h, int channels, float sigma);

// Squared Euclidean distance to the nearest "inside" pixel (Felzenszwalb).
// inside[i] != 0 marks seeds. Output distances in pixels squared.
void distanceTransform(const std::vector<uint8_t>& inside, int w, int h, std::vector<float>& dist2);

// Reading/writing surfaces as premultiplied float RGBA buffers
void readFloat(const RasterSurface& s, const QRect& r, float* out);
void writeFloat(RasterSurface& s, const QRect& r, const float* in, bool dither = false);
void readMask(const MaskSurface& s, const QRect& r, float* out);  // 0..1

// QImage (ARGB32_Premultiplied) <-> surfaces
QImage surfaceToImage(const RasterSurface& s, const QRect& r);
void imageToSurface(const QImage& img, RasterSurface& s, const QPoint& at);
QImage maskToImage(const MaskSurface& s, const QRect& r);  // Grayscale8
void imageToMask(const QImage& gray, MaskSurface& s, const QPoint& at);

// Bilinear sample of a premultiplied ARGB32 image, outside = transparent.
void sampleBilinear(const QImage& img, float x, float y, float* out);

// Runs fn(tileKey) for every tile in range using the global thread pool, blocking.
void parallelForTiles(const QVector<qint64>& keys, const std::function<void(qint64)>& fn);
void parallelFor(int n, const std::function<void(int)>& fn);

}  // namespace gx
