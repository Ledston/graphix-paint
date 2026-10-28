#include "ui/LineartDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerOps.h"
#include "core/Pixel.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "vector/VectorRender.h"

namespace gx {

namespace {

std::vector<float> toFloat(const QImage& img) {
    std::vector<float> v(size_t(img.width()) * img.height() * 4);
    for (int y = 0; y < img.height(); ++y) {
        const quint32* row = reinterpret_cast<const quint32*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) unpack(row[x], &v[(size_t(y) * img.width() + x) * 4]);
    }
    return v;
}

QImage toImage(const std::vector<float>& v, int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        quint32* row = reinterpret_cast<quint32*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) row[x] = pack(&v[(size_t(y) * w + x) * 4]);
    }
    return img;
}

class PreviewView : public QWidget {
public:
    PreviewView(const QImage* result, const QImage* original, const int* bg, QWidget* p)
        : QWidget(p), m_result(result), m_original(original), m_bg(bg) {
        setMinimumSize(420, 360);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    bool busy = false;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Theme::colors().base);
        const QImage& img = m_result->isNull() ? *m_original : *m_result;
        if (img.isNull()) return;
        QSizeF s = QSizeF(img.size()).scaled(QSizeF(size()) - QSizeF(16, 16), Qt::KeepAspectRatio);
        QRectF r(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
        if (*m_bg == 1) p.fillRect(r, checkerBrush(8));
        else p.fillRect(r, Qt::white);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (*m_bg == 2 && !m_original->isNull()) {
            p.setOpacity(0.35);
            p.drawImage(r, *m_original);
            p.setOpacity(1.0);
        }
        if (!m_result->isNull()) p.drawImage(r, *m_result);
        if (busy) {
            p.setPen(Theme::colors().dim);
            p.drawText(rect().adjusted(8, 6, -8, -6), Qt::AlignTop | Qt::AlignRight, T("Updating..."));
        }
    }

private:
    const QImage* m_result;
    const QImage* m_original;
    const int* m_bg;
};

}  // namespace

