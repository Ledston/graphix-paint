#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <vector>

#include "core/Gradient.h"

namespace gx {

// Values are stored in files: append only.
enum class AdjustmentType : int {
    BrightnessContrast = 0,
    HueSaturation = 1,
    Levels = 2,
    Curves = 3,
    ColorBalance = 4,
    GradientMap = 5,
    Invert = 6,
    Desaturate = 7,
    Posterize = 8,
    Threshold = 9,
    Vibrance = 10,
    Exposure = 11,
    Count
};

QString adjustmentTypeName(AdjustmentType t);  // English, pass through T()

struct Adjustment {
    AdjustmentType type = AdjustmentType::BrightnessContrast;
    QJsonObject params;

    static Adjustment defaults(AdjustmentType t);
    QJsonObject toJson() const;
    static Adjustment fromJson(const QJsonObject& o);
};

// Monotone cubic curve through control points (x,y in 0..1) sampled into n values.
std::vector<float> curveLut(QVector<QPointF> pts, int n);
QVector<QPointF> pointsFromJson(const QJsonValue& v);
QJsonValue pointsToJson(const QVector<QPointF>& pts);

class AdjustmentProcessor {
public:
    explicit AdjustmentProcessor(const Adjustment& adj);
    // In-place on premultiplied RGBA floats.
    void apply(float* px, int n) const;
    bool isIdentity() const { return m_identity; }

private:
    static constexpr int LUT = 1024;
    float lut(const std::vector<float>& l, float v) const;
    void applyPixel(float* c, float& a) const;

    AdjustmentType m_type;
    bool m_identity = false;
    std::vector<float> m_rgb[3];  // per channel straight-value LUTs
    std::vector<float> m_alpha;
    bool m_useRgbLut = false, m_useAlphaLut = false;
    // HSL / balance / misc parameters
    float m_hue = 0, m_sat = 0, m_light = 0;
    bool m_colorize = false;
    float m_balance[3][3] = {};
    bool m_preserveLum = true;
    std::vector<float> m_gradient;  // straight rgba, LUT entries
    float m_threshold = 0.5f;
    float m_vibrance = 0, m_vibSat = 0;
};

}  // namespace gx
