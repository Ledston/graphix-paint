#include "core/Lineart.h"

#include <QHash>
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "core/ImageOps.h"
#include "core/Pixel.h"
#include "core/Textures.h"
#include "vector/VectorRender.h"

namespace gx {

namespace {

inline float smoothstep(float e0, float e1, float x) {
    if (e1 == e0) return x >= e1 ? 1.f : 0.f;
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

void blur1(std::vector<float>& a, int W, int H, double sigma) {
    if (sigma < 0.3) return;
    gaussianBlur(a.data(), W, H, 1, float(sigma));
}

// Max (dilate) or min (erode) over a square of radius r, separable.
void morph(std::vector<float>& a, int W, int H, int r, bool dilate) {
    if (r <= 0) return;
    std::vector<float> tmp(a.size());
    parallelFor(H, [&](int y) {
        const float* row = &a[size_t(y) * W];
        float* o = &tmp[size_t(y) * W];
        for (int x = 0; x < W; ++x) {
            float m = row[x];
            for (int k = std::max(0, x - r); k <= std::min(W - 1, x + r); ++k) m = dilate ? std::max(m, row[k]) : std::min(m, row[k]);
            o[x] = m;
        }
    });
    parallelFor(W, [&](int x) {
        for (int y = 0; y < H; ++y) {
            float m = tmp[size_t(y) * W + x];
            for (int k = std::max(0, y - r); k <= std::min(H - 1, y + r); ++k) {
                float v = tmp[size_t(k) * W + x];
                m = dilate ? std::max(m, v) : std::min(m, v);
            }
            a[size_t(y) * W + x] = m;
        }
    });
}

// Removes connected spots whose size is below minArea pixels.
void removeSpecks(std::vector<float>& a, int W, int H, int minArea) {
    if (minArea <= 1) return;
    std::vector<int> label(a.size(), -1);
    std::vector<int> stack;
    std::vector<int> members;
    for (int start = 0; start < W * H; ++start) {
        if (label[size_t(start)] >= 0 || a[size_t(start)] < 0.25f) continue;
        members.clear();
        stack.push_back(start);
        label[size_t(start)] = 1;
        while (!stack.empty()) {
            int p = stack.back();
            stack.pop_back();
            members.push_back(p);
            const int x = p % W, y = p / W;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    const int q = ny * W + nx;
                    if (label[size_t(q)] >= 0 || a[size_t(q)] < 0.25f) continue;
                    label[size_t(q)] = 1;
                    stack.push_back(q);
                }
            if (int(members.size()) >= minArea) {
                // big enough: mark the rest of the spot without collecting it
                while (!stack.empty()) {
                    int q = stack.back();
                    stack.pop_back();
                    const int qx = q % W, qy = q / W;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = qx + dx, ny = qy + dy;
                            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                            const int r = ny * W + nx;
                            if (label[size_t(r)] >= 0 || a[size_t(r)] < 0.25f) continue;
                            label[size_t(r)] = 1;
                            stack.push_back(r);
                        }
                }
                members.clear();
                break;
            }
        }
        for (int p : members) {
            a[size_t(p)] = 0.f;
            // soft edge pixels around the removed spot
            const int x = p % W, y = p / W;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < W && ny < H && a[size_t(ny * W + nx)] < 0.25f) a[size_t(ny * W + nx)] = 0.f;
                }
        }
    }
}

int pickMode(const std::vector<float>& L, const std::vector<float>& col, int W, int H) {
    const size_t n = L.size();
    if (n == 0) return LineartParams::Photo;
    double mean = 0, dark = 0, chroma = 0, flat = 0;
    const int step = std::max(1, int(std::sqrt(double(n) / 40000.0)));
    size_t cnt = 0;
    for (int y = 1; y < H - 1; y += step)
        for (int x = 1; x < W - 1; x += step) {
            const size_t i = size_t(y) * W + x;
            mean += L[i];
            if (L[i] < 0.5f) dark += 1;
            const float* c = &col[i * 3];
            chroma += std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
            const float g = std::fabs(L[i + 1] - L[i - 1]) + std::fabs(L[i + size_t(W)] - L[i - size_t(W)]);
            if (g < 0.01f) flat += 1;
            ++cnt;
        }
    if (!cnt) return LineartParams::Photo;
    mean /= double(cnt);
    dark /= double(cnt);
    chroma /= double(cnt);
    flat /= double(cnt);
    if (mean > 0.72 && dark < 0.22 && chroma < 0.12) return LineartParams::Sketch;  // paper with drawing on it
    if (flat > 0.55) return LineartParams::Anime;                                    // flat colors with clean edges
    return LineartParams::Photo;
}

}  // namespace

