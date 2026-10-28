#include "tools/TextTool.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/LayerOps.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "vector/VectorRender.h"

namespace gx {

TextLayer* TextTool::currentText() const {
    Document* d = app()->activeDocument();
    return d ? dynamic_cast<TextLayer*>(d->currentLayer()) : nullptr;
}

void TextTool::applyEdit(const std::function<void(TextLayer*)>& fn, const QString& key) {
    Document* d = app()->activeDocument();
    TextLayer* t = currentText();
    if (!d || !t || m_syncing) return;
    d->pushUndo(T("Edit text"), "text-" + key + QString::number(t->id));
    t = currentText();
    QRect before = t->cache.tileBounds();
    fn(t);
    QString first = t->text.text.section('\n', 0, 0).left(24).trimmed();
    if (!first.isEmpty()) t->name = first;
    QRect after = renderTextLayer(*t);
    d->pixelsChanged(before | after, t);
    d->layerChanged(t);
}

void TextTool::press(const PointerEvent& e) {
    if (!e.view) return;
    m_view = e.view;
    Document* d = e.view->doc();
    // Clicking an existing text layer selects it for editing
    TextLayer* hit = nullptr;
    for (Layer* l : layersTopToBottom(d->root())) {
        auto* t = dynamic_cast<TextLayer*>(l);
        if (t && t->isEffectivelyVisible() && textBounds(t->text).contains(e.pos)) { hit = t; break; }
    }
    if (hit) {
        d->setCurrentLayer(hit->id);
        d->pushUndo(T("Move text"));
        hit = static_cast<TextLayer*>(d->layer(hit->id));
        m_drag = true;
        m_press = e.pos;
        m_origin = hit->text.origin;
        syncFromLayer();
        return;
    }
    TextParams p;
    p.text = T("Text");
    p.family = m_family;
    p.sizePx = m_sizePx;
    p.color = app()->foreground();
    p.origin = e.pos;
    if (m_bold) {
        p.bold = m_bold->isChecked();
        p.italic = m_italic->isChecked();
        p.underline = m_under->isChecked();
        p.vertical = m_vertical->isChecked();
        p.align = m_align->currentIndex();
        p.lineSpacing = m_line->value();
        p.letterSpacing = m_letter->value();
    }
    ops::addTextLayer(*d, p);
    syncFromLayer();
    if (m_edit) {
        m_edit->setFocus();
        m_edit->selectAll();
    }
}

void TextTool::move(const PointerEvent& e) {
    if (!m_drag || !m_view) return;
    TextLayer* t = currentText();
    if (!t) return;
    QRect before = t->cache.tileBounds();
    t->text.origin = m_origin + (e.pos - m_press);
    QRect after = renderTextLayer(*t);
    m_view->doc()->pixelsChanged(before | after, t);
}

void TextTool::release(const PointerEvent&) {
    if (!m_drag) return;
    m_drag = false;
    if (m_view && currentText() && currentText()->text.origin == m_origin) m_view->doc()->revertLastUndo();
}

void TextTool::drawOverlay(QPainter& p, CanvasView* v) {
    TextLayer* t = currentText();
    if (!t || v->doc() != app()->activeDocument()) return;
    QPolygonF q = v->canvasToWidget().map(QPolygonF(textBounds(t->text)));
    p.save();
    QPen pen(Theme::colors().accent, 1, Qt::DashLine);
    p.setPen(pen);
    p.drawPolygon(q);
    p.restore();
}

void TextTool::syncFromLayer() {
    if (!m_opts) return;
    TextLayer* t = currentText();
    m_syncing = true;
    m_opts->setEnabled(true);
    if (t) {
        if (m_edit->toPlainText() != t->text.text) m_edit->setPlainText(t->text.text);
        if (!t->text.family.isEmpty()) m_font->setCurrentFont(QFont(t->text.family));
        m_size->setValue(t->text.sizePx);
        m_bold->setChecked(t->text.bold);
        m_italic->setChecked(t->text.italic);
        m_under->setChecked(t->text.underline);
        m_vertical->setChecked(t->text.vertical);
        m_align->setCurrentIndex(t->text.align);
        m_line->setValue(t->text.lineSpacing);
        m_letter->setValue(t->text.letterSpacing);
    }
    m_edit->setEnabled(t != nullptr);
    m_syncing = false;
}

QWidget* TextTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    m_opts = w;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 6, 6, 6);
    m_edit = new QPlainTextEdit(w);
    m_edit->setPlaceholderText(T("Click the canvas to add text, then type here."));
    m_edit->setMinimumHeight(90);
    v->addWidget(m_edit);
    m_font = new QFontComboBox(w);
    m_family = m_font->currentFont().family();
    v->addWidget(m_font);
    m_size = new ValueBar(T("Size"), 2, 2000, 0, w);
    m_size->setLogarithmic(true);
    m_size->setSuffix(" px");
    m_size->setValue(m_sizePx);
    v->addWidget(m_size);
    auto* row = new QHBoxLayout;
    m_bold = new QCheckBox(T("Bold"), w);
    m_italic = new QCheckBox(T("Italic"), w);
    m_under = new QCheckBox(T("Underline"), w);
    row->addWidget(m_bold);
    row->addWidget(m_italic);
    row->addWidget(m_under);
    v->addLayout(row);
    m_align = new QComboBox(w);
    m_align->addItems({T("Align left"), T("Align center"), T("Align right")});
    v->addWidget(m_align);
    m_vertical = new QCheckBox(T("Vertical text"), w);
    v->addWidget(m_vertical);
    m_line = new ValueBar(T("Line spacing"), 0.5, 3, 2, w);
    m_line->setValue(1.0);
    v->addWidget(m_line);
    m_letter = new ValueBar(T("Letter spacing"), -50, 200, 0, w);
    m_letter->setSuffix("%");
    m_letter->setValue(0);
    v->addWidget(m_letter);
    auto* colorBtn = new QPushButton(T("Use the main color"), w);
    v->addWidget(colorBtn);
    auto* rast = new QPushButton(T("Rasterize text layer"), w);
    v->addWidget(rast);
    auto* hint = new QLabel(T("Effects such as outline and shadow: Layer > Layer effects."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    v->addStretch();

    connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
        QString s = m_edit->toPlainText();
        applyEdit([s](TextLayer* t) { t->text.text = s; }, "text");
    });
    connect(m_font, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) {
        m_family = f.family();
        QString fam = f.family();
        applyEdit([fam](TextLayer* t) { t->text.family = fam; }, "font");
    });
    connect(m_size, &ValueBar::valueChanged, this, [this](double s) {
        m_sizePx = s;
        applyEdit([s](TextLayer* t) { t->text.sizePx = s; }, "size");
    });
    connect(m_bold, &QCheckBox::toggled, this, [this](bool on) { applyEdit([on](TextLayer* t) { t->text.bold = on; }, "bold"); });
    connect(m_italic, &QCheckBox::toggled, this, [this](bool on) { applyEdit([on](TextLayer* t) { t->text.italic = on; }, "italic"); });
    connect(m_under, &QCheckBox::toggled, this, [this](bool on) { applyEdit([on](TextLayer* t) { t->text.underline = on; }, "under"); });
    connect(m_vertical, &QCheckBox::toggled, this, [this](bool on) { applyEdit([on](TextLayer* t) { t->text.vertical = on; }, "vert"); });
    connect(m_align, &QComboBox::currentIndexChanged, this, [this](int i) { applyEdit([i](TextLayer* t) { t->text.align = i; }, "align"); });
    connect(m_line, &ValueBar::valueChanged, this, [this](double x) { applyEdit([x](TextLayer* t) { t->text.lineSpacing = x; }, "line"); });
    connect(m_letter, &ValueBar::valueChanged, this, [this](double x) { applyEdit([x](TextLayer* t) { t->text.letterSpacing = x; }, "letter"); });
    connect(colorBtn, &QPushButton::clicked, this, [this] {
        QColor c = app()->foreground();
        applyEdit([c](TextLayer* t) { t->text.color = c; }, "color");
    });
    connect(rast, &QPushButton::clicked, this, [this] {
        Document* d = app()->activeDocument();
        if (TextLayer* t = currentText()) ops::rasterize(*d, t->id);
    });
    connect(app(), &App::activeViewChanged, this, [this] { syncFromLayer(); });
    auto hook = [this] {
        if (Document* d = app()->activeDocument()) connect(d, &Document::currentLayerChanged, this, &TextTool::syncFromLayer, Qt::UniqueConnection);
    };
    hook();
    connect(app(), &App::activeViewChanged, this, hook);
    syncFromLayer();
    return w;
}

}  // namespace gx
