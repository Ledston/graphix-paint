#include "core/BlendModes.h"

#include <algorithm>
#include <cmath>

#include "core/Pixel.h"

namespace gx {

namespace {

const BlendModeInfo kInfos[] = {
    {BlendMode::Normal, "Normal", "norm", false},
    {BlendMode::Multiply, "Multiply", "mul ", false},
    {BlendMode::Screen, "Screen", "scrn", false},
    {BlendMode::Overlay, "Overlay", "over", false},
    {BlendMode::Shine, "Shine (SAI Luminosity)", "lddg", false},
    {BlendMode::Shade, "Shade", "lbrn", false},
    {BlendMode::ShineShade, "Shine & Shade", "lLit", false},
    {BlendMode::Darken, "Darken", "dark", true},
    {BlendMode::Lighten, "Lighten", "lite", false},
    {BlendMode::ColorDodge, "Color Dodge", "div ", false},
    {BlendMode::ColorBurn, "Color Burn", "idiv", false},
    {BlendMode::LinearDodge, "Linear Dodge (Add)", "lddg", false},
    {BlendMode::LinearBurn, "Linear Burn", "lbrn", false},
    {BlendMode::HardLight, "Hard Light", "hLit", false},
    {BlendMode::SoftLight, "Soft Light", "sLit", false},
    {BlendMode::VividLight, "Vivid Light", "vLit", false},
    {BlendMode::LinearLight, "Linear Light", "lLit", false},
    {BlendMode::PinLight, "Pin Light", "pLit", false},
    {BlendMode::HardMix, "Hard Mix", "hMix", false},
    {BlendMode::Difference, "Difference", "diff", false},
    {BlendMode::Exclusion, "Exclusion", "smud", false},
    {BlendMode::Subtract, "Subtract", "fsub", false},
    {BlendMode::Divide, "Divide", "fdiv", false},
    {BlendMode::Hue, "Hue", "hue ", false},
    {BlendMode::Saturation, "Saturation", "sat ", false},
    {BlendMode::Color, "Color", "colr", false},
    {BlendMode::Luminosity, "Luminosity", "lum ", false},
    {BlendMode::DarkerColor, "Darker Color", "dkCl", false},
    {BlendMode::LighterColor, "Lighter Color", "lgCl", false},
};
static_assert(sizeof(kInfos) / sizeof(kInfos[0]) == int(BlendMode::Count), "blend mode table out of sync");

inline float screenF(float b, float s) { return b + s - b * s; }
inline float dodgeF(float b, float s) {
    if (b <= 0.f) return 0.f;
    if (s >= 1.f) return 1.f;
    return std::min(1.f, b / (1.f - s));
}
inline float burnF(float b, float s) {
    if (b >= 1.f) return 1.f;
    if (s <= 0.f) return 0.f;
    return 1.f - std::min(1.f, (1.f - b) / s);
}
inline float hardLightF(float b, float s) { return s <= 0.5f ? b * 2.f * s : screenF(b, 2.f * s - 1.f); }
inline float softLightF(float b, float s) {
    if (s <= 0.5f) return b - (1.f - 2.f * s) * b * (1.f - b);
    float d = b <= 0.25f ? ((16.f * b - 12.f) * b + 4.f) * b : std::sqrt(b);
    return b + (2.f * s - 1.f) * (d - b);
}

struct Mul { float operator()(float b, float s) const { return b * s; } };
struct Scr { float operator()(float b, float s) const { return screenF(b, s); } };
struct Ovl { float operator()(float b, float s) const { return hardLightF(s, b); } };
struct Drk { float operator()(float b, float s) const { return std::min(b, s); } };
struct Lgt { float operator()(float b, float s) const { return std::max(b, s); } };
struct Ddg { float operator()(float b, float s) const { return dodgeF(b, s); } };
struct Brn { float operator()(float b, float s) const { return burnF(b, s); } };
struct LDg { float operator()(float b, float s) const { return std::min(1.f, b + s); } };
struct LBr { float operator()(float b, float s) const { return std::max(0.f, b + s - 1.f); } };
struct HLt { float operator()(float b, float s) const { return hardLightF(b, s); } };
struct SLt { float operator()(float b, float s) const { return softLightF(b, s); } };
struct VLt {
    float operator()(float b, float s) const { return s <= 0.5f ? burnF(b, 2.f * s) : dodgeF(b, 2.f * s - 1.f); }
};
struct LLt { float operator()(float b, float s) const { return clamp01(b + 2.f * s - 1.f); } };
struct PLt {
    float operator()(float b, float s) const { return s <= 0.5f ? std::min(b, 2.f * s) : std::max(b, 2.f * s - 1.f); }
};
struct HMx { float operator()(float b, float s) const { return (b + s >= 1.f) ? 1.f : 0.f; } };
struct Dif { float operator()(float b, float s) const { return std::fabs(b - s); } };
struct Exc { float operator()(float b, float s) const { return b + s - 2.f * b * s; } };
struct Sub { float operator()(float b, float s) const { return std::max(0.f, b - s); } };
struct Div {
    float operator()(float b, float s) const { return s <= 0.f ? (b > 0.f ? 1.f : 0.f) : std::min(1.f, b / s); }
};

template <bool Atop, class F>
void sepRow(float* d, const float* s, int n, F f) {
    for (int i = 0; i < n; ++i, d += 4, s += 4) {
        float sa = s[3];
        if (sa <= 0.f) continue;
        float da = d[3];
        if (da <= 0.f) {
            if (!Atop) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = sa; }
            continue;
        }
        float isa = 1.f / sa, ida = 1.f / da;
        float both = sa * da;
        for (int c = 0; c < 3; ++c) {
            float cb = d[c] * ida, cs = s[c] * isa;
            float B = f(cb, cs);
            float v = d[c] * (1.f - sa) + both * B;
            if (!Atop) v += s[c] * (1.f - da);
            d[c] = v;
        }
        if (!Atop) d[3] = sa + da - both;
    }
}

// Non-separable helpers (W3C compositing spec)
inline float lumOf(const float* c) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }
inline void clipColor(float* c) {
    float l = lumOf(c);
    float n = std::min({c[0], c[1], c[2]});
    float x = std::max({c[0], c[1], c[2]});
    if (n < 0.f) for (int i = 0; i < 3; ++i) c[i] = l + (c[i] - l) * l / (l - n);
    if (x > 1.f) for (int i = 0; i < 3; ++i) c[i] = l + (c[i] - l) * (1.f - l) / (x - l);
}
inline void setLum(float* c, float l) {
    float d = l - lumOf(c);
    c[0] += d; c[1] += d; c[2] += d;
    clipColor(c);
}
inline float satOf(const float* c) { return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}); }
inline void setSat(float* c, float s) {
    int imax = 0, imin = 0;
    for (int i = 1; i < 3; ++i) {
        if (c[i] > c[imax]) imax = i;
        if (c[i] < c[imin]) imin = i;
    }
    if (imax == imin) { c[0] = c[1] = c[2] = 0.f; return; }
    int imid = 3 - imax - imin;
    float mx = c[imax], mn = c[imin];
    c[imid] = (c[imid] - mn) * s / (mx - mn);
    c[imax] = s;
    c[imin] = 0.f;
}

