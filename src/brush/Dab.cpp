#include "brush/Dab.h"

#include <algorithm>
#include <cmath>

namespace gx {

QRect dabBounds(const DabParams& d) {
    double r = std::max(0.5, d.diameter * 0.5) + 1.5;
    int x0 = int(std::floor(d.cx - r)), y0 = int(std::floor(d.cy - r));
    int x1 = int(std::ceil(d.cx + r)), y1 = int(std::ceil(d.cy + r));
    return QRect(QPoint(x0, y0), QPoint(x1 - 1, y1 - 1));
}

bool rasterizeDab(const DabParams& d, const QRect& box, float* cov) {
    const int w = box.width(), h = box.height();
    double R = d.diameter * 0.5;
    double scaleCov = 1.0;
    if (R < 0.5) {
        scaleCov = std::max(0.0, d.diameter);  // tiny brushes fade instead of aliasing
        R = 0.5;
    }
    const double ca = std::cos(d.angleRad), sa = std::sin(d.angleRad);
    const double invRound = 1.0 / std::max(0.02, d.roundness);
    const double hard = std::clamp(d.hardness, 0.0, 1.0);
    const bool isHard = hard >= 0.985;
    const double softStart = hard;  // normalized radius where falloff starts
    bool any = false;
    for (int y = 0; y < h; ++y) {
        float* row = cov + size_t(y) * w;
        double py = box.top() + y + 0.5 - d.cy;
        for (int x = 0; x < w; ++x) {
            double px = box.left() + x + 0.5 - d.cx;
            double ux = px * ca + py * sa;
            double uy = (-px * sa + py * ca) * invRound;
            double dist = std::sqrt(ux * ux + uy * uy);
            double c;
            if (!d.antialias) {
                c = dist <= R ? 1.0 : 0.0;
                if (R <= 0.75 && std::fabs(px) < 0.5 && std::fabs(py) < 0.5) c = 1.0;
            } else {
                double edge = std::clamp(R - dist + 0.5, 0.0, 1.0);
                if (edge <= 0) { row[x] = 0; continue; }
                if (isHard) c = edge;
                else {
                    double dn = dist / R;
                    double prof;
                    if (dn <= softStart) prof = 1.0;
                    else {
                        double t = std::clamp((dn - softStart) / std::max(1e-6, 1.0 - softStart), 0.0, 1.0);
                        prof = 1.0 - t * t * (3.0 - 2.0 * t);
                    }
                    c = prof * edge;
                }
            }
            if (c <= 0) { row[x] = 0; continue; }
            if (d.shape) {
                double u = ux / R * 0.5 + 0.5, v = uy / R * 0.5 + 0.5;
                double tv = d.shape->sampleClamped(float(u), float(v));
                if (d.shapeAsTip) c = std::min(c * 4.0, 1.0) * tv;  // keep the tip shape, round edge only clips
                else c *= 1.0 - d.shapeStrength + d.shapeStrength * tv;
            }
            if (d.fringe > 0) {
                double dn = std::min(1.0, dist / R);
                c *= (1.0 - d.fringe) + d.fringe * (0.35 + 0.65 * dn * dn);
            }
            c *= scaleCov;
            row[x] = float(c);
            if (c > 0) any = true;
        }
    }
    return any;
}

}  // namespace gx
