#pragma once
#include <QPointF>
#include <deque>
#include <vector>

namespace gx {

struct InputSample {
    QPointF pos;            // canvas coordinates
    double pressure = 1.0;  // 0..1
    double tiltX = 0, tiltY = 0;  // degrees
    double rotation = 0;    // pen barrel rotation, degrees
    double timeMs = 0;
};

// Smooths raw pen input. Modes follow PaintTool SAI:
//  - Average: numeric levels; a moving average over recent samples, the stroke
//    catches up with the pen when it is lifted.
//  - Speed ("S" levels): smoothing depends on speed. Slow careful lines get a lot
//    of smoothing, quick flicks stay sharp.
//  - Rope: the brush is pulled behind the pen on a string of fixed length.
class Stabilizer {
public:
    enum Mode { None = 0, Average = 1, Speed = 2, Rope = 3 };

    void configure(Mode mode, int level, int pressureSmoothing);
    Mode mode() const { return m_mode; }
    int level() const { return m_level; }

    std::vector<InputSample> begin(const InputSample& s);
    std::vector<InputSample> feed(const InputSample& s);
    std::vector<InputSample> finish();

private:
    InputSample smoothPressure(InputSample s);

    Mode m_mode = Average;
    int m_level = 3;
    int m_pressureLevel = 0;
    std::deque<InputSample> m_window;
    InputSample m_out, m_last;
    double m_pressure = 0;
    bool m_active = false;
};

}  // namespace gx