template <bool Atop>
void nonSepRow(BlendMode m, float* d, const float* s, int n) {
    for (int i = 0; i < n; ++i, d += 4, s += 4) {
        float sa = s[3];
        if (sa <= 0.f) continue;
        float da = d[3];
        if (da <= 0.f) {
            if (!Atop) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = sa; }
            continue;
        }
        float cb[3] = {d[0] / da, d[1] / da, d[2] / da};
        float cs[3] = {s[0] / sa, s[1] / sa, s[2] / sa};
        float B[3];
        switch (m) {
        case BlendMode::Hue:
            B[0] = cs[0]; B[1] = cs[1]; B[2] = cs[2];
            setSat(B, satOf(cb));
            setLum(B, lumOf(cb));
            break;
        case BlendMode::Saturation:
            B[0] = cb[0]; B[1] = cb[1]; B[2] = cb[2];
            setSat(B, satOf(cs));
            setLum(B, lumOf(cb));
            break;
        case BlendMode::Color:
            B[0] = cs[0]; B[1] = cs[1]; B[2] = cs[2];
            setLum(B, lumOf(cb));
            break;
        case BlendMode::Luminosity:
            B[0] = cb[0]; B[1] = cb[1]; B[2] = cb[2];
            setLum(B, lumOf(cs));
            break;
        case BlendMode::DarkerColor: {
            const float* c = lumOf(cs) < lumOf(cb) ? cs : cb;
            B[0] = c[0]; B[1] = c[1]; B[2] = c[2];
            break;
        }
        default: {  // LighterColor
            const float* c = lumOf(cs) > lumOf(cb) ? cs : cb;
            B[0] = c[0]; B[1] = c[1]; B[2] = c[2];
            break;
        }
        }
        float both = sa * da;
        for (int c = 0; c < 3; ++c) {
            float v = d[c] * (1.f - sa) + both * B[c];
            if (!Atop) v += s[c] * (1.f - da);
            d[c] = v;
        }
        if (!Atop) d[3] = sa + da - both;
    }
}

