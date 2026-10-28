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
    auto group = [&](const QString& key, const QString& title, bool* on) {
        auto* g = new QGroupBox(title, body);
        g->setCheckable(true);
        g->setChecked(*on);
        connect(g, &QGroupBox::toggled, this, [this, on](bool x) { *on = x; apply(); });
        auto* gl = new QVBoxLayout(g);
        v->addWidget(g);
        if (key == focus) focusBox = g;
        return gl;
    };
    auto bar = [&](QVBoxLayout* lay, const QString& label, double min, double max, int dec, float* field, double scale = 1,
                   const QString& suffix = {}) {
        auto* b = new ValueBar(label, min, max, dec, body);
        b->setDisplayScale(scale);
        b->setSuffix(suffix);
        b->setValue(*field);
        connect(b, &ValueBar::valueChanged, this, [this, field](double x) { *field = float(x); apply(); });
        lay->addWidget(b);
        return b;
    };
    auto offsetBars = [&](QVBoxLayout* lay, QPointF* field) {
        auto* bx = new ValueBar(T("Offset X"), -200, 200, 0, body);
        bx->setValue(field->x());
        connect(bx, &ValueBar::valueChanged, this, [this, field](double x) { field->setX(x); apply(); });
        auto* by = new ValueBar(T("Offset Y"), -200, 200, 0, body);
        by->setValue(field->y());
        connect(by, &ValueBar::valueChanged, this, [this, field](double y) { field->setY(y); apply(); });
        lay->addWidget(bx);
        lay->addWidget(by);
    };
    auto color = [&](QVBoxLayout* lay, QColor* field, const QString& label = {}) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label.isEmpty() ? T("Color") : label, body));
        auto* cb = new ColorButton(*field, body);
        connect(cb, &ColorButton::colorChanged, this, [this, field](const QColor& c) { *field = c; apply(); });
        row->addWidget(cb);
        row->addStretch();
        lay->addLayout(row);
    };
    auto blendCombo = [&](QVBoxLayout* lay, int* field) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(T("Blending mode"), body));
        auto* cb = new QComboBox(body);
        for (BlendMode bm : blendModeUiOrder()) cb->addItem(T(blendModeName(bm)), int(bm));
        cb->setCurrentIndex(std::max(0, cb->findData(*field)));
        connect(cb, &QComboBox::currentIndexChanged, this, [this, cb, field] { *field = cb->currentData().toInt(); apply(); });
        row->addWidget(cb, 1);
        lay->addLayout(row);
    };
    auto checkBox = [&](QVBoxLayout* lay, const QString& text, bool* field) {
        auto* c = new QCheckBox(text, body);
        c->setChecked(*field);
        connect(c, &QCheckBox::toggled, this, [this, field](bool x) { *field = x; apply(); });
        lay->addWidget(c);
    };

    // ---- around the shape
    auto* o = group("outline", T("Outline"), &m_fx.outline);
    color(o, &m_fx.outlineColor);
    bar(o, T("Size"), 0.5, 100, 1, &m_fx.outlineSize, 1, " px");
    bar(o, T("Opacity"), 0, 1, 2, &m_fx.outlineOpacity, 100, "%");

    auto* s = group("shadow", T("Drop shadow"), &m_fx.shadow);
    color(s, &m_fx.shadowColor);
    bar(s, T("Opacity"), 0, 1, 2, &m_fx.shadowOpacity, 100, "%");
    bar(s, T("Blur"), 0, 100, 1, &m_fx.shadowBlur, 1, " px");
    offsetBars(s, &m_fx.shadowOffset);

    auto* og = group("outerGlow", T("Outer glow"), &m_fx.outerGlow);
    color(og, &m_fx.outerGlowColor);
    bar(og, T("Opacity"), 0, 1, 2, &m_fx.outerGlowOpacity, 100, "%");
    bar(og, T("Size"), 0.5, 150, 1, &m_fx.outerGlowSize, 1, " px");

    // ---- inside the shape
    auto* is = group("innerShadow", T("Inner shadow"), &m_fx.innerShadow);
    color(is, &m_fx.innerShadowColor);
    bar(is, T("Opacity"), 0, 1, 2, &m_fx.innerShadowOpacity, 100, "%");
    bar(is, T("Blur"), 0, 100, 1, &m_fx.innerShadowBlur, 1, " px");
    offsetBars(is, &m_fx.innerShadowOffset);

    auto* ig = group("innerGlow", T("Inner glow"), &m_fx.innerGlow);
    color(ig, &m_fx.innerGlowColor);
    bar(ig, T("Opacity"), 0, 1, 2, &m_fx.innerGlowOpacity, 100, "%");
    bar(ig, T("Size"), 0.5, 150, 1, &m_fx.innerGlowSize, 1, " px");

    auto* c = group("overlay", T("Color overlay"), &m_fx.overlay);
    color(c, &m_fx.overlayColor);
    bar(c, T("Opacity"), 0, 1, 2, &m_fx.overlayOpacity, 100, "%");

    auto* go = group("gradient", T("Gradient overlay"), &m_fx.gradientOverlay);
    color(go, &m_fx.gradientStart, T("From"));
    color(go, &m_fx.gradientEnd, T("To"));
    bar(go, T("Angle"), -180, 180, 0, &m_fx.gradientAngle, 1, QString(QChar(0xB0)));
    bar(go, T("Opacity"), 0, 1, 2, &m_fx.gradientOpacity, 100, "%");
    blendCombo(go, &m_fx.gradientBlend);

    // ---- surface
    auto* b = group("blur", T("Blur"), &m_fx.blur);
    bar(b, T("Radius"), 0.5, 100, 1, &m_fx.blurRadius, 1, " px");

    auto* t = group("texture", T("Paper texture"), &m_fx.texture);
    auto* combo = new QComboBox(body);
    for (TextureKind k : {TextureKind::Paper, TextureKind::Brush})
        for (const QString& n : TextureLibrary::instance().names(k))
            if (combo->findText(n) < 0) combo->addItem(n);
    if (m_fx.textureName.isEmpty() && combo->count()) m_fx.textureName = combo->itemText(0);
    combo->setCurrentText(m_fx.textureName);
    connect(combo, &QComboBox::currentTextChanged, this, [this](const QString& n) { m_fx.textureName = n; apply(); });
    t->addWidget(combo);
    bar(t, T("Strength"), 0, 1, 2, &m_fx.textureStrength, 100, "%");
    bar(t, T("Scale"), 0.1, 8, 2, &m_fx.textureScale, 100, "%");

    auto* gr = group("grain", T("Grain (noise)"), &m_fx.grain);
    bar(gr, T("Amount"), 0, 1, 2, &m_fx.grainAmount, 100, "%");
    bar(gr, T("Grain size"), 1, 8, 1, &m_fx.grainSize, 1, " px");
    checkBox(gr, T("Monochrome"), &m_fx.grainMono);
    blendCombo(gr, &m_fx.grainBlend);
    auto* seed = new QPushButton(T("New random pattern"), body);
    connect(seed, &QPushButton::clicked, this, [this] { ++m_fx.grainSeed; apply(); });
    gr->addWidget(seed);

    auto* st = group("screentone", T("Screentone"), &m_fx.screentone);
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(T("Shape"), body));
        auto* sh = new QComboBox(body);
        sh->addItems({T("Dots"), T("Lines"), T("Squares")});
        sh->setCurrentIndex(std::clamp(m_fx.toneShape, 0, 2));
        connect(sh, &QComboBox::currentIndexChanged, this, [this](int i) { m_fx.toneShape = i; apply(); });
        row->addWidget(sh, 1);
        st->addLayout(row);
    }
    bar(st, T("Cell size"), 2, 64, 1, &m_fx.toneCell, 1, " px");
    bar(st, T("Angle"), -90, 90, 0, &m_fx.toneAngle, 1, QString(QChar(0xB0)));
    color(st, &m_fx.toneColor, T("Ink"));
    checkBox(st, T("Light areas become transparent"), &m_fx.toneKeepWhite);

    auto* ca = group("chromatic", T("Chromatic aberration"), &m_fx.chromatic);
    bar(ca, T("Shift"), -40, 40, 1, &m_fx.chromaShift, 1, " px");
    bar(ca, T("Angle"), -180, 180, 0, &m_fx.chromaAngle, 1, QString(QChar(0xB0)));

    v->addStretch();

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* none = bb->addButton(T("Turn all off"), QDialogButtonBox::ResetRole);
    connect(none, &QPushButton::clicked, this, [this] {
        LayerEffects off = m_fx;
        off.outline = off.shadow = off.blur = off.overlay = off.texture = off.innerShadow = off.outerGlow = off.innerGlow =
            off.gradientOverlay = off.grain = off.screentone = off.chromatic = false;
        m_fx = off;
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
