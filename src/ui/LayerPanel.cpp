#include "ui/LayerPanel.h"

#include <QComboBox>
#include <QCursor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "core/Document.h"
#include "core/LayerOps.h"
#include "core/Pixel.h"
#include "tools/BrushTool.h"
#include "ui/LayerPropertiesDialog.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace gx {

namespace {
const char* kMime = "application/x-graphix-layer";
constexpr int kRowH = 44;
constexpr int kThumb = 36;
}  // namespace

// ============================================================================ model

LayerModel::LayerModel(QObject* parent) : QAbstractItemModel(parent) {}

void LayerModel::setDocument(Document* d) {
    beginResetModel();
    m_doc = d;
    endResetModel();
}

void LayerModel::reset() {
    beginResetModel();
    endResetModel();
}

Layer* LayerModel::layerAt(const QModelIndex& i) const {
    if (!m_doc || !i.isValid()) return nullptr;
    return m_doc->layer(quint32(i.internalId()));
}

static const GroupLayer* groupOf(const LayerModel* m, const QModelIndex& parent) {
    if (!m->document()) return nullptr;
    if (!parent.isValid()) return m->document()->root();
    Layer* l = m->layerAt(parent);
    return (l && l->isGroup()) ? static_cast<GroupLayer*>(l) : nullptr;
}

QModelIndex LayerModel::index(int row, int column, const QModelIndex& parent) const {
    const GroupLayer* g = groupOf(this, parent);
    if (!g || row < 0 || column != 0 || row >= int(g->children.size())) return {};
    const Layer* l = g->children[g->children.size() - 1 - size_t(row)].get();
    return createIndex(row, 0, quintptr(l->id));
}

QModelIndex LayerModel::parent(const QModelIndex& child) const {
    Layer* l = layerAt(child);
    if (!l || !l->parent || l->parent == m_doc->root()) return {};
    GroupLayer* p = l->parent;
    GroupLayer* gp = p->parent;
    if (!gp) return {};
    int ci = gp->indexOf(p);
    return createIndex(int(gp->children.size()) - 1 - ci, 0, quintptr(p->id));
}

int LayerModel::rowCount(const QModelIndex& parent) const {
    const GroupLayer* g = groupOf(this, parent);
    return g ? int(g->children.size()) : 0;
}

QModelIndex LayerModel::indexOf(quint32 id) const {
    if (!m_doc) return {};
    Layer* l = m_doc->layer(id);
    if (!l || !l->parent) return {};
    GroupLayer* p = l->parent;
    int ci = p->indexOf(l);
    return createIndex(int(p->children.size()) - 1 - ci, 0, quintptr(l->id));
}

QVariant LayerModel::data(const QModelIndex& index, int role) const {
    Layer* l = layerAt(index);
    if (!l) return {};
    if (role == Qt::DisplayRole || role == Qt::EditRole) return l->name;
    if (role == Qt::ToolTipRole)
        return QStringLiteral("%1\n%2 · %3%").arg(l->name, T(blendModeName(l->blend))).arg(int(std::round(l->opacity * 100)));
    return {};
}

bool LayerModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    Layer* l = layerAt(index);
    if (!l || role != Qt::EditRole) return false;
    QString n = value.toString().trimmed();
    if (n.isEmpty() || n == l->name) return false;
    m_doc->pushUndo(T("Rename layer"));
    l = layerAt(index);
    if (!l) return false;
    l->name = n;
    m_doc->layerChanged(l);
    Q_EMIT dataChanged(index, index);
    return true;
}

Qt::ItemFlags LayerModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
    Layer* l = layerAt(index);
    if (!index.isValid() || (l && l->isGroup())) f |= Qt::ItemIsDropEnabled;
    return f;
}

QStringList LayerModel::mimeTypes() const { return {kMime}; }

QMimeData* LayerModel::mimeData(const QModelIndexList& indexes) const {
    auto* md = new QMimeData;
    if (!indexes.isEmpty() && m_doc) {
        const quint32 id = quint32(indexes.first().internalId());
        QList<quint32> ids = m_doc->isLayerSelected(id) ? m_doc->selectedLayerIds() : QList<quint32>{id};
        QStringList parts;
        for (quint32 x : ids) parts << QString::number(x);
        md->setData(kMime, parts.join(',').toLatin1());
    }
    return md;
}

bool LayerModel::canDropMimeData(const QMimeData* data, Qt::DropAction, int, int, const QModelIndex&) const {
    return data && data->hasFormat(kMime);
}

