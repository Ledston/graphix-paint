#include "ui/BrushControls.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <cmath>
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
// Two rows of common sizes.
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

// A row of round tips from very soft to very hard: one click sets the edge hardness.
class EdgePresets : public QWidget {
public:
    explicit EdgePresets(std::function<void(double)> pick, QWidget* parent) : QWidget(parent), m_pick(std::move(pick)) {
        setFixedHeight(30);
        setMouseTracking(true);
        setToolTip(T("Edge: from very soft to very hard"));
    }
    void setCurrent(double h) {
        m_cur = h;
        update();
    }
    static int nearest(double h) {
        const QVector<double>& v = BrushSettings::hardnessPresets();
        int best = 0;
        for (int i = 1; i < v.size(); ++i)
            if (std::abs(v[i] - h) < std::abs(v[best] - h)) best = i;
        return best;
    }

protected:
    QRectF cell(int i) const {
        const int n = int(BrushSettings::hardnessPresets().size());
        const double cw = width() / double(n);
        return QRectF(i * cw + 1, 1, cw - 2, height() - 2);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const ThemeColors& c = Theme::colors();
        const QVector<double>& v = BrushSettings::hardnessPresets();
        const int cur = nearest(m_cur);
        const bool exact = std::abs(v[cur] - m_cur) < 0.02;
        for (int i = 0; i < v.size(); ++i) {
            const QRectF r = cell(i);
            const bool on = i == cur;
            p.setPen(on ? QPen(c.accent, exact ? 2.0 : 1.2) : QPen(c.border, 1));
            p.setBrush(i == m_hover ? c.button.lighter(115) : c.base);
            p.drawRoundedRect(r, 4, 4);
            // the tip: solid inside the hardness radius, fading to the edge
            const double rad = std::min(r.width(), r.height()) * 0.36;
            QRadialGradient g(r.center(), rad);
            QColor ink = c.text;
            g.setColorAt(0, ink);
            g.setColorAt(std::clamp(v[i] * 0.97, 0.0, 0.97), ink);
            ink.setAlpha(0);
            g.setColorAt(1, ink);
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(r.center(), rad, rad);
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        for (int i = 0; i < BrushSettings::hardnessPresets().size(); ++i)
            if (cell(i).contains(e->position())) m_pick(BrushSettings::hardnessPresets()[i]);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        int h = -1;
        for (int i = 0; i < BrushSettings::hardnessPresets().size(); ++i)
            if (cell(i).contains(e->position())) h = i;
        if (h != m_hover) {
            m_hover = h;
            update();
        }
    }
    void leaveEvent(QEvent*) override {
        m_hover = -1;
        update();
    }

private:
    std::function<void(double)> m_pick;
    double m_cur = 0.9;
    int m_hover = -1;
};

QLabel* title(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setObjectName("sectionTitle");
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    l->setContentsMargins(0, 6, 0, 0);
    return l;
}

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
    auto edges = [this](QWidget* w) {
        auto* e = new EdgePresets([this](double h) {
            settings.hardness = h;
            changed();
            load(settings);
        }, w);
        m_loaders << [this, e] { e->setCurrent(settings.hardness); };
        return e;
    };

    // ---- the tool panel: only what is changed all the time
    if (compact) {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        v->addWidget(bar(w, T("Size"), 0.5, 5000, 1, &s.size, 1, " px", true));
        auto* quick = new QuickSizes([this](double sz) {
            settings.size = sz;
            changed();
            load(settings);
        }, w);
        m_loaders << [this, quick] { quick->setCurrent(settings.size); };
        v->addWidget(quick);
        v->addWidget(edges(w));
        v->addWidget(bar(w, T("Opacity"), 0, 1, 2, &s.opacity, 100, "%"));
        pages << Page{"basic", T("Brush"), "brush", w, always};
        refreshLists();
        return pages;
    }

