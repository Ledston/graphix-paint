#include "ui/MaterialsPanel.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "app/Materials.h"
#include "io/ImageIO.h"
#include "ui/Theme.h"

namespace gx {

namespace {
enum { KindRole = Qt::UserRole + 1, ValueRole };

// A list whose items can be dragged onto the canvas.
class MaterialList : public QListWidget {
public:
    explicit MaterialList(QWidget* parent) : QListWidget(parent) {
        setViewMode(QListView::IconMode);
        setIconSize(QSize(56, 56));
        setGridSize(QSize(80, 86));
        setResizeMode(QListView::Adjust);
        setMovement(QListView::Static);
        setWordWrap(true);
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setUniformItemSizes(true);
    }

protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(kMaterialMime)}; }
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        if (items.isEmpty()) return nullptr;
        auto* md = new QMimeData;
        QJsonObject o{{"kind", items.first()->data(KindRole).toString()}, {"value", items.first()->data(ValueRole).toString()}};
        md->setData(QString::fromLatin1(kMaterialMime), QJsonDocument(o).toJson(QJsonDocument::Compact));
        return md;
    }
};

QIcon gradientIcon(const Gradient& g) {
    QImage img(56, 56, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    QLinearGradient lg(0, 0, 56, 0);
    for (const GradientStop& s : g.stops) {
        QColor c = s.color;
        c.setAlphaF(s.opacity);
        lg.setColorAt(std::clamp(s.pos, 0.0, 1.0), c);
    }
    p.fillRect(img.rect(), lg);
    return QIcon(QPixmap::fromImage(img));
}

QIcon toneIcon(const TonePreset& t) {
    QImage img(56, 56, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    const double cell = std::max(4.0, t.cell * 0.8);
    p.translate(28, 28);
    p.rotate(t.angle);
    for (double y = -60; y < 60; y += cell)
        for (double x = -60; x < 60; x += cell) {
            QPointF c = QTransform().rotate(t.angle).map(QPointF(x, y));
            const double gray = t.gradient ? std::clamp(0.15 + 0.85 * (c.y() + 28) / 56.0, 0.0, 1.0) : t.gray;
            const double k = std::sqrt(std::max(0.0, 1.0 - gray));
            if (t.shape == 1) p.drawRect(QRectF(x - cell / 2, y - cell * k / 2, cell, cell * k));
            else if (t.shape == 2) p.drawRect(QRectF(x - cell * k / 2, y - cell * k / 2, cell * k, cell * k));
            else p.drawEllipse(QPointF(x, y), cell * k * 0.56, cell * k * 0.56);
        }
    return QIcon(QPixmap::fromImage(img));
}

QIcon imageIcon(const QImage& src) {
    QImage img(56, 56, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    // checkerboard under transparent pictures
    for (int y = 0; y < 56; y += 8)
        for (int x = 0; x < 56; x += 8) p.fillRect(x, y, 8, 8, ((x + y) / 8) % 2 ? QColor(205, 205, 205) : QColor(245, 245, 245));
    QImage s = src.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    p.drawImage((56 - s.width()) / 2, (56 - s.height()) / 2, s);
    return QIcon(QPixmap::fromImage(img));
}

QIcon patternIcon(const QImage& tile) {
    QImage img(56, 56, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    if (!tile.isNull()) p.fillRect(img.rect(), QBrush(tile.width() > 28 ? tile.scaled(tile.width() / 2, tile.height() / 2, Qt::KeepAspectRatio, Qt::SmoothTransformation) : tile));
    return QIcon(QPixmap::fromImage(img));
}

QToolButton* toolButton(QWidget* parent, const QString& icon, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setIcon(Theme::icon(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}
}  // namespace

MaterialsPanel::MaterialsPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(3);
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    v->addWidget(m_tabs, 1);
    auto page = [&](const QString& title, QListWidget** list, const QString& hint) {
        auto* w = new QWidget(m_tabs);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(0, 2, 0, 0);
        l->setSpacing(2);
        *list = new MaterialList(w);
        l->addWidget(*list, 1);
        auto* h = new QLabel(hint, w);
        h->setWordWrap(true);
        h->setObjectName("sectionTitle");
        l->addWidget(h);
        auto* row = new QHBoxLayout;
        row->setSpacing(1);
        l->addLayout(row);
        m_tabs->addTab(w, title);
        connect(*list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
            Q_EMIT place(it->data(KindRole).toString(), it->data(ValueRole).toString());
        });
        return row;
    };
    const QString dragHint = T("Drag onto the canvas or double-click.");
    // ---- pictures
    {
        QHBoxLayout* row = page(T("Pictures"), &m_images, dragHint + " " + T("A picture becomes a new layer."));
        auto* add = toolButton(this, "plus", T("Add pictures..."));
        connect(add, &QToolButton::clicked, this, [this] {
            const QStringList files = QFileDialog::getOpenFileNames(this, T("Add pictures"), {}, io::openFilter());
            for (const QString& f : files) {
                QImage img = io::readImage(f, nullptr);
                if (!img.isNull()) Materials::instance().addImage(img, QFileInfo(f).completeBaseName());
            }
        });
        auto* sel = toolButton(this, "duplicate", T("Save the selected part of the layer as a picture"));
        connect(sel, &QToolButton::clicked, this, [this] { Q_EMIT saveSelection(false); });
        row->addWidget(add);
        row->addWidget(sel);
        row->addStretch();
        auto* del = toolButton(this, "trash", T("Delete"));
        connect(del, &QToolButton::clicked, this, [this] {
            if (QListWidgetItem* it = m_images->currentItem())
                if (QMessageBox::question(this, T("Materials"), T("Delete \"%1\"?").arg(it->text())) == QMessageBox::Yes)
                    Materials::instance().removeFile(it->data(ValueRole).toString());
        });
        auto* open = toolButton(this, "folder-new", T("Open the folder"));
        connect(open, &QToolButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Materials::instance().imagesFolder())); });
        row->addWidget(open);
        row->addWidget(del);
    }
    // ---- patterns
    {
        QHBoxLayout* row = page(T("Patterns"), &m_patterns,
                                dragHint + " " + T("Fills the selection (or the whole picture) on a new layer. The chosen pattern is also used by the fill tools."));
        auto* add = toolButton(this, "plus", T("Add patterns..."));
        connect(add, &QToolButton::clicked, this, [this] {
            const QStringList files = QFileDialog::getOpenFileNames(this, T("Add patterns"), {}, io::openFilter());
            for (const QString& f : files) {
                QImage img = io::readImage(f, nullptr);
                if (!img.isNull()) img.save(Materials::instance().patternsFolder() + "/" + QFileInfo(f).completeBaseName() + ".png");
            }
            Q_EMIT Materials::instance().changed();
        });
        auto* sel = toolButton(this, "duplicate", T("Save the selected part of the layer as a pattern"));
        connect(sel, &QToolButton::clicked, this, [this] { Q_EMIT saveSelection(true); });
        row->addWidget(add);
        row->addWidget(sel);
        row->addStretch();
        auto* open = toolButton(this, "folder-new", T("Open the folder"));
        connect(open, &QToolButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Materials::instance().patternsFolder())); });
        auto* del = toolButton(this, "trash", T("Delete"));
        connect(del, &QToolButton::clicked, this, [this] {
            QListWidgetItem* it = m_patterns->currentItem();
            if (!it) return;
            if (QMessageBox::question(this, T("Materials"), T("Delete \"%1\"?").arg(it->text())) != QMessageBox::Yes) return;
            QDir d(Materials::instance().patternsFolder());
            for (const QString& f : d.entryList(QDir::Files))
                if (QFileInfo(f).completeBaseName() == it->data(ValueRole).toString()) Materials::instance().removeFile(d.filePath(f));
        });
        row->addWidget(open);
        row->addWidget(del);
        connect(m_patterns, &QListWidget::itemClicked, this, [](QListWidgetItem* it) {
            Materials::instance().setCurrentPattern(it->data(ValueRole).toString());
        });
    }
    // ---- screentones
    page(T("Tones"), &m_tones, dragHint + " " + T("Fills the selection (or the whole picture) with a screentone layer; change it later in Layer > Effects."));
    // ---- gradient maps
    {
        QHBoxLayout* row = page(T("Gradient maps"), &m_gradients,
                                dragHint + " " + T("Adds a gradient map layer that recolors everything under it by brightness."));
        auto* apply = toolButton(this, "adjustment", T("Recolor the pixels of the current layer..."));
        connect(apply, &QToolButton::clicked, this, [this] {
            if (QListWidgetItem* it = m_gradients->currentItem()) Q_EMIT applyGradient(it->data(ValueRole).toInt());
        });
        auto* save = toolButton(this, "plus", T("Save the gradient of the selected gradient map layer"));
        connect(save, &QToolButton::clicked, this, [this] { Q_EMIT place("save-gradient", {}); });
        row->addWidget(apply);
        row->addWidget(save);
        row->addStretch();
        auto* del = toolButton(this, "trash", T("Delete (only your own gradient maps)"));
        connect(del, &QToolButton::clicked, this, [this] {
            QListWidgetItem* it = m_gradients->currentItem();
            if (!it) return;
            const QList<Gradient> g = Materials::instance().gradients();
            const int i = it->data(ValueRole).toInt();
            if (i >= 0 && i < g.size() && Materials::instance().isUserGradient(g[i].name)) Materials::instance().removeGradient(g[i].name);
        });
        row->addWidget(del);
    }
    connect(&Materials::instance(), &Materials::changed, this, &MaterialsPanel::refill);
    // tabs switched off in Settings > Interface
    auto applyHidden = [this] {
        const QStringList keys = {"Pictures", "Patterns", "Tones", "Gradient maps"};
        for (int i = 0; i < keys.size() && i < m_tabs->count(); ++i) m_tabs->setTabVisible(i, !app()->prefs().hiddenMaterialTabs.contains(keys[i]));
    };
    connect(app(), &App::prefsChanged, this, applyHidden);
    applyHidden();
    connect(&Materials::instance(), &Materials::currentPatternChanged, this, [this] {
        const QSignalBlocker b(m_patterns);
        for (int i = 0; i < m_patterns->count(); ++i)
            if (m_patterns->item(i)->data(ValueRole).toString() == Materials::instance().currentPattern()) m_patterns->setCurrentRow(i);
    });
    refill();
}

