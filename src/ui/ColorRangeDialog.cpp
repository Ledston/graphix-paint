#include "ui/ColorRangeDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerOps.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

namespace {
class MaskView : public QWidget {
public:
    MaskView(const QImage* img, const QImage* mask, std::function<void(QPointF)> pick, QWidget* p)
        : QWidget(p), m_img(img), m_mask(mask), m_pick(std::move(pick)) {
        setMinimumSize(360, 300);
        setCursor(Qt::CrossCursor);
        setToolTip(T("Click to pick the color to select"));
    }
    bool showImage = false;

protected:
    QRectF target() const {
        if (m_img->isNull()) return {};
        QSizeF s = QSizeF(m_img->size()).scaled(QSizeF(size()) - QSizeF(8, 8), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Theme::colors().base);
        QRectF r = target();
        if (r.isEmpty()) return;
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(r, showImage ? *m_img : *m_mask);
    }
    void mousePressEvent(QMouseEvent* e) override {
        QRectF r = target();
        if (!r.contains(e->position())) return;
        m_pick(QPointF((e->position().x() - r.left()) / r.width(), (e->position().y() - r.top()) / r.height()));
    }

private:
    const QImage* m_img;
    const QImage* m_mask;
    std::function<void(QPointF)> m_pick;
};
}  // namespace

ColorRangeDialog::ColorRangeDialog(Document* doc, QWidget* parent) : QDialog(parent), m_doc(doc), m_color(app()->foreground()) {
    setWindowTitle(T("Color range"));
    auto* v = new QVBoxLayout(this);
    auto* view = new MaskView(&m_small, &m_mask, [this](QPointF f) {
        if (m_small.isNull()) return;
        QPoint p(std::clamp(int(f.x() * m_small.width()), 0, m_small.width() - 1), std::clamp(int(f.y() * m_small.height()), 0, m_small.height() - 1));
        m_color = QColor::fromRgb(m_small.pixel(p));
        m_colorBtn->setColor(m_color);
        updatePreview();
    }, this);
    m_view = view;
    v->addWidget(view, 1);
    auto* show = new QCheckBox(T("Show the picture instead of the selection"), this);
    connect(show, &QCheckBox::toggled, this, [view](bool on) {
        view->showImage = on;
        view->update();
    });
    v->addWidget(show);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(T("Color"), this));
    m_colorBtn = new ColorButton(m_color, this);
    connect(m_colorBtn, &ColorButton::colorChanged, this, [this](const QColor& c) {
        m_color = c;
        updatePreview();
    });
    row->addWidget(m_colorBtn);
    row->addSpacing(12);
    row->addWidget(new QLabel(T("Take from"), this));
    m_source = new QComboBox(this);
    m_source->addItems({T("All visible layers"), T("Current layer")});
    connect(m_source, &QComboBox::currentIndexChanged, this, [this] {
        loadSource();
        updatePreview();
    });
    row->addWidget(m_source, 1);
    v->addLayout(row);

    auto* fuzz = new ValueBar(T("Fuzziness"), 0, 200, 0, this);
    fuzz->setValue(m_fuzz);
    connect(fuzz, &ValueBar::valueChanged, this, [this](double x) {
        m_fuzz = x;
        updatePreview();
    });
    v->addWidget(fuzz);
    auto* inv = new QCheckBox(T("Invert"), this);
    connect(inv, &QCheckBox::toggled, this, [this](bool on) {
        m_invert = on;
        updatePreview();
    });
    v->addWidget(inv);
    auto* mrow = new QHBoxLayout;
    mrow->addWidget(new QLabel(T("Mode"), this));
    m_mode = new QComboBox(this);
    m_mode->addItems({T("New selection"), T("Add (Shift)"), T("Subtract (Alt)"), T("Intersect (Shift+Alt)")});
    mrow->addWidget(m_mode, 1);
    v->addLayout(mrow);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &ColorRangeDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(520, 620);
    loadSource();
    updatePreview();
}

void ColorRangeDialog::loadSource() {
    if (!m_doc) return;
    QImage img;
    if (m_source->currentIndex() == 1 && m_doc->currentLayer() && m_doc->currentLayer()->displaySurface())
        img = surfaceToImage(*m_doc->currentLayer()->displaySurface(), m_doc->bounds());
    else
        img = Compositor::renderImage(m_doc->root(), m_doc->bounds());
    m_full = img;
    const double px = double(img.width()) * img.height();
    const double s = px > 300000.0 ? std::sqrt(300000.0 / px) : 1.0;
    m_small = (s < 1 ? img.scaled(img.size() * s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : img)
                  .convertToFormat(QImage::Format_ARGB32);
}

float ColorRangeDialog::weight(quint32 c) const {
    const int a = int(c >> 24);
    if (a == 0) return m_invert ? 1.f : 0.f;
    // straight color from premultiplied
    const float r = float((c >> 16) & 255) * 255.f / a, g = float((c >> 8) & 255) * 255.f / a, b = float(c & 255) * 255.f / a;
    const float dr = r - m_color.red(), dg = g - m_color.green(), db = b - m_color.blue();
    const float d = std::sqrt((dr * dr * 0.3f + dg * dg * 0.59f + db * db * 0.11f) * 3.f);
    const float f = float(std::max(1.0, m_fuzz));
    float w = d <= f * 0.5f ? 1.f : std::clamp(1.f - (d - f * 0.5f) / (f * 0.5f), 0.f, 1.f);
    w *= a / 255.f;
    return m_invert ? 1.f - w : w;
}

void ColorRangeDialog::updatePreview() {
    if (m_small.isNull()) return;
    QImage pre = m_small.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_mask = QImage(pre.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < pre.height(); ++y) {
        const quint32* row = reinterpret_cast<const quint32*>(pre.constScanLine(y));
        uchar* o = m_mask.scanLine(y);
        for (int x = 0; x < pre.width(); ++x) o[x] = uchar(std::lround(weight(row[x]) * 255.f));
    }
    m_view->update();
}

void ColorRangeDialog::accept() {
    if (!m_doc || m_full.isNull()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QImage img = m_full.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    MaskSurface mask(0);
    std::vector<float> row(size_t(img.width()));
    for (int y = 0; y < img.height(); ++y) {
        const quint32* src = reinterpret_cast<const quint32*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) row[size_t(x)] = weight(src[x]);
        writeMask(mask, QRect(0, y, img.width(), 1), row.data());
    }
    mask.optimize();
    ops::combineSelection(*m_doc, mask, ops::SelectMode(m_mode->currentIndex()), T("Color range"));
    QApplication::restoreOverrideCursor();
    QDialog::accept();
}

}  // namespace gx
