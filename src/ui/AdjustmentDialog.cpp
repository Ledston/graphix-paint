#include "ui/AdjustmentDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMutex>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "core/BlendModes.h"
#include "core/Document.h"
#include "core/Filters.h"
#include "core/ImageOps.h"
#include "core/LayerOps.h"
#include "core/Pixel.h"
#include "ui/Dialogs.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

namespace {
class Histogram : public QWidget {
public:
    explicit Histogram(QWidget* p) : QWidget(p) { setFixedHeight(70); }
    QVector<float> data;
    QColor color;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Theme::colors().base);
        if (data.isEmpty()) return;
        float mx = *std::max_element(data.begin(), data.end());
        if (mx <= 0) return;
        QColor c = color.isValid() ? color : Theme::colors().dim;
        for (int i = 0; i < data.size(); ++i) {
            double x = double(i) / data.size() * width();
            double h = std::sqrt(data[i] / mx) * height();
            p.fillRect(QRectF(x, height() - h, double(width()) / data.size() + 0.5, h), c);
        }
        p.setPen(Theme::colors().border);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
};
}  // namespace

QList<QPair<QString, QString>> AdjustmentDialog::filterIds() { return filterList(); }

AdjustmentDialog::AdjustmentDialog(Document* doc, quint32 layerId, const Adjustment& adj, Mode mode, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_layer(layerId), m_mode(mode), m_adj(adj) {
    setWindowTitle(T(adjustmentTypeName(adj.type)));
    if (mode == Mode::EditLayer) doc->pushUndo(T("Edit adjustment"));
    buildAdjustment();
}

AdjustmentDialog::AdjustmentDialog(Document* doc, quint32 layerId, const QString& filterId, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_layer(layerId), m_mode(Mode::Filter), m_filter(filterId) {
    for (const auto& f : filterList())
        if (f.first == filterId) setWindowTitle(T(f.second));
    m_params = app()->settings().value("filters/" + filterId).toJsonObject();
    QJsonObject def = filterDefaults(filterId);
    for (auto it = def.begin(); it != def.end(); ++it)
        if (!m_params.contains(it.key())) m_params.insert(it.key(), it.value());
    buildFilter();
}

AdjustmentDialog::~AdjustmentDialog() {
    if (!m_done) finish(false);
}

QWidget* AdjustmentDialog::slider(const QString& label, const QString& key, double min, double max, int dec, const QString& suffix,
                                  double scale) {
    auto* b = new ValueBar(label, min, max, dec, this);
    b->setSuffix(suffix);
    b->setDisplayScale(scale);
    QJsonObject& obj = m_mode == Mode::Filter ? m_params : m_adj.params;
    b->setValue(obj.value(key).toDouble());
    connect(b, &ValueBar::valueChanged, this, [this, key](double v) {
        QJsonObject& o = m_mode == Mode::Filter ? m_params : m_adj.params;
        o[key] = v;
        schedulePreview();
    });
    return b;
}

QWidget* AdjustmentDialog::checkbox(const QString& label, const QString& key) {
    auto* c = new QCheckBox(label, this);
    QJsonObject& obj = m_mode == Mode::Filter ? m_params : m_adj.params;
    c->setChecked(obj.value(key).toBool());
    connect(c, &QCheckBox::toggled, this, [this, key](bool on) {
        QJsonObject& o = m_mode == Mode::Filter ? m_params : m_adj.params;
        o[key] = on;
        schedulePreview();
    });
    return c;
}

QVector<float> AdjustmentDialog::histogram(int channel) const {
    QVector<float> h(256, 0.f);
    if (!m_doc) return h;
    Layer* l = m_doc->layer(m_layer);
    const RasterSurface* s = nullptr;
    if (l && l->isRaster()) s = &static_cast<RasterLayer*>(l)->pixels;
    QRect r = m_area.isEmpty() ? m_doc->bounds() : m_area;
    int step = std::max(1, int(std::sqrt(double(r.width()) * r.height() / 400000.0)));
    for (int y = r.top(); y <= r.bottom(); y += step)
        for (int x = r.left(); x <= r.right(); x += step) {
            if (!s) continue;
            quint32 p = s->pixel(x, y);
            QColor c = colorFromPremul(p);
            if (c.alpha() == 0) continue;
            int v = channel == 1 ? c.red() : channel == 2 ? c.green() : channel == 3 ? c.blue()
                                                                     : int(0.3 * c.red() + 0.59 * c.green() + 0.11 * c.blue());
            h[std::clamp(v, 0, 255)] += 1;
        }
    return h;
}

void AdjustmentDialog::buildAdjustment() {
    auto* v = new QVBoxLayout(this);
    m_body = new QVBoxLayout;
    v->addLayout(m_body);
    if (Layer* l = m_doc->layer(m_layer); l && l->isRaster()) {
        m_original = static_cast<RasterLayer*>(l)->pixels;
        m_area = m_original.exactBounds() & m_doc->bounds();
        if (m_doc->hasSelection()) m_area &= m_doc->selectionBounds();
    }
    QJsonObject& p = m_adj.params;
    switch (m_adj.type) {
    case AdjustmentType::BrightnessContrast:
        m_body->addWidget(slider(T("Brightness"), "brightness", -100, 100, 0));
        m_body->addWidget(slider(T("Contrast"), "contrast", -100, 100, 0));
        break;
    case AdjustmentType::HueSaturation:
        m_body->addWidget(slider(T("Hue"), "hue", -180, 180, 0, QString(QChar(0xB0))));
        m_body->addWidget(slider(T("Saturation"), "saturation", -100, 100, 0));
        m_body->addWidget(slider(T("Lightness"), "lightness", -100, 100, 0));
        m_body->addWidget(checkbox(T("Colorize"), "colorize"));
        break;
    case AdjustmentType::Exposure:
        m_body->addWidget(slider(T("Exposure"), "exposure", -5, 5, 2));
        m_body->addWidget(slider(T("Offset"), "offset", -0.5, 0.5, 3));
        m_body->addWidget(slider(T("Gamma"), "gamma", 0.1, 5, 2));
        break;
    case AdjustmentType::Vibrance:
        m_body->addWidget(slider(T("Vibrance"), "vibrance", -100, 100, 0));
        m_body->addWidget(slider(T("Saturation"), "saturation", -100, 100, 0));
        break;
    case AdjustmentType::Posterize: m_body->addWidget(slider(T("Levels"), "levels", 2, 64, 0)); break;
    case AdjustmentType::Threshold: m_body->addWidget(slider(T("Threshold"), "level", 0, 255, 0)); break;
    case AdjustmentType::Levels: {
        auto* ch = new QComboBox(this);
        ch->addItems({T("RGB"), T("Red"), T("Green"), T("Blue")});
        auto* hist = new Histogram(this);
        hist->data = histogram(0);
        m_body->addWidget(ch);
        m_body->addWidget(hist);
        auto* holder = new QWidget(this);
        auto* hl = new QVBoxLayout(holder);
        hl->setContentsMargins(0, 0, 0, 0);
        m_body->addWidget(holder);
        auto rebuild = [this, holder, hl, hist, ch](int idx) {
            while (QLayoutItem* it = hl->takeAt(0)) {
                delete it->widget();
                delete it;
            }
            hist->data = histogram(idx);
            hist->color = idx == 1 ? QColor(220, 80, 80) : idx == 2 ? QColor(80, 190, 80) : idx == 3 ? QColor(80, 120, 230) : QColor();
            hist->update();
            const char* keys[5] = {"inBlack", "gamma", "inWhite", "outBlack", "outWhite"};
            const char* labels[5] = {"Input black", "Gamma", "Input white", "Output black", "Output white"};
            for (int k = 0; k < 5; ++k) {
                QJsonArray arr = m_adj.params.value("channels").toArray();
                QJsonObject o = arr[idx].toObject();
                bool gamma = k == 1;
                auto* b = new ValueBar(T(labels[k]), gamma ? 0.1 : 0, gamma ? 9.99 : 1, gamma ? 2 : 3, holder);
                if (!gamma) b->setDisplayScale(255);
                b->setValue(o.value(keys[k]).toDouble(gamma ? 1 : (k == 2 || k == 4) ? 1 : 0));
                QString key = keys[k];
                connect(b, &ValueBar::valueChanged, this, [this, idx, key](double val) {
                    QJsonArray a = m_adj.params.value("channels").toArray();
                    QJsonObject oo = a[idx].toObject();
                    oo[key] = val;
                    a[idx] = oo;
                    m_adj.params["channels"] = a;
                    schedulePreview();
                });
                hl->addWidget(b);
            }
            (void)ch;
        };
        connect(ch, &QComboBox::currentIndexChanged, this, rebuild);
        rebuild(0);
        break;
    }
    case AdjustmentType::Curves: {
        auto* ch = new QComboBox(this);
        ch->addItems({T("RGB"), T("Red"), T("Green"), T("Blue"), T("Alpha")});
        auto* ce = new CurveEditor(this);
        m_body->addWidget(ch);
        m_body->addWidget(ce);
        auto load = [this, ce](int idx) {
            QJsonArray c = m_adj.params.value("curves").toArray();
            ce->setPoints(pointsFromJson(c[idx]));
            ce->setHistogram(histogram(std::min(idx, 3)));
            ce->setCurveColor(idx == 1 ? QColor(220, 80, 80) : idx == 2 ? QColor(80, 190, 80) : idx == 3 ? QColor(80, 120, 230) : QColor());
        };
        connect(ch, &QComboBox::currentIndexChanged, this, load);
        connect(ce, &CurveEditor::pointsChanged, this, [this, ch](const QVector<QPointF>& pts) {
            QJsonArray c = m_adj.params.value("curves").toArray();
            c[ch->currentIndex()] = pointsToJson(pts);
            m_adj.params["curves"] = c;
            schedulePreview();
        });
        load(0);
        auto* hint = new QLabel(T("Click to add points, drag to move, right-click removes, double-click resets."), this);
        hint->setObjectName("sectionTitle");
        hint->setWordWrap(true);
        m_body->addWidget(hint);
        break;
    }
    case AdjustmentType::ColorBalance: {
        auto* tone = new QComboBox(this);
        tone->addItems({T("Shadows"), T("Midtones"), T("Highlights")});
        m_body->addWidget(tone);
        auto* holder = new QWidget(this);
        auto* hl = new QVBoxLayout(holder);
        hl->setContentsMargins(0, 0, 0, 0);
        m_body->addWidget(holder);
        auto rebuild = [this, hl, holder](int idx) {
            while (QLayoutItem* it = hl->takeAt(0)) {
                delete it->widget();
                delete it;
            }
            const char* keys[3] = {"shadows", "midtones", "highlights"};
            const char* labels[3] = {"Cyan - Red", "Magenta - Green", "Yellow - Blue"};
            QString key = keys[idx];
            for (int c = 0; c < 3; ++c) {
                auto* b = new ValueBar(T(labels[c]), -100, 100, 0, holder);
                b->setValue(m_adj.params.value(key).toArray()[c].toDouble());
                connect(b, &ValueBar::valueChanged, this, [this, key, c](double val) {
                    QJsonArray a = m_adj.params.value(key).toArray();
                    while (a.size() < 3) a.append(0);
                    a[c] = val;
                    m_adj.params[key] = a;
                    schedulePreview();
                });
                hl->addWidget(b);
            }
        };
        connect(tone, &QComboBox::currentIndexChanged, this, rebuild);
        rebuild(1);
        tone->setCurrentIndex(1);
        m_body->addWidget(checkbox(T("Preserve luminosity"), "preserveLuminosity"));
        break;
    }
    case AdjustmentType::GradientMap: {
        auto* ed = new QPushButton(T("Edit gradient..."), this);
        m_body->addWidget(ed);
        connect(ed, &QPushButton::clicked, this, [this] {
            Gradient g = Gradient::fromJson(m_adj.params.value("gradient").toObject());
            if (Dialogs::editGradient(this, g)) {
                m_adj.params["gradient"] = g.toJson();
                schedulePreview();
            }
        });
        auto* fgbg = new QPushButton(T("Use main and sub color"), this);
        m_body->addWidget(fgbg);
        connect(fgbg, &QPushButton::clicked, this, [this] {
            m_adj.params["gradient"] = Gradient::twoColor(app()->foreground(), app()->background()).toJson();
            schedulePreview();
        });
        m_body->addWidget(checkbox(T("Reverse"), "reverse"));
        break;
    }
    default: {
        auto* l = new QLabel(T("No settings."), this);
        l->setObjectName("sectionTitle");
        m_body->addWidget(l);
        break;
    }
    }
    (void)p;
    auto* row = new QHBoxLayout;
    if (m_mode == Mode::ApplyToLayer) {
        m_preview = new QCheckBox(T("Preview"), this);
        m_preview->setChecked(true);
        connect(m_preview, &QCheckBox::toggled, this, [this] { schedulePreview(); });
        row->addWidget(m_preview);
        auto* asLayer = new QPushButton(Theme::icon("adjustment"), T("Make adjustment layer"), this);
        asLayer->setToolTip(T("Keep the change editable as an adjustment layer instead of changing pixels"));
        connect(asLayer, &QPushButton::clicked, this, [this] {
            Adjustment a = m_adj;
            finish(false);
            if (m_doc) ops::addAdjustmentLayer(*m_doc, a);
            QDialog::reject();
        });
        row->addWidget(asLayer);
    }
    row->addStretch();
    v->addLayout(row);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_timer.setSingleShot(true);
    m_timer.setInterval(60);
    connect(&m_timer, &QTimer::timeout, this, &AdjustmentDialog::computePreview);
    setMinimumWidth(340);
    schedulePreview();
}

void AdjustmentDialog::buildFilter() {
    auto* v = new QVBoxLayout(this);
    m_body = new QVBoxLayout;
    v->addLayout(m_body);
    if (Layer* l = m_doc->layer(m_layer); l && l->isRaster()) {
        m_original = static_cast<RasterLayer*>(l)->pixels;
        m_area = m_original.exactBounds() & m_doc->bounds();
        if (m_doc->hasSelection()) m_area &= m_doc->selectionBounds();
        if (m_filter == "noise") {
            // noise may also fill empty areas: work on the whole canvas (or the selection)
            m_area = m_doc->bounds();
            if (m_doc->hasSelection()) m_area &= m_doc->selectionBounds();
        } else if (m_filter == "glow" || m_filter == "motion" || m_filter == "gaussian") {
            // blurs may spread into empty areas next to the content
            int mg = filterMargin(m_filter, m_params);
            m_area = m_area.adjusted(-mg, -mg, mg, mg) & m_doc->bounds();
            if (m_doc->hasSelection()) m_area &= m_doc->selectionBounds();
        }
    }
    const QString& f = m_filter;
    if (f == "gaussian") m_body->addWidget(slider(T("Radius"), "radius", 0.5, 250, 1, " px"));
    else if (f == "motion") {
        m_body->addWidget(slider(T("Angle"), "angle", -180, 180, 0, QString(QChar(0xB0))));
        m_body->addWidget(slider(T("Distance"), "distance", 1, 500, 0, " px"));
    } else if (f == "unsharp") {
        m_body->addWidget(slider(T("Radius"), "radius", 0.5, 50, 1, " px"));
        m_body->addWidget(slider(T("Amount"), "amount", 0, 500, 0, "%"));
        m_body->addWidget(slider(T("Threshold"), "threshold", 0, 255, 0));
    } else if (f == "sharpen") m_body->addWidget(slider(T("Amount"), "amount", 0, 200, 0, "%"));
    else if (f == "glow") {
        m_body->addWidget(slider(T("Radius"), "radius", 1, 200, 0, " px"));
        m_body->addWidget(slider(T("Strength"), "strength", 0, 300, 0, "%"));
        m_body->addWidget(slider(T("Threshold"), "threshold", 0, 255, 0));
    } else if (f == "noise") {
        m_body->addWidget(slider(T("Amount"), "amount", 0, 100, 0, "%"));
        m_body->addWidget(slider(T("Grain size"), "size", 1, 8, 1, " px"));
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(T("How to apply"), this));
        auto* mode = new QComboBox(this);
        mode->addItem(T("Add to the colors"), -1);
        for (BlendMode bm : {BlendMode::Overlay, BlendMode::SoftLight, BlendMode::Multiply, BlendMode::Screen, BlendMode::HardLight,
                             BlendMode::Luminosity, BlendMode::Normal})
            mode->addItem(T(blendModeName(bm)), int(bm));
        mode->setCurrentIndex(std::max(0, mode->findData(m_params.value("blend").toInt(-1))));
        connect(mode, &QComboBox::currentIndexChanged, this, [this, mode] {
            m_params["blend"] = mode->currentData().toInt();
            schedulePreview();
        });
        row->addWidget(mode, 1);
        m_body->addLayout(row);
        m_body->addWidget(checkbox(T("Monochrome"), "mono"));
        m_body->addWidget(checkbox(T("Also fill empty areas (for a noise layer)"), "fill"));
        auto* seed = new QPushButton(T("New random pattern"), this);
        connect(seed, &QPushButton::clicked, this, [this] {
            m_params["seed"] = m_params.value("seed").toInt() + 1;
            schedulePreview();
        });
        m_body->addWidget(seed);
    } else if (f == "pixelate") m_body->addWidget(slider(T("Cell size"), "cell", 2, 200, 0, " px"));
    auto* row = new QHBoxLayout;
    m_preview = new QCheckBox(T("Preview"), this);
    m_preview->setChecked(true);
    connect(m_preview, &QCheckBox::toggled, this, [this] { schedulePreview(); });
    row->addWidget(m_preview);
    row->addStretch();
    v->addLayout(row);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_timer.setSingleShot(true);
    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, &AdjustmentDialog::computePreview);
    setMinimumWidth(320);
    schedulePreview();
}

