#include "ui/LiquifyDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QTabletEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>

#include "app/App.h"
#include "app/I18n.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

// ---------------------------------------------------------------------------- the picture

class LiquifyCanvas : public QWidget {
public:
    explicit LiquifyCanvas(LiquifyDialog* dlg) : QWidget(dlg), m_dlg(dlg) {
        setMouseTracking(true);
        setAttribute(Qt::WA_TabletTracking);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(420, 320);
        setCursor(Qt::BlankCursor);
    }
    QPointF toImage(QPointF w) const { return (w - m_pan) / m_zoom; }
    QPointF toWidget(QPointF i) const { return i * m_zoom + m_pan; }
    void fit() {
        const QSize s = m_dlg->m_field.size();
        if (s.isEmpty() || width() < 10) return;
        m_zoom = std::min((width() - 20.0) / s.width(), (height() - 20.0) / s.height());
        m_zoom = std::clamp(m_zoom, 0.02, 8.0);
        m_pan = QPointF((width() - s.width() * m_zoom) / 2, (height() - s.height() * m_zoom) / 2);
        m_fitted = true;
        update();
    }
    void frozenChanged(const QRect& r) {
        // the red veil over frozen places, kept small
        const QSize s = m_dlg->m_field.size();
        if (s.isEmpty()) return;
        const double k = std::min(1.0, 1024.0 / std::max(s.width(), s.height()));
        if (m_frozenImg.isNull()) {
            m_frozenScale = k;
            m_frozenImg = QImage(std::max(1, int(s.width() * k)), std::max(1, int(s.height() * k)), QImage::Format_ARGB32_Premultiplied);
            m_frozenImg.fill(Qt::transparent);
        }
        QRect fr = r.isNull() ? m_frozenImg.rect()
                              : QRect(QPoint(int(r.left() * k) - 1, int(r.top() * k) - 1), QPoint(int(r.right() * k) + 1, int(r.bottom() * k) + 1)) &
                                    m_frozenImg.rect();
        for (int y = fr.top(); y <= fr.bottom(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(m_frozenImg.scanLine(y));
            for (int x = fr.left(); x <= fr.right(); ++x) {
                const int a = int(m_dlg->m_field.frozenAt(int((x + 0.5) / k), int((y + 0.5) / k)) * 120);
                row[x] = qRgba(230 * a / 255, 40 * a / 255, 40 * a / 255, a);
            }
        }
        update();
    }
    // one pen position: dabs along the way from the last one
    void penAt(QPointF w, double pressure, bool alt) {
        const QPointF p = toImage(w);
        if (!m_down) return;
        int mode = m_dlg->m_mode;
        if (alt) {  // Alt: the opposite tool, as in Photoshop
            if (mode == LiquifyField::TwirlCW) mode = LiquifyField::TwirlCCW;
            else if (mode == LiquifyField::TwirlCCW) mode = LiquifyField::TwirlCW;
            else if (mode == LiquifyField::Pucker) mode = LiquifyField::Bloat;
            else if (mode == LiquifyField::Bloat) mode = LiquifyField::Pucker;
        }
        const double pr = m_dlg->m_penPressure ? pressure : 1.0;
        const LiquifyField::Brush& b = m_dlg->m_brush;
        const QPointF d = p - m_last;
        const double len = std::hypot(d.x(), d.y());
        const double step = std::max(1.0, b.size * 0.08);
        LiquifyField::Mode m = LiquifyField::Mode(mode);
        QRect changed;
        if (m == LiquifyField::Forward || m == LiquifyField::PushLeft) {
            if (len < step * 0.5) return;
            QPointF dd = d;
            if (m == LiquifyField::PushLeft && alt) dd = -dd;  // Alt: push right
            changed = m_dlg->m_field.dab(m, m_last, dd, pr, b);
            m_last = p;
        } else {
            if (len < step && !m_first) return;
            const int n = m_first ? 1 : int(len / step);
            for (int i = 1; i <= n; ++i) changed |= m_dlg->m_field.dab(m, m_first ? p : m_last + d * (double(i) / n), {}, pr, b);
            m_last = p;
        }
        m_first = false;
        if (m == LiquifyField::Freeze || m == LiquifyField::Thaw) frozenChanged(changed);
        update();
    }
    void begin(QPointF w, double pressure, bool alt) {
        m_dlg->m_field.saveUndo();
        m_down = true;
        m_first = true;
        m_last = toImage(w);
        penAt(w, pressure, alt);
    }

protected:
    void resizeEvent(QResizeEvent*) override {
        if (!m_fitted) fit();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Theme::colors().window.darker(115));
        const QImage& img = m_dlg->m_field.output();
        if (img.isNull()) return;
        const QRectF target(m_pan, QSizeF(img.width() * m_zoom, img.height() * m_zoom));
        // checkerboard under the picture
        {
            p.save();
            p.setClipRect(target);
            const int c = 8;
            for (int y = int(std::max(0.0, target.top())) / c * c; y < std::min(double(height()), target.bottom()); y += c)
                for (int x = int(std::max(0.0, target.left())) / c * c; x < std::min(double(width()), target.right()); x += c)
                    p.fillRect(x, y, c, c, ((x / c + y / c) % 2) ? QColor(205, 205, 205) : QColor(250, 250, 250));
            p.restore();
        }
        // only the visible part of the picture is scaled
        const QRectF vis = target & QRectF(rect());
        if (!vis.isEmpty()) {
            const QRectF src((vis.left() - m_pan.x()) / m_zoom, (vis.top() - m_pan.y()) / m_zoom, vis.width() / m_zoom, vis.height() / m_zoom);
            p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
            p.drawImage(vis, img, src);
        }
        if (m_dlg->m_showFrozen && !m_frozenImg.isNull()) p.drawImage(target, m_frozenImg);
        if (m_dlg->m_showMesh) {
            // the grid of the picture as it is bent now
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(60, 140, 255, 170), 1));
            const int stepPx = std::max(8, int(32 / std::max(0.05, m_zoom)) / 8 * 8);
            const QSize s = img.size();
            auto bent = [&](double x, double y) { return toWidget(QPointF(x, y) - m_dlg->m_field.displacementAt(x, y)); };
            for (int y = 0; y <= s.height(); y += stepPx) {
                QPainterPath path;
                for (int x = 0; x <= s.width(); x += std::max(4, stepPx / 4)) {
                    const QPointF q = bent(x, y);
                    if (x == 0) path.moveTo(q);
                    else path.lineTo(q);
                }
                p.drawPath(path);
            }
            for (int x = 0; x <= s.width(); x += stepPx) {
                QPainterPath path;
                for (int y = 0; y <= s.height(); y += std::max(4, stepPx / 4)) {
                    const QPointF q = bent(x, y);
                    if (y == 0) path.moveTo(q);
                    else path.lineTo(q);
                }
                p.drawPath(path);
            }
        }
        // the brush
        if (m_inside) {
            p.setRenderHint(QPainter::Antialiasing);
            const double r = m_dlg->m_brush.size / 2 * m_zoom;
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0, 0, 0, 170), 1.6));
            p.drawEllipse(m_hover, r, r);
            p.setPen(QPen(QColor(255, 255, 255, 230), 0.8));
            p.drawEllipse(m_hover, r, r);
            p.drawLine(m_hover - QPointF(3, 0), m_hover + QPointF(3, 0));
            p.drawLine(m_hover - QPointF(0, 3), m_hover + QPointF(0, 3));
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (m_tablet) return;
        if (e->button() == Qt::MiddleButton || m_space) {
            m_panning = true;
            m_panFrom = e->position();
            return;
        }
        if (e->button() == Qt::LeftButton) begin(e->position(), 1.0, e->modifiers() & Qt::AltModifier);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        m_hover = e->position();
        m_inside = true;
        if (m_panning) {
            m_pan += e->position() - m_panFrom;
            m_panFrom = e->position();
            update();
            return;
        }
        if (!m_tablet && m_down) penAt(e->position(), 1.0, e->modifiers() & Qt::AltModifier);
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        m_panning = false;
        if (!m_tablet) m_down = false;
    }
    void tabletEvent(QTabletEvent* e) override {
        m_hover = e->position();
        m_inside = true;
        switch (e->type()) {
        case QEvent::TabletPress:
            m_tablet = true;
            if (m_space) {
                m_panning = true;
                m_panFrom = e->position();
            } else {
                begin(e->position(), e->pressure(), e->modifiers() & Qt::AltModifier);
            }
            break;
        case QEvent::TabletMove:
            if (m_panning) {
                m_pan += e->position() - m_panFrom;
                m_panFrom = e->position();
            } else if (m_down) {
                penAt(e->position(), e->pressure(), e->modifiers() & Qt::AltModifier);
            }
            update();
            break;
        case QEvent::TabletRelease:
            m_down = false;
            m_panning = false;
            m_tablet = false;
            break;
        default: break;
        }
        e->accept();
    }
    void wheelEvent(QWheelEvent* e) override {
        const QPointF at = e->position();
        const QPointF img = toImage(at);
        const double f = e->angleDelta().y() > 0 ? 1.2 : 1 / 1.2;
        m_zoom = std::clamp(m_zoom * f, 0.02, 16.0);
        m_pan = at - img * m_zoom;
        update();
    }
    void leaveEvent(QEvent*) override {
        m_inside = false;
        update();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
            m_space = true;
            setCursor(Qt::OpenHandCursor);
            return;
        }
        QWidget::keyPressEvent(e);
    }
    void keyReleaseEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
            m_space = false;
            setCursor(Qt::BlankCursor);
            return;
        }
        QWidget::keyReleaseEvent(e);
    }

