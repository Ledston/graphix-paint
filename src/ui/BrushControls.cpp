#include "ui/BrushControls.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/I18n.h"
#include "core/Textures.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

namespace {
// Two rows of common sizes (SAI's size palette).
class QuickSizes : public QWidget {
public:
    explicit QuickSizes(std::function<void(double)> pick, QWidget* parent) : QWidget(parent), m_pick(std::move(pick)) {
        setFixedHeight(2 * 22 + 2);
        setToolTip(T("Quick brush sizes"));
    }
    void setCurrent(double s) {
        m_cur = s;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const ThemeColors& c = Theme::colors();
        int n = int(sizes().size()), per = (n + 1) / 2;
        double cw = width() / double(per);
        for (int i = 0; i < n; ++i) {
            QRectF r((i % per) * cw, (i / per) * 22, cw - 1, 21);
            double s = sizes()[size_t(i)];
            bool cur = std::fabs(s - m_cur) < 0.01;
            p.setPen(cur ? c.accent : c.border);
            p.setBrush(cur ? c.accent : c.base);
            p.drawRoundedRect(r, 2, 2);
            const bool room = cw >= 40;  // narrow panel: numbers only
            if (room) {
                double dot = std::clamp(std::log2(s + 1) * 1.6, 1.5, 9.0);
                p.setPen(Qt::NoPen);
                p.setBrush(cur ? c.accentText : c.text);
                p.drawEllipse(QPointF(r.left() + 8, r.center().y()), dot / 2, dot / 2);
            }
            p.setPen(cur ? c.accentText : c.text);
            QFont f = font();
            f.setPointSizeF(std::max(6.0, f.pointSizeF() - 1.5));
            p.setFont(f);
            if (room) p.drawText(r.adjusted(14, 0, -2, 0), Qt::AlignVCenter | Qt::AlignRight, QString::number(s));
            else p.drawText(r, Qt::AlignCenter, QString::number(s));
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        int n = int(sizes().size()), per = (n + 1) / 2;
        double cw = width() / double(per);
        int i = int(e->position().y() / 22) * per + int(e->position().x() / cw);
        if (i >= 0 && i < n) m_pick(sizes()[size_t(i)]);
    }

private:
    static const std::vector<double>& sizes() {
        static const std::vector<double> s = {1, 2, 3, 5, 7, 10, 15, 20, 30, 50, 70, 100, 150, 250, 400, 700};
        return s;
    }
    std::function<void(double)> m_pick;
    double m_cur = 0;
};

QLabel* hint(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setObjectName("sectionTitle");
    return l;
}
}  // namespace

BrushControls::BrushControls(QWidget* parent, std::function<void()> changed) : QObject(parent), m_changed(std::move(changed)) {}

void BrushControls::changed() {
    if (m_loading) return;
    if (m_changed) m_changed();
}

ValueBar* BrushControls::bar(QWidget* parent, const QString& label, double min, double max, int dec, double* field, double scale,
                             const QString& suffix, bool log, const QString& tip) {
    auto* b = new ValueBar(label, min, max, dec, parent);
    b->setDisplayScale(scale);
    b->setSuffix(suffix);
    b->setLogarithmic(log);
    if (!tip.isEmpty()) b->setToolTip(tip);
    connect(b, &ValueBar::valueChanged, this, [this, field](double v) {
        if (m_loading) return;
        *field = v;
        changed();
    });
    m_loaders << [b, field] { b->setValue(*field); };
    return b;
}

ValueBar* BrushControls::intBar(QWidget* parent, const QString& label, int min, int max, int* field, const QString& tip) {
    auto* b = new ValueBar(label, min, max, 0, parent);
    if (!tip.isEmpty()) b->setToolTip(tip);
    connect(b, &ValueBar::valueChanged, this, [this, field](double v) {
        if (m_loading) return;
        const int iv = int(std::lround(v));
        const bool pagesChange = (*field <= 1) != (iv <= 1) || (*field <= 0) != (iv <= 0);
        *field = iv;
        changed();
        if (pagesChange) Q_EMIT visibilityChanged();
    });
    m_loaders << [b, field] { b->setValue(*field); };
    return b;
}

QCheckBox* BrushControls::check(QWidget* parent, const QString& label, bool* field, const QString& tip) {
    auto* c = new QCheckBox(label, parent);
    if (!tip.isEmpty()) c->setToolTip(tip);
    connect(c, &QCheckBox::toggled, this, [this, field](bool on) {
        if (m_loading) return;
        *field = on;
        changed();
        Q_EMIT visibilityChanged();
    });
    m_loaders << [c, field] { c->setChecked(*field); };
    return c;
}

QWidget* BrushControls::page(QWidget* parent, QVBoxLayout** layout) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    *layout = v;
    return w;
}