LineartDialog::LineartDialog(Document* doc, QWidget* parent) : QDialog(parent), m_doc(doc) {
    setWindowTitle(T("Make line art"));
    auto* h = new QHBoxLayout(this);

    // ---- preview
    auto* left = new QVBoxLayout;
    auto* view = new PreviewView(&m_result, &m_small, &m_bg, this);
    m_view = view;
    left->addWidget(view, 1);
    auto* bgRow = new QHBoxLayout;
    bgRow->addWidget(new QLabel(T("Preview on"), this));
    auto* bg = new QComboBox(this);
    bg->addItems({T("White"), T("Transparent (checkerboard)"), T("The original, faded")});
    connect(bg, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_bg = i;
        m_view->update();
    });
    bgRow->addWidget(bg, 1);
    left->addLayout(bgRow);
    h->addLayout(left, 1);

    // ---- settings
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(330);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 0, 6, 0);
    scroll->setWidget(body);
    auto* right = new QVBoxLayout;
    right->addWidget(scroll, 1);
    h->addLayout(right);

    auto row = [&](QVBoxLayout* lay, const QString& label, QWidget* w) {
        auto* r = new QHBoxLayout;
        r->addWidget(new QLabel(label, body));
        r->addWidget(w, 1);
        lay->addLayout(r);
    };
    auto group = [&](const QString& title) {
        auto* g = new QGroupBox(title, body);
        auto* l = new QVBoxLayout(g);
        v->addWidget(g);
        return l;
    };
    auto bar = [&](QVBoxLayout* lay, const QString& label, double min, double max, int dec, double* field, const QString& suffix = {},
                   const QString& tip = {}) {
        auto* b = new ValueBar(label, min, max, dec, body);
        b->setSuffix(suffix);
        b->setToolTip(tip);
        b->setValue(*field);
        connect(b, &ValueBar::valueChanged, this, [this, field](double x) {
            if (m_loading) return;
            *field = x;
            schedule();
        });
        m_loaders << [b, field] { b->setValue(*field); };
        lay->addWidget(b);
    };
    auto checkBox = [&](QVBoxLayout* lay, const QString& text, bool* field) {
        auto* c = new QCheckBox(text, body);
        c->setChecked(*field);
        connect(c, &QCheckBox::toggled, this, [this, field](bool on) {
            if (m_loading) return;
            *field = on;
            schedule();
        });
        m_loaders << [c, field] { c->setChecked(*field); };
        lay->addWidget(c);
    };

    auto* top = group(T("Picture"));
    m_sourceBox = new QComboBox(body);
    m_sourceBox->addItem(T("Current layer"));
    m_sourceBox->addItem(T("All visible layers"));
    if (!dynamic_cast<RasterLayer*>(doc->currentLayer())) m_sourceBox->setCurrentIndex(1);
    connect(m_sourceBox, &QComboBox::currentIndexChanged, this, [this] {
        loadSource();
        schedule();
    });
    row(top, T("Take from"), m_sourceBox);
    auto* presets = new QComboBox(body);
    presets->addItems({T("Choose a preset..."), T("Clean ink"), T("Pencil sketch"), T("Comic / manga"), T("Soft charcoal"),
                       T("Scan cleanup"), T("Colored lines")});
    connect(presets, &QComboBox::activated, this, [this, presets](int i) {
        applyPreset(i);
        presets->setCurrentIndex(0);
    });
    row(top, T("Preset"), presets);
    m_modeBox = new QComboBox(body);
    m_modeBox->addItem(T("Automatic"), LineartParams::Auto);
    m_modeBox->addItem(T("Photo: find contours"), LineartParams::Photo);
    m_modeBox->addItem(T("Sketch or scan: clean up"), LineartParams::Sketch);
    m_modeBox->addItem(T("Anime / render: sharp edges"), LineartParams::Anime);
    connect(m_modeBox, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading) return;
        m_p.mode = m_modeBox->currentData().toInt();
        schedule();
    });
    m_loaders << [this] { m_modeBox->setCurrentIndex(std::max(0, m_modeBox->findData(m_p.mode))); };
    row(top, T("Kind of picture"), m_modeBox);
    m_autoInfo = new QLabel(body);
    m_autoInfo->setObjectName("sectionTitle");
    top->addWidget(m_autoInfo);

    auto* lines = group(T("Lines"));
    bar(lines, T("Detail"), 0, 100, 0, &m_p.detail, "%", T("More detail finds fainter lines"));
    bar(lines, T("Thickness"), 0.5, 12, 1, &m_p.thickness, " px");
    bar(lines, T("Smoothness"), 0, 5, 1, &m_p.smooth);
    bar(lines, T("Remove specks"), 0, 30, 0, &m_p.cleanup, " px", T("Small dots and dirt up to this size disappear"));
    {
        auto* cg = new ValueBar(T("Close gaps"), 0, 20, 0, body);
        cg->setSuffix(" px");
        cg->setValue(m_p.closeGaps);
        connect(cg, &ValueBar::valueChanged, this, [this](double x) {
            if (m_loading) return;
            m_p.closeGaps = int(x);
            schedule();
        });
        m_loaders << [this, cg] { cg->setValue(m_p.closeGaps); };
        lines->addWidget(cg);
    }
    checkBox(lines, T("Keep big dark areas as solid ink"), &m_p.fillDarks);

    auto* colorG = group(T("Line color"));
    {
        auto* r = new QHBoxLayout;
        auto* cm = new QComboBox(body);
        cm->addItems({T("One color"), T("Colors from the picture")});
        cm->setCurrentIndex(m_p.colorMode);
        auto* cb = new ColorButton(m_p.color, body);
        connect(cm, &QComboBox::currentIndexChanged, this, [this, cb](int i) {
            if (m_loading) return;
            m_p.colorMode = i;
            cb->setEnabled(i == 0);
            schedule();
        });
        connect(cb, &ColorButton::colorChanged, this, [this](const QColor& c) {
            if (m_loading) return;
            m_p.color = c;
            schedule();
        });
        m_loaders << [this, cm, cb] {
            cm->setCurrentIndex(m_p.colorMode);
            cb->setColor(m_p.color);
            cb->setEnabled(m_p.colorMode == 0);
        };
        r->addWidget(cm, 1);
        r->addWidget(cb);
        colorG->addLayout(r);
    }

    auto* hand = group(T("Hand-drawn look"));
    bar(hand, T("Wobble"), 0, 8, 1, &m_p.wobble, " px", T("Lines drift a little, like drawn by hand"));
    bar(hand, T("Wobble length"), 5, 200, 0, &m_p.wobbleScale, " px");
    bar(hand, T("Pressure"), 0, 1, 2, &m_p.pressure, {}, T("Thickness changes along the lines, like pen pressure"));
    bar(hand, T("Sketchy second line"), 0, 1, 2, &m_p.sketchy);
    {
        auto* r = new QHBoxLayout;
        auto* tex = new QComboBox(body);
        tex->addItems({T("No texture"), T("Pencil"), T("Charcoal"), T("Ink bleed")});
        connect(tex, &QComboBox::currentIndexChanged, this, [this](int i) {
            if (m_loading) return;
            m_p.texture = i;
            schedule();
        });
        m_loaders << [this, tex] { tex->setCurrentIndex(m_p.texture); };
        r->addWidget(new QLabel(T("Texture"), body));
        r->addWidget(tex, 1);
        hand->addLayout(r);
    }
    bar(hand, T("Texture strength"), 0, 1, 2, &m_p.textureStrength);
    auto* seed = new QPushButton(T("New random pattern"), body);
    connect(seed, &QPushButton::clicked, this, [this] {
        ++m_p.seed;
        schedule();
    });
    hand->addWidget(seed);

    auto* res = group(T("Result"));
    m_hideSource = new QCheckBox(T("Hide the original layer"), body);
    m_vector = new QCheckBox(T("Make editable linework (every line can be changed)"), body);
    m_vector->setToolTip(T("Creates a linework layer: change points, thickness, pressure and color of each line with the linework tools."));
    res->addWidget(m_hideSource);
    res->addWidget(m_vector);
    res->addWidget(new QLabel(T("The line art goes on a new transparent layer above, so you can color under it."), body));
    static_cast<QLabel*>(res->itemAt(res->count() - 1)->widget())->setWordWrap(true);
    static_cast<QLabel*>(res->itemAt(res->count() - 1)->widget())->setObjectName("sectionTitle");
    v->addStretch();

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(T("Make line art"));
    right->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &LineartDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_timer.setSingleShot(true);
    m_timer.setInterval(90);
    connect(&m_timer, &QTimer::timeout, this, &LineartDialog::startPreview);
    connect(&m_watch, &QFutureWatcher<QPair<QImage, int>>::finished, this, [this] {
        auto r = m_watch.result();
        m_result = r.first;
        static_cast<PreviewView*>(m_view)->busy = false;
        if (m_p.mode == LineartParams::Auto) {
            QString kind = r.second == LineartParams::Sketch ? T("Sketch or scan") : r.second == LineartParams::Anime ? T("Anime / render") : T("Photo");
            m_autoInfo->setText(T("Recognized as: %1").arg(kind));
        } else {
            m_autoInfo->clear();
        }
        m_view->update();
        if (m_dirty) {
            m_dirty = false;
            startPreview();
        }
    });
    resize(1100, 760);
    loadSource();
    schedule();
}

