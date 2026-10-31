#include "ui/EffectFilterDialog.h"
#include "core/LayerOps.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/Effects.h"
#include "ui/EffectsDialog.h"

namespace gx {

EffectFilterDialog::EffectFilterDialog(Document* doc, quint32 layerId, const QString& effect, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_layer(layerId), m_key(effect) {
    QString title = effect;
    for (const auto& e : effectKeys())
        if (e.first == effect) title = T(e.second);
    setWindowTitle(title);
    // the settings used last time for this effect
    const QJsonObject saved = QJsonDocument::fromJson(app()->settings().value("effectFilters/" + effect).toByteArray()).object();
    m_fx = saved.isEmpty() ? LayerEffects() : LayerEffects::fromJson(saved);
    for (const auto& e : effectKeys()) *effectSwitch(m_fx, e.first) = e.first == effect;

    auto* v = new QVBoxLayout(this);
    auto* info = new QLabel(T("The effect is drawn as pixels: onto a new layer above, or into the layer itself. To keep it changeable, use "
                              "Layer > Effects instead."), this);
    info->setWordWrap(true);
    info->setObjectName("sectionTitle");
    v->addWidget(info);
    auto* box = new QWidget(this);
    auto* bl = new QVBoxLayout(box);
    bl->setContentsMargins(0, 4, 0, 4);
    addEffectControls(effect, &m_fx, bl, box, this, [this] { schedule(); });
    v->addWidget(box);
    auto* row = new QHBoxLayout;
    m_previewBox = new QCheckBox(T("Preview"), this);
    m_previewBox->setChecked(true);
    connect(m_previewBox, &QCheckBox::toggled, this, [this](bool on) {
        if (on) schedule();
        else clearPreview();
    });
    row->addWidget(m_previewBox);
    m_newLayer = new QCheckBox(T("On a new layer"), this);
    m_newLayer->setToolTip(T("The effect is put on its own layer above; the layer itself is not changed."));
    m_newLayer->setChecked(app()->settings().value("effectFilters/newLayer", true).toBool());
    row->addWidget(m_newLayer);
    row->addStretch();
    m_busy = new QLabel(this);
    m_busy->setObjectName("sectionTitle");
    row->addWidget(m_busy);
    v->addLayout(row);
    v->addStretch();
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    setMinimumWidth(360);

    m_timer.setSingleShot(true);
    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, &EffectFilterDialog::startPreview);
    connect(&m_watch, &QFutureWatcher<RasterSurface>::finished, this, [this] {
        const quint64 ver = m_runningVersion;
        m_result = m_watch.result();
        m_shownVersion = ver;
        if (m_previewBox->isChecked()) showPreview(m_result);
        m_busy->clear();
        if (m_again) {
            m_again = false;
            startPreview();
        }
    });
    schedule();
}

EffectFilterDialog::~EffectFilterDialog() {
    m_watch.waitForFinished();
    if (!m_done) clearPreview();
}

void EffectFilterDialog::setNewLayer(bool on) {
    if (m_newLayer) m_newLayer->setChecked(on);
}

QRect EffectFilterDialog::area() const {
    if (!m_doc) return {};
    Layer* l = m_doc->layer(m_layer);
    if (!l || !l->isRaster()) return {};
    const int m = m_fx.margin();
    QRect r = static_cast<RasterLayer*>(l)->pixels.exactBounds().adjusted(-m, -m, m, m) & m_doc->bounds();
    if (m_doc->hasSelection()) r &= m_doc->selectionBounds();
    return r;
}

void EffectFilterDialog::schedule() {
    ++m_version;
    m_timer.start();
}

void EffectFilterDialog::startPreview() {
    if (!m_doc) return;
    if (m_watch.isRunning()) {
        m_again = true;
        return;
    }
    Layer* l = m_doc->layer(m_layer);
    if (!l || !l->isRaster()) return;
    // a copy of everything the worker needs: the layer may change only after the dialog closes
    auto copy = std::static_pointer_cast<RasterLayer>(l->clone());
    const LayerEffects fx = m_fx;
    const QRect a = area();
    std::shared_ptr<MaskSurface> sel;
    if (m_doc->hasSelection()) sel = std::make_shared<MaskSurface>(m_doc->selection());
    m_runningVersion = m_version;
    m_busy->setText(T("Working..."));
    m_watch.setFuture(QtConcurrent::run([copy, fx, a, sel] { return bakeEffects(*copy, fx, a, sel.get()); }));
}

void EffectFilterDialog::showPreview(const RasterSurface& s) {
    if (!m_doc) return;
    auto* l = dynamic_cast<RasterLayer*>(m_doc->layer(m_layer));
    if (!l) return;
    l->preview = std::make_shared<RasterSurface>(s);
    m_doc->pixelsChanged(area().isEmpty() ? m_doc->bounds() : area().adjusted(-64, -64, 64, 64) & m_doc->bounds(), l);
}

void EffectFilterDialog::clearPreview() {
    if (!m_doc) return;
    auto* l = dynamic_cast<RasterLayer*>(m_doc->layer(m_layer));
    if (!l || !l->preview) return;
    l->preview.reset();
    m_doc->pixelsChanged(m_doc->bounds(), l);
}

void EffectFilterDialog::accept() {
    m_timer.stop();
    m_watch.waitForFinished();
    m_again = false;
    if (m_doc) {
        auto* l = dynamic_cast<RasterLayer*>(m_doc->layer(m_layer));
        if (l) {
            RasterSurface result = (m_shownVersion == m_version && !m_result.isEmpty())
                                       ? m_result
                                       : bakeEffects(*l, m_fx, area(), m_doc->hasSelection() ? &m_doc->selection() : nullptr);
            l->preview.reset();
            const QRect a = area();
            if (m_newLayer && m_newLayer->isChecked()) {
                // Only what the effect changed goes onto the new layer: the new layer over the old one
                // looks like the result, and the layer itself stays as it was.
                auto nl = std::make_shared<RasterLayer>();
                nl->name = l->name + " - " + windowTitle();
                if (!a.isEmpty()) {
                    const size_t n = size_t(a.width()) * a.height();
                    std::vector<quint32> before(n), after(n);
                    l->pixels.readRect(a, before.data(), a.width());
                    result.readRect(a, after.data(), a.width());
                    for (size_t i = 0; i < n; ++i)
                        if (after[i] == before[i]) after[i] = 0;
                    nl->pixels.writeRect(a, after.data(), a.width());
                    nl->pixels.optimizeRect(a);
                }
                ops::insertLayer(*m_doc, nl, l, true, windowTitle());
            } else {
                m_doc->pushUndo(windowTitle());
                l = dynamic_cast<RasterLayer*>(m_doc->layer(m_layer));  // the same layer after the undo snapshot
                if (l) {
                    l->pixels = result;
                    l->pixels.optimizeRect(a);
                    m_doc->pixelsChanged(m_doc->bounds(), l);
                }
            }
            if (m_newLayer) app()->settings().setValue("effectFilters/newLayer", m_newLayer->isChecked());
        }
        QJsonObject o = m_fx.toJson();
        app()->settings().setValue("effectFilters/" + m_key, QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
    m_done = true;
    QDialog::accept();
}

void EffectFilterDialog::reject() {
    m_timer.stop();
    m_watch.waitForFinished();
    clearPreview();
    m_done = true;
    QDialog::reject();
}

}  // namespace gx
