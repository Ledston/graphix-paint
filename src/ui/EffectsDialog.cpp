#include "ui/EffectsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "app/I18n.h"
#include "core/BlendModes.h"
#include "core/Document.h"
#include "core/Textures.h"
#include "ui/Widgets.h"

namespace gx {

QList<QPair<QString, QString>> effectKeys() {
    return {{"outline", "Outline"},          {"shadow", "Drop shadow"},          {"outerGlow", "Outer glow"},
            {"innerShadow", "Inner shadow"}, {"innerGlow", "Inner glow"},        {"overlay", "Color overlay"},
            {"gradient", "Gradient overlay"}, {"blur", "Blur"},                  {"texture", "Paper texture"},
            {"grain", "Grain (noise)"},      {"screentone", "Screentone"},       {"chromatic", "Chromatic aberration"}};
}

bool* effectSwitch(LayerEffects& fx, const QString& key) {
    if (key == "outline") return &fx.outline;
    if (key == "shadow") return &fx.shadow;
    if (key == "outerGlow") return &fx.outerGlow;
    if (key == "innerShadow") return &fx.innerShadow;
    if (key == "innerGlow") return &fx.innerGlow;
    if (key == "overlay") return &fx.overlay;
    if (key == "gradient") return &fx.gradientOverlay;
    if (key == "blur") return &fx.blur;
    if (key == "texture") return &fx.texture;
    if (key == "grain") return &fx.grain;
    if (key == "screentone") return &fx.screentone;
    if (key == "chromatic") return &fx.chromatic;
    return nullptr;
}

void addEffectControls(const QString& key, LayerEffects* fx, QVBoxLayout* lay, QWidget* body, QObject* ctx, const std::function<void()>& changed) {
    auto bar = [&](const QString& label, double min, double max, int dec, float* field, double scale = 1, const QString& suffix = {}) {
        auto* b = new ValueBar(label, min, max, dec, body);
        b->setDisplayScale(scale);
        b->setSuffix(suffix);
        b->setValue(*field);
        QObject::connect(b, &ValueBar::valueChanged, ctx, [field, changed](double x) { *field = float(x); changed(); });
        lay->addWidget(b);
        return b;
    };
    auto offsetBars = [&](QPointF* field) {
        auto* bx = new ValueBar(T("Offset X"), -200, 200, 0, body);
        bx->setValue(field->x());
        QObject::connect(bx, &ValueBar::valueChanged, ctx, [field, changed](double x) { field->setX(x); changed(); });
        auto* by = new ValueBar(T("Offset Y"), -200, 200, 0, body);
        by->setValue(field->y());
        QObject::connect(by, &ValueBar::valueChanged, ctx, [field, changed](double y) { field->setY(y); changed(); });
        lay->addWidget(bx);
        lay->addWidget(by);
    };
    auto color = [&](QColor* field, const QString& label = {}) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label.isEmpty() ? T("Color") : label, body));
        auto* cb = new ColorButton(*field, body);
        QObject::connect(cb, &ColorButton::colorChanged, ctx, [field, changed](const QColor& c) { *field = c; changed(); });
        row->addWidget(cb);
        row->addStretch();
        lay->addLayout(row);
    };
    auto blendCombo = [&](int* field) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(T("Blending mode"), body));
        auto* cb = new QComboBox(body);
        for (BlendMode bm : blendModeUiOrder()) cb->addItem(T(blendModeName(bm)), int(bm));
        cb->setCurrentIndex(std::max(0, cb->findData(*field)));
        QObject::connect(cb, &QComboBox::currentIndexChanged, ctx, [cb, field, changed] { *field = cb->currentData().toInt(); changed(); });
        row->addWidget(cb, 1);
        lay->addLayout(row);
    };
    auto checkBox = [&](const QString& text, bool* field) {
        auto* c = new QCheckBox(text, body);
        c->setChecked(*field);
        QObject::connect(c, &QCheckBox::toggled, ctx, [field, changed](bool x) { *field = x; changed(); });
        lay->addWidget(c);
    };

    if (key == "outline") {
        color(&fx->outlineColor);
        bar(T("Size"), 0.5, 100, 1, &fx->outlineSize, 1, " px");
        bar(T("Opacity"), 0, 1, 2, &fx->outlineOpacity, 100, "%");
    } else if (key == "shadow") {
        color(&fx->shadowColor);
        bar(T("Opacity"), 0, 1, 2, &fx->shadowOpacity, 100, "%");
        bar(T("Blur"), 0, 100, 1, &fx->shadowBlur, 1, " px");
        offsetBars(&fx->shadowOffset);
    } else if (key == "outerGlow") {
        color(&fx->outerGlowColor);
        bar(T("Opacity"), 0, 1, 2, &fx->outerGlowOpacity, 100, "%");
        bar(T("Size"), 0.5, 150, 1, &fx->outerGlowSize, 1, " px");
    } else if (key == "innerShadow") {
        color(&fx->innerShadowColor);
        bar(T("Opacity"), 0, 1, 2, &fx->innerShadowOpacity, 100, "%");
        bar(T("Blur"), 0, 100, 1, &fx->innerShadowBlur, 1, " px");
        offsetBars(&fx->innerShadowOffset);
    } else if (key == "innerGlow") {
        color(&fx->innerGlowColor);
        bar(T("Opacity"), 0, 1, 2, &fx->innerGlowOpacity, 100, "%");
        bar(T("Size"), 0.5, 150, 1, &fx->innerGlowSize, 1, " px");
    } else if (key == "overlay") {
        color(&fx->overlayColor);
        bar(T("Opacity"), 0, 1, 2, &fx->overlayOpacity, 100, "%");
    } else if (key == "gradient") {
        color(&fx->gradientStart, T("From"));
        color(&fx->gradientEnd, T("To"));
        bar(T("Angle"), -180, 180, 0, &fx->gradientAngle, 1, QString(QChar(0xB0)));
        bar(T("Opacity"), 0, 1, 2, &fx->gradientOpacity, 100, "%");
        blendCombo(&fx->gradientBlend);
    } else if (key == "blur") {
        bar(T("Radius"), 0.5, 100, 1, &fx->blurRadius, 1, " px");
    } else if (key == "texture") {
        auto* combo = new QComboBox(body);
        for (TextureKind k : {TextureKind::Paper, TextureKind::Brush})
            for (const QString& n : TextureLibrary::instance().names(k))
                if (combo->findData(n) < 0) combo->addItem(T(n), n);
        if (fx->textureName.isEmpty() && combo->count()) fx->textureName = combo->itemData(0).toString();
        combo->setCurrentIndex(std::max(0, combo->findData(fx->textureName)));
        QObject::connect(combo, &QComboBox::currentIndexChanged, ctx, [fx, combo, changed] {
            fx->textureName = combo->currentData().toString();
            changed();
        });
        lay->addWidget(combo);
        bar(T("Strength"), 0, 1, 2, &fx->textureStrength, 100, "%");
        bar(T("Scale"), 0.1, 8, 2, &fx->textureScale, 100, "%");
    } else if (key == "grain") {
        bar(T("Amount"), 0, 1, 2, &fx->grainAmount, 100, "%");
        bar(T("Grain size"), 1, 8, 1, &fx->grainSize, 1, " px");
        checkBox(T("Monochrome"), &fx->grainMono);
        blendCombo(&fx->grainBlend);
        auto* seed = new QPushButton(T("New random pattern"), body);
        QObject::connect(seed, &QPushButton::clicked, ctx, [fx, changed] { ++fx->grainSeed; changed(); });
        lay->addWidget(seed);
    } else if (key == "screentone") {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(T("Shape"), body));
        auto* sh = new QComboBox(body);
        sh->addItems({T("Dots"), T("Lines"), T("Squares")});
        sh->setCurrentIndex(std::clamp(fx->toneShape, 0, 2));
        QObject::connect(sh, &QComboBox::currentIndexChanged, ctx, [fx, changed](int i) { fx->toneShape = i; changed(); });
        row->addWidget(sh, 1);
        lay->addLayout(row);
        bar(T("Cell size"), 2, 64, 1, &fx->toneCell, 1, " px");
        bar(T("Angle"), -90, 90, 0, &fx->toneAngle, 1, QString(QChar(0xB0)));
        color(&fx->toneColor, T("Ink"));
        checkBox(T("Light areas become transparent"), &fx->toneKeepWhite);
    } else if (key == "chromatic") {
        bar(T("Shift"), -40, 40, 1, &fx->chromaShift, 1, " px");
        bar(T("Angle"), -180, 180, 0, &fx->chromaAngle, 1, QString(QChar(0xB0)));
    }
}

