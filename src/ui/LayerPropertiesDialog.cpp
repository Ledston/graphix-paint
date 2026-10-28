#include "ui/LayerPropertiesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/I18n.h"
#include "core/BlendModes.h"
#include "core/Document.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

LayerPropertiesDialog::LayerPropertiesDialog(Document* doc, quint32 layerId, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_id(layerId) {
    setWindowTitle(T("Layer properties"));
    Layer* l = doc->layer(layerId);
    doc->pushUndo(T("Layer properties"));
    auto* v = new QVBoxLayout(this);
    auto* f = new QFormLayout;
    v->addLayout(f);

    // every change shows right away; Cancel puts everything back
    auto changed = [this] {
        m_changed = true;
        if (Layer* x = m_doc ? m_doc->layer(m_id) : nullptr) {
            m_doc->layerChanged(x);
            m_doc->layersChanged();
        }
    };
    auto layer = [this]() { return m_doc ? m_doc->layer(m_id) : nullptr; };

    auto* name = new QLineEdit(l->name, this);
    connect(name, &QLineEdit::textChanged, this, [layer, changed](const QString& t) {
        if (Layer* x = layer(); x && !t.trimmed().isEmpty()) {
            x->name = t.trimmed();
            changed();
        }
    });
    f->addRow(T("Name"), name);

    auto* op = new ValueBar(T("Opacity"), 0, 1, 2, this);
    op->setDisplayScale(100);
    op->setSuffix("%");
    op->setValue(l->opacity);
    connect(op, &ValueBar::valueChanged, this, [layer, changed](double x) {
        if (Layer* y = layer()) {
            y->opacity = float(x);
            changed();
        }
    });
    f->addRow(op);

    auto* blend = new QComboBox(this);
    for (BlendMode bm : blendModeUiOrder()) blend->addItem(T(blendModeName(bm)), int(bm));
    blend->setCurrentIndex(std::max(0, blend->findData(int(l->blend))));
    connect(blend, &QComboBox::currentIndexChanged, this, [blend, layer, changed] {
        if (Layer* y = layer()) {
            y->blend = BlendMode(blend->currentData().toInt());
            changed();
        }
    });
    f->addRow(T("Blending mode"), blend);

    if (l->isGroup()) {
        auto* pass = new QCheckBox(T("Pass through: the folder's blending modes affect the layers below it"), this);
        pass->setChecked(static_cast<GroupLayer*>(l)->passThrough);
        connect(pass, &QCheckBox::toggled, this, [layer, changed](bool on) {
            if (auto* g = dynamic_cast<GroupLayer*>(layer())) {
                g->passThrough = on;
                changed();
            }
        });
        f->addRow(pass);
    }

    auto* label = new QComboBox(this);
    const QList<QPair<QString, QColor>> labels = {{T("None"), QColor()}, {T("Red"), QColor("#e5534b")}, {T("Orange"), QColor("#f0883e")},
                                                  {T("Yellow"), QColor("#e3c34c")}, {T("Green"), QColor("#57ab5a")},
                                                  {T("Blue"), QColor("#539bf5")}, {T("Violet"), QColor("#b083f0")}};
    for (const auto& lc : labels) {
        QPixmap sw(12, 12);
        sw.fill(lc.second.isValid() ? lc.second : Qt::transparent);
        label->addItem(QIcon(sw), lc.first, lc.second);
        if (lc.second == l->labelColor || (!lc.second.isValid() && !l->labelColor.isValid())) label->setCurrentIndex(label->count() - 1);
    }
    connect(label, &QComboBox::currentIndexChanged, this, [label, layer, changed] {
        if (Layer* y = layer()) {
            y->labelColor = label->currentData().value<QColor>();
            changed();
        }
    });
    f->addRow(T("Label color"), label);

    auto flag = [&](const QString& text, bool Layer::*field, const QString& tip = {}) {
        auto* c = new QCheckBox(text, this);
        c->setChecked(l->*field);
        c->setToolTip(tip);
        connect(c, &QCheckBox::toggled, this, [layer, changed, field](bool on) {
            if (Layer* y = layer()) {
                y->*field = on;
                changed();
            }
        });
        f->addRow(c);
    };
    flag(T("Visible"), &Layer::visible);
    flag(T("Clipping"), &Layer::clipping, T("Clipping: use the layer (or folder) below as a mask"));
    flag(T("Preserve opacity"), &Layer::alphaLock, T("Preserve opacity: paint only where the layer already has paint"));
    flag(T("Lock layer"), &Layer::locked);
    flag(T("Hide from the timelapse"), &Layer::hideInTimelapse,
         T("The layer stays visible while you work, but the timelapse video is recorded without it."));
    flag(T("Reference layer (fills take borders from it)"), &Layer::reference,
         T("Mark the line art as reference, then fill on the layers under it with 'Borders from: reference layer'."));

    if (!l->isGroup() && l->type() != LayerType::Adjustment) {
        auto* fx = new QPushButton(Theme::icon("fx"), T("Layer effects..."), this);
        connect(fx, &QPushButton::clicked, this, [this] {
            accept();
            Q_EMIT requestEffects(m_id);
        });
        f->addRow(fx);
    }

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    setMinimumWidth(380);
    name->selectAll();
}

void LayerPropertiesDialog::accept() {
    if (!m_done && !m_changed && m_doc) m_doc->revertLastUndo();  // nothing changed: no empty history step
    m_done = true;
    QDialog::accept();
}

void LayerPropertiesDialog::reject() {
    if (!m_done && m_doc) m_doc->revertLastUndo();
    m_done = true;
    QDialog::reject();
}

}  // namespace gx
