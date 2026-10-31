#include "core/LiquifyField.h"

#include <QLineF>
#include <algorithm>
#include <cmath>

#include "core/ImageOps.h"
#include "core/Pixel.h"

namespace gx {

void LiquifyField::begin(const QImage& src, const std::vector<float>* frozen) {
    m_src = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_out = m_src.copy();
    const qint64 area = qint64(m_src.width()) * m_src.height();
    m_scale = area > 24000000 ? 4 : area > 6000000 ? 2 : 1;
    m_fw = (m_src.width() + m_scale - 1) / m_scale + 1;
    m_fh = (m_src.height() + m_scale - 1) / m_scale + 1;
    const size_t n = size_t(m_fw) * m_fh;
    m_dx.assign(n, 0.f);
    m_dy.assign(n, 0.f);
    m_frozen.assign(n, 0.f);
    if (frozen && frozen->size() == size_t(m_src.width()) * m_src.height())
        for (int y = 0; y < m_fh; ++y)
            for (int x = 0; x < m_fw; ++x) {
                const int sx = std::min(x * m_scale, m_src.width() - 1), sy = std::min(y * m_scale, m_src.height() - 1);
                m_frozen[size_t(y) * m_fw + x] = std::clamp((*frozen)[size_t(sy) * m_src.width() + sx], 0.f, 1.f);
            }
    m_undo.clear();
}

float LiquifyField::field(const std::vector<float>& f, double fx, double fy) const {
    fx = std::clamp(fx, 0.0, double(m_fw - 1));
    fy = std::clamp(fy, 0.0, double(m_fh - 1));
    const int x0 = std::min(int(fx), std::max(0, m_fw - 2)), y0 = std::min(int(fy), std::max(0, m_fh - 2));
    const double tx = fx - x0, ty = fy - y0;
    auto at = [&](int x, int y) { return f[size_t(std::min(y, m_fh - 1)) * m_fw + std::min(x, m_fw - 1)]; };
    return float((at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty);
}

float LiquifyField::frozenAt(int x, int y) const {
    if (m_frozen.empty()) return 0.f;
    return field(m_frozen, double(x) / m_scale, double(y) / m_scale);
}

QPointF LiquifyField::displacementAt(double x, double y) const {
    if (m_dx.empty()) return {};
    return QPointF(field(m_dx, x / m_scale, y / m_scale), field(m_dy, x / m_scale, y / m_scale));
}

bool LiquifyField::hasFrozen() const {
    return std::any_of(m_frozen.begin(), m_frozen.end(), [](float f) { return f > 0.001f; });
}

bool LiquifyField::changed() const {
    for (size_t i = 0; i < m_dx.size(); ++i)
        if (std::abs(m_dx[i]) > 0.01f || std::abs(m_dy[i]) > 0.01f) return true;
    return false;
}

void LiquifyField::render(const QRect& r0) {
    const QRect r = r0 & m_out.rect();
    if (r.isEmpty()) return;
    const int s = m_scale;
    parallelFor(r.height(), [&](int yy) {
        const int y = r.top() + yy;
        quint32* row = reinterpret_cast<quint32*>(m_out.scanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            const double lx = x + 0.5, ly = y + 0.5;
            const double dx = field(m_dx, lx / s, ly / s), dy = field(m_dy, lx / s, ly / s);
            if (std::abs(dx) < 1e-4 && std::abs(dy) < 1e-4) {
                row[x] = reinterpret_cast<const quint32*>(m_src.constScanLine(y))[x];
                continue;
            }
            float o[4];
            sampleBilinear(m_src, float(lx + dx), float(ly + dy), o);
            row[x] = pack(o);
        }
    });
}

QRect LiquifyField::dab(Mode mode, QPointF p, QPointF delta, double penPressure, const Brush& b) {
    if (!isActive()) return {};
    const double R = std::max(1.0, b.size / 2);
    const double str = std::clamp(b.pressure, 0.0, 1.0) * std::clamp(penPressure, 0.05, 1.0);
    // density: how far to the edge the brush stays strong
    const double edge = 0.4 + 2.2 * (1.0 - std::clamp(b.density, 0.0, 1.0));
    const double rate = 0.25 + 1.5 * std::clamp(b.rate, 0.0, 1.0);
    const int s = m_scale;
    const int fx0 = std::max(0, int((p.x() - R) / s) - 1), fx1 = std::min(m_fw - 1, int((p.x() + R) / s) + 1);
    const int fy0 = std::max(0, int((p.y() - R) / s) - 1), fy1 = std::min(m_fh - 1, int((p.y() + R) / s) + 1);
    if (fx0 > fx1 || fy0 > fy1) return {};
    const int W = fx1 - fx0 + 1, H = fy1 - fy0 + 1;
    std::vector<float> ndx(size_t(W) * H), ndy(size_t(W) * H), nfz(size_t(W) * H);
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            const int fx = fx0 + i, fy = fy0 + j;
            const size_t fi = size_t(fy) * m_fw + fx, li = size_t(j) * W + i;
            const double cx = fx * s, cy = fy * s;
            const double d = std::hypot(cx - p.x(), cy - p.y());
            const float odx = m_dx[fi], ody = m_dy[fi], ofz = m_frozen[fi];
            ndx[li] = odx;
            ndy[li] = ody;
            nfz[li] = ofz;
            if (d >= R) continue;
            const double raw = std::pow(1.0 - (d / R) * (d / R), edge) * str;
            if (mode == Freeze) {
                nfz[li] = float(ofz + (1.0 - ofz) * std::min(1.0, raw * 1.5));
                continue;
            }
            if (mode == Thaw) {
                nfz[li] = float(ofz * (1.0 - std::min(1.0, raw * 1.5)));
                continue;
            }
            const double w = raw * (1.0 - ofz);  // frozen places stay
            if (w <= 0) continue;
            double qx = cx, qy = cy;
            switch (mode) {
            case Forward:
                qx = cx - delta.x() * w;
                qy = cy - delta.y() * w;
                break;
            case PushLeft:  // pixels go to the left of the pen's way (dragging up pushes left)
                qx = cx - delta.y() * w;
                qy = cy + delta.x() * w;
                break;
            case TwirlCW:
            case TwirlCCW: {
                const double a = (mode == TwirlCW ? -1 : 1) * w * 0.12 * rate;
                const double rx = cx - p.x(), ry = cy - p.y();
                qx = p.x() + rx * std::cos(a) - ry * std::sin(a);
                qy = p.y() + rx * std::sin(a) + ry * std::cos(a);
                break;
            }
            case Pucker:
                qx = p.x() + (cx - p.x()) * (1 + 0.08 * w * rate);
                qy = p.y() + (cy - p.y()) * (1 + 0.08 * w * rate);
                break;
            case Bloat:
                qx = p.x() + (cx - p.x()) * (1 - 0.08 * w * rate);
                qy = p.y() + (cy - p.y()) * (1 - 0.08 * w * rate);
                break;
            case Reconstruct:
                ndx[li] = float(odx * (1 - std::min(1.0, w * 0.6)));
                ndy[li] = float(ody * (1 - std::min(1.0, w * 0.6)));
                continue;
            case Smooth: {
                float ax = 0, ay = 0;
                int cnt = 0;
                for (int oy = -2; oy <= 2; ++oy)
                    for (int ox = -2; ox <= 2; ++ox) {
                        const int xx = std::clamp(fx + ox, 0, m_fw - 1), yy = std::clamp(fy + oy, 0, m_fh - 1);
                        ax += m_dx[size_t(yy) * m_fw + xx];
                        ay += m_dy[size_t(yy) * m_fw + xx];
                        ++cnt;
                    }
                ndx[li] = float(odx + (ax / cnt - odx) * std::min(1.0, w));
                ndy[li] = float(ody + (ay / cnt - ody) * std::min(1.0, w));
                continue;
            }
            default: continue;
            }
            // the place this pixel now shows, followed through the displacement there before
            ndx[li] = float(qx - cx) + field(m_dx, qx / s, qy / s);
            ndy[li] = float(qy - cy) + field(m_dy, qx / s, qy / s);
        }
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            const size_t fi = size_t(fy0 + j) * m_fw + fx0 + i, li = size_t(j) * W + i;
            m_dx[fi] = ndx[li];
            m_dy[fi] = ndy[li];
            m_frozen[fi] = nfz[li];
        }
    const QRect r = QRect(fx0 * s, fy0 * s, W * s, H * s).adjusted(-2, -2, 2, 2) & m_out.rect();
    if (mode != Freeze && mode != Thaw) render(r);
    return r;
}