void AdjustmentDialog::schedulePreview() {
    if (m_mode == Mode::EditLayer) {
        if (!m_doc) return;
        if (auto* l = dynamic_cast<AdjustmentLayer*>(m_doc->layer(m_layer))) {
            l->adjustment = m_adj;
            m_doc->layerChanged(l);
        }
        return;
    }
    m_timer.start();
}

void AdjustmentDialog::computePreview() {
    if (!m_doc) return;
    auto* l = dynamic_cast<RasterLayer*>(m_doc->layer(m_layer));
    if (!l) return;
    if (m_preview && !m_preview->isChecked()) {
        l->preview.reset();
        m_doc->pixelsChanged(m_area, l);
        return;
    }
    if (m_area.isEmpty()) return;
    RasterSurface result = m_original;
    const bool sel = m_doc->hasSelection();
    if (m_mode == Mode::Filter) {
        int mg = filterMargin(m_filter, m_params);
        QRect big = m_area.adjusted(-mg, -mg, mg, mg) & m_doc->bounds().adjusted(-mg, -mg, mg, mg);
        std::vector<float> buf(size_t(big.width()) * big.height() * 4);
        readFloat(m_original, big, buf.data());
        applyFilter(m_filter, m_params, buf, big.width(), big.height(), big.left(), big.top());
        const QRect r = m_area;
        std::vector<float> out(size_t(r.width()) * r.height() * 4), orig(out.size()), m;
        readFloat(m_original, r, orig.data());
        if (sel) {
            m.resize(size_t(r.width()) * r.height());
            readMask(m_doc->selection(), r, m.data());
        }
        for (int y = 0; y < r.height(); ++y)
            for (int x = 0; x < r.width(); ++x) {
                size_t i = size_t(y) * r.width() + x;
                const float* f = &buf[(size_t(y + r.top() - big.top()) * big.width() + size_t(x + r.left() - big.left())) * 4];
                float w = sel ? m[i] : 1.f;
                for (int k = 0; k < 4; ++k) out[i * 4 + size_t(k)] = orig[i * 4 + size_t(k)] + (f[k] - orig[i * 4 + size_t(k)]) * w;
            }
        writeFloat(result, r, out.data(), true);
    } else {
        AdjustmentProcessor proc(m_adj);
        TileRange tr = tileRangeFor(m_area);
        QVector<qint64> keys;
        for (int ty = tr.y0; ty <= tr.y1; ++ty)
            for (int tx = tr.x0; tx <= tr.x1; ++tx) keys << tileKey(tx, ty);
        QMutex mx;
        QHash<qint64, std::vector<float>> outs;
        parallelForTiles(keys, [&](qint64 k) {
            QRect r = tileRectForKey(k) & m_area;
            const size_t n = size_t(r.width()) * r.height();
            std::vector<float> px(n * 4);
            readFloat(m_original, r, px.data());
            std::vector<float> orig = px;
            proc.apply(px.data(), int(n));
            if (sel) {
                std::vector<float> m(n);
                readMask(m_doc->selection(), r, m.data());
                for (size_t i = 0; i < n; ++i)
                    for (int c = 0; c < 4; ++c) px[i * 4 + size_t(c)] = orig[i * 4 + size_t(c)] + (px[i * 4 + size_t(c)] - orig[i * 4 + size_t(c)]) * m[i];
            }
            QMutexLocker lock(&mx);
            outs.insert(k, std::move(px));
        });
        for (qint64 k : keys) {
            QRect r = tileRectForKey(k) & m_area;
            writeFloat(result, r, outs[k].data(), true);
        }
    }
    l->preview = std::make_shared<RasterSurface>(std::move(result));
    m_doc->pixelsChanged(m_area, l);
}

void AdjustmentDialog::finish(bool ok) {
    if (m_done) return;
    m_done = true;
    m_timer.stop();
    if (!m_doc) return;
    if (m_mode == Mode::EditLayer) {
        if (!ok) m_doc->revertLastUndo();
        return;
    }
    auto* l = dynamic_cast<RasterLayer*>(m_doc->layer(m_layer));
    if (!l) return;
    if (ok) {
        if (m_preview && !m_preview->isChecked()) m_preview->setChecked(true);
        computePreview();
        if (!l->preview) return;
        auto prev = l->preview;
        l->preview.reset();
        m_doc->pushUndo(windowTitle());
        l = dynamic_cast<RasterLayer*>(m_doc->layer(m_layer));
        l->pixels = std::move(*prev);
        l->pixels.optimizeRect(m_area);
        m_doc->pixelsChanged(m_area, l);
        if (m_mode == Mode::Filter) app()->settings().setValue("filters/" + m_filter, m_params);
    } else {
        l->preview.reset();
        m_doc->pixelsChanged(m_area, l);
    }
}

void AdjustmentDialog::accept() {
    finish(true);
    QDialog::accept();
}

void AdjustmentDialog::reject() {
    finish(false);
    QDialog::reject();
}

}  // namespace gx
