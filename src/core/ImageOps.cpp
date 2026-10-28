#include "core/ImageOps.h"

#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <cmath>
#include <numeric>

#include "core/Pixel.h"

namespace gx {

namespace {

std::vector<int> boxesForGauss(float sigma, int n) {
    float wIdeal = std::sqrt((12.f * sigma * sigma / n) + 1.f);
    int wl = int(std::floor(wIdeal));
    if (wl % 2 == 0) wl--;
    int wu = wl + 2;
    float mIdeal = (12.f * sigma * sigma - n * wl * wl - 4.f * n * wl - 3.f * n) / (-4.f * wl - 4.f);
    int m = int(std::round(mIdeal));
    std::vector<int> sizes;
    for (int i = 0; i < n; ++i) sizes.push_back(i < m ? wl : wu);
    return sizes;
}

// Box blur of radius r along one axis. Zero padding outside (correct for premultiplied data).
void boxLine(const float* src, float* dst, int n, int stride, int ch, int r) {
    if (r <= 0) {
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < ch; ++c) dst[i * stride + c] = src[i * stride + c];
        return;
    }
    float inv = 1.f / float(2 * r + 1);
    float sum[4] = {0, 0, 0, 0};
    for (int i = 0; i <= std::min(r, n - 1); ++i)
        for (int c = 0; c < ch; ++c) sum[c] += src[i * stride + c];
    for (int i = 0; i < n; ++i) {
        for (int c = 0; c < ch; ++c) dst[i * stride + c] = sum[c] * inv;
        int add = i + r + 1, rem = i - r;
        if (add < n)
            for (int c = 0; c < ch; ++c) sum[c] += src[add * stride + c];
        if (rem >= 0)
            for (int c = 0; c < ch; ++c) sum[c] -= src[rem * stride + c];
    }
}

constexpr double kDtInf = 1e12;

void dt1d(const double* f, double* d, int n, std::vector<int>& v, std::vector<double>& z) {
    int k = 0;
    v[0] = 0;
    z[0] = -1e30;
    z[1] = 1e30;
    for (int q = 1; q < n; ++q) {
        double s = ((f[q] + double(q) * q) - (f[v[size_t(k)]] + double(v[size_t(k)]) * v[size_t(k)])) /
                   (2.0 * q - 2.0 * v[size_t(k)]);
        while (k > 0 && s <= z[size_t(k)]) {
            --k;
            s = ((f[q] + double(q) * q) - (f[v[size_t(k)]] + double(v[size_t(k)]) * v[size_t(k)])) /
                (2.0 * q - 2.0 * v[size_t(k)]);
        }
        ++k;
        v[size_t(k)] = q;
        z[size_t(k)] = s;
        z[size_t(k + 1)] = 1e30;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[size_t(k + 1)] < q) ++k;
        int p = v[size_t(k)];
        d[q] = double(q - p) * double(q - p) + f[p];
    }
}

}  // namespace

void gaussianBlur(float* data, int w, int h, int ch, float sigma) {
    if (sigma < 0.3f || w <= 0 || h <= 0) return;
    std::vector<int> boxes = boxesForGauss(sigma, 3);
    for (int b : boxes) {
        int r = (b - 1) / 2;
        // horizontal
        parallelFor(h, [&, r](int y) {
            std::vector<float> l(size_t(w) * size_t(ch));
            float* row = data + size_t(y) * size_t(w) * size_t(ch);
            boxLine(row, l.data(), w, ch, ch, r);
            std::copy(l.begin(), l.end(), row);
        });
        // vertical
        parallelFor(w, [&, r](int x) {
            std::vector<float> in(size_t(h) * size_t(ch)), out(size_t(h) * size_t(ch));
            for (int y = 0; y < h; ++y)
                for (int c = 0; c < ch; ++c) in[size_t(y) * size_t(ch) + size_t(c)] = data[(size_t(y) * w + x) * ch + c];
            boxLine(in.data(), out.data(), h, ch, ch, r);
            for (int y = 0; y < h; ++y)
                for (int c = 0; c < ch; ++c) data[(size_t(y) * w + x) * ch + c] = out[size_t(y) * size_t(ch) + size_t(c)];
        });
    }
}

