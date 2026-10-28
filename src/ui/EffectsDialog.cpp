#include "ui/EffectsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "app/I18n.h"
#include "core/Document.h"
#include "core/Textures.h"
#include "ui/Widgets.h"

namespace gx {

EffectsDialog::EffectsDialog(Document* doc, quint32 layerId, QWidget* parent) : QDialog(parent), m_doc(doc), m_layer(layerId) {
    setWindowTitle(T("Layer effects"));
    Layer* l = doc->layer(layerId);
    m_fx = l->fx;
    doc->pushUndo(T("Layer effects"));
    auto* v = new QVBoxLayout(this);

    auto group = [&](const QString& title, bool* on) {
        auto* g = new QGroupBox(title, this);
        g->setCheckable(true);
        g->setChecked(*on);
        connect(g, &QGroupBox::toggled, this, [this, on](bool x) { *on = x; apply(); });
        auto* gl = new QVBoxLayout(g);
        v->addWidget(g);
        return gl;
    };
    auto bar = [&](QVBoxLayout* lay, const QString& label, double min, double max, int dec, float* field, double scale = 1,
                   const QString& suffix = {}) {
        auto* b = new ValueBar(label, min, max, dec, this);
        b->setDisplayScale(scale);
        b->setSuffix(suffix);
        b->setValue(*field);
        connect(b, &ValueBar::valueChanged, this, [this, field](double x) { *field = float(x); apply(); });
        lay->addWidget(b);
    };
    auto color = [&](QVBoxLayout* lay, QColor* field) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(T("Color"), this));
        auto* cb = new ColorButton(*field, this);
        connect(cb, &ColorButton::colorChanged, this, [this, field](const QColor& c) { *field = c; apply(); });
        row->addWidget(cb);
        row->addStretch();
        lay->addLayout(row);
    };

    auto* o = group(T("Outline"), &m_fx.outline);
    color(o, &m_fx.outlineColor);
    bar(o, T("Size"), 0.5, 100, 1, &m_fx.outlineSize, 1, " px");
    bar(o, T("Opacity"), 0, 1, 2, &m_fx.outlineOpacity, 100, "%");

    auto* s = group(T("Drop shadow"), &m_fx.shadow);
    color(s, &m_fx.shadowColor);
    bar(s, T("Opacity"), 0, 1, 2, &m_fx.shadowOpacity, 100, "%");
    bar(s, T("Blur"), 0, 100, 1, &m_fx.shadowBlur, 1, " px");
    {
        auto* bx = new ValueBar(T("Offset X"), -200, 200, 0, this);
        bx->setValue(m_fx.shadowOffset.x());
        connect(bx, &ValueBar::valueChanged, this, [this](double x) { m_fx.shadowOffset.setX(x); apply(); });
        auto* by = new ValueBar(T("Offset Y"), -200, 200, 0, this);
        by->setValue(m_fx.shadowOffset.y());
        connect(by, &ValueBar::valueChanged, this, [this](double y) { m_fx.shadowOffset.setY(y); apply(); });
        s->addWidget(bx);
        s->addWidget(by);
    }

    auto* b = group(T("Blur"), &m_fx.blur);
    bar(b, T("Radius"), 0.5, 100, 1, &m_fx.blurRadius, 1, " px");

    auto* c = group(T("Color overlay"), &m_fx.overlay);
    color(c, &m_fx.overlayColor);
    bar(c, T("Opacity"), 0, 1, 2, &m_fx.overlayOpacity, 100, "%");

    auto* t = group(T("Paper texture"), &m_fx.texture);
    auto* combo = new QComboBox(this);
    for (TextureKind k : {TextureKind::Paper, TextureKind::Brush})
        for (const QString& n : TextureLibrary::instance().names(k))
            if (combo->findText(n) < 0) combo->addItem(n);
    if (m_fx.textureName.isEmpty() && combo->count()) m_fx.textureName = combo->itemText(0);
    combo->setCurrentText(m_fx.textureName);
    connect(combo, &QComboBox::currentTextChanged, this, [this](const QString& n) { m_fx.textureName = n; apply(); });
    t->addWidget(combo);
    bar(t, T("Strength"), 0, 1, 2, &m_fx.textureStrength, 100, "%");
    bar(t, T("Scale"), 0.1, 8, 2, &m_fx.textureScale, 100, "%");

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    setMinimumWidth(320);
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