bool LayerModel::dropMimeData(const QMimeData* data, Qt::DropAction, int row, int, const QModelIndex& parent) {
    if (!m_doc || !data->hasFormat(kMime)) return false;
    QList<quint32> ids;
    for (const QByteArray& part : data->data(kMime).split(','))
        if (quint32 x = part.toUInt()) ids << x;
    if (ids.isEmpty()) return false;
    const quint32 id = ids.first();
    Layer* moving = m_doc->layer(id);
    if (!moving) return false;
    GroupLayer* g = nullptr;
    int childIndex = 0;
    if (!parent.isValid()) g = m_doc->root();
    else {
        Layer* pl = layerAt(parent);
        if (!pl) return false;
        if (pl->isGroup()) g = static_cast<GroupLayer*>(pl);
        else {
            g = pl->parent;
            row = -1;
            childIndex = g->indexOf(pl) + 1;
        }
    }
    if (!g) return false;
    for (quint32 x : ids)
        if (Layer* ml = m_doc->layer(x); !ml || g == ml || isAncestor(ml, g)) return false;
    if (row >= 0) childIndex = int(g->children.size()) - row;
    else if (parent.isValid() && layerAt(parent) && layerAt(parent)->isGroup()) childIndex = int(g->children.size());
    quint32 gid = g->id;
    bool isRoot = g == m_doc->root();
    QMetaObject::invokeMethod(this, [doc = m_doc, ids, gid, isRoot, childIndex] {
        if (!doc) return;
        GroupLayer* target = isRoot ? doc->root() : static_cast<GroupLayer*>(doc->layer(gid));
        if (target) ops::moveLayers(*doc, ids, target, childIndex);
    }, Qt::QueuedConnection);
    return false;
}

// ============================================================================ delegate

QRect LayerDelegate::eyeRect(const QRect& r) { return QRect(r.left() + 2, r.top() + (r.height() - 20) / 2, 20, 20); }
QRect LayerDelegate::thumbRect(const QRect& r) { return QRect(r.left() + 30, r.top() + (r.height() - kThumb) / 2, kThumb, kThumb); }
QRect LayerDelegate::maskRect(const QRect& r) { return thumbRect(r).translated(kThumb + 4, 0); }

QSize LayerDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const {
    return QSize(opt.rect.width(), kRowH);
}

QImage LayerDelegate::thumbnail(Layer* l, Document* d, bool mask) const {
    quint64 key = (quint64(l->id) << 1) | (mask ? 1 : 0);
    Thumb& t = m_thumbs[key];
    if (t.version == l->version && !t.img.isNull() && t.img.devicePixelRatio() > 0) return t.img;
    QSize cs = d->size();
    double s = std::min(double(kThumb) / cs.width(), double(kThumb) / cs.height());
    int tw = std::max(1, int(cs.width() * s)), th = std::max(1, int(cs.height() * s));
    QImage img(tw * 2, th * 2, QImage::Format_ARGB32_Premultiplied);
    const RasterSurface* surf = l->displaySurface();
    const double step = double(cs.width()) / (tw * 2);
    QPoint off = l->previewOffset;
    for (int y = 0; y < img.height(); ++y) {
        quint32* row = reinterpret_cast<quint32*>(img.scanLine(y));
        int cy = int((y + 0.5) * double(cs.height()) / img.height()) - off.y();
        for (int x = 0; x < img.width(); ++x) {
            int cx = int((x + 0.5) * step) - off.x();
            if (mask) {
                quint8 v = l->mask.pixel(cx, cy);
                row[x] = 0xff000000u | (quint32(v) * 0x010101u);
            } else {
                row[x] = surf ? surf->pixel(cx, cy) : 0u;
            }
        }
    }
    img.setDevicePixelRatio(2.0);
    t.version = l->version;
    t.img = img;
    return img;
}

void LayerDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    auto* model = static_cast<const LayerModel*>(idx.model());
    Layer* l = model->layerAt(idx);
    Document* d = model->document();
    if (!l || !d) return;
    const ThemeColors& c = Theme::colors();
    const QRect r = opt.rect;
    const bool isCurrent = d->currentLayerId() == l->id || (opt.state & QStyle::State_Selected);
    const bool sel = isCurrent || d->isLayerSelected(l->id);
    p->save();
    QColor selBg = c.accent;
    if (sel && !isCurrent) selBg.setAlphaF(0.55);  // also selected, but not the active layer
    p->fillRect(r, idx.row() % 2 ? c.alt : c.base);
    if (sel) p->fillRect(r, selBg);
    if (l->labelColor.isValid()) p->fillRect(QRect(r.left(), r.top(), 3, r.height()), l->labelColor);
    QColor fg = sel ? c.accentText : c.text;
    QColor dim = sel ? c.accentText : c.dim;
    p->drawPixmap(eyeRect(r), Theme::iconPixmap(l->visible ? "eye" : "eye-off", 20, l->visible ? fg : dim));
    QRect tr = thumbRect(r);
    if (l->clipping) {
        p->fillRect(QRect(tr.left() - 5, r.top() + 3, 3, r.height() - 6), sel ? c.accentText : c.accent);
    }
    if (l->isGroup()) {
        p->drawPixmap(tr.adjusted(4, 4, -4, -4), Theme::iconPixmap("folder", kThumb - 8, fg));
    } else if (l->type() == LayerType::Adjustment) {
        p->drawPixmap(tr.adjusted(4, 4, -4, -4), Theme::iconPixmap("adjustment", kThumb - 8, fg));
    } else {
        QImage th = thumbnail(l, d, false);
        QSizeF ts = QSizeF(th.size()) / th.devicePixelRatio();
        QRectF ir(tr.left() + (kThumb - ts.width()) / 2, tr.top() + (kThumb - ts.height()) / 2, ts.width(), ts.height());
        p->fillRect(ir, checkerBrush(3));
        p->drawImage(ir, th);
        p->setPen(c.border);
        p->drawRect(ir.adjusted(-0.5, -0.5, 0.5, 0.5));
        if (l->type() == LayerType::Vector || l->type() == LayerType::Text) {
            p->drawPixmap(QRect(tr.right() - 12, tr.bottom() - 12, 14, 14),
                          Theme::iconPixmap(l->type() == LayerType::Vector ? "vector-pen" : "text", 14, fg));
        }
    }
    int textLeft = tr.right() + 6;
    if (l->hasMask) {
        QRect mr = maskRect(r);
        QImage mi = thumbnail(l, d, true);
        QSizeF ms = QSizeF(mi.size()) / mi.devicePixelRatio();
        QRectF ir(mr.left() + (kThumb - ms.width()) / 2, mr.top() + (kThumb - ms.height()) / 2, ms.width(), ms.height());
        p->drawImage(ir, mi);
        bool editing = BrushTool::editMask && d->currentLayerId() == l->id;
        p->setPen(QPen(editing ? (sel ? c.accentText : c.accent) : c.border, editing ? 2 : 1));
        p->drawRect(ir.adjusted(-0.5, -0.5, 0.5, 0.5));
        if (!l->maskEnabled) {
            p->setPen(QPen(c.danger, 2));
            p->drawLine(ir.topLeft(), ir.bottomRight());
        }
        textLeft = mr.right() + 6;
    }
    QRect textR(textLeft, r.top() + 4, r.right() - textLeft - 4, r.height() - 8);
    QFont f = opt.font;
    p->setFont(f);
    p->setPen(fg);
    QString name = p->fontMetrics().elidedText(l->name, Qt::ElideRight, textR.width() - 40);
    p->drawText(QRect(textR.left(), textR.top(), textR.width(), textR.height() / 2), Qt::AlignLeft | Qt::AlignVCenter, name);
    QString sub;
    if (l->isGroup() && static_cast<GroupLayer*>(l)->passThrough) sub = T("Pass through");
    else sub = T(blendModeName(l->blend));
    sub += QStringLiteral("  %1%").arg(int(std::round(l->opacity * 100)));
    QFont sf = f;
    sf.setPointSizeF(std::max(6.0, f.pointSizeF() - 1));
    p->setFont(sf);
    p->setPen(dim);
    p->drawText(QRect(textR.left(), textR.center().y(), textR.width(), textR.height() / 2), Qt::AlignLeft | Qt::AlignVCenter, sub);
    int ix = r.right() - 18;
    auto badge = [&](const QString& icon) {
        p->drawPixmap(QRect(ix, r.top() + 4, 14, 14), Theme::iconPixmap(icon, 14, fg));
        ix -= 16;
    };
    if (l->locked) badge("lock");
    if (l->reference) badge("reference");
    if (l->hideInTimelapse) badge("timelapse-off");
    if (l->alphaLock) badge("alpha-lock");
    if (l->fx.anyActive()) badge("fx");
    p->restore();
}

bool LayerDelegate::editorEvent(QEvent* e, QAbstractItemModel* model, const QStyleOptionViewItem& opt, const QModelIndex& idx) {
    auto* m = static_cast<LayerModel*>(model);
    Layer* l = m->layerAt(idx);
    Document* d = m->document();
    if (!l || !d) return false;
    if (e->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() != Qt::LeftButton) return false;
        QPoint pos = me->position().toPoint();
        if (eyeRect(opt.rect).adjusted(-2, -8, 4, 8).contains(pos)) {
            const bool show = !l->visible;
            if (d->isLayerSelected(l->id) && d->selectedCount() > 1) ops::setLayersVisible(*d, d->selectedLayerIds(), show);
            else ops::setLayersVisible(*d, {l->id}, show);
            return true;
        }
        if (thumbRect(opt.rect).contains(pos) && (me->modifiers() & Qt::ControlModifier)) {
            ops::selectLayerAlpha(*d, l->id, (me->modifiers() & Qt::ShiftModifier) ? ops::SelectMode::Add : ops::SelectMode::Replace);
            return true;
        }
        if (l->hasMask && maskRect(opt.rect).contains(pos)) {
            BrushTool::editMask = true;
            d->setCurrentLayer(l->id);
            if (me->modifiers() & Qt::ShiftModifier) {
                d->pushUndo(T("Toggle mask"));
                if (Layer* ll = m->layerAt(idx)) {
                    ll->maskEnabled = !ll->maskEnabled;
                    d->layerChanged(ll);
                }
            }
            d->layersChanged();
            return false;
        }
        if (thumbRect(opt.rect).contains(pos)) {
            BrushTool::editMask = false;
            d->layersChanged();
        }
    }
    return QStyledItemDelegate::editorEvent(e, model, opt, idx);
}

QWidget* LayerDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const {
    return new QLineEdit(parent);
}

void LayerDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    auto* m = static_cast<const LayerModel*>(idx.model());
    Layer* l = m->layerAt(idx);
    int left = (l && l->hasMask ? maskRect(opt.rect) : thumbRect(opt.rect)).right() + 4;
    editor->setGeometry(QRect(left, opt.rect.top() + 3, opt.rect.right() - left - 4, opt.rect.height() / 2));
}

// ============================================================================ panel

LayerPanel::LayerPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(3);

    auto* top = new QHBoxLayout;
    m_blend = new QComboBox(this);
    for (BlendMode bm : blendModeUiOrder()) m_blend->addItem(T(blendModeName(bm)), int(bm));
    m_blend->setToolTip(T("Blending mode"));
    top->addWidget(m_blend, 1);
    m_pass = new QToolButton(this);
    m_pass->setText(T("Pass"));
    m_pass->setCheckable(true);
    m_pass->setToolTip(T("Pass through: the folder's blending modes affect the layers below it"));
    top->addWidget(m_pass);
    v->addLayout(top);

    m_opacity = new ValueBar(T("Opacity"), 0, 1, 2, this);
    m_opacity->setDisplayScale(100);
    m_opacity->setSuffix("%");
    v->addWidget(m_opacity);

    auto* toggles = new QHBoxLayout;
    toggles->setSpacing(2);
    auto toggle = [&](const QString& icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(Theme::icon(icon));
        b->setCheckable(true);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        toggles->addWidget(b);
        return b;
    };
    m_alphaLock = toggle("alpha-lock", T("Preserve opacity: paint only where the layer already has paint"));
    m_clip = toggle("clip", T("Clipping: use the layer (or folder) below as a mask"));
    m_lock = toggle("lock", T("Lock layer"));
    m_maskEdit = toggle("mask", T("Paint on the layer mask (white shows, black hides)"));
    toggles->addStretch();
    v->addLayout(toggles);

    m_model = new LayerModel(this);
    m_delegate = new LayerDelegate(this);
    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
    m_tree->setItemDelegate(m_delegate);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);  // F2 renames, double-click opens the properties
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setMinimumHeight(70);
    v->addWidget(m_tree, 1);

    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(1);
    auto btn = [&](const QString& icon, const QString& tip, std::function<void()> fn) {
        auto* b = new QToolButton(this);
        b->setIcon(Theme::icon(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [this, fn] { if (m_doc) fn(); });
        bottom->addWidget(b);
        return b;
    };
    btn("layer-new", T("New layer"), [this] { ops::addRasterLayer(*m_doc); });
    btn("linework-new", T("New linework layer"), [this] { ops::addVectorLayer(*m_doc); });
    btn("folder-new", T("New folder"), [this] { ops::addGroup(*m_doc); });
    auto* adj = btn("adjustment", T("New adjustment layer"), [] {});
    auto* adjMenu = new QMenu(adj);
    for (int t = 0; t < int(AdjustmentType::Count); ++t)
        adjMenu->addAction(T(adjustmentTypeName(AdjustmentType(t))), this, [this, t] { Q_EMIT requestNewAdjustment(t); });
    adj->setMenu(adjMenu);
    adj->setPopupMode(QToolButton::InstantPopup);
    btn("mask", T("Add layer mask (from the selection if there is one)"), [this] {
        if (Layer* l = current()) ops::addMask(*m_doc, l->id, m_doc->hasSelection());
    });
    btn("duplicate", T("Duplicate layer"), [this] { ops::duplicateLayers(*m_doc, targets()); });
    btn("merge", T("Merge down (or merge the selected layers)"), [this] {
        QString err;
        if (m_doc->selectedCount() > 1) {
            if (!ops::mergeLayers(*m_doc, targets(), &err) && !err.isEmpty()) app()->showStatus(T(err));
        } else if (Layer* l = current()) {
            if (!ops::mergeDown(*m_doc, l->id, &err) && !err.isEmpty()) app()->showStatus(T(err));
        }
    });
    btn("fx", T("Layer effects"), [this] {
        if (Layer* l = current()) Q_EMIT requestEffects(l->id, QString());
    });
    btn("clear", T("Clear layer"), [this] {
        if (Layer* l = current()) ops::clearLayer(*m_doc, l->id);
    });
    bottom->addStretch();
    btn("trash", T("Delete layer"), [this] { ops::deleteLayers(*m_doc, targets()); });
    v->addLayout(bottom);

    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& i) {
        if (m_updating || !m_doc) return;
        if (Layer* l = m_model->layerAt(i)) {
            if (!l->hasMask) BrushTool::editMask = false;
            if (m_doc->selectedCount() > 1 && m_doc->isLayerSelected(l->id)) m_doc->setLayerSelection(m_doc->selectedLayerIds(), l->id);
            else m_doc->setCurrentLayer(l->id);
        }
    });
    connect(m_tree, &QTreeView::expanded, this, [this](const QModelIndex& i) {
        if (Layer* l = m_model->layerAt(i); l && l->isGroup()) static_cast<GroupLayer*>(l)->expanded = true;
    });
    connect(m_tree, &QTreeView::collapsed, this, [this](const QModelIndex& i) {
        if (Layer* l = m_model->layerAt(i); l && l->isGroup()) static_cast<GroupLayer*>(l)->expanded = false;
    });
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& i) {
        // a quick double click on the eye only toggles visibility twice, no window
        const QPoint at = m_tree->viewport()->mapFromGlobal(QCursor::pos());
        const QRect r = m_tree->visualRect(i);
        if (LayerDelegate::eyeRect(r).adjusted(-2, -8, 4, 8).contains(at)) return;
        if (Layer* l = m_model->layerAt(i)) openProperties(l->id);
    });
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &LayerPanel::contextMenu);
    m_tree->viewport()->installEventFilter(this);

    connect(m_blend, &QComboBox::activated, this, [this](int idx) {
        Layer* l = current();
        if (!l || m_updating) return;
        m_doc->pushUndo(T("Blending mode"));
        for (quint32 id : targets())
            if (Layer* x = m_doc->layer(id)) {
                x->blend = BlendMode(m_blend->itemData(idx).toInt());
                m_doc->layerChanged(x);
            }
    });
    connect(m_pass, &QToolButton::toggled, this, [this](bool on) {
        Layer* l = current();
        if (!l || m_updating || !l->isGroup()) return;
        m_doc->pushUndo(T("Pass through"));
        l = current();
        static_cast<GroupLayer*>(l)->passThrough = on;
        m_doc->layerChanged(l);
    });
    connect(m_opacity, &ValueBar::valueChanged, this, [this](double v) {
        Layer* l = current();
        if (!l || m_updating) return;
        m_doc->pushUndo(T("Layer opacity"), QStringLiteral("opacity-%1").arg(l->id));
        for (quint32 id : targets())
            if (Layer* x = m_doc->layer(id)) {
                x->opacity = float(v);
                m_doc->layerChanged(x);
            }
    });
    auto flagToggle = [this](QToolButton* b, const QString& name, std::function<void(Layer*, bool)> set) {
        connect(b, &QToolButton::toggled, this, [this, name, set](bool on) {
            Layer* l = current();
            if (!l || m_updating) return;
            m_doc->pushUndo(name);
            for (quint32 id : targets())
                if (Layer* x = m_doc->layer(id)) {
                    set(x, on);
                    m_doc->layerChanged(x);
                }
            m_doc->layersChanged();
        });
    };
    flagToggle(m_alphaLock, T("Preserve opacity"), [](Layer* l, bool on) { l->alphaLock = on; });
    flagToggle(m_clip, T("Clipping"), [](Layer* l, bool on) { l->clipping = on; });
    flagToggle(m_lock, T("Lock layer"), [](Layer* l, bool on) { l->locked = on; });
    connect(m_maskEdit, &QToolButton::toggled, this, [this](bool on) {
        if (m_updating) return;
        Layer* l = current();
        if (on && l && !l->hasMask) ops::addMask(*m_doc, l->id, m_doc->hasSelection());
        BrushTool::editMask = on;
        if (m_doc) m_doc->layersChanged();
    });

    connect(app(), &App::activeViewChanged, this, [this](CanvasView*) { setDocument(app()->activeDocument()); });
}