EffectsDialog::EffectsDialog(Document* doc, quint32 layerId, QWidget* parent, const QString& focus)
    : QDialog(parent), m_doc(doc), m_layer(layerId) {
    setWindowTitle(T("Layer effects"));
    Layer* l = doc->layer(layerId);
    m_fx = l->fx;
    doc->pushUndo(T("Layer effects"));

    auto* outer = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 0, 6, 0);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    QGroupBox* focusBox = nullptr;
    for (const auto& e : effectKeys()) {
        bool* on = effectSwitch(m_fx, e.first);
        auto* g = new QGroupBox(T(e.second), body);
        g->setCheckable(true);
        g->setChecked(*on);
        connect(g, &QGroupBox::toggled, this, [this, on](bool x) { *on = x; apply(); });
        auto* gl = new QVBoxLayout(g);
        v->addWidget(g);
        if (e.first == focus) focusBox = g;
        addEffectControls(e.first, &m_fx, gl, body, this, [this] { apply(); });
    }
    v->addStretch();

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* none = bb->addButton(T("Turn all off"), QDialogButtonBox::ResetRole);
    connect(none, &QPushButton::clicked, this, [this] {
        for (const auto& e : effectKeys()) *effectSwitch(m_fx, e.first) = false;
        for (QGroupBox* g : findChildren<QGroupBox*>()) {
            const QSignalBlocker block(g);
            g->setChecked(false);
        }
        apply();
    });
    outer->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    setMinimumWidth(360);
    resize(380, 720);
    if (focusBox) QMetaObject::invokeMethod(this, [scroll, focusBox] { scroll->ensureWidgetVisible(focusBox, 0, 20); }, Qt::QueuedConnection);
}

void EffectsDialog::apply() {
    if (!m_doc) return;
    if (Layer* l = m_doc->layer(m_layer)) {
        l->fx = m_fx;
        m_doc->layerChanged(l);
        m_doc->layersChanged();
    }
}

void EffectsDialog::accept() {
    m_done = true;
    apply();
    QDialog::accept();
}

void EffectsDialog::reject() {
    if (!m_done && m_doc) m_doc->revertLastUndo();
    m_done = true;
    QDialog::reject();
}

}  // namespace gx
