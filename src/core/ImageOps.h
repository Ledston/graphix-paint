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
void writeMask(MaskSurface& s, const QRect& r, const float* in);  // 0..1

// QImage (ARGB32_Premultiplied) <-> surfaces
QImage surfaceToImage(const RasterSurface& s, const QRect& r);
void imageToSurface(const QImage& img, RasterSurface& s, const QPoint& at);
QImage maskToImage(const MaskSurface& s, const QRect& r);  // Grayscale8
void imageToMask(const QImage& gray, MaskSurface& s, const QPoint& at);

// Bilinear sample of a premultiplied ARGB32 image, outside = transparent.
void sampleBilinear(const QImage& img, float x, float y, float* out);

// Repeatable noise in -0.5..0.5 for pixel (x, y), channel c. size > 1 gives soft, larger grain.
float grainNoise(double x, double y, unsigned channel, unsigned seed, double size = 1.0);
// Halftone (screentone) coverage 0..1 at canvas point (x, y) for a tone darkness 0..1.
// shape: 0 round dots, 1 lines, 2 square dots.
float halftoneCoverage(double x, double y, double cell, double angleDeg, int shape, float darkness);

// Runs fn(tileKey) for every tile in range using the global thread pool, blocking.
void parallelForTiles(const QVector<qint64>& keys, const std::function<void(qint64)>& fn);
void parallelFor(int n, const std::function<void(int)>& fn);

}  // namespace gx