Layer* LayerPanel::current() const { return m_doc ? m_doc->currentLayer() : nullptr; }

QList<quint32> LayerPanel::targets() const {
    if (!m_doc) return {};
    QList<quint32> ids = m_doc->selectedLayerIds();
    if (ids.isEmpty())
        if (Layer* l = current()) ids << l->id;
    return ids;
}

// Ctrl + click adds or removes a layer, Shift + click selects a run of layers.
bool LayerPanel::eventFilter(QObject* o, QEvent* e) {
    if (o == m_tree->viewport() && e->type() == QEvent::MouseButtonPress && m_doc) {
        auto* me = static_cast<QMouseEvent*>(e);
        const auto mods = me->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
        if (me->button() == Qt::LeftButton && mods) {
            QModelIndex i = m_tree->indexAt(me->position().toPoint());
            Layer* l = m_model->layerAt(i);
            if (!l) return false;
            QRect r = m_tree->visualRect(i);
            QPoint p = me->position().toPoint();
            if (LayerDelegate::eyeRect(r).adjusted(-2, -8, 4, 8).contains(p)) return false;
            // Ctrl + click on the thumbnail selects the layer's pixels (handled by the list)
            if ((mods & Qt::ControlModifier) && LayerDelegate::thumbRect(r).contains(p)) return false;
            if (mods & Qt::ShiftModifier) m_doc->selectLayerRange(m_doc->currentLayerId(), l->id);
            else m_doc->toggleLayerSelected(l->id);
            m_tree->viewport()->update();
            return true;
        }
        // a plain click on one of several selected layers: keep them selected in case they
        // get dragged; a click without dragging selects just that layer (on release)
        m_pendingSingle = 0;
        if (me->button() == Qt::LeftButton && !mods && m_doc->selectedCount() > 1) {
            QModelIndex i = m_tree->indexAt(me->position().toPoint());
            if (Layer* l = m_model->layerAt(i); l && m_doc->isLayerSelected(l->id) &&
                                                !LayerDelegate::eyeRect(m_tree->visualRect(i)).adjusted(-2, -8, 4, 8).contains(me->position().toPoint()))
                m_pendingSingle = l->id;
        }
    }
    if (o == m_tree->viewport() && e->type() == QEvent::MouseButtonRelease && m_doc && m_pendingSingle) {
        const quint32 id = m_pendingSingle;
        m_pendingSingle = 0;
        if (m_doc->layer(id)) m_doc->setCurrentLayer(id);
    }
    return QWidget::eventFilter(o, e);
}