    // ---- the brush: everything that makes the brush, in one place
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
            mode->setEnabled(settings.engine == BrushEngine::Paint);
        };
        v->addWidget(bar(w, T("Size"), 0.5, 5000, 1, &s.size, 1, " px", true));
        v->addWidget(bar(w, T("Min size"), 0, 1, 2, &s.minSize, 100, "%", false, T("Size with the lightest pressure")));
        v->addWidget(bar(w, T("Opacity"), 0, 1, 2, &s.opacity, 100, "%", false,
                         T("The whole stroke is never more opaque than this (the digit keys 1..0 set it)")));
        v->addWidget(bar(w, T("Density"), 0, 1, 2, &s.density, 100, "%", false, T("How much paint every dab puts down")));
        v->addWidget(bar(w, T("Min density"), 0, 1, 2, &s.minDensity, 100, "%", false, T("Density with the lightest pressure")));

        v->addWidget(title(w, T("Brush shape")));
        auto* shape = new QComboBox(w);
        m_shapeCombos << shape;
        v->addWidget(shape);
        connect(shape, &QComboBox::activated, this, [this, shape](int i) {
            settings.shape = shape->itemData(i).toString();
            changed();
            load(settings);
        });
        m_loaders << [this, shape] { shape->setCurrentIndex(std::max(0, shape->findData(settings.shape))); };
        v->addWidget(edges(w));
        v->addWidget(bar(w, T("Edge hardness"), 0, 1, 2, &s.hardness, 100, "%", false, T("Fine tuning between the edge buttons above")));
        auto* shapeStrength = bar(w, T("Shape strength"), 0, 1, 2, &s.shapeStrength, 100, "%");
        v->addWidget(shapeStrength);
        m_loaders << [this, shapeStrength] { shapeStrength->setEnabled(!settings.shape.isEmpty()); };

        v->addWidget(title(w, T("Brush texture")));
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
        auto* texBox = new QWidget(w);
        auto* tv = new QVBoxLayout(texBox);
        tv->setContentsMargins(0, 0, 0, 0);
        tv->setSpacing(3);
        tv->addWidget(bar(texBox, T("Strength"), 0, 1, 2, &s.textureStrength, 100, "%"));
        tv->addWidget(bar(texBox, T("Texture size"), 0.05, 20, 2, &s.textureScale, 100, "%", true));
        v->addWidget(texBox);
        m_loaders << [this, texBox] { texBox->setEnabled(!settings.texture.isEmpty()); };

        // mixing: only for the brushes that pick up color
        auto* mix = new QWidget(w);
        auto* mv = new QVBoxLayout(mix);
        mv->setContentsMargins(0, 0, 0, 0);
        mv->setSpacing(3);
        mv->addWidget(title(mix, T("Mixing")));
        mv->addWidget(bar(mix, T("Blending"), 0, 1, 2, &s.blending, 100, "%", false, T("How much of the colors under the brush it picks up. 0 = a plain brush.")));
        mv->addWidget(bar(mix, T("Dilution"), 0, 1, 2, &s.dilution, 100, "%", false, T("Water: makes the paint transparent")));
        mv->addWidget(bar(mix, T("Persistence"), 0, 1, 2, &s.persistence, 100, "%", false, T("How long the picked up color stays in the brush")));
        mv->addWidget(bar(mix, T("Fringe"), 0, 1, 2, &s.fringe, 100, "%", false, T("Darker edges, like watercolor")));
        v->addWidget(mix);
        m_loaders << [this, mix] { mix->setVisible(settings.engine == BrushEngine::Mix || settings.engine == BrushEngine::Smudge); };
        v->addWidget(check(w, T("Keep opacity"), &s.keepOpacity, T("Paint only where the layer already has paint")));

        v->addWidget(title(w, T("Pressure")));
        auto* pr = new QHBoxLayout;
        pr->addWidget(check(w, T("Size"), &s.sizePressure));
        pr->addWidget(check(w, T("Density"), &s.densityPressure));
        auto* mixPr = check(w, T("Mixing"), &s.blendingPressure, T("Light pressure mixes more"));
        pr->addWidget(mixPr);
        pr->addStretch();
        v->addLayout(pr);
        const QString softTip = T("How soft the pen feels: 100 = as the pressure curve, less = needs a harder press, "
                                  "more = a light touch is enough.");
        v->addWidget(bar(w, T("Hard <-> soft: size"), 0, 200, 0, &s.sizeSoftness, 1, "", false, softTip));
        v->addWidget(bar(w, T("Hard <-> soft: density"), 0, 200, 0, &s.densitySoftness, 1, "", false, softTip));
        auto* mixSoft = bar(w, T("Hard <-> soft: mixing"), 0, 200, 0, &s.blendSoftness, 1, "", false,
                            T("How soft the pen feels for mixing: 100 = as the pressure curve, more = a light touch is enough."));
        v->addWidget(mixSoft);
        m_loaders << [this, mixPr, mixSoft] {
            const bool mixing = settings.engine == BrushEngine::Mix || settings.engine == BrushEngine::Smudge;
            mixPr->setVisible(mixing);
            mixSoft->setVisible(mixing);
        };
        v->addWidget(title(w, T("Stroke")));
        v->addWidget(bar(w, T("Spacing"), 0.01, 2, 2, &s.spacing, 100, "%", false, T("Distance between the dabs, a part of the brush size")));
        pages << Page{"basic", T("Brush"), "brush", w, always};
    }
    // ---- stabilizer, cursor and the quick key
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        auto* own = new QCheckBox(T("Own stabilizer for this brush"), w);
        own->setToolTip(T("Off: the brush uses the stabilizer of the toolbar"));
        v->addWidget(own);
        auto* smode = new QComboBox(w);
        smode->addItem(T("Off"), 0);
        smode->addItem(T("Average"), 1);
        smode->addItem(T("Speed (S)"), 2);
        smode->addItem(T("Rope"), 3);
        v->addWidget(smode);
        auto* slevel = new ValueBar(T("Stabilizer strength"), 0, 30, 0, w);
        v->addWidget(slevel);
        v->addWidget(hint(w, T("Average = levels 1-15, Speed = S-levels, Rope = pulled string. Without its own stabilizer "
                               "the brush follows the toolbar, and the toolbar shows the stabilizer of the brush that has one.")));
        connect(own, &QCheckBox::toggled, this, [this, smode](bool on) {
            if (m_loading) return;
            settings.stabilizerMode = on ? std::max(0, smode->currentData().toInt()) : -1;
            changed();
            load(settings);
        });
        connect(smode, &QComboBox::activated, this, [this, smode](int i) {
            if (settings.stabilizerMode < 0) return;
            settings.stabilizerMode = smode->itemData(i).toInt();
            changed();
        });
        connect(slevel, &ValueBar::valueChanged, this, [this](double x) {
            if (m_loading) return;
            settings.stabilizerLevel = int(std::lround(x));
            changed();
        });
        m_loaders << [this, own, smode, slevel] {
            const bool on = settings.stabilizerMode >= 0;
            own->setChecked(on);
            smode->setCurrentIndex(std::max(0, smode->findData(on ? settings.stabilizerMode : 1)));
            slevel->setValue(settings.stabilizerLevel);
            smode->setEnabled(on);
            slevel->setEnabled(on);
        };

        v->addWidget(title(w, T("Cursor")));
        auto* cur = new QComboBox(w);
        cur->addItem(T("As in Settings"), 0);
        cur->addItem(T("Brush outline"), 1);
        cur->addItem(T("Outline and cross"), 2);
        cur->addItem(T("Cross"), 3);
        cur->addItem(T("Dot"), 4);
        cur->setToolTip(T("How the pointer looks over the canvas with this brush"));
        v->addWidget(cur);
        connect(cur, &QComboBox::activated, this, [this, cur](int i) {
            settings.cursorStyle = cur->itemData(i).toInt();
            changed();
        });
        m_loaders << [this, cur] { cur->setCurrentIndex(std::max(0, cur->findData(settings.cursorStyle))); };

        v->addWidget(title(w, T("Quick key")));
        v->addWidget(check(w, T("Hold the key: only while it is held"), &s.holdReturns,
                           T("Hold the brush's quick key (or F for the eraser), paint, let go: the brush you had comes back. "
                             "A short tap still switches as usual.")));
        pages << Page{"control", T("Stabilizer and cursor"), "cursor", w, always};
    }
    // ---- tip shape (more)
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        v->addWidget(check(w, T("Shape is the whole tip"), &s.shapeAsTip));
        v->addWidget(bar(w, T("Roundness"), 0.02, 1, 2, &s.roundness, 100, "%"));
        v->addWidget(bar(w, T("Angle"), -180, 180, 0, &s.angle, 1, QString(QChar(0xB0))));
        v->addWidget(check(w, T("Angle follows the stroke"), &s.angleFollowsStroke));
        v->addWidget(check(w, T("Angle from pen tilt"), &s.angleFromTilt));
        v->addWidget(check(w, T("Anti-aliasing"), &s.antialias, T("Off: hard pixel edges (pixel art, binary pen)")));
        pages << Page{"tip", T("Tip shape"), "brush", w, always, true};
    }
    // ---- texture (more)
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        v->addWidget(bar(w, T("Texture density"), 0, 1, 2, &s.textureDensity, 100, "%", false,
                         T("Lower: only the high grain takes paint. Higher: the grain fills in.")));
        v->addWidget(check(w, T("Texture follows the stroke"), &s.textureFollowsStroke,
                           T("The texture runs along the stroke (hatching lines, bristles) instead of lying still on the paper")));
        v->addWidget(check(w, T("Invert"), &s.textureInvert));
        v->addWidget(check(w, T("Texture every dab"), &s.textureEachDab));
        v->addWidget(hint(w, T("Add your own textures: Help > Open textures folder.")));
        pages << Page{"texture", T("Texture"), "image", w, always, true};
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
        pages << Page{"lines", T("Lines and dots"), "hatching", w, always, true};
    }
    // ---- decor: stamps and ribbons
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        v->addWidget(hint(w, T("Stamps: every dab is a random picture of the set. Ribbons: a pattern runs along the stroke, as wide as the brush.")));
        auto* stamps = new QComboBox(w);
        stamps->setToolTip(T("Stamp set"));
        m_stampCombos << stamps;
        v->addWidget(stamps);
        connect(stamps, &QComboBox::activated, this, [this, stamps](int i) {
            settings.stampSet = stamps->itemData(i).toString();
            if (!settings.stampSet.isEmpty()) settings.ribbon.clear();
            changed();
            load(settings);
        });
        m_loaders << [this, stamps] { stamps->setCurrentIndex(std::max(0, stamps->findData(settings.stampSet))); };
        auto* ribbon = new QComboBox(w);
        ribbon->setToolTip(T("Ribbon pattern"));
        m_ribbonCombos << ribbon;
        v->addWidget(ribbon);
        connect(ribbon, &QComboBox::activated, this, [this, ribbon](int i) {
            settings.ribbon = ribbon->itemData(i).toString();
            if (!settings.ribbon.isEmpty()) settings.stampSet.clear();
            changed();
            load(settings);
        });
        m_loaders << [this, ribbon] { ribbon->setCurrentIndex(std::max(0, ribbon->findData(settings.ribbon))); };
        auto* len = bar(w, T("Pattern length"), 0.1, 10, 2, &s.ribbonLength, 100, "%", true, T("Stretches the ribbon pattern along the stroke"));
        v->addWidget(len);
        m_loaders << [this, len] { len->setEnabled(!settings.ribbon.isEmpty()); };
        v->addWidget(hint(w, T("Your own stamps: a folder of pictures in textures/stamps. Your own ribbons: pictures in textures/ribbons.")));
        pages << Page{"decor", T("Stamps and ribbons"), "stamp", w, always, true};
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
        pages << Page{"dynamics", T("Dynamics"), "speed", w, always, true};
    }
    // ---- behaviour
    {
        QVBoxLayout* v;
        QWidget* w = page(parent, &v);
        v->addWidget(check(w, T("Build up (airbrush style)"), &s.buildup));
        v->addWidget(check(w, T("Spray while pen is still"), &s.airbrush));
        v->addWidget(bar(w, T("Spray rate"), 1, 200, 0, &s.airbrushRate, 1, "/s"));
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
        pages << Page{"behaviour", T("Behaviour"), "settings", w, always, true};
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
    for (QComboBox* st : m_stampCombos) {
        st->clear();
        st->addItem(T("No stamps"), QString());
        for (const QString& n : lib.stampSets()) {
            const auto& pics = lib.stampSet(n);
            st->addItem(pics.empty() ? QIcon() : QIcon(QPixmap::fromImage(pics.front()->preview)), T(n), n);
        }
    }
    for (QComboBox* rb : m_ribbonCombos) {
        rb->clear();
        rb->addItem(T("No ribbon"), QString());
        for (const QString& n : lib.names(TextureKind::Ribbon)) {
            auto t = lib.get(TextureKind::Ribbon, n);
            rb->addItem(QIcon(QPixmap::fromImage(t->preview)), T(n), n);
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