int makeLineart(const float* rgba, int W, int H, const LineartParams& p, float* out, double pxScale, const QPoint& origin) {
    const size_t N = size_t(W) * H;
    std::fill_n(out, N * 4, 0.f);
    if (N == 0) return LineartParams::Photo;
    const double s = std::max(0.05, pxScale);  // sizes in px of the real picture -> this buffer

    // luminance over white paper, and straight colors
    std::vector<float> L(N), col(N * 3);
    parallelFor(H, [&](int y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * W + x;
            const float* q = rgba + i * 4;
            const float a = q[3];
            float c[3];
            for (int k = 0; k < 3; ++k) c[k] = q[k] + (1.f - a);  // over white
            col[i * 3] = c[0];
            col[i * 3 + 1] = c[1];
            col[i * 3 + 2] = c[2];
            L[i] = luminance(c[0], c[1], c[2]);
        }
    });
    int mode = p.mode == LineartParams::Auto ? pickMode(L, col, W, H) : p.mode;
    const float detail = float(std::clamp(p.detail, 0.0, 100.0) / 100.0);
    std::vector<float> A(N, 0.f);  // line strength 0..1

    if (mode == LineartParams::Sketch) {
        // even out the paper (shadows of a phone photo), then keep what is darker than paper
        std::vector<float> bg = L;
        blur1(bg, W, H, std::max(4.0, 18.0 * s));
        std::vector<float> Ls = L;
        blur1(Ls, W, H, 0.35 * p.smooth * s);
        const float lo = 0.03f + (1.f - detail) * 0.22f;
        parallelFor(H, [&](int y) {
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y) * W + x;
                const float norm = Ls[i] / std::max(0.08f, std::max(bg[i], Ls[i]));
                A[i] = smoothstep(lo, lo + 0.3f, 1.f - norm);
            }
        });
        const double t = p.thickness * s;
        if (t > 1.6) morph(A, W, H, int(std::lround((t - 1.0) * 0.5)), true);
        else if (t < 0.7) morph(A, W, H, 1, false);
    } else if (mode == LineartParams::Anime) {
        // edges between flat color areas + the dark outlines already in the picture
        std::vector<float> c = col;
        if (p.smooth > 0.1) gaussianBlur(c.data(), W, H, 3, float(0.4 * p.smooth * s));
        const float t = 0.04f + (1.f - detail) * 0.35f;
        parallelFor(H, [&](int y) {
            if (y == 0 || y == H - 1) return;
            for (int x = 1; x < W - 1; ++x) {
                const size_t i = size_t(y) * W + x;
                float g = 0;
                for (int k = 0; k < 3; ++k) {
                    auto C = [&](int dx, int dy) { return c[(i + size_t(dy) * W + size_t(dx)) * 3 + size_t(k)]; };
                    const float gx = (C(1, -1) + 2 * C(1, 0) + C(1, 1)) - (C(-1, -1) + 2 * C(-1, 0) + C(-1, 1));
                    const float gy = (C(-1, 1) + 2 * C(0, 1) + C(1, 1)) - (C(-1, -1) + 2 * C(0, -1) + C(1, -1));
                    g = std::max(g, std::sqrt(gx * gx + gy * gy) * 0.25f);
                }
                const float edge = smoothstep(t, t * 1.8f, g);
                const float ink = smoothstep(0.32f, 0.12f, L[i]);  // existing black lines
                A[i] = std::max(edge, ink);
            }
        });
        const double th = p.thickness * s;
        if (th > 2.2) morph(A, W, H, int(std::lround((th - 2.0) * 0.5)), true);
        else if (th < 1.2) morph(A, W, H, 1, false);
    } else {
        // photo: difference of Gaussians, lines on the dark side of edges
        mode = LineartParams::Photo;
        std::vector<float> base = L;
        blur1(base, W, H, 0.5 * p.smooth * s);
        const double sigma = std::max(0.6, (0.4 + 0.45 * p.thickness) * s);
        std::vector<float> g1 = base, g2 = base;
        blur1(g1, W, H, sigma);
        blur1(g2, W, H, sigma * 1.6);
        const float t = 0.0015f + (1.f - detail) * (1.f - detail) * 0.05f;
        std::vector<float> dark;
        if (p.fillDarks) {
            dark = base;
            blur1(dark, W, H, sigma * 1.2);
        }
        parallelFor(H, [&](int y) {
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y) * W + x;
                const float v = g2[i] - g1[i];
                float a = smoothstep(t, t * 2.2f + 0.006f, v);
                if (p.fillDarks) a = std::max(a, smoothstep(0.16f, 0.05f, dark[i]));
                A[i] = a;
            }
        });
    }

    // ---- clean up
    if (p.closeGaps > 0) {
        const int r = std::max(1, int(std::lround(p.closeGaps * s * 0.5)));
        morph(A, W, H, r, true);
        morph(A, W, H, r, false);
    }
    if (p.cleanup > 0.5) removeSpecks(A, W, H, int(std::lround(std::pow(p.cleanup * s, 2.0) * 0.6)));
    if (p.smooth > 0.2) {
        std::vector<float> b = A;
        blur1(b, W, H, 0.3 * p.smooth * s);
        for (size_t i = 0; i < N; ++i) A[i] = smoothstep(0.15f, 0.6f, b[i]);
    }

    // ---- hand-drawn look
    const double ox = origin.x(), oy = origin.y();
    if (p.pressure > 0.01) {
        std::vector<float> thin = A, thick = A;
        const int r = std::max(1, int(std::lround(0.6 * p.thickness * s)));
        morph(thin, W, H, r, false);
        morph(thick, W, H, r, true);
        const double scale = std::max(4.0, 60.0 * s);
        parallelFor(H, [&](int y) {
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y) * W + x;
                const float n = grainNoise((x + ox * s) , (y + oy * s), 7, p.seed, scale) * 2.f;  // -1..1
                const float t = clamp01(0.5f + n * float(p.pressure));
                A[i] = t < 0.5f ? thin[i] + (A[i] - thin[i]) * t * 2.f : A[i] + (thick[i] - A[i]) * (t - 0.5f) * 2.f;
            }
        });
    }
    auto displaced = [&](const std::vector<float>& src, double amount, unsigned seed) {
        std::vector<float> dst(N, 0.f);
        const double scale = std::max(3.0, p.wobbleScale * s);
        const double amt = amount * s;
        parallelFor(H, [&](int y) {
            for (int x = 0; x < W; ++x) {
                const double gx = x + ox * s, gy = y + oy * s;
                const double dx = grainNoise(gx, gy, 11, seed, scale) * 2.0 * amt;
                const double dy = grainNoise(gx, gy, 13, seed, scale) * 2.0 * amt;
                const double sx = x + dx, sy = y + dy;
                const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
                const float fx = float(sx - x0), fy = float(sy - y0);
                auto at = [&](int xx, int yy) {
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) return 0.f;
                    return src[size_t(yy) * W + xx];
                };
                dst[size_t(y) * W + x] = (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
            }
        });
        return dst;
    };
    if (p.sketchy > 0.01) {
        // a second quick pass next to the first one
        std::vector<float> second = displaced(A, std::max(1.5, p.wobble + 1.5), p.seed + 101);
        const float k = float(std::clamp(p.sketchy, 0.0, 1.0)) * 0.7f;
        for (size_t i = 0; i < N; ++i) A[i] = std::max(A[i], second[i] * k);
    }
    if (p.wobble > 0.05) A = displaced(A, p.wobble, p.seed);

    if (p.texture > 0 && p.textureStrength > 0) {
        const float st = float(std::clamp(p.textureStrength, 0.0, 1.0));
        TexturePtr paper = TextureLibrary::instance().findAny(p.texture == 2 ? "Rough grain" : "Fine paper");
        if (p.texture == 3) {
            // ink bleed: soft, slightly uneven edges
            std::vector<float> b = A;
            blur1(b, W, H, 0.9 * s);
            for (size_t i = 0; i < N; ++i) {
                const int x = int(i % size_t(W)), y = int(i / size_t(W));
                const float n = grainNoise(x + ox * s, y + oy * s, 5, p.seed, 2.0) * st;
                A[i] = smoothstep(0.25f + n * 0.4f, 0.55f + n * 0.4f, b[i]);
            }
        } else {
            const double grain = p.texture == 2 ? 2.2 : 1.0;
            parallelFor(H, [&](int y) {
                for (int x = 0; x < W; ++x) {
                    const size_t i = size_t(y) * W + x;
                    if (A[i] <= 0) continue;
                    const double gx = (x / s) + ox, gy = (y / s) + oy;  // texture in real picture pixels
                    float t = paper ? paper->sample(float(gx), float(gy)) : 1.f;
                    t = t * (0.75f + 0.5f * grainNoise(gx, gy, 3, p.seed, grain));
                    A[i] *= clamp01(1.f - st + st * t);
                }
            });
        }
    }

    // ---- color
    const float lc[3] = {float(p.color.redF()), float(p.color.greenF()), float(p.color.blueF())};
    parallelFor(H, [&](int y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * W + x;
            const float a = clamp01(A[i]);
            float* o = out + i * 4;
            if (a <= 0) continue;
            if (p.colorMode == 1) {
                for (int k = 0; k < 3; ++k) o[k] = col[i * 3 + size_t(k)] * 0.55f * a;  // the picture's color, darker
            } else {
                for (int k = 0; k < 3; ++k) o[k] = lc[k] * a;
            }
            o[3] = a;
        }
    });
    return mode;
}