void LayerPanel::setDocument(Document* d) {
    if (m_doc == d) return;
    if (m_doc) disconnect(m_doc, nullptr, this, nullptr);
    m_doc = d;
    m_model->setDocument(d);
    if (d) {
        connect(d, &Document::layersChanged, this, &LayerPanel::rebuild);
        connect(d, &Document::currentLayerChanged, this, [this] { selectCurrent(); syncControls(); });
        connect(d, &Document::layerPropertiesChanged, this, [this](quint32 id) {
            QModelIndex i = m_model->indexOf(id);
            if (i.isValid()) m_tree->update(i);
            syncControls();
        });
        connect(d, &Document::imageChanged, this, [this] { m_tree->viewport()->update(); });
    }
    rebuild();
}

void LayerPanel::rebuild() {
    m_updating = true;
    m_model->reset();
    if (m_doc) {
        // restore folder expansion
        forEachLayer(m_doc->root(), [this](Layer* l) {
            if (l->isGroup()) m_tree->setExpanded(m_model->indexOf(l->id), static_cast<GroupLayer*>(l)->expanded);
            return true;
        });
    }
    m_updating = false;
    selectCurrent();
    syncControls();
}

void LayerPanel::selectCurrent() {
    if (!m_doc) return;
    m_updating = true;
    QModelIndex i = m_model->indexOf(m_doc->currentLayerId());
    if (!i.isValid() && m_doc->currentLayer()) i = m_model->indexOf(m_doc->currentLayer()->id);
    if (i.isValid()) {
        m_tree->setCurrentIndex(i);
        m_tree->scrollTo(i);
    }
    m_updating = false;
}

void LayerPanel::syncControls() {
    m_updating = true;
    Layer* l = current();
    bool has = l != nullptr;
    for (QWidget* w : {static_cast<QWidget*>(m_blend), static_cast<QWidget*>(m_opacity), static_cast<QWidget*>(m_alphaLock),
                       static_cast<QWidget*>(m_clip), static_cast<QWidget*>(m_lock), static_cast<QWidget*>(m_maskEdit)})
        w->setEnabled(has);
    if (l) {
        m_blend->setCurrentIndex(std::max(0, m_blend->findData(int(l->blend))));
        m_opacity->setValue(l->opacity);
        m_alphaLock->setChecked(l->alphaLock);
        m_clip->setChecked(l->clipping);
        m_lock->setChecked(l->locked);
        m_maskEdit->setChecked(BrushTool::editMask && l->hasMask);
        m_pass->setVisible(l->isGroup());
        m_pass->setChecked(l->isGroup() && static_cast<GroupLayer*>(l)->passThrough);
        m_blend->setEnabled(!(l->isGroup() && static_cast<GroupLayer*>(l)->passThrough));
    }
    m_updating = false;
}