template <bool Atop>
void normalRow(float* d, const float* s, int n) {
    for (int i = 0; i < n; ++i, d += 4, s += 4) {
        float sa = s[3];
        if (sa <= 0.f) continue;
        float k = 1.f - sa;
        if (Atop) {
            float da = d[3];
            d[0] = s[0] * da + d[0] * k;
            d[1] = s[1] * da + d[1] * k;
            d[2] = s[2] * da + d[2] * k;
        } else {
            d[0] = s[0] + d[0] * k;
            d[1] = s[1] + d[1] * k;
            d[2] = s[2] + d[2] * k;
            d[3] = sa + d[3] * k;
        }
    }
}

// SAI style "Luminosity" (Shine): premultiplied source is added to the backdrop, so a
// soft glow brightens what is below and still shows on transparent areas.
template <bool Atop>
void shineRow(float* d, const float* s, int n) {
    for (int i = 0; i < n; ++i, d += 4, s += 4) {
        float sa = s[3];
        if (sa <= 0.f) continue;
        float da = d[3];
        float oa = Atop ? da : sa + da - sa * da;
        float w = Atop ? da : 1.f;
        for (int c = 0; c < 3; ++c) d[c] = std::min(oa, d[c] + s[c] * w);
        d[3] = oa;
    }
}

// SAI style "Shade": darkens the backdrop by the inverse of the source.
template <bool Atop>
void shadeRow(float* d, const float* s, int n) {
    for (int i = 0; i < n; ++i, d += 4, s += 4) {
        float sa = s[3];
        if (sa <= 0.f) continue;
        float da = d[3];
        if (da <= 0.f) {
            if (!Atop) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = sa; }
            continue;
        }
        float oa = Atop ? da : sa + da - sa * da;
        for (int c = 0; c < 3; ++c) {
            float dark = (sa - s[c]) * da;  // amount of darkness carried by the source
            float v = d[c] - dark;
            if (!Atop) v += s[c] * (1.f - da);
            d[c] = std::max(0.f, v);
        }
        d[3] = oa;
    }
}

