#pragma once
#include <QImage>
#include <QPointF>
#include <QRect>
#include <vector>

namespace gx {

// The displacement field behind Filter > Liquify (like Photoshop's Liquify window):
// every output pixel is taken from the source at (x + dx, y + dy). Frozen places do not move.
class LiquifyField {
public:
    enum Mode { Forward = 0, Reconstruct, Smooth, TwirlCW, TwirlCCW, Pucker, Bloat, PushLeft, Freeze, Thaw, ModeCount };
    struct Brush {
        double size = 150;      // px
        double pressure = 0.5;  // strength 0..1
        double density = 0.5;   // 0 = soft edge, 1 = strong to the edge
        double rate = 0.5;      // speed of twirl, pucker and bloat
    };

    // src: premultiplied ARGB. frozen: optional 0..1 per pixel (W*H), e.g. outside the selection.
    void begin(const QImage& src, const std::vector<float>* frozen = nullptr);
    bool isActive() const { return !m_src.isNull(); }
    QSize size() const { return m_src.size(); }

    // One dab at p (image coordinates); delta = how far the pen moved (Forward, Push left).
    // Returns the image area that changed (the output is up to date there).
    QRect dab(Mode mode, QPointF p, QPointF delta, double penPressure, const Brush& b);

    const QImage& output() const { return m_out; }
    float frozenAt(int x, int y) const;  // 0..1, image pixel
    QPointF displacementAt(double x, double y) const;
    bool hasFrozen() const;

    // Whole-picture commands (each can be undone)
    void reconstructAll(double amount);  // 1 = back to the start
    void thawAll();
    void invertFrozen();
    void saveUndo();  // before each stroke or command
    bool undo();
    bool changed() const;  // anything moved at all

private:
    void render(const QRect& r);
    float field(const std::vector<float>& f, double fx, double fy) const;

    QImage m_src, m_out;
    int m_scale = 1, m_fw = 0, m_fh = 0;
    std::vector<float> m_dx, m_dy, m_frozen;
    struct State {
        std::vector<float> dx, dy, frozen;
    };
    std::vector<State> m_undo;
};

}  // namespace gx
