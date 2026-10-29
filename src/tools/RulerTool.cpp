#include "tools/RulerTool.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainterPathStroker>
#include <QPushButton>
#include <QtMath>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

static QPointF symHandle(const Symmetry& s, double zoom) {
    double a = qDegreesToRadians(s.angle);
    return s.center + QPointF(std::cos(a), std::sin(a)) * (80.0 / zoom);
}

void RulerTool::press(const PointerEvent& e) {
    if (!e.view) return;
    m_view = e.view;
    Document* d = e.view->doc();
    m_drag = -1;
    double best = 14;
    if (d->perspective.enabled)
        for (int i = 0; i < d->perspective.points; ++i) {
            double dist = QLineF(e.view->toWidget(d->perspective.vp[i]), e.widgetPos).length();
            if (dist < best) { best = dist; m_drag = i; m_off = d->perspective.vp[i] - e.pos; }
        }
    if (d->ruler.isOn()) {
        const QVector<QPointF> hs = d->ruler.handles();
        for (int i = 0; i < hs.size(); ++i) {
            const double dist = QLineF(e.view->toWidget(hs[i]), e.widgetPos).length();
            if (dist < best) { best = dist; m_drag = 20 + i; m_off = hs[i] - e.pos; }
        }
        // dragging the line of a straight / parallel ruler moves it whole
        if (m_drag < 0 && (d->ruler.kind == Ruler::Straight || d->ruler.kind == Ruler::Parallel || d->ruler.kind == Ruler::Curve)) {
            QPainterPathStroker st;
            st.setWidth(12 / e.view->zoom());
            if (st.createStroke(d->ruler.mainPath()).contains(e.pos)) { m_drag = 30; m_off = e.pos; }
        }
    }
    if (d->symmetry.isOn()) {
        double dc = QLineF(e.view->toWidget(d->symmetry.center), e.widgetPos).length();
        if (dc < best) { best = dc; m_drag = 10; m_off = d->symmetry.center - e.pos; }
        double dh = QLineF(e.view->toWidget(symHandle(d->symmetry, e.view->zoom())), e.widgetPos).length();
        if (dh < best) { best = dh; m_drag = 11; }
    }
}

void RulerTool::move(const PointerEvent& e) {
    if (m_drag < 0 || !m_view) return;
    Document* d = m_view->doc();
    if (m_drag >= 20 && m_drag < 30) d->ruler.setHandle(m_drag - 20, e.pos + m_off, e.mods & Qt::ShiftModifier);
    else if (m_drag == 30) {
        const QPointF dd = e.pos - m_off;
        d->ruler.a += dd;
        d->ruler.b += dd;
        d->ruler.c += dd;
        m_off = e.pos;
    } else if (m_drag <= 2) d->perspective.vp[m_drag] = e.pos + m_off;
    else if (m_drag == 10) d->symmetry.center = e.pos + m_off;
    else if (m_drag == 11) {
        QPointF v = e.pos - d->symmetry.center;
        double a = qRadiansToDegrees(std::atan2(v.y(), v.x()));
        if (e.mods & Qt::ShiftModifier) a = std::round(a / 15) * 15;
        d->symmetry.angle = a;
    }
    d->guidesModified();
}

void RulerTool::release(const PointerEvent&) { m_drag = -1; }

void RulerTool::drawOverlay(QPainter& p, CanvasView* v) {
    Document* d = v->doc();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    if (d->perspective.enabled) {
        for (int i = 0; i < d->perspective.points; ++i) {
            QPointF w = v->toWidget(d->perspective.vp[i]);
            p.setPen(QPen(Qt::black, 1.5));
            p.setBrush(d->perspective.color);
            p.drawEllipse(w, 7, 7);
            p.setPen(Qt::white);
            p.drawText(QRectF(w.x() - 7, w.y() - 7, 14, 14), Qt::AlignCenter, QString::number(i + 1));
        }
    }
    if (d->ruler.isOn()) {
        const QVector<QPointF> hs = d->ruler.handles();
        for (int i = 0; i < hs.size(); ++i) {
            const QPointF w = v->toWidget(hs[i]);
            p.setPen(QPen(Qt::black, 1.5));
            p.setBrush(i == 0 ? d->ruler.color : QColor(Qt::white));
            p.drawRect(QRectF(w.x() - 5, w.y() - 5, 10, 10));
        }
    }
    if (d->symmetry.isOn()) {
        QPointF c = v->toWidget(d->symmetry.center), h = v->toWidget(symHandle(d->symmetry, v->zoom()));
        p.setPen(QPen(QColor(255, 80, 160), 1.5));
        p.drawLine(c, h);
        p.setBrush(QColor(255, 80, 160));
        p.drawEllipse(c, 6, 6);
        p.setBrush(Qt::white);
        p.drawEllipse(h, 5, 5);
    }
    p.restore();
}