// ---------------------------------------------------------------------------- tracing

namespace {
// Zhang-Suen thinning of a binary image (1 = line).
void thin(std::vector<uint8_t>& b, int W, int H) {
    auto at = [&](int x, int y) -> int { return (x < 0 || y < 0 || x >= W || y >= H) ? 0 : b[size_t(y) * W + x]; };
    std::vector<int> del;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int pass = 0; pass < 2; ++pass) {
            del.clear();
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    if (!b[size_t(y) * W + x]) continue;
                    const int p2 = at(x, y - 1), p3 = at(x + 1, y - 1), p4 = at(x + 1, y), p5 = at(x + 1, y + 1);
                    const int p6 = at(x, y + 1), p7 = at(x - 1, y + 1), p8 = at(x - 1, y), p9 = at(x - 1, y - 1);
                    const int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                    if (B < 2 || B > 6) continue;
                    const int seq[9] = {p2, p3, p4, p5, p6, p7, p8, p9, p2};
                    int A = 0;
                    for (int k = 0; k < 8; ++k) A += (!seq[k] && seq[k + 1]);
                    if (A != 1) continue;
                    if (pass == 0 && (p2 * p4 * p6 || p4 * p6 * p8)) continue;
                    if (pass == 1 && (p2 * p4 * p8 || p2 * p6 * p8)) continue;
                    del.push_back(y * W + x);
                }
            for (int i : del) b[size_t(i)] = 0;
            if (!del.empty()) changed = true;
        }
    }
}
}  // namespace