template <bool Atop>
void dispatch(BlendMode mode, float* d, const float* s, int n) {
    switch (mode) {
    case BlendMode::Normal: normalRow<Atop>(d, s, n); break;
    case BlendMode::Multiply: sepRow<Atop>(d, s, n, Mul{}); break;
    case BlendMode::Screen: sepRow<Atop>(d, s, n, Scr{}); break;
    case BlendMode::Overlay: sepRow<Atop>(d, s, n, Ovl{}); break;
    case BlendMode::Shine: shineRow<Atop>(d, s, n); break;
    case BlendMode::Shade: shadeRow<Atop>(d, s, n); break;
    case BlendMode::ShineShade: sepRow<Atop>(d, s, n, LLt{}); break;
    case BlendMode::Darken: sepRow<Atop>(d, s, n, Drk{}); break;
    case BlendMode::Lighten: sepRow<Atop>(d, s, n, Lgt{}); break;
    case BlendMode::ColorDodge: sepRow<Atop>(d, s, n, Ddg{}); break;
    case BlendMode::ColorBurn: sepRow<Atop>(d, s, n, Brn{}); break;
    case BlendMode::LinearDodge: sepRow<Atop>(d, s, n, LDg{}); break;
    case BlendMode::LinearBurn: sepRow<Atop>(d, s, n, LBr{}); break;
    case BlendMode::HardLight: sepRow<Atop>(d, s, n, HLt{}); break;
    case BlendMode::SoftLight: sepRow<Atop>(d, s, n, SLt{}); break;
    case BlendMode::VividLight: sepRow<Atop>(d, s, n, VLt{}); break;
    case BlendMode::LinearLight: sepRow<Atop>(d, s, n, LLt{}); break;
    case BlendMode::PinLight: sepRow<Atop>(d, s, n, PLt{}); break;
    case BlendMode::HardMix: sepRow<Atop>(d, s, n, HMx{}); break;
    case BlendMode::Difference: sepRow<Atop>(d, s, n, Dif{}); break;
    case BlendMode::Exclusion: sepRow<Atop>(d, s, n, Exc{}); break;
    case BlendMode::Subtract: sepRow<Atop>(d, s, n, Sub{}); break;
    case BlendMode::Divide: sepRow<Atop>(d, s, n, Div{}); break;
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Luminosity:
    case BlendMode::DarkerColor:
    case BlendMode::LighterColor: nonSepRow<Atop>(mode, d, s, n); break;
    default: normalRow<Atop>(d, s, n); break;
    }
}

}  // namespace

const BlendModeInfo* blendModeInfos() { return kInfos; }

QString blendModeName(BlendMode m) {
    int i = int(m);
    if (i < 0 || i >= int(BlendMode::Count)) i = 0;
    return QString::fromLatin1(kInfos[i].name);
}

QByteArray blendModePsdKey(BlendMode m) {
    int i = int(m);
    if (i < 0 || i >= int(BlendMode::Count)) i = 0;
    return QByteArray(kInfos[i].psdKey, 4);
}

BlendMode blendModeFromPsdKey(const QByteArray& key, bool* passThrough) {
    if (passThrough) *passThrough = (key == "pass");
    // Prefer the Photoshop modes over the SAI aliases that share a key.
    static const BlendMode prefer[] = {BlendMode::LinearDodge, BlendMode::LinearBurn, BlendMode::LinearLight};
    for (BlendMode p : prefer)
        if (blendModePsdKey(p) == key) return p;
    for (int i = 0; i < int(BlendMode::Count); ++i)
        if (key == QByteArray(kInfos[i].psdKey, 4)) return kInfos[i].mode;
    return BlendMode::Normal;
}

QList<BlendMode> blendModeUiOrder() {
    QList<BlendMode> l;
    for (int i = 0; i < int(BlendMode::Count); ++i) l << kInfos[i].mode;
    return l;
}

void blendRow(BlendMode mode, float* dst, const float* src, int n) { dispatch<false>(mode, dst, src, n); }
void blendRowAtop(BlendMode mode, float* dst, const float* src, int n) { dispatch<true>(mode, dst, src, n); }

}  // namespace gx