QWidget* RulerTool::createOptions(QWidget* parent) {
    auto* w = new QWidget(parent);
    m_opts = w;
    auto* f = new QFormLayout(w);
    f->setContentsMargins(6, 6, 6, 6);
    auto* rt = new QLabel(T("Ruler"), w);
    rt->setObjectName("sectionTitle");
    f->addRow(rt);
    auto* kind = new QComboBox(w);
    for (const QString& k : Ruler::kindNames()) kind->addItem(T(k));
    auto* rsnap = new QCheckBox(T("Snap strokes to the ruler"), w);
    auto* rreset = new QPushButton(T("Put the ruler in the middle"), w);
    f->addRow(T("Kind"), kind);
    f->addRow(rsnap);
    f->addRow(rreset);
    auto* title = new QLabel(T("Perspective"), w);
    title->setObjectName("sectionTitle");
    f->addRow(title);
    auto* on = new QCheckBox(T("Show perspective ruler"), w);
    auto* snap = new QCheckBox(T("Snap strokes to it"), w);
    auto* pts = new QComboBox(w);
    pts->addItems({T("1-point"), T("2-point"), T("3-point")});
    auto* reset = new QPushButton(T("Reset points"), w);
    f->addRow(on);
    f->addRow(snap);
    f->addRow(T("Type"), pts);
    f->addRow(reset);
    auto* t2 = new QLabel(T("Symmetry"), w);
    t2->setObjectName("sectionTitle");
    f->addRow(t2);
    auto* sym = new QComboBox(w);
    sym->addItems({T("Off"), T("Mirror left/right"), T("Mirror top/bottom"), T("Mirror both"), T("Radial")});
    auto* count = new ValueBar(T("Copies"), 2, 32, 0, w);
    auto* mirror = new QCheckBox(T("Mirror radial copies"), w);
    auto* center = new QPushButton(T("Center on canvas"), w);
    f->addRow(T("Mode"), sym);
    f->addRow(count);
    f->addRow(mirror);
    f->addRow(center);
    auto* hint = new QLabel(T("Drag the square handles to shape the ruler (Shift: steps of 45° / a circle), the line itself to move it. "
                              "Drag the numbered points to place vanishing points. Drag the pink dot to move the symmetry center, "
                              "the white dot to rotate the axis (Shift: 15° steps)."), w);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    f->addRow(hint);

    auto sync = [=] {
        Document* d = app()->activeDocument();
        w->setEnabled(d != nullptr);
        if (!d) return;
        QSignalBlocker b1(on), b2(snap), b3(pts), b4(sym), b6(mirror), b7(kind), b8(rsnap);
        kind->setCurrentIndex(d->ruler.kind);
        rsnap->setChecked(d->ruler.snap);
        rsnap->setEnabled(d->ruler.isOn());
        rreset->setEnabled(d->ruler.isOn());
        on->setChecked(d->perspective.enabled);
        snap->setChecked(d->perspective.snap);
        pts->setCurrentIndex(d->perspective.points - 1);
        sym->setCurrentIndex(d->symmetry.mode);
        count->setValue(d->symmetry.radialCount);
        mirror->setChecked(d->symmetry.radialMirror);
    };
    auto edit = [](const std::function<void(Document*)>& fn) {
        if (Document* d = app()->activeDocument()) {
            fn(d);
            d->guidesModified();
        }
    };
    connect(kind, &QComboBox::currentIndexChanged, w, [edit](int i) {
        edit([i](Document* d) {
            d->ruler.kind = i;
            d->ruler.snap = true;
            d->ruler.resetFor(d->size());
        });
    });
    connect(rsnap, &QCheckBox::toggled, w, [edit](bool x) { edit([x](Document* d) { d->ruler.snap = x; }); });
    connect(rreset, &QPushButton::clicked, w, [edit] { edit([](Document* d) { d->ruler.resetFor(d->size()); }); });
    connect(on, &QCheckBox::toggled, w, [edit](bool x) { edit([x](Document* d) { d->perspective.enabled = x; }); });
    connect(snap, &QCheckBox::toggled, w, [edit](bool x) { edit([x](Document* d) { d->perspective.snap = x; }); });
    connect(pts, &QComboBox::currentIndexChanged, w, [edit](int i) {
        edit([i](Document* d) {
            d->perspective.points = i + 1;
            d->perspective.resetFor(d->size());
        });
    });
    connect(reset, &QPushButton::clicked, w, [edit] { edit([](Document* d) { d->perspective.resetFor(d->size()); }); });
    connect(sym, &QComboBox::currentIndexChanged, w, [edit](int i) { edit([i](Document* d) { d->symmetry.mode = i; }); });
    connect(count, &ValueBar::valueChanged, w, [edit](double x) { edit([x](Document* d) { d->symmetry.radialCount = int(x); }); });
    connect(mirror, &QCheckBox::toggled, w, [edit](bool x) { edit([x](Document* d) { d->symmetry.radialMirror = x; }); });
    connect(center, &QPushButton::clicked, w, [edit] {
        edit([](Document* d) { d->symmetry.center = QPointF(d->size().width() / 2.0, d->size().height() / 2.0); });
    });
    connect(app(), &App::activeViewChanged, w, sync);
    auto hookDoc = [w, sync] {
        if (Document* d = app()->activeDocument()) QObject::connect(d, &Document::guidesChanged, w, sync);
    };
    hookDoc();
    connect(app(), &App::activeViewChanged, w, hookDoc);
    sync();
    return w;
}

}  // namespace gx
