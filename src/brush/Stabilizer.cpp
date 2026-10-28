#include "brush/Stabilizer.h"

#include <algorithm>
#include <cmath>

namespace gx {

namespace {
double dist(const QPointF& a, const QPointF& b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }
InputSample lerp(const InputSample& a, const InputSample& b, double t) {
    InputSample o = b;
    o.pos = a.pos + (b.pos - a.pos) * t;
    o.pressure = a.pressure + (b.pressure - a.pressure) * t;
    o.tiltX = a.tiltX + (b.tiltX - a.tiltX) * t;
    o.tiltY = a.tiltY + (b.tiltY - a.tiltY) * t;
    o.timeMs = a.timeMs + (b.timeMs - a.timeMs) * t;
    return o;
}
}  // namespace

void Stabilizer::configure(Mode mode, int level, int pressureSmoothing) {
    m_mode = mode;
    m_level = std::clamp(level, 0, 30);
    m_pressureLevel = std::clamp(pressureSmoothing, 0, 20);
}

InputSample Stabilizer::smoothPressure(InputSample s) {
    if (m_pressureLevel <= 0) return s;
    double a = 1.0 / (1.0 + m_pressureLevel * 0.6);
    m_pressure += (s.pressure - m_pressure) * a;
    s.pressure = m_pressure;
    return s;
}

std::vector<InputSample> Stabilizer::begin(const InputSample& s) {
    m_active = true;
    m_window.clear();
    m_window.push_back(s);
    m_out = s;
    m_last = s;
    m_pressure = s.pressure;
    return {s};
}

std::vector<InputSample> Stabilizer::feed(const InputSample& s0) {
    if (!m_active) return begin(s0);
    InputSample s = smoothPressure(s0);
    m_last = s;
    std::vector<InputSample> out;
    if (m_mode == None || m_level == 0) {
        m_out = s;
        out.push_back(s);
        return out;
    }
    switch (m_mode) {
    case Average: {
        // Resample the raw input so the window covers a distance, not a number of
        // events; that keeps the feel independent of the tablet report rate.
        const double step = 1.0;
        InputSample prev = m_window.back();
        double d = dist(prev.pos, s.pos);
        int n = std::max(1, int(d / step));
        const size_t cap = size_t(m_level) * 6 + 1;
        for (int i = 1; i <= n; ++i) {
            m_window.push_back(lerp(prev, s, double(i) / n));
            if (m_window.size() > cap) m_window.pop_front();
            // Weighted average, recent samples count more.
            double wsum = 0;
            QPointF p(0, 0);
            double pr = 0, tx = 0, ty = 0;
            size_t k = 0;
            for (const auto& q : m_window) {
                double w = 1.0 + double(k++);
                p += q.pos * w;
                pr += q.pressure * w;
                tx += q.tiltX * w;
                ty += q.tiltY * w;
                wsum += w;
            }
            InputSample o = m_window.back();
            o.pos = p / wsum;
            o.pressure = pr / wsum;
            o.tiltX = tx / wsum;
            o.tiltY = ty / wsum;
            if (dist(o.pos, m_out.pos) >= 0.25 || i == n) {
                m_out = o;
                out.push_back(o);
            }
        }
        break;
    }
    case Speed: {
        double dt = std::max(1.0, s.timeMs - m_out.timeMs);
        double v = dist(s.pos, m_out.pos) / dt;  // px per ms
        double strength = m_level * 0.9;
        double a = std::clamp(1.0 / (1.0 + strength / (1.0 + v * 1.5)), 0.02, 1.0);
        InputSample o = s;
        o.pos = m_out.pos + (s.pos - m_out.pos) * a;
        m_out = o;
        out.push_back(o);
        break;
    }
    case Rope: {
        double len = m_level * 2.5;
        double d = dist(s.pos, m_out.pos);
        if (d > len) {
            QPointF dir = (s.pos - m_out.pos) / d;
            InputSample o = s;
            o.pos = s.pos - dir * len;
            m_out = o;
            out.push_back(o);
        } else {
            m_out.pressure = s.pressure;
        }
        break;
    }
    default: out.push_back(s); break;
    }
    return out;
}

std::vector<InputSample> Stabilizer::finish() {
    std::vector<InputSample> out;
    if (!m_active) return out;
    m_active = false;
    if (m_mode == None || m_level == 0) return out;
    // Catch up with the pen so the line ends where the pen was lifted (like SAI).
    InputSample target = m_last;
    double d = dist(m_out.pos, target.pos);
    if (m_mode == Average) {
        const size_t cap = size_t(m_level) * 6 + 1;
        for (size_t i = 0; i < cap; ++i) {
            m_window.push_back(target);
            if (m_window.size() > cap) m_window.pop_front();
            double wsum = 0;
            QPointF p(0, 0);
            double pr = 0;
            size_t k = 0;
            for (const auto& q : m_window) {
                double w = 1.0 + double(k++);
                p += q.pos * w;
                pr += q.pressure * w;
                wsum += w;
            }
            InputSample o = target;
            o.pos = p / wsum;
            o.pressure = pr / wsum;
            if (dist(o.pos, m_out.pos) >= 0.25) {
                m_out = o;
                out.push_back(o);
            }
        }
    }
    if (d > 0.5 || dist(m_out.pos, target.pos) > 0.5) {
        int n = std::max(1, int(dist(m_out.pos, target.pos) / 2.0));
        InputSample from = m_out;
        for (int i = 1; i <= n; ++i) out.push_back(lerp(from, target, double(i) / n));
    }
    return out;
}

}  // namespace gx
