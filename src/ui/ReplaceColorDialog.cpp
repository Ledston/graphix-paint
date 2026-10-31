#include "ui/ReplaceColorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "ui/Widgets.h"

namespace gx {

ReplaceColorDialog::ReplaceColorDialog(Document* doc, quint32 layerId, QWidget* parent) : QDialog(parent), m_doc(doc), m_layer(layerId) {
    setWindowTitle(T("Replace color"));
    QSettings& s = app()->settings();
    m_p.from = app()->foreground();
    m_p.to = app()->background();
    m_p.tolerance = s.value("replaceColor/tolerance", 40).toDouble();
    m_p.softness = s.value("replaceColor/softness", 30).toDouble();
    m_p.keepShading = s.value("replaceColor/keepShading", true).toBool();
    auto* f = new QFormLayout(this);
    auto* info = new QLabel(T("Colors close to the first one become the second. Tip: pick the colors with the eyedropper (Alt) before opening."), this);
    info->setWordWrap(true);
    info->setObjectName("sectionTitle");
    f->addRow(info);
    auto colorRow = [&](QColor* field, const QString& label) {
        auto* row = new QHBoxLayout;
        auto* b = new ColorButton(*field, this);
        row->addWidget(b);
        auto* mainB = new QPushButton(T("Main color"), this);
        auto* subB = new QPushButton(T("Sub color"), this);
        row->addWidget(mainB);
        row->addWidget(subB);
        row->addStretch();
        connect(b, &ColorButton::colorChanged, this, [this, field](const QColor& c) {
            *field = c;
            m_timer.start();
        });
        connect(mainB, &QPushButton::clicked, this, [b] { b->setColor(app()->foreground()); });
        connect(subB, &QPushButton::clicked, this, [b] { b->setColor(app()->background()); });
        f->addRow(label, row);
    };
    colorRow(&m_p.from, T("Find"));
    colorRow(&m_p.to, T("Replace with"));
    auto* tol = new ValueBar(T("Tolerance"), 0, 255, 0, this);
    tol->setValue(m_p.tolerance);
    connect(tol, &ValueBar::valueChanged, this, [this](double x) {
        m_p.tolerance = x;
        m_timer.start();
    });
    f->addRow(tol);
    auto* soft = new ValueBar(T("Soft edge"), 0, 255, 0, this);
    soft->setValue(m_p.softness);
    soft->setToolTip(T("Colors a bit beyond the tolerance are replaced partly, so the change blends in."));
    connect(soft, &ValueBar::valueChanged, this, [this](double x) {
        m_p.softness = x;
        m_timer.start();
    });
    f->addRow(soft);
    auto* shade = new QCheckBox(T("Keep the shading (lighter and darker shades stay lighter and darker)"), this);
    shade->setChecked(m_p.keepShading);
    connect(shade, &QCheckBox::toggled, this, [this](bool on) {
        m_p.keepShading = on;
        m_timer.start();
    });
    f->addRow(shade);
    m_preview = new QCheckBox(T("Preview"), this);
    m_preview->setChecked(true);
    connect(m_preview, &QCheckBox::toggled, this, [this](bool on) {
        if (on) updatePreview();
        else clearPreview();
    });
    f->addRow(m_preview);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    f->addRow(bb);
    m_timer.setSingleShot(true);
    m_timer.setInterval(80);
    connect(&m_timer, &QTimer::timeout, this, &ReplaceColorDialog::updatePreview);
    setMinimumWidth(420);
    m_timer.start();
}

ReplaceColorDialog::~ReplaceColorDialog() { clearPreview(); }

void ReplaceColorDialog::updatePreview() {
    if (!m_doc || !m_preview->isChecked()) return;
    auto* l = dynamic_cast<RasterLayer*>(m_doc->layer(m_layer));
    if (!l) return;
    QRect area = l->pixels.exactBounds();
    if (m_doc->hasSelection()) area &= m_doc->selectionBounds();
    auto prev = std::make_shared<RasterSurface>(l->pixels);
    if (!area.isEmpty()) {
        const size_t n = size_t(area.width()) * area.height();
        std::vector<float> px(n * 4), out;
        readFloat(l->pixels, area, px.data());
        out = px;
        replaceColorPixels(out.data(), int(n), m_p);
        if (m_doc->hasSelection()) {
            std::vector<float> sel(n);
            readMask(m_doc->selection(), area, sel.data());
            for (size_t i = 0; i < n; ++i)
                for (int k = 0; k < 4; ++k) out[i * 4 + k] = px[i * 4 + k] + (out[i * 4 + k] - px[i * 4 + k]) * sel[i];
        }
        writeFloat(*prev, area, out.data());
    }
    l->preview = prev;
    m_doc->pixelsChanged(area.isEmpty() ? m_doc->bounds() : area, l);
}

void ReplaceColorDialog::clearPreview() {
    if (!m_doc) return;
    auto* l = dynamic_cast<RasterLayer*>(m_doc->layer(m_layer));
    if (!l || !l->preview) return;
    l->preview.reset();
    m_doc->pixelsChanged(m_doc->bounds(), l);
}

void ReplaceColorDialog::accept() {
    m_timer.stop();
    clearPreview();
    if (m_doc) replaceColor(*m_doc, m_layer, m_p);
    QSettings& s = app()->settings();
    s.setValue("replaceColor/tolerance", m_p.tolerance);
    s.setValue("replaceColor/softness", m_p.softness);
    s.setValue("replaceColor/keepShading", m_p.keepShading);
    QDialog::accept();
}

void ReplaceColorDialog::reject() {
    m_timer.stop();
    clearPreview();
    QDialog::reject();
}

}  // namespace gx
