#include "core/Adjustments.h"

#include <QColor>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

#include "core/Pixel.h"

namespace gx {

QString adjustmentTypeName(AdjustmentType t) {
    switch (t) {
    case AdjustmentType::BrightnessContrast: return "Brightness / Contrast";
    case AdjustmentType::HueSaturation: return "Hue / Saturation";
    case AdjustmentType::Levels: return "Levels";
    case AdjustmentType::Curves: return "Curves";
    case AdjustmentType::ColorBalance: return "Color Balance";
    case AdjustmentType::GradientMap: return "Gradient Map";
    case AdjustmentType::Invert: return "Invert";
    case AdjustmentType::Desaturate: return "Desaturate";
    case AdjustmentType::Posterize: return "Posterize";
    case AdjustmentType::Threshold: return "Threshold";
    case AdjustmentType::Vibrance: return "Vibrance";
    case AdjustmentType::Exposure: return "Exposure";
    default: return "Adjustment";
    }
}

QVector<QPointF> pointsFromJson(const QJsonValue& v) {
    QVector<QPointF> pts;
    for (const auto& p : v.toArray()) {
        QJsonArray a = p.toArray();
        if (a.size() >= 2) pts.push_back(QPointF(a[0].toDouble(), a[1].toDouble()));
    }
    if (pts.size() < 2) pts = {QPointF(0, 0), QPointF(1, 1)};
    return pts;
}

QJsonValue pointsToJson(const QVector<QPointF>& pts) {
    QJsonArray a;
    for (const auto& p : pts) a.append(QJsonArray{p.x(), p.y()});
    return a;
}

static QJsonObject levelsChannel() {
    return QJsonObject{{"inBlack", 0.0}, {"inWhite", 1.0}, {"gamma", 1.0}, {"outBlack", 0.0}, {"outWhite", 1.0}};
}

Adjustment Adjustment::defaults(AdjustmentType t) {
    Adjustment a;
    a.type = t;
    switch (t) {
    case AdjustmentType::BrightnessContrast: a.params = {{"brightness", 0}, {"contrast", 0}}; break;
    case AdjustmentType::HueSaturation:
        a.params = {{"hue", 0}, {"saturation", 0}, {"lightness", 0}, {"colorize", false}};
        break;
    case AdjustmentType::Levels: {
        QJsonArray ch;
        for (int i = 0; i < 4; ++i) ch.append(levelsChannel());
        a.params = {{"channels", ch}};
        break;
    }
    case AdjustmentType::Curves: {
        QJsonArray c;
        for (int i = 0; i < 5; ++i) c.append(pointsToJson({QPointF(0, 0), QPointF(1, 1)}));
        a.params = {{"curves", c}};
        break;
    }
    case AdjustmentType::ColorBalance:
        a.params = {{"shadows", QJsonArray{0, 0, 0}},
                    {"midtones", QJsonArray{0, 0, 0}},
                    {"highlights", QJsonArray{0, 0, 0}},
                    {"preserveLuminosity", true}};
        break;
    case AdjustmentType::GradientMap:
        a.params = {{"gradient", Gradient::twoColor(Qt::black, Qt::white).toJson()}, {"reverse", false}};
        break;
    case AdjustmentType::Posterize: a.params = {{"levels", 6}}; break;
    case AdjustmentType::Threshold: a.params = {{"level", 128}}; break;
    case AdjustmentType::Vibrance: a.params = {{"vibrance", 0}, {"saturation", 0}}; break;
    case AdjustmentType::Exposure: a.params = {{"exposure", 0.0}, {"offset", 0.0}, {"gamma", 1.0}}; break;
    default: break;
    }
    return a;
}

QJsonObject Adjustment::toJson() const { return QJsonObject{{"type", int(type)}, {"params", params}}; }

Adjustment Adjustment::fromJson(const QJsonObject& o) {
    int t = o.value("type").toInt();
    if (t < 0 || t >= int(AdjustmentType::Count)) t = 0;
    Adjustment a = defaults(AdjustmentType(t));
    QJsonObject p = o.value("params").toObject();
    for (auto it = p.begin(); it != p.end(); ++it) a.params.insert(it.key(), it.value());
    return a;
}

std::vector<float> curveLut(QVector<QPointF> pts, int n) {
    std::vector<float> out(size_t(std::max(2, n)));
    n = int(out.size());
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    QVector<QPointF> p;
    for (const auto& q : pts) {
        QPointF c(std::clamp(q.x(), 0.0, 1.0), std::clamp(q.y(), 0.0, 1.0));
        if (!p.isEmpty() && std::fabs(c.x() - p.back().x()) < 1e-6) p.back() = c;
        else p.push_back(c);
    }
    if (p.size() < 2) {
        for (int i = 0; i < n; ++i) out[size_t(i)] = p.isEmpty() ? float(i) / (n - 1) : float(p[0].y());
        return out;
    }
    const int k = int(p.size());
    std::vector<double> m(size_t(k - 1)), t(static_cast<size_t>(k));
    for (int i = 0; i < k - 1; ++i) m[size_t(i)] = (p[i + 1].y() - p[i].y()) / (p[i + 1].x() - p[i].x());
    t[0] = m[0];
    t[size_t(k - 1)] = m[size_t(k - 2)];
    for (int i = 1; i < k - 1; ++i)
        t[size_t(i)] = (m[size_t(i - 1)] * m[size_t(i)] <= 0) ? 0.0 : (m[size_t(i - 1)] + m[size_t(i)]) * 0.5;
    for (int i = 0; i < k - 1; ++i) {
        if (std::fabs(m[size_t(i)]) < 1e-12) { t[size_t(i)] = t[size_t(i + 1)] = 0; continue; }
        double a = t[size_t(i)] / m[size_t(i)], b = t[size_t(i + 1)] / m[size_t(i)];
        double s = a * a + b * b;
        if (s > 9.0) {
            double tau = 3.0 / std::sqrt(s);
            t[size_t(i)] = tau * a * m[size_t(i)];
            t[size_t(i + 1)] = tau * b * m[size_t(i)];
        }
    }
    int seg = 0;
    for (int i = 0; i < n; ++i) {
        double x = double(i) / (n - 1);
        double y;
        if (x <= p[0].x()) y = p[0].y();
        else if (x >= p[k - 1].x()) y = p[k - 1].y();
        else {
            while (seg < k - 2 && x > p[seg + 1].x()) ++seg;
            double h = p[seg + 1].x() - p[seg].x();
            double s = (x - p[seg].x()) / h;
            double s2 = s * s, s3 = s2 * s;
            y = (2 * s3 - 3 * s2 + 1) * p[seg].y() + (s3 - 2 * s2 + s) * h * t[size_t(seg)] +
                (-2 * s3 + 3 * s2) * p[seg + 1].y() + (s3 - s2) * h * t[size_t(seg + 1)];
        }
        out[size_t(i)] = float(std::clamp(y, 0.0, 1.0));
    }
    return out;
}

namespace {

std::vector<float> identityLut(int n) {
    std::vector<float> l(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) l[size_t(i)] = float(i) / (n - 1);
    return l;
}

void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    l = (mx + mn) * 0.5f;
    if (mx - mn < 1e-6f) { h = 0; s = 0; return; }
    float d = mx - mn;
    s = l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    h /= 6.f;
}
float hue2rgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6;
    return p;
}
void hslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s <= 0) { r = g = b = l; return; }
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    float p = 2 * l - q;
    r = hue2rgb(p, q, h + 1.f / 3);
    g = hue2rgb(p, q, h);
    b = hue2rgb(p, q, h - 1.f / 3);
}

}  // namespace