void distanceTransform(const std::vector<uint8_t>& inside, int w, int h, std::vector<float>& dist2) {
    std::vector<double> grid(size_t(w) * size_t(h), kDtInf);
    for (size_t i = 0; i < grid.size(); ++i)
        if (inside[i]) grid[i] = 0;
    int n = std::max(w, h);
    std::vector<double> f(static_cast<size_t>(n)), d(static_cast<size_t>(n));
    std::vector<int> v(static_cast<size_t>(n));
    std::vector<double> z(size_t(n) + 1);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) f[size_t(y)] = grid[size_t(y) * w + x];
        dt1d(f.data(), d.data(), h, v, z);
        for (int y = 0; y < h; ++y) grid[size_t(y) * w + x] = d[size_t(y)];
    }
    for (int y = 0; y < h; ++y) {
        double* row = &grid[size_t(y) * w];
        std::copy(row, row + w, f.begin());
        dt1d(f.data(), d.data(), w, v, z);
        std::copy(d.begin(), d.begin() + w, row);
    }
    dist2.resize(grid.size());
    for (size_t i = 0; i < grid.size(); ++i) dist2[i] = float(std::min(grid[i], 1e30));
}

void readFloat(const RasterSurface& s, const QRect& r, float* out) {
    std::vector<quint32> tmp(size_t(r.width()));
    for (int y = r.top(); y <= r.bottom(); ++y) {
        s.readRect(QRect(r.left(), y, r.width(), 1), tmp.data(), r.width());
        float* o = out + size_t(y - r.top()) * size_t(r.width()) * 4;
        for (int x = 0; x < r.width(); ++x) unpack(tmp[size_t(x)], o + x * 4);
    }
}

void writeFloat(RasterSurface& s, const QRect& r, const float* in, bool dither) {
    std::vector<quint32> tmp(size_t(r.width()) * size_t(r.height()));
    for (int y = 0; y < r.height(); ++y)
        for (int x = 0; x < r.width(); ++x) {
            const float* p = in + (size_t(y) * r.width() + x) * 4;
            tmp[size_t(y) * r.width() + x] = dither ? packDither(p, r.left() + x, r.top() + y) : pack(p);
        }
    s.writeRect(r, tmp.data(), r.width());
}

void readMask(const MaskSurface& s, const QRect& r, float* out) {
    std::vector<quint8> tmp(size_t(r.width()) * size_t(r.height()));
    s.readRect(r, tmp.data(), r.width());
    for (size_t i = 0; i < tmp.size(); ++i) out[i] = tmp[i] * kInv255;
}

void writeMask(MaskSurface& s, const QRect& r, const float* in) {
    std::vector<quint8> tmp(size_t(r.width()) * size_t(r.height()));
    for (size_t i = 0; i < tmp.size(); ++i) tmp[i] = quint8(std::lround(std::clamp(in[i], 0.f, 1.f) * 255.f));
    s.writeRect(r, tmp.data(), r.width());
}

QImage surfaceToImage(const RasterSurface& s, const QRect& r) {
    QImage img(r.size(), QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) return img;
    s.readRect(r, reinterpret_cast<quint32*>(img.bits()), int(img.bytesPerLine() / 4));
    return img;
}

void imageToSurface(const QImage& src, RasterSurface& s, const QPoint& at) {
    QImage img = src.format() == QImage::Format_ARGB32_Premultiplied ? src : src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) return;
    QRect r(at, img.size());
    s.writeRect(r, reinterpret_cast<const quint32*>(img.constBits()), int(img.bytesPerLine() / 4));
    s.optimizeRect(r);
}

QImage maskToImage(const MaskSurface& s, const QRect& r) {
    QImage img(r.size(), QImage::Format_Grayscale8);
    if (img.isNull()) return img;
    s.readRect(r, img.bits(), int(img.bytesPerLine()));
    return img;
}

void imageToMask(const QImage& src, MaskSurface& s, const QPoint& at) {
    QImage img = src.format() == QImage::Format_Grayscale8 ? src : src.convertToFormat(QImage::Format_Grayscale8);
    if (img.isNull()) return;
    QRect r(at, img.size());
    s.writeRect(r, img.constBits(), int(img.bytesPerLine()));
    s.optimizeRect(r);
}

void sampleBilinear(const QImage& img, float x, float y, float* out) {
    x -= 0.5f;
    y -= 0.5f;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    float fx = x - x0, fy = y - y0;
    out[0] = out[1] = out[2] = out[3] = 0;
    const int w = img.width(), h = img.height();
    for (int j = 0; j < 2; ++j) {
        int yy = y0 + j;
        if (yy < 0 || yy >= h) continue;
        const quint32* row = reinterpret_cast<const quint32*>(img.constScanLine(yy));
        float wy = j ? fy : 1.f - fy;
        for (int i = 0; i < 2; ++i) {
            int xx = x0 + i;
            if (xx < 0 || xx >= w) continue;
            float wgt = wy * (i ? fx : 1.f - fx);
            float p[4];
            unpack(row[xx], p);
            out[0] += p[0] * wgt; out[1] += p[1] * wgt; out[2] += p[2] * wgt; out[3] += p[3] * wgt;
        }
    }
}

