#include "core/Filters.h"

#include <algorithm>
#include <cmath>

#include "core/ImageOps.h"
#include "core/BlendModes.h"
#include "core/Pixel.h"

namespace gx {

QList<QPair<QString, QString>> filterList() {
    return {{"gaussian", "Gaussian blur"}, {"motion", "Motion blur"}, {"unsharp", "Unsharp mask"}, {"sharpen", "Sharpen"},
            {"glow", "Glow"},           {"noise", "Add noise"},     {"pixelate", "Pixelate"}};
}

QJsonObject filterDefaults(const QString& id) {
    if (id == "gaussian") return {{"radius", 4.0}};
    if (id == "motion") return {{"angle", 0.0}, {"distance", 20.0}};
    if (id == "unsharp") return {{"radius", 2.0}, {"amount", 80.0}, {"threshold", 0.0}};
    if (id == "sharpen") return {{"amount", 50.0}};
    if (id == "glow") return {{"radius", 20.0}, {"strength", 80.0}, {"threshold", 150.0}};
    if (id == "noise") return {{"amount", 10.0}, {"mono", true}, {"seed", 1}, {"size", 1.0}, {"blend", -1}, {"fill", false}};
    if (id == "pixelate") return {{"cell", 8}};
    return {};
}

int filterMargin(const QString& id, const QJsonObject& p) {
    if (id == "gaussian") return int(std::ceil(p.value("radius").toDouble() * 3)) + 2;
    if (id == "motion") return int(std::ceil(p.value("distance").toDouble())) + 2;
    if (id == "unsharp") return int(std::ceil(p.value("radius").toDouble() * 3)) + 2;
    if (id == "glow") return int(std::ceil(p.value("radius").toDouble() * 3)) + 2;
    if (id == "sharpen") return 2;
    if (id == "pixelate") return p.value("cell").toInt(8);
    return 0;
}

void applyFilter(const QString& id, const QJsonObject& p, std::vector<float>& buf, int w, int h, int ox, int oy) {
    const size_t n = size_t(w) * h;
    if (id == "gaussian") {
        gaussianBlur(buf.data(), w, h, 4, float(p.value("radius").toDouble(4)) * 0.5f);
    } else if (id == "motion") {
        double ang = p.value("angle").toDouble() * M_PI / 180.0;
        double dist = std::max(1.0, p.value("distance").toDouble(20));
        int steps = std::max(2, int(dist));
        double dx = std::cos(ang), dy = std::sin(ang);
        std::vector<float> src = buf;
        parallelFor(h, [&](int y) {
            for (int x = 0; x < w; ++x) {
                float acc[4] = {0, 0, 0, 0};
                int cnt = 0;
                for (int s = 0; s < steps; ++s) {
                    double t = (double(s) / (steps - 1) - 0.5) * dist;
                    double sx = x + dx * t, sy = y + dy * t;
                    int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                    if (ix < 0 || iy < 0 || ix >= w - 1 || iy >= h - 1) { ++cnt; continue; }
                    double fx = sx - ix, fy = sy - iy;
                    const float* a = &src[(size_t(iy) * w + ix) * 4];
                    const float* b = a + 4;
                    const float* c = &src[(size_t(iy + 1) * w + ix) * 4];
                    const float* d = c + 4;
                    for (int k = 0; k < 4; ++k)
                        acc[k] += float((a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy);
                    ++cnt;
                }
                float* o = &buf[(size_t(y) * w + x) * 4];
                for (int k = 0; k < 4; ++k) o[k] = acc[k] / cnt;
            }
        });
    } else if (id == "unsharp" || id == "sharpen") {
        std::vector<float> blur = buf;
        float amount, thr = 0;
        if (id == "unsharp") {
            gaussianBlur(blur.data(), w, h, 4, float(p.value("radius").toDouble(2)) * 0.5f);
            amount = float(p.value("amount").toDouble(80)) / 100.f;
            thr = float(p.value("threshold").toDouble(0)) / 255.f;
        } else {
            gaussianBlur(blur.data(), w, h, 4, 0.8f);
            amount = float(p.value("amount").toDouble(50)) / 100.f * 1.5f;
        }
        for (size_t i = 0; i < n; ++i) {
            float* o = &buf[i * 4];
            const float* b = &blur[i * 4];
            float a = o[3];
            if (a <= 0) continue;
            for (int k = 0; k < 3; ++k) {
                float diff = o[k] - b[k];
                if (std::fabs(diff) < thr * a) continue;
                o[k] = std::clamp(o[k] + diff * amount, 0.f, a);
            }
        }
    } else if (id == "glow") {
        float thr = float(p.value("threshold").toDouble(150)) / 255.f;
        float str = float(p.value("strength").toDouble(80)) / 100.f;
        std::vector<float> bright(n * 4, 0.f);
        for (size_t i = 0; i < n; ++i) {
            const float* o = &buf[i * 4];
            if (o[3] <= 0) continue;
            float l = luminance(o[0], o[1], o[2]) / o[3];
            float k = clamp01((l - thr) / std::max(0.01f, 1.f - thr));
            for (int c = 0; c < 4; ++c) bright[i * 4 + size_t(c)] = o[c] * k;
        }
        gaussianBlur(bright.data(), w, h, 4, float(p.value("radius").toDouble(20)) * 0.5f);
        for (size_t i = 0; i < n; ++i) {
            float* o = &buf[i * 4];
            float g[4];
            for (int c = 0; c < 4; ++c) g[c] = bright[i * 4 + size_t(c)] * str;
            g[3] = std::min(1.f, g[3]);
            blendRow(BlendMode::Screen, o, g, 1);
        }
    } else if (id == "noise") {
        const float amt = float(p.value("amount").toDouble(10)) / 100.f;
        const bool mono = p.value("mono").toBool(true);
        const unsigned seed = unsigned(p.value("seed").toInt(1));
        const double size = std::max(1.0, p.value("size").toDouble(1.0));
        const int blend = p.value("blend").toInt(-1);  // -1 = add to the colors, otherwise a blending mode
        const bool fill = p.value("fill").toBool(false);  // also make noise where the layer is empty
        parallelFor(h, [&](int y) {
            float g[4];
            for (int x = 0; x < w; ++x) {
                float* o = &buf[(size_t(y) * w + x) * 4];
                if (o[3] <= 0 && !fill) continue;
                const double gx = x + ox, gy = y + oy;
                const float base = grainNoise(gx, gy, 0, seed, size);
                float n[3];
                for (int k = 0; k < 3; ++k) n[k] = mono ? base : grainNoise(gx, gy, unsigned(k + 1), seed, size);
                if (o[3] <= 0) {  // empty: gray noise, ready for an Overlay / Soft light layer
                    for (int k = 0; k < 3; ++k) o[k] = clamp01(0.5f + n[k] * amt * 2.f);
                    o[3] = 1.f;
                    continue;
                }
                if (blend < 0) {
                    for (int k = 0; k < 3; ++k) o[k] = std::clamp(o[k] + n[k] * amt * 2.f * o[3], 0.f, o[3]);
                } else {
                    for (int k = 0; k < 3; ++k) g[k] = clamp01(0.5f + n[k] * amt * 2.f);
                    g[3] = 1.f;
                    blendRowAtop(BlendMode(blend), o, g, 1);
                }
            }
        });
    } else if (id == "pixelate") {
        int cell = std::max(2, p.value("cell").toInt(8));
        for (int by = -((oy % cell + cell) % cell); by < h; by += cell)
            for (int bx = -((ox % cell + cell) % cell); bx < w; bx += cell) {
                int x0 = std::max(0, bx), y0 = std::max(0, by), x1 = std::min(w, bx + cell), y1 = std::min(h, by + cell);
                if (x0 >= x1 || y0 >= y1) continue;
                double acc[4] = {0, 0, 0, 0};
                int cnt = 0;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) {
                        const float* o = &buf[(size_t(y) * w + x) * 4];
                        for (int k = 0; k < 4; ++k) acc[k] += o[k];
                        ++cnt;
                    }
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) {
                        float* o = &buf[(size_t(y) * w + x) * 4];
                        for (int k = 0; k < 4; ++k) o[k] = float(acc[k] / cnt);
                    }
            }
    }
}

}  // namespace gx