AdjustmentProcessor::AdjustmentProcessor(const Adjustment& adj) : m_type(adj.type) {
    const QJsonObject& p = adj.params;
    auto setAll = [&](const std::vector<float>& l) { m_rgb[0] = m_rgb[1] = m_rgb[2] = l; m_useRgbLut = true; };
    switch (adj.type) {
    case AdjustmentType::BrightnessContrast: {
        float b = float(p.value("brightness").toDouble()) / 100.f;
        float c = float(p.value("contrast").toDouble()) / 100.f;
        if (std::fabs(b) < 1e-4f && std::fabs(c) < 1e-4f) { m_identity = true; break; }
        float k = c >= 0 ? 1.f / std::max(0.01f, 1.f - c * 0.99f) : 1.f + c;
        std::vector<float> l(LUT);
        for (int i = 0; i < LUT; ++i) {
            float v = float(i) / (LUT - 1);
            v = b >= 0 ? v + (1.f - v) * b * 0.6f : v * (1.f + b * 0.6f);
            v = (v - 0.5f) * k + 0.5f;
            l[size_t(i)] = clamp01(v);
        }
        setAll(l);
        break;
    }
    case AdjustmentType::Levels: {
        QJsonArray ch = p.value("channels").toArray();
        std::vector<float> lv[4];
        for (int c = 0; c < 4; ++c) {
            QJsonObject o = c < ch.size() ? ch[c].toObject() : QJsonObject();
            float ib = float(o.value("inBlack").toDouble(0)), iw = float(o.value("inWhite").toDouble(1));
            float g = float(std::clamp(o.value("gamma").toDouble(1), 0.05, 20.0));
            float ob = float(o.value("outBlack").toDouble(0)), ow = float(o.value("outWhite").toDouble(1));
            lv[c].resize(LUT);
            for (int i = 0; i < LUT; ++i) {
                float v = float(i) / (LUT - 1);
                float t = clamp01((v - ib) / std::max(1e-4f, iw - ib));
                t = std::pow(t, 1.f / g);
                lv[c][size_t(i)] = clamp01(ob + (ow - ob) * t);
            }
        }
        for (int c = 0; c < 3; ++c) {
            m_rgb[c].resize(LUT);
            for (int i = 0; i < LUT; ++i) m_rgb[c][size_t(i)] = lut(lv[c + 1], lv[0][size_t(i)]);
        }
        m_useRgbLut = true;
        break;
    }
    case AdjustmentType::Curves: {
        QJsonArray cv = p.value("curves").toArray();
        std::vector<float> c[5];
        for (int i = 0; i < 5; ++i) c[i] = curveLut(pointsFromJson(i < cv.size() ? cv[i] : QJsonValue()), LUT);
        for (int ch = 0; ch < 3; ++ch) {
            m_rgb[ch].resize(LUT);
            for (int i = 0; i < LUT; ++i) m_rgb[ch][size_t(i)] = lut(c[ch + 1], c[0][size_t(i)]);
        }
        m_useRgbLut = true;
        m_alpha = c[4];
        m_useAlphaLut = true;
        break;
    }
    case AdjustmentType::Invert: {
        std::vector<float> l(LUT);
        for (int i = 0; i < LUT; ++i) l[size_t(i)] = 1.f - float(i) / (LUT - 1);
        setAll(l);
        break;
    }
    case AdjustmentType::Posterize: {
        int n = std::clamp(p.value("levels").toInt(6), 2, 255);
        std::vector<float> l(LUT);
        for (int i = 0; i < LUT; ++i) l[size_t(i)] = std::round(float(i) / (LUT - 1) * (n - 1)) / float(n - 1);
        setAll(l);
        break;
    }
    case AdjustmentType::Exposure: {
        float e = float(p.value("exposure").toDouble()), o = float(p.value("offset").toDouble());
        float g = float(std::clamp(p.value("gamma").toDouble(1), 0.05, 20.0));
        std::vector<float> l(LUT);
        for (int i = 0; i < LUT; ++i) {
            float v = float(i) / (LUT - 1) * std::pow(2.f, e) + o;
            l[size_t(i)] = std::pow(clamp01(v), 1.f / g);
        }
        setAll(l);
        break;
    }
    case AdjustmentType::HueSaturation:
        m_hue = float(p.value("hue").toDouble());
        m_sat = float(p.value("saturation").toDouble()) / 100.f;
        m_light = float(p.value("lightness").toDouble()) / 100.f;
        m_colorize = p.value("colorize").toBool();
        if (!m_colorize && m_hue == 0 && m_sat == 0 && m_light == 0) m_identity = true;
        break;
    case AdjustmentType::ColorBalance: {
        const char* keys[3] = {"shadows", "midtones", "highlights"};
        bool any = false;
        for (int r = 0; r < 3; ++r) {
            QJsonArray a = p.value(keys[r]).toArray();
            for (int c = 0; c < 3; ++c) {
                m_balance[r][c] = c < a.size() ? float(a[c].toDouble()) / 100.f : 0.f;
                any |= m_balance[r][c] != 0;
            }
        }
        m_preserveLum = p.value("preserveLuminosity").toBool(true);
        m_identity = !any;
        break;
    }
    case AdjustmentType::GradientMap: {
        Gradient g = Gradient::fromJson(p.value("gradient").toObject());
        bool rev = p.value("reverse").toBool();
        m_gradient.resize(size_t(LUT) * 4);
        for (int i = 0; i < LUT; ++i) {
            double t = double(i) / (LUT - 1);
            g.eval(rev ? 1.0 - t : t, &m_gradient[size_t(i) * 4]);
        }
        break;
    }
    case AdjustmentType::Threshold: m_threshold = float(p.value("level").toDouble(128)) / 255.f; break;
    case AdjustmentType::Vibrance:
        m_vibrance = float(p.value("vibrance").toDouble()) / 100.f;
        m_vibSat = float(p.value("saturation").toDouble()) / 100.f;
        m_identity = m_vibrance == 0 && m_vibSat == 0;
        break;
    case AdjustmentType::Desaturate:
    default: break;
    }
    if (m_useRgbLut) {
        // Detect identity LUTs so empty adjustment layers cost nothing.
        bool id = true;
        for (int c = 0; c < 3 && id; ++c)
            for (int i = 0; i < LUT; ++i)
                if (std::fabs(m_rgb[c][size_t(i)] - float(i) / (LUT - 1)) > 1e-4f) { id = false; break; }
        if (id && m_useAlphaLut)
            for (int i = 0; i < LUT; ++i)
                if (std::fabs(m_alpha[size_t(i)] - float(i) / (LUT - 1)) > 1e-4f) { id = false; break; }
        m_identity = id;
    }
}