void LayerPanel::contextMenu(const QPoint& pos) {
    if (!m_doc) return;
    QModelIndex i = m_tree->indexAt(pos);
    Layer* l = m_model->layerAt(i);
    // right-click on a layer outside the selection selects just that one
    if (l && !m_doc->isLayerSelected(l->id)) m_doc->setCurrentLayer(l->id);
    else if (l) m_doc->setLayerSelection(m_doc->selectedLayerIds(), l->id);
    QMenu m(this);
    QPointer<Document> dp = m_doc;
    Document& d = *m_doc;

    // ---- new
    QMenu* add = m.addMenu(Theme::icon("layer-new"), T("New"));
    add->addAction(Theme::icon("layer-new"), T("New layer"), this, [&d] { ops::addRasterLayer(d); });
    add->addAction(Theme::icon("linework-new"), T("New linework layer"), this, [&d] { ops::addVectorLayer(d); });
    add->addAction(Theme::icon("folder-new"), T("New folder"), this, [&d] { ops::addGroup(d); });
    QMenu* adjM = add->addMenu(Theme::icon("adjustment"), T("New adjustment layer"));
    for (int t = 0; t < int(AdjustmentType::Count); ++t)
        adjM->addAction(T(adjustmentTypeName(AdjustmentType(t))), this, [this, t] { Q_EMIT requestNewAdjustment(t); });
    if (!l) {
        m.exec(m_tree->viewport()->mapToGlobal(pos));
        return;
    }
    const quint32 id = l->id;
    const QList<quint32> sel = m_doc->selectedLayerIds();
    const bool many = sel.size() > 1;
    const bool raster = l->isRaster();

    m.addSeparator();
    m.addAction(Theme::icon("settings"), T("Layer properties..."), this, [this, id] { openProperties(id); });
    m.addAction(T("Rename"), this, [this, i] { m_tree->edit(i); })->setShortcut(QKeySequence(Qt::Key_F2));

    // ---- layer list
    m.addSeparator();
    m.addAction(Theme::icon("duplicate"), many ? T("Duplicate selected") : T("Duplicate"), this, [&d, sel] { ops::duplicateLayers(d, sel); });
    if (many) {
        m.addAction(Theme::icon("merge"), T("Merge selected layers"), this, [&d, sel] {
            QString err;
            if (!ops::mergeLayers(d, sel, &err) && !err.isEmpty()) app()->showStatus(T(err));
        });
    } else {
        m.addAction(Theme::icon("merge"), T("Merge down"), this, [&d, id] {
            QString err;
            if (!ops::mergeDown(d, id, &err) && !err.isEmpty()) app()->showStatus(T(err));
        });
    }
    m.addAction(Theme::icon("folder-new"), T("Put into a new folder"), this, [&d, sel] { ops::groupLayers(d, sel); });
    if (!raster && !l->isGroup() && l->type() != LayerType::Adjustment)
        m.addAction(T("Rasterize"), this, [&d, id] { ops::rasterize(d, id); });
    if (l->isGroup()) m.addAction(T("Merge folder into one layer"), this, [&d, id] { ops::rasterize(d, id); });

    // ---- switches
    m.addSeparator();
    auto flag = [&](const QString& icon, const QString& text, bool Layer::*field) {
        QAction* a = m.addAction(icon.isEmpty() ? QIcon() : Theme::icon(icon), text);
        a->setCheckable(true);
        a->setChecked(l->*field);
        connect(a, &QAction::toggled, this, [dp, sel, field, text](bool on) {
            if (!dp) return;
            dp->pushUndo(text);
            for (quint32 x : sel)
                if (Layer* y = dp->layer(x)) {
                    y->*field = on;
                    dp->layerChanged(y);
                }
            dp->layersChanged();
        });
    };
    flag("reference", T("Reference layer (fills take borders from it)"), &Layer::reference);
    flag("timelapse-off", T("Hide from the recording"), &Layer::hideInTimelapse);
    flag("clip", T("Clipping"), &Layer::clipping);
    flag("alpha-lock", T("Preserve opacity"), &Layer::alphaLock);
    flag("lock", T("Lock layer"), &Layer::locked);
    QMenu* label = m.addMenu(T("Label color"));
    const QList<QPair<QString, QColor>> labels = {{T("None"), QColor()}, {T("Red"), QColor("#e5534b")}, {T("Orange"), QColor("#f0883e")},
                                                  {T("Yellow"), QColor("#e3c34c")}, {T("Green"), QColor("#57ab5a")},
                                                  {T("Blue"), QColor("#539bf5")}, {T("Violet"), QColor("#b083f0")}};
    for (const auto& lc : labels) {
        QColor col = lc.second;
        QPixmap sw(12, 12);
        sw.fill(col.isValid() ? col : Qt::transparent);
        label->addAction(QIcon(sw), lc.first, this, [dp, sel, col] {
            if (!dp) return;
            dp->pushUndo(T("Label color"));
            for (quint32 x : sel)
                if (Layer* y = dp->layer(x)) {
                    y->labelColor = col;
                    dp->layerChanged(y);  // marks the picture as changed (the label is saved)
                }
            dp->layersChanged();
        });
    }

    // ---- effects: quick switches with good defaults, everything else in the window
    const bool fxOk = !l->isGroup() && l->type() != LayerType::Adjustment;
    QMenu* fxM = m.addMenu(Theme::icon("fx"), T("Effects"));
    fxM->setEnabled(fxOk);
    fxM->addAction(Theme::icon("fx"), T("Layer effects..."), this, [this, id] { Q_EMIT requestEffects(id, QString()); });
    fxM->addSeparator();
    auto fxToggle = [&](QMenu* menu, const QString& text, bool LayerEffects::*field, const QString& key) {
        QAction* a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(l->fx.*field);
        connect(a, &QAction::toggled, this, [dp, sel, field, text](bool on) {
            if (!dp) return;
            dp->pushUndo(text);
            for (quint32 x : sel)
                if (Layer* y = dp->layer(x); y && !y->isGroup() && y->type() != LayerType::Adjustment) {
                    y->fx.*field = on;
                    dp->layerChanged(y);
                }
            dp->layersChanged();
        });
        Q_UNUSED(key);
        return a;
    };
    fxToggle(fxM, T("Outline"), &LayerEffects::outline, "outline");
    fxToggle(fxM, T("Drop shadow"), &LayerEffects::shadow, "shadow");
    fxToggle(fxM, T("Outer glow"), &LayerEffects::outerGlow, "outerGlow");
    fxToggle(fxM, T("Inner shadow"), &LayerEffects::innerShadow, "innerShadow");
    fxToggle(fxM, T("Inner glow"), &LayerEffects::innerGlow, "innerGlow");
    fxToggle(fxM, T("Color overlay"), &LayerEffects::overlay, "overlay");
    fxToggle(fxM, T("Gradient overlay"), &LayerEffects::gradientOverlay, "gradient");
    fxToggle(fxM, T("Blur"), &LayerEffects::blur, "blur");
    fxToggle(fxM, T("Paper texture"), &LayerEffects::texture, "texture");
    fxToggle(fxM, T("Grain (noise)"), &LayerEffects::grain, "grain");
    fxToggle(fxM, T("Screentone"), &LayerEffects::screentone, "screentone");

    // ---- channels (alpha and color channels)
    QMenu* ch = m.addMenu(Theme::icon("channels"), T("Channels"));
    QAction* ca = fxToggle(ch, T("Chromatic aberration"), &LayerEffects::chromatic, "chromatic");
    ca->setIcon(Theme::icon("channels"));
    ca->setEnabled(fxOk);
    ch->addAction(T("Chromatic aberration settings..."), this, [this, id] { Q_EMIT requestEffects(id, QStringLiteral("chromatic")); })
        ->setEnabled(fxOk);
    ch->addSeparator();
    ch->addAction(T("Alpha to selection"), this, [&d, id] { ops::selectLayerAlpha(d, id); });
    ch->addAction(T("Add alpha to selection"), this, [&d, id] { ops::selectLayerAlpha(d, id, ops::SelectMode::Add); });
    ch->addAction(T("Transparency to layer mask"), this, [&d, id] { ops::transparencyToMask(d, id); })->setEnabled(raster);
    ch->addAction(T("Alpha channel as a gray layer"), this, [&d, id] { ops::alphaAsLayer(d, id); })->setEnabled(raster);
    ch->addAction(T("Invert alpha"), this, [&d, id] { ops::invertAlpha(d, id); })->setEnabled(raster);
    ch->addSeparator();
    ch->addAction(T("Split into red, green and blue layers"), this, [&d, id] { ops::splitChannels(d, id); })->setEnabled(raster);
    ch->addSeparator();
    ch->addAction(T("Remove white background"), this, [&d, id] { ops::removeWhite(d, id); })->setEnabled(raster);
    ch->addAction(T("Recolor with the main color"), this, [&d, id] { ops::recolorLayer(d, id, app()->foreground()); })->setEnabled(raster);

    // ---- mask
    QMenu* mask = m.addMenu(Theme::icon("mask"), T("Layer mask"));
    if (!l->hasMask) {
        mask->addAction(T("Add (reveal all)"), this, [&d, id] { ops::addMask(d, id, false); });
        mask->addAction(T("Add from selection"), this, [&d, id] { ops::addMask(d, id, true); })->setEnabled(d.hasSelection());
    } else {
        mask->addAction(l->maskEnabled ? T("Disable") : T("Enable"), this, [&d, id] {
            d.pushUndo(T("Toggle mask"));
            if (Layer* x = d.layer(id)) { x->maskEnabled = !x->maskEnabled; d.layerChanged(x); d.layersChanged(); }
        });
        mask->addAction(T("Apply"), this, [&d, id] { ops::applyMask(d, id); })->setEnabled(raster);
        mask->addAction(T("Delete"), this, [&d, id] { ops::deleteMask(d, id); });
    }

    // ---- pixels
    m.addSeparator();
    QMenu* px = m.addMenu(Theme::icon("image"), T("Pixels"));
    px->addAction(T("New layer from selection (copy)"), this, [&d] { ops::layerFromSelection(d, false); })->setEnabled(raster && d.hasSelection());
    px->addAction(T("New layer from selection (cut)"), this, [&d] { ops::layerFromSelection(d, true); })->setEnabled(raster && d.hasSelection());
    px->addSeparator();
    px->addAction(T("Center on canvas"), this, [&d, id] { ops::centerLayer(d, id); });
    px->addAction(Theme::icon("flip"), T("Flip layer horizontally"), this, [&d, id] { ops::flipLayer(d, id, true); });
    px->addAction(T("Flip layer vertically"), this, [&d, id] { ops::flipLayer(d, id, false); });
    px->addAction(Theme::icon("fill"), T("Fill with main color"), this, [&d, id] { ops::fillLayer(d, id, app()->foreground()); })->setEnabled(raster);
    px->addAction(Theme::icon("clear"), T("Clear"), this, [&d, id] { ops::clearLayer(d, id); });

    m.addSeparator();
    m.addAction(Theme::icon("trash"), many ? T("Delete selected") : T("Delete"), this, [&d, sel] { ops::deleteLayers(d, sel); });
    m.exec(m_tree->viewport()->mapToGlobal(pos));
}

void LayerPanel::openProperties(quint32 id) {
    if (!m_doc || !m_doc->layer(id)) return;
    Layer* l = m_doc->layer(id);
    if (l->type() == LayerType::Adjustment) {
        Q_EMIT requestAdjustmentEdit(id);
        return;
    }
    LayerPropertiesDialog dlg(m_doc, id, this);
    connect(&dlg, &LayerPropertiesDialog::requestEffects, this, [this](quint32 x) { Q_EMIT requestEffects(x, QString()); });
    dlg.exec();
}

}  // namespace gx
