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

void parallelForTiles(const QVector<qint64>& keys, const std::function<void(qint64)>& fn) {
    if (keys.size() <= 1) {
        for (qint64 k : keys) fn(k);
        return;
    }
    QVector<qint64> copy = keys;
    QtConcurrent::blockingMap(copy, [&fn](qint64& k) { fn(k); });
}

void parallelFor(int n, const std::function<void(int)>& fn) {
    if (n <= 0) return;
    if (n < 4) {
        for (int i = 0; i < n; ++i) fn(i);
        return;
    }
    std::vector<int> idx(static_cast<size_t>(n));
    std::iota(idx.begin(), idx.end(), 0);
    QtConcurrent::blockingMap(idx, [&fn](int& i) { fn(i); });
}

}  // namespace gx