float AdjustmentProcessor::lut(const std::vector<float>& l, float v) const {
    float f = clamp01(v) * float(l.size() - 1);
    int i = int(f);
    if (i >= int(l.size()) - 1) return l.back();
    float t = f - float(i);
    return l[size_t(i)] + (l[size_t(i + 1)] - l[size_t(i)]) * t;
}

void AdjustmentProcessor::applyPixel(float* c, float& a) const {
    switch (m_type) {
    case AdjustmentType::HueSaturation: {
        float h, s, l;
        rgbToHsl(c[0], c[1], c[2], h, s, l);
        if (m_colorize) {
            h = float(std::fmod(m_hue + 360.0, 360.0)) / 360.f;
            s = clamp01((m_sat + 1.f) * 0.5f);
        } else {
            h = h + m_hue / 360.f;
            h -= std::floor(h);
            s = m_sat >= 0 ? s + (1.f - s) * m_sat * s : s * (1.f + m_sat);
            s = clamp01(s);
        }
        float r, g, b;
        hslToRgb(h, s, l, r, g, b);
        if (m_light > 0) { r += (1 - r) * m_light; g += (1 - g) * m_light; b += (1 - b) * m_light; }
        else if (m_light < 0) { r *= 1 + m_light; g *= 1 + m_light; b *= 1 + m_light; }
        c[0] = r; c[1] = g; c[2] = b;
        break;
    }
    case AdjustmentType::ColorBalance: {
        float l = (std::max({c[0], c[1], c[2]}) + std::min({c[0], c[1], c[2]})) * 0.5f;
        const float A = 0.25f, B = 0.333f, S = 0.7f;
        float w[3] = {clamp01((l - B) / -A + 0.5f) * S,
                      clamp01((l - B) / A + 0.5f) * clamp01((l + B - 1.f) / -A + 0.5f) * S,
                      clamp01((l + B - 1.f) / A + 0.5f) * S};
        float lum0 = luminance(c[0], c[1], c[2]);
        for (int ch = 0; ch < 3; ++ch) {
            float d = 0;
            for (int r = 0; r < 3; ++r) d += w[r] * m_balance[r][ch];
            c[ch] = clamp01(c[ch] + d);
        }
        if (m_preserveLum) {
            float dl = lum0 - luminance(c[0], c[1], c[2]);
            for (int ch = 0; ch < 3; ++ch) c[ch] = clamp01(c[ch] + dl);
        }
        break;
    }
    case AdjustmentType::GradientMap: {
        float l = clamp01(luminance(c[0], c[1], c[2]));
        int i = std::min(LUT - 1, int(l * (LUT - 1) + 0.5f));
        const float* g = &m_gradient[size_t(i) * 4];
        c[0] = g[0]; c[1] = g[1]; c[2] = g[2];
        a *= g[3];
        break;
    }
    case AdjustmentType::Desaturate: {
        float l = luminance(c[0], c[1], c[2]);
        c[0] = c[1] = c[2] = l;
        break;
    }
    case AdjustmentType::Threshold: {
        float v = luminance(c[0], c[1], c[2]) >= m_threshold ? 1.f : 0.f;
        c[0] = c[1] = c[2] = v;
        break;
    }
    case AdjustmentType::Vibrance: {
        float mx = std::max({c[0], c[1], c[2]}), mn = std::min({c[0], c[1], c[2]});
        float sat = mx - mn;
        float k = (1.f + m_vibrance * (1.f - sat)) * (1.f + m_vibSat);
        float l = luminance(c[0], c[1], c[2]);
        for (int ch = 0; ch < 3; ++ch) c[ch] = clamp01(l + (c[ch] - l) * k);
        break;
    }
    default: break;
    }
}

void AdjustmentProcessor::apply(float* px, int n) const {
    if (m_identity) return;
    for (int i = 0; i < n; ++i, px += 4) {
        float a = px[3];
        if (a <= 0.f) continue;
        float ia = 1.f / a;
        float c[3] = {px[0] * ia, px[1] * ia, px[2] * ia};
        if (m_useRgbLut) {
            c[0] = lut(m_rgb[0], c[0]);
            c[1] = lut(m_rgb[1], c[1]);
            c[2] = lut(m_rgb[2], c[2]);
        } else {
            applyPixel(c, a);
        }
        if (m_useAlphaLut) a = lut(m_alpha, a);
        px[0] = clamp01(c[0]) * a;
        px[1] = clamp01(c[1]) * a;
        px[2] = clamp01(c[2]) * a;
        px[3] = a;
    }
}

}  // namespace gx