QList<BrushControls::Page> BrushControls::build(QWidget* parent, bool compact) {
    QList<Page> pages;
    BrushSettings& s = settings;
    auto always = [](const BrushSettings&) { return true; };

    // ---- the basics
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        auto* row = new QHBoxLayout;
        auto* engine = new QComboBox(w);
        for (int e = 0; e <= 4; ++e) engine->addItem(T(brushEngineName(BrushEngine(e))), e);
        engine->setToolTip(T("How the brush puts paint on the canvas"));
        auto* mode = new QComboBox(w);
        for (BlendMode bm : blendModeUiOrder()) mode->addItem(T(blendModeName(bm)), int(bm));
        mode->setToolTip(T("Brush blending mode"));
        row->addWidget(engine, 1);
        row->addWidget(mode, 1);
        v->addLayout(row);
        connect(engine, &QComboBox::activated, this, [this, engine](int i) {
            settings.engine = BrushEngine(engine->itemData(i).toInt());
            changed();
            Q_EMIT visibilityChanged();
        });
        connect(mode, &QComboBox::activated, this, [this, mode](int i) {
            settings.paintMode = BlendMode(mode->itemData(i).toInt());
            changed();
        });
        m_loaders << [this, engine, mode] {
            engine->setCurrentIndex(std::max(0, engine->findData(int(settings.engine))));
            mode->setCurrentIndex(std::max(0, mode->findData(int(settings.paintMode))));
            mode->setVisible(settings.engine == BrushEngine::Paint);
        };
        v->addWidget(bar(w, T("Size"), 0.5, 5000, 1, &s.size, 1, " px", true));
        if (compact) {
            auto* quick = new QuickSizes([this](double sz) {
                settings.size = sz;
                changed();
                load(settings);
            }, w);
            m_loaders << [this, quick] { quick->setCurrent(settings.size); };
            v->addWidget(quick);
        }
        v->addWidget(bar(w, T("Min size"), 0, 1, 2, &s.minSize, 100, "%", false, T("Size with the lightest pressure")));
        v->addWidget(bar(w, T("Opacity"), 0, 1, 2, &s.opacity, 100, "%"));
        v->addWidget(bar(w, T("Density"), 0, 1, 2, &s.density, 100, "%"));
        v->addWidget(bar(w, T("Min density"), 0, 1, 2, &s.minDensity, 100, "%", false, T("Density with the lightest pressure")));
        v->addWidget(bar(w, T("Hardness"), 0, 1, 2, &s.hardness, 100, "%"));
        v->addWidget(bar(w, T("Spacing"), 0.01, 2, 2, &s.spacing, 100, "%"));
        auto* pr = new QHBoxLayout;
        pr->addWidget(new QLabel(T("Pressure:"), w));
        pr->addWidget(check(w, T("Size"), &s.sizePressure));
        pr->addWidget(check(w, T("Density"), &s.densityPressure));
        pr->addStretch();
        v->addLayout(pr);
        pages << Page{"basic", T("Brush"), "brush", w, always};
    }
    // ---- mixing
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        v->addWidget(bar(w, T("Blending"), 0, 1, 2, &s.blending, 100, "%", false, T("How much of the colors under the brush it picks up. 0 = a plain brush.")));
        v->addWidget(bar(w, T("Dilution"), 0, 1, 2, &s.dilution, 100, "%", false, T("Water: makes the paint transparent")));
        v->addWidget(bar(w, T("Persistence"), 0, 1, 2, &s.persistence, 100, "%", false, T("How long the picked up color stays in the brush")));
        v->addWidget(bar(w, T("Fringe"), 0, 1, 2, &s.fringe, 100, "%", false, T("Darker edges, like watercolor")));
        v->addWidget(check(w, T("Light pressure mixes more"), &s.blendingPressure));
        pages << Page{"mixing", T("Mixing"), "blender", w,
                      [](const BrushSettings& b) { return b.engine == BrushEngine::Mix || b.engine == BrushEngine::Smudge; }};
    }
    // ---- lines and dots (hatching, stipple) and the second color
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        v->addWidget(intBar(w, T("Lines"), 1, 40, &s.bundleLines, T("Lines in one stroke (hatching). 1 = an ordinary brush.")));
        QWidget* lines = new QWidget(w);
        auto* lv = new QVBoxLayout(lines);
        lv->setContentsMargins(0, 0, 0, 0);
        lv->setSpacing(3);
        lv->addWidget(bar(lines, T("Line thickness"), 0.05, 1, 2, &s.bundleThickness, 100, "%", false, T("Width of each line compared to the space between the lines")));
        lv->addWidget(bar(lines, T("Thickness variation"), 0, 1, 2, &s.bundleThicknessJitter, 100, "%"));
        lv->addWidget(bar(lines, T("Wobble"), 0, 1, 2, &s.bundleWobble, 100, "%", false, T("The lines drift a little, like drawn by hand")));
        lv->addWidget(bar(lines, T("Different lengths"), 0, 1, 2, &s.bundleLengthJitter, 100, "%", false, T("The lines start at different places")));
        v->addWidget(lines);
        m_loaders << [this, lines] { lines->setEnabled(settings.bundleLines > 1); };
        v->addWidget(intBar(w, T("Dots"), 0, 60, &s.particles, T("Stipple: every dab scatters this many dots inside the brush. 0 = off.")));
        auto* dotSize = bar(w, T("Dot size"), 0.01, 1, 2, &s.particleSize, 100, "%", false, T("Dot size compared to the brush size"));
        v->addWidget(dotSize);
        m_loaders << [this, dotSize] { dotSize->setEnabled(settings.particles > 0); };
        v->addWidget(bar(w, T("Second color"), 0, 1, 2, &s.subColorShare, 100, "%", false,
                         T("This part of the lines, dots or dabs is painted with the second color (the back color of the color panel), "
                           "for example white lines in hatching.")));
        v->addWidget(hint(w, T("The second color is the back color of the color panel (X swaps the colors).")));
        pages << Page{"lines", T("Lines and dots"), "hatching", w, always};
    }
    // ---- tip shape
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        auto* shape = new QComboBox(w);
        m_shapeCombos << shape;
        v->addWidget(shape);
        connect(shape, &QComboBox::activated, this, [this, shape](int i) {
            settings.shape = shape->itemData(i).toString();
            changed();
        });
        m_loaders << [this, shape] { shape->setCurrentIndex(std::max(0, shape->findData(settings.shape))); };
        v->addWidget(bar(w, T("Shape strength"), 0, 1, 2, &s.shapeStrength, 100, "%"));
        v->addWidget(check(w, T("Shape is the whole tip"), &s.shapeAsTip));
        v->addWidget(bar(w, T("Roundness"), 0.02, 1, 2, &s.roundness, 100, "%"));
        v->addWidget(bar(w, T("Angle"), -180, 180, 0, &s.angle, 1, QString(QChar(0xB0))));
        v->addWidget(check(w, T("Angle follows the stroke"), &s.angleFollowsStroke));
        v->addWidget(check(w, T("Angle from pen tilt"), &s.angleFromTilt));
        v->addWidget(check(w, T("Anti-aliasing"), &s.antialias));
        pages << Page{"tip", T("Tip shape"), "brush", w, always};
    }
    // ---- texture
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        auto* tex = new QComboBox(w);
        m_textureCombos << tex;
        v->addWidget(tex);
        connect(tex, &QComboBox::activated, this, [this, tex](int i) {
            settings.texture = tex->itemData(i).toString();
            if (!settings.texture.isEmpty() && settings.textureStrength <= 0) settings.textureStrength = 0.5;
            changed();
            load(settings);
        });
        m_loaders << [this, tex] { tex->setCurrentIndex(std::max(0, tex->findData(settings.texture))); };
        v->addWidget(bar(w, T("Strength"), 0, 1, 2, &s.textureStrength, 100, "%"));
        v->addWidget(bar(w, T("Texture size"), 0.05, 20, 2, &s.textureScale, 100, "%", true));
        v->addWidget(bar(w, T("Texture density"), 0, 1, 2, &s.textureDensity, 100, "%", false,
                         T("Lower: only the high grain takes paint. Higher: the grain fills in.")));
        v->addWidget(check(w, T("Texture follows the stroke"), &s.textureFollowsStroke,
                           T("The texture runs along the stroke (hatching lines, bristles) instead of lying still on the paper")));
        v->addWidget(check(w, T("Invert"), &s.textureInvert));
        v->addWidget(check(w, T("Texture every dab"), &s.textureEachDab));
        v->addWidget(hint(w, T("Add your own textures: Help > Open textures folder.")));
        pages << Page{"texture", T("Texture"), "image", w, always};
    }
    // ---- dynamics
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        v->addWidget(bar(w, T("Size jitter"), 0, 1, 2, &s.sizeJitter, 100, "%"));
        v->addWidget(bar(w, T("Opacity jitter"), 0, 1, 2, &s.opacityJitter, 100, "%"));
        v->addWidget(bar(w, T("Angle jitter"), 0, 1, 2, &s.angleJitter, 100, "%"));
        v->addWidget(bar(w, T("Scatter"), 0, 5, 2, &s.scatter, 100, "%"));
        v->addWidget(bar(w, T("Hue jitter"), 0, 1, 2, &s.hueJitter, 100, "%"));
        v->addWidget(bar(w, T("Saturation jitter"), 0, 1, 2, &s.saturationJitter, 100, "%"));
        v->addWidget(bar(w, T("Value jitter"), 0, 1, 2, &s.valueJitter, 100, "%"));
        v->addWidget(bar(w, T("Taper start"), 0, 500, 0, &s.taperIn, 1, " px"));
        v->addWidget(bar(w, T("Taper end"), 0, 500, 0, &s.taperOut, 1, " px"));
        pages << Page{"dynamics", T("Dynamics"), "speed", w, always};
    }
    // ---- behaviour
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        v->addWidget(check(w, T("Build up (airbrush style)"), &s.buildup));
        v->addWidget(check(w, T("Spray while pen is still"), &s.airbrush));
        v->addWidget(bar(w, T("Spray rate"), 1, 200, 0, &s.airbrushRate, 1, "/s"));
        v->addWidget(check(w, T("Keep opacity"), &s.keepOpacity));
        v->addWidget(check(w, T("Own pressure curve"), &s.customPressureCurve));
        auto* curveBtn = new QPushButton(T("Edit brush pressure curve..."), w);
        v->addWidget(curveBtn);
        connect(curveBtn, &QPushButton::clicked, this, [this, w] {
            QDialog d(w);
            d.setWindowTitle(T("Brush pressure curve"));
            auto* l = new QVBoxLayout(&d);
            auto* ce = new CurveEditor(&d);
            ce->setPoints(settings.pressureCurve);
            l->addWidget(ce);
            auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
            l->addWidget(bb);
            connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
            connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
            if (d.exec() == QDialog::Accepted) {
                settings.pressureCurve = ce->points();
                settings.customPressureCurve = true;
                changed();
                load(settings);
            }
        });
        pages << Page{"behaviour", T("Behaviour"), "settings", w, always};
    }
    // narrow panels: combo boxes shrink instead of widening the dock
    for (const Page& p : pages)
        for (QComboBox* cb : p.widget->findChildren<QComboBox*>()) {
            cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            cb->setMinimumContentsLength(4);
        }
    refreshLists();
    return pages;
}

void BrushControls::refreshLists() {
    auto& lib = TextureLibrary::instance();
    for (QComboBox* shape : m_shapeCombos) {
        shape->clear();
        shape->addItem(T("Round"), QString());
        for (const QString& n : lib.names(TextureKind::Shape)) {
            auto t = lib.get(TextureKind::Shape, n);
            shape->addItem(QIcon(QPixmap::fromImage(t->preview)), T(n), n);
        }
    }
    for (QComboBox* tex : m_textureCombos) {
        tex->clear();
        tex->addItem(T("None"), QString());
        for (TextureKind k : {TextureKind::Brush, TextureKind::Paper})
            for (const QString& n : lib.names(k)) {
                auto t = lib.get(k, n);
                if (tex->findData(n) < 0) tex->addItem(QIcon(QPixmap::fromImage(t->preview)), T(n), n);
            }
    }
}

void BrushControls::load(const BrushSettings& b) {
    m_loading = true;
    settings = b;
    for (auto& f : m_loaders) f();
    m_loading = false;
}

}  // namespace gx
