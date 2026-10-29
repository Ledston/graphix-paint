#include "tools/FocusLinesTool.h"

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/LayerOps.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/Widgets.h"

namespace gx {

FocusLinesTool::FocusLinesTool(int kind, QObject* parent)
    : Tool(kind == FocusLines::Focus ? "focuslines" : "speedlines", kind == FocusLines::Focus ? "Focus lines" : "Speed lines",
           kind == FocusLines::Focus ? "focuslines" : "speedlines", parent),
      m_kind(kind) {
    m_params.kind = kind;
    if (kind == FocusLines::Speed) {
        m_params.count = 70;
        m_params.width = 4;
        m_params.gaps = 0.1;
        m_params.spread = 0.8;
    }
    if (app()) {
        const QJsonObject o = QJsonDocument::fromJson(app()->settings().value("lines/" + m_id).toByteArray()).object();
        if (!o.isEmpty()) {
            m_params = FocusLines::fromJson(o);
            m_params.kind = kind;
        }
    }
    m_rerender.setSingleShot(true);
    m_rerender.setInterval(60);
    connect(&m_rerender, &QTimer::timeout, this, &FocusLinesTool::rerender);
}

void FocusLinesTool::saveDefaults() {
    if (app()) app()->settings().setValue("lines/" + m_id, QJsonDocument(m_params.toJson()).toJson(QJsonDocument::Compact));
}

RasterLayer* FocusLinesTool::editedLayer() const {
    Document* d = app() ? app()->activeDocument() : nullptr;
    if (!d) return nullptr;
    auto* l = dynamic_cast<RasterLayer*>(d->currentLayer());
    if (!l || !l->generatorEditable() || !FocusLines::isFocusLines(l->generator)) return nullptr;
    if (l->generator.value("kind").toInt() != m_kind) return nullptr;
    return l;
}

FocusLines FocusLinesTool::fromDrag(Document* d, QPointF from, QPointF to, bool shift) const {
    FocusLines f = m_params;
    f.kind = m_kind;
    if (RasterLayer* l = editedLayer()) f.area = FocusLines::fromJson(l->generator).area;
    else f.area = (d->hasSelection() ? d->selectionBounds() : d->bounds()) & d->bounds();
    const QPointF v = to - from;
    const double len = std::hypot(v.x(), v.y());
    if (m_kind == FocusLines::Focus) {
        f.center = from;
        if (len >= 3) {
            // the drag reaches the edge of the empty middle; Shift makes it round
            f.rx = std::max(4.0, std::abs(v.x()));
            f.ry = std::max(4.0, std::abs(v.y()));
            if (shift || f.rx < 8 || f.ry < 8) f.rx = f.ry = std::max(4.0, len);
        }
    } else if (len >= 3) {
        double a = std::atan2(v.y(), v.x()) * 180 / M_PI;
        if (shift) a = std::round(a / 15) * 15;
        f.angle = a;
    }
    return f;
}

void FocusLinesTool::press(const PointerEvent& e) {
    if (!e.view) return;
    m_view = e.view;
    m_dragging = true;
    m_from = m_to = e.pos;
    m_shift = e.mods & Qt::ShiftModifier;
    Q_EMIT overlayChanged();
}

void FocusLinesTool::move(const PointerEvent& e) {
    if (!m_dragging) return;
    m_to = e.pos;
    m_shift = e.mods & Qt::ShiftModifier;
    Q_EMIT overlayChanged();
}

void FocusLinesTool::release(const PointerEvent& e) {
    if (!m_dragging) return;
    m_dragging = false;
    m_to = e.pos;
    if (m_view) applyDrag(m_view->doc(), m_from, m_to, e.mods & Qt::ShiftModifier);
    Q_EMIT overlayChanged();
}

void FocusLinesTool::applyDrag(Document* d, QPointF from, QPointF to, bool shift) {
    if (!d) return;
    app()->tools()->commitPending();
    FocusLines f = fromDrag(d, from, to, shift);
    if (!editedLayer()) f.color = app()->foreground();  // new lines in the main color, like a brush
    m_params = f;
    saveDefaults();
    if (RasterLayer* l = editedLayer()) {
        const quint32 id = l->id;
        d->pushUndo(m_kind == FocusLines::Focus ? "Focus lines" : "Speed lines");
        if (auto* nl = dynamic_cast<RasterLayer*>(d->layer(id))) f.renderInto(*d, *nl);
    } else {
        auto nl = std::make_shared<RasterLayer>();
        nl->name = d->uniqueLayerName(T(m_kind == FocusLines::Focus ? "Focus lines" : "Speed lines"));
        auto* added = static_cast<RasterLayer*>(ops::insertLayer(*d, nl, nullptr, true, m_kind == FocusLines::Focus ? "Focus lines" : "Speed lines"));
        f.renderInto(*d, *added);
    }
    if (m_sync) m_sync();
}

void FocusLinesTool::settingsChanged() {
    saveDefaults();
    if (editedLayer()) m_rerender.start();
}

void FocusLinesTool::rerender() {
    RasterLayer* l = editedLayer();
    Document* d = app()->activeDocument();
    if (!l || !d) return;
    FocusLines f = FocusLines::fromJson(l->generator);
    // the placement stays, the look comes from the panel
    const FocusLines& p = m_params;
    f.count = p.count;
    f.width = p.width;
    f.widthJitter = p.widthJitter;
    f.lengthJitter = p.lengthJitter;
    f.spread = p.spread;
    f.gaps = p.gaps;
    f.length = p.length;
    f.color = p.color;
    f.seed = p.seed;
    f.rx = p.rx;
    f.ry = p.ry;
    f.angle = p.angle;
    const quint32 id = l->id;
    d->pushUndo("Change lines", QStringLiteral("lines-%1").arg(id));
    if (auto* nl = dynamic_cast<RasterLayer*>(d->layer(id))) f.renderInto(*d, *nl);
}

void FocusLinesTool::drawOverlay(QPainter& p, CanvasView* v) {
    if (!m_dragging || !v) return;
    const FocusLines f = fromDrag(v->doc(), m_from, m_to, m_shift);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setTransform(v->canvasToWidget(), true);
    QColor c = f.color;
    c.setAlpha(150);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.setClipRect(QRectF(f.area));
    for (const QPolygonF& poly : f.shapes()) p.drawPolygon(poly);
    QPen guide(QColor(255, 120, 40), 0, Qt::DashLine);
    guide.setCosmetic(true);
    p.setPen(guide);
    p.setBrush(Qt::NoBrush);
    if (m_kind == FocusLines::Focus) p.drawEllipse(f.center, f.rx, f.ry);
    else p.drawLine(m_from, m_to);
    p.restore();
}

QWidget* FocusLinesTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    m_opts = w;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(4);
    auto* state = new QLabel(w);
    state->setWordWrap(true);
    state->setObjectName("sectionTitle");
    v->addWidget(state);
    auto bar = [&](const QString& label, double min, double max, int dec, const QString& suffix, double scale = 1) {
        auto* b = new ValueBar(label, min, max, dec, w);
        b->setSuffix(suffix);
        b->setDisplayScale(scale);
        v->addWidget(b);
        return b;
    };
    const bool focus = m_kind == FocusLines::Focus;
    auto* count = bar(T("Lines"), 4, 1000, 0, {});
    auto* width = bar(T("Thickness"), 0.5, 80, 1, T(" px"));
    width->setLogarithmic(true);
    auto* wj = bar(T("Thickness variation"), 0, 1, 2, "%", 100);
    auto* lj = bar(T("Uneven ends"), 0, 1, 2, "%", 100);
    auto* sp = bar(T("Uneven spacing"), 0, 1, 2, "%", 100);
    auto* gaps = bar(T("Gaps"), 0, 1, 2, "%", 100);
    ValueBar *rx = nullptr, *ry = nullptr, *len = nullptr, *ang = nullptr;
    if (focus) {
        rx = bar(T("Empty middle, width"), 0, 6000, 0, T(" px"));
        ry = bar(T("Empty middle, height"), 0, 6000, 0, T(" px"));
    } else {
        len = bar(T("Line length"), 0.05, 1, 2, "%", 100);
        ang = bar(T("Direction"), -180, 180, 0, QString::fromUtf8("°"));
    }
    auto* row = new QHBoxLayout;
    auto* seed = new QPushButton(T("Shuffle"), w);
    seed->setToolTip(T("Another random arrangement of the same lines"));
    auto* color = new QPushButton(T("Main color"), w);
    color->setToolTip(T("Paint the lines with the main color"));
    row->addWidget(seed);
    row->addWidget(color);
    v->addLayout(row);
    auto* hint = new QLabel(focus ? T("Drag from the center outward: the ellipse stays empty (Shift: a circle). "
                                      "Inside a selection the lines fill only the selection.")
                                  : T("Drag in the direction of motion (Shift: steps of 15°). "
                                      "Inside a selection the lines fill only the selection."),
                            w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    v->addStretch();

    m_sync = [=, this] {
        if (!m_opts) return;
        // the selected lines layer shows its own settings
        if (RasterLayer* l = editedLayer()) {
            m_params = FocusLines::fromJson(l->generator);
            state->setText(T("Changing the lines of \"%1\". Select another layer to make new lines.").arg(l->name));
        } else {
            state->setText(focus ? T("Drag on the canvas to make focus lines on a new layer.")
                                 : T("Drag on the canvas to make speed lines on a new layer."));
        }
        const FocusLines& p = m_params;
        QSignalBlocker b1(count), b2(width), b3(wj), b4(lj), b5(sp), b6(gaps);
        count->setValue(p.count);
        width->setValue(p.width);
        wj->setValue(p.widthJitter);
        lj->setValue(p.lengthJitter);
        sp->setValue(p.spread);
        gaps->setValue(p.gaps);
        if (rx) {
            QSignalBlocker c1(rx), c2(ry);
            rx->setValue(p.rx);
            ry->setValue(p.ry);
        }
        if (len) {
            QSignalBlocker c1(len), c2(ang);
            len->setValue(p.length);
            ang->setValue(p.angle);
        }
    };
    auto link = [this](ValueBar* b, double FocusLines::*field) {
        if (!b) return;
        connect(b, &ValueBar::valueChanged, b, [this, field](double x) {
            m_params.*field = x;
            settingsChanged();
        });
    };
    connect(count, &ValueBar::valueChanged, w, [this](double x) {
        m_params.count = int(x);
        settingsChanged();
    });
    link(width, &FocusLines::width);
    link(wj, &FocusLines::widthJitter);
    link(lj, &FocusLines::lengthJitter);
    link(sp, &FocusLines::spread);
    link(gaps, &FocusLines::gaps);
    link(rx, &FocusLines::rx);
    link(ry, &FocusLines::ry);
    link(len, &FocusLines::length);
    link(ang, &FocusLines::angle);
    connect(seed, &QPushButton::clicked, w, [this] {
        m_params.seed = m_params.seed * 1103515245u + 12345u;
        settingsChanged();
    });
    connect(color, &QPushButton::clicked, w, [this] {
        m_params.color = app()->foreground();
        settingsChanged();
    });
    auto hook = [this] {
        if (Document* d = app()->activeDocument())
            connect(d, &Document::currentLayerChanged, m_opts, [this] { if (m_sync) m_sync(); });
        if (m_sync) m_sync();
    };
    connect(app(), &App::activeViewChanged, w, hook);
    hook();
    return w;
}

}  // namespace gx
