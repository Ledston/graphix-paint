#include "ui/HistoryPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/ImageOps.h"
#include "core/LayerOps.h"
#include "ui/CanvasView.h"
#include "ui/Theme.h"

namespace gx {

HistoryPanel::HistoryPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    m_list = new QListWidget(this);
    v->addWidget(m_list);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || !m_doc || row < 0) return;
        m_doc->jumpToHistory(row);
    });
    connect(app(), &App::activeViewChanged, this, [this] { setDocument(app()->activeDocument()); });
}

void HistoryPanel::setDocument(Document* d) {
    if (m_doc == d) return;
    if (m_doc) disconnect(m_doc, nullptr, this, nullptr);
    m_doc = d;
    if (d) connect(d, &Document::historyChanged, this, &HistoryPanel::rebuild);
    rebuild();
}

void HistoryPanel::rebuild() {
    m_updating = true;
    m_list->clear();
    if (m_doc) {
        QStringList names = m_doc->historyNames();
        int cur = m_doc->historyIndex();
        for (int i = 0; i < names.size(); ++i) {
            auto* it = new QListWidgetItem(i == 0 ? T("Start") : T(names[i]), m_list);
            if (i > cur) it->setForeground(Theme::colors().dim);
        }
        m_list->setCurrentRow(cur);
        m_list->scrollToItem(m_list->currentItem());
    }
    m_updating = false;
}

// ============================================================================ scratchpad

ScratchpadPanel::ScratchpadPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(3);
    QString file = app()->dataDir() + "/scratchpad.png";
    QImage img(file);
    QSize size = img.isNull() ? QSize(1400, 900) : img.size();
    m_doc = std::make_shared<Document>(size);
    auto bg = std::make_shared<RasterLayer>();
    bg->id = m_doc->newLayerId();
    bg->name = "Paper";
    bg->pixels.fillRect(m_doc->bounds(), 0xffffffffu);
    m_doc->root()->insert(0, bg);
    auto l = std::make_shared<RasterLayer>();
    l->id = m_doc->newLayerId();
    l->name = "Paint";
    if (!img.isNull()) imageToSurface(img, l->pixels, QPoint(0, 0));
    m_doc->root()->insert(1, l);
    m_doc->setCurrentLayer(l->id);
    m_doc->setUndoLimit(30);
    auto* view = new CanvasView(m_doc, this, true);
    m_view = view;
    v->addWidget(view, 1);
    auto* row = new QHBoxLayout;
    auto* clear = new QPushButton(Theme::icon("clear"), T("Clear"), this);
    auto* undo = new QPushButton(Theme::icon("undo"), QString(), this);
    undo->setToolTip(T("Undo on the scratchpad"));
    auto* fit = new QPushButton(Theme::icon("zoom"), QString(), this);
    fit->setToolTip(T("Fit"));
    row->addWidget(clear);
    row->addWidget(undo);
    row->addWidget(fit);
    row->addStretch();
    v->addLayout(row);
    auto* hint = new QLabel(T("Mix and test colors here with any brush. Right-click picks a color."), this);
    hint->setWordWrap(true);
    hint->setObjectName("sectionTitle");
    v->addWidget(hint);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (Layer* layer = m_doc->currentLayer()) ops::clearLayer(*m_doc, layer->id);
    });
    connect(undo, &QPushButton::clicked, this, [this] { m_doc->undo(); });
    connect(fit, &QPushButton::clicked, view, &CanvasView::fitToWindow);
    QTimer::singleShot(0, view, [view] { view->fitToWindow(); });
}

ScratchpadPanel::~ScratchpadPanel() { save(); }

void ScratchpadPanel::save() {
    if (!m_doc) return;
    Layer* l = m_doc->currentLayer();
    if (!l || !l->displaySurface()) return;
    QImage img = surfaceToImage(*l->displaySurface(), m_doc->bounds());
    img.convertToFormat(QImage::Format_ARGB32).save(app()->dataDir() + "/scratchpad.png");
}

}  // namespace gx