void LiquifyField::reconstructAll(double amount) {
    const float k = float(1.0 - std::clamp(amount, 0.0, 1.0));
    for (size_t i = 0; i < m_dx.size(); ++i) {
        const float keep = k + (1.f - k) * m_frozen[i];  // frozen places keep their shape
        m_dx[i] *= keep;
        m_dy[i] *= keep;
    }
    render(m_out.rect());
}

void LiquifyField::thawAll() { std::fill(m_frozen.begin(), m_frozen.end(), 0.f); }

void LiquifyField::invertFrozen() {
    for (float& f : m_frozen) f = 1.f - f;
}

void LiquifyField::saveUndo() {
    if (!isActive()) return;
    // a few steps back; big pictures keep fewer
    const size_t cells = m_dx.size();
    const size_t keep = cells > 8000000 ? 3 : cells > 2000000 ? 8 : 20;
    m_undo.push_back({m_dx, m_dy, m_frozen});
    while (m_undo.size() > keep) m_undo.erase(m_undo.begin());
}

bool LiquifyField::undo() {
    if (m_undo.empty()) return false;
    State st = std::move(m_undo.back());
    m_undo.pop_back();
    m_dx = std::move(st.dx);
    m_dy = std::move(st.dy);
    m_frozen = std::move(st.frozen);
    render(m_out.rect());
    return true;
}

}  // namespace gx