namespace {
// Set while a thread runs a piece of a parallel loop. A loop started from inside another
// one runs on that thread: waiting for the shared pool from a pool thread can deadlock.
thread_local bool tl_inParallel = false;
struct ParallelScope {
    bool old;
    ParallelScope() : old(tl_inParallel) { tl_inParallel = true; }
    ~ParallelScope() { tl_inParallel = old; }
};
}  // namespace

void parallelForTiles(const QVector<qint64>& keys, const std::function<void(qint64)>& fn) {
    if (keys.size() <= 1 || tl_inParallel) {
        for (qint64 k : keys) fn(k);
        return;
    }
    QVector<qint64> copy = keys;
    QtConcurrent::blockingMap(copy, [&fn](qint64& k) {
        ParallelScope scope;
        fn(k);
    });
}

void parallelFor(int n, const std::function<void(int)>& fn) {
    if (n <= 0) return;
    if (n < 4 || tl_inParallel) {
        for (int i = 0; i < n; ++i) fn(i);
        return;
    }
    std::vector<int> idx(static_cast<size_t>(n));
    std::iota(idx.begin(), idx.end(), 0);
    QtConcurrent::blockingMap(idx, [&fn](int& i) {
        ParallelScope scope;
        fn(i);
    });
}

}  // namespace gx

namespace gx {

namespace {
inline float hashNoise(qint64 x, qint64 y, unsigned c, unsigned seed) {
    quint32 h = quint32(x) * 374761393u + quint32(y) * 668265263u + c * 2246822519u + seed * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h & 0xffff) / 65535.f - 0.5f;
}
}  // namespace

float grainNoise(double x, double y, unsigned channel, unsigned seed, double size) {
    if (size <= 1.0) return hashNoise(qint64(std::floor(x)), qint64(std::floor(y)), channel, seed);
    // value noise: smooth blend of random values on a grid of 'size' pixels
    const double gx = x / size, gy = y / size;
    const qint64 x0 = qint64(std::floor(gx)), y0 = qint64(std::floor(gy));
    double fx = gx - double(x0), fy = gy - double(y0);
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    const float a = hashNoise(x0, y0, channel, seed), b = hashNoise(x0 + 1, y0, channel, seed);
    const float c = hashNoise(x0, y0 + 1, channel, seed), d = hashNoise(x0 + 1, y0 + 1, channel, seed);
    const double top = a + (b - a) * fx, bot = c + (d - c) * fx;
    return float((top + (bot - top) * fy) * 1.6);  // keep the contrast of fine grain
}

float halftoneCoverage(double x, double y, double cell, double angleDeg, int shape, float darkness) {
    darkness = std::clamp(darkness, 0.f, 1.f);
    if (darkness <= 0.002f) return 0.f;
    if (darkness >= 0.998f) return 1.f;
    cell = std::max(2.0, cell);
    const double a = angleDeg * M_PI / 180.0, ca = std::cos(a), sa = std::sin(a);
    const double u = (x * ca + y * sa) / cell, v = (-x * sa + y * ca) / cell;
    const double fu = (u - std::floor(u) - 0.5) * cell, fv = (v - std::floor(v) - 0.5) * cell;  // px from the cell center
    if (shape == 1) {  // lines
        const double half = darkness * cell * 0.5;
        return float(std::clamp(half - std::fabs(fv) + 0.5, 0.0, 1.0));
    }
    if (shape == 2) {  // squares
        const double half = std::sqrt(darkness) * cell * 0.5;
        const double d = std::max(std::fabs(fu), std::fabs(fv));
        return float(std::clamp(half - d + 0.5, 0.0, 1.0));
    }
    // round dots that grow smoothly into a checker and then into white holes
    // ("euclidean dot" used for printed screentone)
    const double k = 2.0 * M_PI / cell;
    const double f = 0.5 * (std::cos(k * fu) + std::cos(k * fv));  // 1 at the dot center, -1 between dots
    const double thr = 1.0 - 2.0 * darkness;
    const double grad = 0.5 * k * std::sqrt(std::pow(std::sin(k * fu), 2) + std::pow(std::sin(k * fv), 2));
    return float(std::clamp((f - thr) / std::max(grad, 0.25 * k) + 0.5, 0.0, 1.0));
}

}  // namespace gx