void MaterialsPanel::refill() {
    Materials& m = Materials::instance();
    m_images->clear();
    for (const QString& f : m.images()) {
        QImage img(f);
        auto* it = new QListWidgetItem(imageIcon(img), QFileInfo(f).completeBaseName(), m_images);
        it->setData(KindRole, "image");
        it->setData(ValueRole, f);
        it->setToolTip(QFileInfo(f).fileName());
    }
    m_patterns->clear();
    for (const QString& n : m.patterns()) {
        auto* it = new QListWidgetItem(patternIcon(m.pattern(n)), T(n), m_patterns);
        it->setData(KindRole, "pattern");
        it->setData(ValueRole, n);
        if (n == m.currentPattern()) m_patterns->setCurrentItem(it);
    }
    m_tones->clear();
    const QList<TonePreset> tones = Materials::tones();
    for (int i = 0; i < tones.size(); ++i) {
        auto* it = new QListWidgetItem(toneIcon(tones[i]), T(tones[i].name), m_tones);
        it->setData(KindRole, "tone");
        it->setData(ValueRole, QString::number(i));
    }
    m_gradients->clear();
    const QList<Gradient> grads = m.gradients();
    for (int i = 0; i < grads.size(); ++i) {
        auto* it = new QListWidgetItem(gradientIcon(grads[i]), T(grads[i].name), m_gradients);
        it->setData(KindRole, "gradient");
        it->setData(ValueRole, QString::number(i));
    }
}

}  // namespace gx