QVector<VectorStroke> traceLineart(const float* alpha, int W, int H, const QPoint& at, const QColor& color) {
    QVector<VectorStroke> strokes;
    const size_t N = size_t(W) * H;
    if (!N) return strokes;
    std::vector<uint8_t> bin(N);
    for (size_t i = 0; i < N; ++i) bin[i] = alpha[i] >= 0.5f ? 1 : 0;
    // line width from the distance to the background
    std::vector<uint8_t> outside(N);
    for (size_t i = 0; i < N; ++i) outside[i] = bin[i] ? 0 : 1;
    std::vector<float> din;
    distanceTransform(outside, W, H, din);  // distance from line pixels to the nearest background pixel
    thin(bin, W, H);

    auto neighbors = [&](int p, int* out) {
        int n = 0;
        const int x = p % W, y = p / W;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                const int q = ny * W + nx;
                if (bin[size_t(q)]) out[n++] = q;
            }
        return n;
    };
    std::vector<uint8_t> used(N, 0);
    auto walk = [&](int start) {
        QVector<VectorPoint> pts;
        int cur = start, prev = -1;
        int nb[8];
        while (cur >= 0) {
            used[size_t(cur)] = 1;
            VectorPoint vp;
            vp.pos = QPointF(cur % W + 0.5 + at.x(), cur / W + 0.5 + at.y());
            vp.pressure = std::sqrt(din[size_t(cur)]);  // half width, scaled below
            pts << vp;
            int n = neighbors(cur, nb), next = -1;
            for (int k = 0; k < n; ++k)
                if (!used[size_t(nb[k])] && nb[k] != prev) {
                    next = nb[k];
                    break;
                }
            prev = cur;
            cur = next;
            if (n > 2 && pts.size() > 1) break;  // stop at a junction; the branches become their own strokes
        }
        return pts;
    };
    auto addStroke = [&](QVector<VectorPoint> pts) {
        if (pts.size() < 3) return;
        float maxW = 0;
        for (const auto& p : pts) maxW = std::max(maxW, p.pressure);
        if (maxW <= 0) return;
        for (auto& p : pts) p.pressure = std::clamp(p.pressure / maxW, 0.05f, 1.f);
        VectorStroke s;
        s.points = simplifyPoints(pts, 0.6);
        s.width = std::max(1.f, maxW * 2.f);
        s.minWidth = 0.05f;
        s.hardness = 0.9f;
        s.color = color;
        strokes << s;
    };
    int nb[8];
    // open lines start at their ends
    for (int p = 0; p < int(N); ++p)
        if (bin[size_t(p)] && !used[size_t(p)] && neighbors(p, nb) == 1) addStroke(walk(p));
    // branches and closed loops
    for (int p = 0; p < int(N); ++p)
        if (bin[size_t(p)] && !used[size_t(p)]) addStroke(walk(p));
    return strokes;
}

}  // namespace gx