private:
    LiquifyDialog* m_dlg;
    double m_zoom = 1;
    QPointF m_pan;
    bool m_fitted = false;
    bool m_down = false, m_first = false, m_tablet = false, m_panning = false, m_space = false, m_inside = false;
    QPointF m_last, m_hover, m_panFrom;
    QImage m_frozenImg;
    double m_frozenScale = 1;
};

// ---------------------------------------------------------------------------- the window

namespace {
struct ToolInfo {
    int mode;
    const char* name;
    Qt::Key key;
    const char* tip;
};
const ToolInfo kTools[] = {
    {LiquifyField::Forward, "Forward warp", Qt::Key_W, "Pushes the pixels along with the pen."},
    {LiquifyField::Reconstruct, "Reconstruct", Qt::Key_R, "Brings the pixels back to where they were."},
    {LiquifyField::Smooth, "Smooth", Qt::Key_E, "Evens out bumpy warps."},
    {LiquifyField::TwirlCW, "Twirl clockwise", Qt::Key_C, "Turns the pixels under the brush (Alt: the other way)."},
    {LiquifyField::TwirlCCW, "Twirl counterclockwise", Qt::Key_V, "Turns the pixels under the brush the other way."},
    {LiquifyField::Pucker, "Pucker", Qt::Key_S, "Pulls the pixels toward the middle of the brush (Alt: bloat)."},
    {LiquifyField::Bloat, "Bloat", Qt::Key_B, "Pushes the pixels out from the middle of the brush (Alt: pucker)."},
    {LiquifyField::PushLeft, "Push left", Qt::Key_O, "Moves the pixels to the left of the pen's way (Alt: to the right)."},
    {LiquifyField::Freeze, "Freeze", Qt::Key_F, "Paints places that must not move (shown in red)."},
    {LiquifyField::Thaw, "Thaw", Qt::Key_D, "Lets frozen places move again."},
};
}  // namespace

