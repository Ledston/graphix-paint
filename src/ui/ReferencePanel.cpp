#include "ui/ReferencePanel.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "io/ImageIO.h"
#include "ui/Theme.h"

namespace gx {

ReferenceView::ReferenceView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(140);
    setCursor(Qt::CrossCursor);
    setToolTip(T("Click to pick a color. Drag with the right or middle button to pan, wheel to zoom."));
}

void ReferenceView::setImage(const QImage& img) {
    m_img = img;
    m_grayImg = img.isNull() ? QImage() : img.convertToFormat(QImage::Format_Grayscale8);
    fit();
}

void ReferenceView::fit() {
    if (m_img.isNull()) { update(); return; }
    m_zoom = std::min(double(width()) / m_img.width(), double(height()) / m_img.height());
    m_center = QPointF(m_img.width() / 2.0, m_img.height() / 2.0);
    update();
}

void ReferenceView::resizeEvent(QResizeEvent*) { fit(); }

QTransform ReferenceView::xf() const {
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.scale(m_flip ? -m_zoom : m_zoom, m_zoom);
    t.translate(-m_center.x(), -m_center.y());
    return t;
}

void ReferenceView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Theme::colors().base);
    if (m_img.isNull()) {
        p.setPen(Theme::colors().dim);
        p.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, T("Drop images here, or use Open / Paste."));
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 2);
    p.setTransform(xf());
    p.drawImage(0, 0, m_gray ? m_grayImg : m_img);
}

void ReferenceView::pick(const QPointF& w) {
    if (m_img.isNull()) return;
    QPointF c = xf().inverted().map(w);
    int x = int(c.x()), y = int(c.y());
    if (x < 0 || y < 0 || x >= m_img.width() || y >= m_img.height()) return;
    QColor col = m_gray ? QColor(m_grayImg.pixel(x, y)) : QColor(m_img.pixel(x, y));
    col.setAlpha(255);
    app()->setForeground(col);
}

void ReferenceView::mousePressEvent(QMouseEvent* e) {
    m_last = e->position();
    if (e->button() == Qt::LeftButton) {
        m_picking = true;
        pick(e->position());
    } else {
        m_pan = true;
    }
}

void ReferenceView::mouseMoveEvent(QMouseEvent* e) {
    if (m_picking) pick(e->position());
    if (m_pan) {
        QPointF d = e->position() - m_last;
        m_center -= QPointF(m_flip ? -d.x() : d.x(), d.y()) / m_zoom;
        m_last = e->position();
        update();
    }
}

void ReferenceView::mouseReleaseEvent(QMouseEvent*) {
    if (m_picking) app()->pushColorHistory(app()->foreground());
    m_picking = m_pan = false;
}

void ReferenceView::wheelEvent(QWheelEvent* e) {
    double f = std::pow(1.2, e->angleDelta().y() / 120.0);
    QPointF before = xf().inverted().map(e->position());
    m_zoom = std::clamp(m_zoom * f, 0.01, 64.0);
    QPointF after = xf().inverted().map(e->position());
    m_center += before - after;
    update();
}

ReferencePanel::ReferencePanel(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(3);
    m_view = new ReferenceView(this);
    v->addWidget(m_view, 1);
    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(48, 48));
    m_list->setFlow(QListView::LeftToRight);
    m_list->setWrapping(false);
    m_list->setFixedHeight(70);
    m_list->setMovement(QListView::Static);
    v->addWidget(m_list);
    auto* row = new QHBoxLayout;
    auto btn = [&](const QString& icon, const QString& tip, std::function<void()> fn, bool check = false) {
        auto* b = new QToolButton(this);
        b->setIcon(Theme::icon(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setCheckable(check);
        if (fn) connect(b, &QToolButton::clicked, this, fn);
        row->addWidget(b);
        return b;
    };
    btn("image", T("Open reference images"), [this] {
        QStringList files = QFileDialog::getOpenFileNames(this, T("Open reference"), {}, io::openFilter());
        for (const QString& f : files) {
            QImage img = io::readImage(f, nullptr);
            if (!img.isNull()) addImage(img, f);
        }
    });
    btn("plus", T("Paste from the clipboard"), [this] {
        QImage img = QApplication::clipboard()->image();
        if (!img.isNull()) addImage(img, {});
    });
    QToolButton* gray = btn("adjustment", T("Grayscale (check values)"), {}, true);
    connect(gray, &QToolButton::toggled, m_view, &ReferenceView::setGray);
    QToolButton* flip = btn("flip", T("Flip horizontally"), {}, true);
    connect(flip, &QToolButton::toggled, m_view, &ReferenceView::setFlip);
    btn("zoom", T("Fit"), [this] { m_view->fit(); });
    row->addStretch();
    btn("trash", T("Remove"), [this] {
        int i = m_list->currentRow();
        if (i < 0) return;
        delete m_list->takeItem(i);
        m_images.removeAt(i);
        m_paths.removeAt(i);
        show(std::min(i, int(m_images.size()) - 1));
        save();
    });
    v->addLayout(row);
    connect(m_list, &QListWidget::currentRowChanged, this, &ReferencePanel::show);

    for (const QString& p : app()->settings().value("references/files").toStringList()) {
        QImage img = io::readImage(p, nullptr);
        if (!img.isNull()) addImage(img, p);
    }
}

ReferencePanel::~ReferencePanel() { save(); }

void ReferencePanel::addImage(const QImage& img0, const QString& path) {
    QImage img = img0;
    QString p = path;
    if (p.isEmpty()) {
        // pasted images are kept in the data folder so they survive restarts
        QDir().mkpath(app()->dataDir() + "/references");
        p = app()->dataDir() + "/references/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
        img.save(p);
    }
    m_images << img;
    m_paths << p;
    auto* it = new QListWidgetItem(QIcon(QPixmap::fromImage(img.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation))), QString(), m_list);
    it->setToolTip(QFileInfo(p).fileName());
    m_list->setCurrentItem(it);
    save();
}

void ReferencePanel::show(int index) {
    if (index < 0 || index >= m_images.size()) {
        m_view->setImage(QImage());
        return;
    }
    m_view->setImage(m_images[index]);
}

void ReferencePanel::save() { app()->settings().setValue("references/files", m_paths); }

void ReferencePanel::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls() || e->mimeData()->hasImage()) e->acceptProposedAction();
}

void ReferencePanel::dropEvent(QDropEvent* e) {
    if (e->mimeData()->hasImage()) addImage(qvariant_cast<QImage>(e->mimeData()->imageData()), {});
    for (const QUrl& u : e->mimeData()->urls()) {
        QImage img = io::readImage(u.toLocalFile(), nullptr);
        if (!img.isNull()) addImage(img, u.toLocalFile());
    }
    e->acceptProposedAction();
}

}  // namespace gx
