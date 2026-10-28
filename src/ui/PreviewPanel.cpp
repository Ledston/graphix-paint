#include "ui/PreviewPanel.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

PreviewPanel::PreviewPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    auto* bar = new QHBoxLayout;
    m_flip = new QToolButton(this);
    m_flip->setIcon(Theme::icon("flip"));
    m_flip->setCheckable(true);
    m_flip->setChecked(true);
    m_flip->setAutoRaise(true);
    m_flip->setToolTip(T("Mirrored: mistakes in proportions jump out"));
    m_gray = new QToolButton(this);
    m_gray->setIcon(Theme::icon("gray"));
    m_gray->setCheckable(true);
    m_gray->setAutoRaise(true);
    m_gray->setToolTip(T("In grays: check light and dark values"));
    connect(m_flip, &QToolButton::toggled, this, [this] { update(); });
    connect(m_gray, &QToolButton::toggled, this, [this] { m_timer.start(0); });
    bar->addWidget(m_flip);
    bar->addWidget(m_gray);
    bar->addStretch();
    v->addLayout(bar);
    v->addStretch(1);
    setMinimumSize(160, 140);
    m_timer.setSingleShot(true);
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &PreviewPanel::refresh);
    connect(app(), &App::activeViewChanged, this, [this](CanvasView* view) { setView(view); });
    setView(app()->activeView());
}

void PreviewPanel::setView(CanvasView* v) {
    if (m_view == v) return;
    if (m_view && m_view->doc()) disconnect(m_view->doc(), nullptr, this, nullptr);
    m_view = v;
    m_img = QImage();
    if (v) {
        connect(v->doc(), &Document::imageChanged, this, [this] { if (!m_timer.isActive()) m_timer.start(); });
        connect(v->doc(), &Document::sizeChanged, this, [this] { m_timer.start(0); });
    }
    m_timer.start(0);
}

QRect PreviewPanel::area() const { return rect().adjusted(4, 30, -4, -4); }

void PreviewPanel::refresh() {
    if (!m_view || !isVisible()) {
        update();
        return;
    }
    QImage img;
    const QRect a = area();
    bool done = m_view->doc()->projection()->thumbnail(int(std::max(a.width(), a.height()) * devicePixelRatioF()), 12.0, img);
    if (m_gray->isChecked() && !img.isNull()) img = img.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_img = img;
    if (!done) m_timer.start(40);
    update();
}

void PreviewPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Theme::colors().base);
    if (!m_view) return;
    const QRect a = area();
    QSizeF cs = m_view->doc()->size();
    QSizeF s = cs.scaled(QSizeF(a.size()), Qt::KeepAspectRatio);
    QRectF r(a.left() + (a.width() - s.width()) / 2, a.top() + (a.height() - s.height()) / 2, s.width(), s.height());
    p.fillRect(r, checkerBrush(5));
    if (!m_img.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_flip->isChecked()) {
            p.save();
            p.translate(r.center());
            p.scale(-1, 1);
            p.translate(-r.center());
            p.drawImage(r, m_img);
            p.restore();
        } else {
            p.drawImage(r, m_img);
        }
    }
    p.setPen(Theme::colors().border);
    p.drawRect(r);
}

void PreviewPanel::resizeEvent(QResizeEvent*) { m_timer.start(); }
void PreviewPanel::showEvent(QShowEvent*) { m_timer.start(0); }

}  // namespace gx