LiquifyDialog::LiquifyDialog(const QImage& src, const std::vector<float>* frozen, QWidget* parent) : QDialog(parent) {
    setWindowTitle(T("Liquify"));
    QSettings& s = app()->settings();
    m_brush.size = s.value("liquify/size", 150).toDouble();
    m_brush.pressure = s.value("liquify/pressure", 0.5).toDouble();
    m_brush.density = s.value("liquify/density", 0.5).toDouble();
    m_brush.rate = s.value("liquify/rate", 0.5).toDouble();
    m_penPressure = s.value("liquify/penPressure", true).toBool();
    m_showMesh = s.value("liquify/mesh", false).toBool();
    m_mode = std::clamp(s.value("liquify/mode", 0).toInt(), 0, int(LiquifyField::ModeCount) - 1);
    m_field.begin(src, frozen);

    auto* top = new QHBoxLayout(this);
    // tools on the left
    auto* toolsBox = new QVBoxLayout;
    toolsBox->setSpacing(2);
    m_tools = new QButtonGroup(this);
    m_tools->setExclusive(true);
    for (const ToolInfo& t : kTools) {
        auto* b = new QToolButton(this);
        b->setText(QStringLiteral("%1  (%2)").arg(T(t.name), QKeySequence(t.key).toString(QKeySequence::NativeText)));
        b->setToolTip(T(t.tip));
        b->setCheckable(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMinimumWidth(170);
        m_tools->addButton(b, t.mode);
        toolsBox->addWidget(b);
    }
    toolsBox->addStretch();
    top->addLayout(toolsBox);
    connect(m_tools, &QButtonGroup::idClicked, this, [this](int id) { setMode(id); });

    m_canvas = new LiquifyCanvas(this);
    top->addWidget(m_canvas, 1);

    // settings on the right
    auto* right = new QVBoxLayout;
    auto* title = new QLabel(T("Brush"), this);
    title->setObjectName("sectionTitle");
    right->addWidget(title);
    m_size = new ValueBar(T("Size"), 2, 3000, 0, this);
    m_size->setLogarithmic(true);
    m_size->setSuffix(" px");
    m_size->setValue(m_brush.size);
    connect(m_size, &ValueBar::valueChanged, this, [this](double x) {
        m_brush.size = x;
        m_canvas->update();
    });
    right->addWidget(m_size);
    auto bar = [&](const QString& name, double* field, const QString& tip) {
        auto* b = new ValueBar(name, 0, 1, 2, this);
        b->setDisplayScale(100);
        b->setSuffix("%");
        b->setValue(*field);
        b->setToolTip(tip);
        connect(b, &ValueBar::valueChanged, this, [field](double x) { *field = x; });
        right->addWidget(b);
    };
    bar(T("Pressure"), &m_brush.pressure, T("How strongly the brush moves the pixels."));
    bar(T("Density"), &m_brush.density, T("How strong the brush stays toward its edge. Low: soft edge."));
    bar(T("Rate"), &m_brush.rate, T("How fast twirl, pucker and bloat work."));
    auto* pen = new QCheckBox(T("Pen pressure"), this);
    pen->setChecked(m_penPressure);
    connect(pen, &QCheckBox::toggled, this, [this](bool on) { m_penPressure = on; });
    right->addWidget(pen);

    auto* t2 = new QLabel(T("View"), this);
    t2->setObjectName("sectionTitle");
    right->addWidget(t2);
    auto* mesh = new QCheckBox(T("Show mesh"), this);
    mesh->setChecked(m_showMesh);
    connect(mesh, &QCheckBox::toggled, this, [this](bool on) {
        m_showMesh = on;
        m_canvas->update();
    });
    right->addWidget(mesh);
    auto* frz = new QCheckBox(T("Show frozen places"), this);
    frz->setChecked(m_showFrozen);
    connect(frz, &QCheckBox::toggled, this, [this](bool on) {
        m_showFrozen = on;
        m_canvas->update();
    });
    right->addWidget(frz);
    auto* fit = new QPushButton(T("Fit in the window"), this);
    connect(fit, &QPushButton::clicked, this, [this] { m_canvas->fit(); });
    right->addWidget(fit);

    auto* t3 = new QLabel(T("Whole picture"), this);
    t3->setObjectName("sectionTitle");
    right->addWidget(t3);
    auto* amount = new ValueBar(T("Amount"), 0, 1, 2, this);
    amount->setDisplayScale(100);
    amount->setSuffix("%");
    amount->setValue(1.0);
    right->addWidget(amount);
    auto* rec = new QPushButton(T("Reconstruct"), this);
    rec->setToolTip(T("Brings the whole picture back toward the start by the amount above (frozen places stay)."));
    connect(rec, &QPushButton::clicked, this, [this, amount] {
        m_field.saveUndo();
        m_field.reconstructAll(amount->value());
        m_canvas->update();
    });
    right->addWidget(rec);
    auto* thaw = new QPushButton(T("Thaw all"), this);
    connect(thaw, &QPushButton::clicked, this, [this] {
        m_field.saveUndo();
        m_field.thawAll();
        m_canvas->frozenChanged({});
    });
    right->addWidget(thaw);
    auto* inv = new QPushButton(T("Invert frozen"), this);
    connect(inv, &QPushButton::clicked, this, [this] {
        m_field.saveUndo();
        m_field.invertFrozen();
        m_canvas->frozenChanged({});
    });
    right->addWidget(inv);
    auto* undo = new QPushButton(Theme::icon("undo"), T("Undo"), this);
    undo->setToolTip("Ctrl+Z");
    connect(undo, &QPushButton::clicked, this, [this] {
        if (m_field.undo()) m_canvas->frozenChanged({});
    });
    right->addWidget(undo);
    auto* hint = new QLabel(T("Mouse wheel zooms, Space or the middle button moves the picture. [ and ] change the size."), this);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    right->addWidget(hint);
    right->addStretch();
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    right->addWidget(bb);
    auto* rw = new QWidget(this);
    rw->setLayout(right);
    rw->setFixedWidth(250);
    top->addWidget(rw);

    setMode(m_mode);
    if (frozen) m_canvas->frozenChanged({});
    resize(1200, 780);
    m_canvas->setFocus();
}

LiquifyDialog::~LiquifyDialog() { saveSettings(); }

QWidget* LiquifyDialog::canvasWidget() const { return m_canvas; }
QPointF LiquifyDialog::canvasPoint(QPointF imagePos) const { return m_canvas->toWidget(imagePos); }

void LiquifyDialog::saveSettings() const {
    if (!app()) return;
    QSettings& s = app()->settings();
    s.setValue("liquify/size", m_brush.size);
    s.setValue("liquify/pressure", m_brush.pressure);
    s.setValue("liquify/density", m_brush.density);
    s.setValue("liquify/rate", m_brush.rate);
    s.setValue("liquify/penPressure", m_penPressure);
    s.setValue("liquify/mesh", m_showMesh);
    s.setValue("liquify/mode", m_mode);
}

void LiquifyDialog::setMode(int mode) {
    m_mode = std::clamp(mode, 0, int(LiquifyField::ModeCount) - 1);
    if (QAbstractButton* b = m_tools->button(m_mode)) b->setChecked(true);
}

void LiquifyDialog::keyPressEvent(QKeyEvent* e) {
    if (e->matches(QKeySequence::Undo)) {
        if (m_field.undo()) m_canvas->frozenChanged({});
        return;
    }
    if (e->key() == Qt::Key_BracketLeft || e->key() == Qt::Key_BracketRight) {
        const double f = e->key() == Qt::Key_BracketRight ? 1.15 : 1 / 1.15;
        m_size->setValue(std::clamp(m_brush.size * f, 2.0, 3000.0));
        m_brush.size = m_size->value();
        m_canvas->update();
        return;
    }
    if (e->modifiers() == Qt::NoModifier)
        for (const ToolInfo& t : kTools)
            if (e->key() == t.key) {
                setMode(t.mode);
                return;
            }
    QDialog::keyPressEvent(e);
}

}  // namespace gx