void LineartDialog::loadSource() {
    if (!m_doc) return;
    auto* rl = dynamic_cast<RasterLayer*>(m_doc->currentLayer());
    if (m_sourceBox->currentIndex() == 0 && rl) {
        QRect b = rl->pixels.exactBounds() & m_doc->bounds();
        if (b.isEmpty()) b = m_doc->bounds();
        m_source = surfaceToImage(rl->pixels, b);
        m_sourceAt = b.topLeft();
    } else {
        m_source = Compositor::renderImage(m_doc->root(), m_doc->bounds());
        m_sourceAt = QPoint(0, 0);
    }
    // the preview works on a copy of about one megapixel, so it updates while you drag
    const double px = double(m_source.width()) * m_source.height();
    m_scale = px > 1.0e6 ? std::sqrt(1.0e6 / px) : 1.0;
    m_small = m_scale < 1.0 ? m_source.scaled(m_source.size() * m_scale, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                .convertToFormat(QImage::Format_ARGB32_Premultiplied)
                            : m_source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_result = QImage();
}

void LineartDialog::schedule() { m_timer.start(); }

void LineartDialog::startPreview() {
    if (m_small.isNull()) return;
    if (m_watch.isRunning()) {
        m_dirty = true;
        return;
    }
    static_cast<PreviewView*>(m_view)->busy = true;
    m_view->update();
    const QImage img = m_small;
    const LineartParams p = m_p;
    const double scale = m_scale;
    const QPoint at = m_sourceAt;
    m_watch.setFuture(QtConcurrent::run([img, p, scale, at]() {
        std::vector<float> in = toFloat(img), out(in.size());
        int used = makeLineart(in.data(), img.width(), img.height(), p, out.data(), scale, at);
        return qMakePair(toImage(out, img.width(), img.height()), used);
    }));
}

void LineartDialog::applyPreset(int i) {
    LineartParams p;
    p.seed = m_p.seed;
    switch (i) {
    case 1:  // clean ink
        p.detail = 45; p.thickness = 2; p.smooth = 1.5; p.cleanup = 6;
        break;
    case 2:  // pencil sketch
        p.detail = 60; p.thickness = 1.2; p.smooth = 0.5; p.cleanup = 3; p.wobble = 0.8; p.pressure = 0.5; p.texture = 1;
        p.textureStrength = 0.6; p.sketchy = 0.35; p.color = QColor(60, 60, 70);
        break;
    case 3:  // comic / manga
        p.detail = 50; p.thickness = 2.5; p.smooth = 2; p.cleanup = 8; p.fillDarks = true; p.pressure = 0.3;
        break;
    case 4:  // soft charcoal
        p.detail = 55; p.thickness = 3; p.smooth = 1; p.cleanup = 4; p.wobble = 1.2; p.pressure = 0.6; p.texture = 2;
        p.textureStrength = 0.8; p.color = QColor(40, 36, 34);
        break;
    case 5:  // scan cleanup
        p.mode = LineartParams::Sketch; p.detail = 55; p.thickness = 1; p.smooth = 0.6; p.cleanup = 5; p.closeGaps = 2;
        break;
    case 6:  // colored lines
        p.detail = 50; p.thickness = 2; p.smooth = 1.2; p.colorMode = 1;
        break;
    default: return;
    }
    m_p = p;
    syncControls();
    schedule();
}

void LineartDialog::syncControls() {
    m_loading = true;
    for (auto& f : m_loaders) f();
    m_loading = false;
}

void LineartDialog::accept() {
    if (!m_doc || m_source.isNull()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_watch.waitForFinished();
    std::vector<float> in = toFloat(m_source), out(in.size());
    makeLineart(in.data(), m_source.width(), m_source.height(), m_p, out.data(), 1.0, m_sourceAt);
    Document& d = *m_doc;
    Layer* src = d.currentLayer();
    const quint32 srcId = src ? src->id : 0;
    Layer* added = nullptr;
    // on top of the picture it came from (or of everything, for "all visible layers")
    Layer* above = m_sourceBox->currentIndex() == 0 ? src : (d.root()->children.empty() ? nullptr : d.root()->children.back().get());
    if (m_vector->isChecked()) {
        std::vector<float> alpha(size_t(m_source.width()) * m_source.height());
        for (size_t i = 0; i < alpha.size(); ++i) alpha[i] = out[i * 4 + 3];
        QVector<VectorStroke> strokes = traceLineart(alpha.data(), m_source.width(), m_source.height(), m_sourceAt, m_p.color);
        auto vl = std::make_shared<VectorLayer>();
        vl->id = d.newLayerId();
        vl->name = d.uniqueLayerName(T("Line art"));
        quint32 sid = 1;
        for (auto& s : strokes) {
            s.id = sid++;
            vl->strokes.push_back(s);
        }
        renderVectorLayer(*vl);
        added = ops::insertLayer(d, vl, above, true, T("Make line art"));
    } else {
        auto rl = std::make_shared<RasterLayer>();
        rl->id = d.newLayerId();
        rl->name = d.uniqueLayerName(T("Line art"));
        writeFloat(rl->pixels, QRect(m_sourceAt, m_source.size()), out.data());
        rl->pixels.optimize();
        rl->reference = true;  // ready for "fill by line art"
        added = ops::insertLayer(d, rl, above, true, T("Make line art"));
    }
    if (m_hideSource->isChecked() && m_sourceBox->currentIndex() == 0)
        if (Layer* s = d.layer(srcId)) {
            s->visible = false;
            d.layerChanged(s);
        }
    if (added) d.setCurrentLayer(added->id);
    d.layersChanged();
    QApplication::restoreOverrideCursor();
    app()->showStatus(T("Line art is ready on its own layer. Paint colors on a layer under it."), 6000);
    QDialog::accept();
}

}  // namespace gx
