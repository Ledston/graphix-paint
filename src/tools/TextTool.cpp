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
    // the end of the balloon's tail can be dragged
    if (TextLayer* cur = currentText(); cur && cur->text.balloon && cur->text.tail) {
        const QPointF tip = e.view->toWidget(balloonTailTip(cur->text));
        if (QLineF(tip, e.widgetPos).length() < 10) {
            d->pushUndo(T("Edit text"));
            m_dragTail = true;
            return;
        }
    }
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
        p.balloon = m_newBalloon;
        if (m_tail) p.tail = m_tail->isChecked();
        if (m_pad) p.balloonPadding = m_pad->value();
        if (m_border) p.balloonBorder = m_border->value();
    }
    ops::addTextLayer(*d, p);
    syncFromLayer();
    if (m_edit) {
        m_edit->setFocus();
        m_edit->selectAll();
    }
}

void TextTool::move(const PointerEvent& e) {
    if (m_dragTail && m_view) {
        TextLayer* t = currentText();
        if (!t) return;
        QRect before = t->cache.tileBounds();
        t->text.tailTip = textLocal(t->text, e.pos);
        if (t->text.tailTip.isNull()) t->text.tailTip = QPointF(0.01, 0.01);
        QRect after = renderTextLayer(*t);
        m_view->doc()->pixelsChanged(before | after, t);
        Q_EMIT overlayChanged();
        return;
    }
    if (!m_drag || !m_view) return;
    TextLayer* t = currentText();
    if (!t) return;
    QRect before = t->cache.tileBounds();
    t->text.origin = m_origin + (e.pos - m_press);
    QRect after = renderTextLayer(*t);
    m_view->doc()->pixelsChanged(before | after, t);
}

void TextTool::release(const PointerEvent&) {
    m_dragTail = false;
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
    if (t->text.balloon && t->text.tail) {
        // the handle at the end of the tail
        const QPointF tip = v->toWidget(balloonTailTip(t->text));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(Theme::colors().accent);
        p.drawEllipse(tip, 6, 6);
    }
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
        m_balloon->setCurrentIndex(t->text.balloon);
        m_tail->setChecked(t->text.tail);
        m_pad->setValue(t->text.balloonPadding);
        m_border->setValue(t->text.balloonBorder);
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
    auto* row2 = new QHBoxLayout;  // two short rows keep the panel narrow
    row2->addWidget(m_under);
    row2->addStretch();
    v->addLayout(row);
    v->addLayout(row2);
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
    auto* bt = new QLabel(T("Speech balloon"), w);
    bt->setObjectName("sectionTitle");
    v->addWidget(bt);
    m_balloon = new QComboBox(w);
    m_balloon->addItems({T("No balloon"), T("Oval"), T("Box"), T("Thought (cloud)"), T("Shout (spikes)")});
    m_balloon->setCurrentIndex(m_newBalloon);
    v->addWidget(m_balloon);
    m_tail = new QCheckBox(T("Tail (drag its end on the canvas)"), w);
    m_tail->setChecked(true);
    v->addWidget(m_tail);
    m_pad = new ValueBar(T("Room around the text"), 0, 3, 2, w);
    m_pad->setValue(0.6);
    v->addWidget(m_pad);
    m_border = new ValueBar(T("Outline"), 0, 30, 1, w);
    m_border->setSuffix(" px");
    m_border->setValue(3);
    v->addWidget(m_border);
    auto* bcol = new QPushButton(T("Balloon: main color outline, sub color fill"), w);
    v->addWidget(bcol);
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
    connect(m_balloon, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_newBalloon = i;
        applyEdit([i](TextLayer* t) { t->text.balloon = i; }, "balloon");
    });
    connect(m_tail, &QCheckBox::toggled, this, [this](bool on) { applyEdit([on](TextLayer* t) { t->text.tail = on; }, "tail"); });
    connect(m_pad, &ValueBar::valueChanged, this, [this](double x) { applyEdit([x](TextLayer* t) { t->text.balloonPadding = x; }, "pad"); });
    connect(m_border, &ValueBar::valueChanged, this, [this](double x) { applyEdit([x](TextLayer* t) { t->text.balloonBorder = x; }, "border"); });
    connect(bcol, &QPushButton::clicked, this, [this] {
        const QColor line = app()->foreground(), fill = app()->background();
        applyEdit([line, fill](TextLayer* t) {
            t->text.balloonLine = line;
            t->text.balloonFill = fill;
        }, "bcolor");
    });
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
