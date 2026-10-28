#include "core/Document.h"

#include <QFileInfo>
#include <functional>
#include "app/I18n.h"
#include <QRandomGenerator>

#include "core/Effects.h"

namespace gx {

DocState DocState::clone() const {
    DocState s;
    s.root = std::static_pointer_cast<GroupLayer>(root->clone());
    s.selection = selection;
    s.hasSelection = hasSelection;
    s.currentId = currentId;
    s.size = size;
    return s;
}

Document::Document(const QSize& size, QObject* parent) : QObject(parent) {
    m_uid = QRandomGenerator::global()->generate64();
    m_state.size = size.expandedTo(QSize(1, 1));
    m_state.root = std::make_shared<GroupLayer>();
    m_state.root->name = "Root";
    m_state.root->id = 0;
    m_projection = std::make_unique<Projection>(this);
    perspective.resetFor(m_state.size);
    symmetry.center = QPointF(m_state.size.width() / 2.0, m_state.size.height() / 2.0);
    m_lastChange = QDateTime::currentDateTime();
}

Document::~Document() = default;

Projection* Document::recordingProjection() {
    if (!m_recProjection) m_recProjection = std::make_unique<Projection>(this, true);
    return m_recProjection.get();
}

void Document::releaseRecordingProjection() { m_recProjection.reset(); }

void Document::setSizeNoUndo(const QSize& s) {
    m_state.size = s.expandedTo(QSize(1, 1));
    m_projection->reset();
    if (m_recProjection) m_recProjection->reset();
    touch();
    Q_EMIT sizeChanged();
    Q_EMIT imageChanged(bounds());
}

void Document::setFilePath(const QString& p) {
    m_filePath = p;
    Q_EMIT filePathChanged();
}

QString Document::displayName() const {
    if (m_filePath.isEmpty()) return T("Untitled");
    return QFileInfo(m_filePath).fileName();
}

void Document::setModified(bool m) {
    if (m_modified == m) return;
    m_modified = m;
    Q_EMIT modifiedChanged(m);
}

void Document::touch() {
    m_lastChange = QDateTime::currentDateTime();
    setModified(true);
}

Layer* Document::currentLayer() const {
    Layer* l = layer(m_state.currentId);
    if (!l && !root()->children.empty()) l = root()->children.back().get();
    return l;
}

void Document::setCurrentLayer(quint32 id) {
    const bool selChanged = !(m_selectedLayers.size() == 1 && m_selectedLayers.contains(id));
    if (m_state.currentId == id && !selChanged) return;
    m_state.currentId = id;
    m_selectedLayers = {id};
    Q_EMIT currentLayerChanged();
}

QList<quint32> Document::selectedLayerIds() const {
    QList<quint32> out;
    const Layer* cur = currentLayer();
    forEachLayer(root(), [&](Layer* l) {
        if (m_selectedLayers.contains(l->id) || (cur && l == cur)) out << l->id;
        return true;
    });
    return out;
}

QList<quint32> Document::topLevelSelectedIds() const {
    QList<quint32> all = selectedLayerIds();
    QSet<quint32> set(all.begin(), all.end());
    QList<quint32> out;
    for (quint32 id : all) {
        const Layer* l = layer(id);
        bool inside = false;
        for (const GroupLayer* p = l ? l->parent : nullptr; p; p = p->parent)
            if (set.contains(p->id)) inside = true;
        if (!inside) out << id;
    }
    return out;
}

bool Document::isLayerSelected(quint32 id) const {
    if (const Layer* cur = currentLayer(); cur && cur->id == id) return true;
    return m_selectedLayers.contains(id) && layer(id);
}

void Document::toggleLayerSelected(quint32 id) {
    if (!layer(id)) return;
    if (const Layer* cur = currentLayer()) m_selectedLayers.insert(cur->id);
    if (m_selectedLayers.contains(id) && m_selectedLayers.size() > 1) {
        m_selectedLayers.remove(id);
        if (m_state.currentId == id || !layer(m_state.currentId)) {
            // the clicked layer was the active one: the topmost remaining becomes active
            QList<quint32> rest = selectedLayerIds();
            rest.removeAll(id);
            if (!rest.isEmpty()) m_state.currentId = rest.last();
        }
    } else {
        m_selectedLayers.insert(id);
        m_state.currentId = id;
    }
    Q_EMIT currentLayerChanged();
}

void Document::selectLayerRange(quint32 from, quint32 to) {
    if (!layer(to)) return;
    if (!layer(from)) {
        setCurrentLayer(to);
        return;
    }
    // the order the rows have in the layer list: top layer first, folder before its contents
    QList<quint32> order;
    std::function<void(const GroupLayer*)> visual = [&](const GroupLayer* g) {
        for (auto it = g->children.rbegin(); it != g->children.rend(); ++it) {
            order << (*it)->id;
            if ((*it)->isGroup()) visual(static_cast<const GroupLayer*>(it->get()));
        }
    };
    visual(root());
    int a = order.indexOf(from), b = order.indexOf(to);
    if (a > b) std::swap(a, b);
    m_selectedLayers.clear();
    for (int i = a; i <= b; ++i) m_selectedLayers.insert(order[i]);
    m_state.currentId = to;
    Q_EMIT currentLayerChanged();
}

void Document::setLayerSelection(const QList<quint32>& ids, quint32 current) {
    m_selectedLayers = QSet<quint32>(ids.begin(), ids.end());
    if (layer(current)) m_selectedLayers.insert(current);
    m_state.currentId = current;
    Q_EMIT currentLayerChanged();
}

quint32 Document::newLayerId() {
    while (m_nextId == 0 || layer(m_nextId)) ++m_nextId;
    return m_nextId++;
}

QString Document::uniqueLayerName(const QString& base) const {
    QSet<QString> names;
    forEachLayer(root(), [&](Layer* l) { names.insert(l->name); return true; });
    for (int i = 1;; ++i) {
        QString n = QStringLiteral("%1 %2").arg(T(base)).arg(i);  // "Layer 3" / "Слой 3"
        if (!names.contains(n)) return n;
    }
}

void Document::setHasSelection(bool on) {
    if (on == m_state.hasSelection) return;
    m_state.hasSelection = on;
    if (!on) m_state.selection.clear();
}

QRect Document::selectionBounds() const {
    if (!m_state.hasSelection) return bounds();
    return m_state.selection.exactBounds() & bounds();
}

void Document::pushUndo(const QString& name, const QString& mergeKey) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_redo.clear();
    if (!mergeKey.isEmpty() && !m_undo.empty() && m_undo.back().mergeKey == mergeKey && now - m_undo.back().timeMs < 2000) {
        m_undo.back().timeMs = now;
        touch();
        return;
    }
    HistoryEntry e;
    e.name = name;
    e.state = m_state.clone();
    e.mergeKey = mergeKey;
    e.timeMs = now;
    m_undo.push_back(std::move(e));
    trimHistory();
    touch();
    Q_EMIT historyChanged();
}

void Document::revertLastUndo() {
    if (m_undo.empty()) return;
    HistoryEntry e = std::move(m_undo.back());
    m_undo.pop_back();
    restore(std::move(e.state));
    Q_EMIT historyChanged();
}

void Document::undo() {
    if (m_undo.empty()) return;
    HistoryEntry e = std::move(m_undo.back());
    m_undo.pop_back();
    HistoryEntry r;
    r.name = e.name;
    r.state = m_state.clone();
    m_redo.push_back(std::move(r));
    restore(std::move(e.state));
    Q_EMIT historyChanged();
}

void Document::redo() {
    if (m_redo.empty()) return;
    HistoryEntry e = std::move(m_redo.back());
    m_redo.pop_back();
    HistoryEntry u;
    u.name = e.name;
    u.state = m_state.clone();
    u.timeMs = 0;
    m_undo.push_back(std::move(u));
    restore(std::move(e.state));
    Q_EMIT historyChanged();
}

QStringList Document::historyNames() const {
    QStringList l;
    l << QStringLiteral("Start");
    for (const auto& e : m_undo) l << e.name;
    for (auto it = m_redo.rbegin(); it != m_redo.rend(); ++it) l << it->name;
    return l;
}

void Document::jumpToHistory(int index) {
    while (historyIndex() > index && canUndo()) undo();
    while (historyIndex() < index && canRedo()) redo();
}

void Document::clearHistory() {
    m_undo.clear();
    m_redo.clear();
    Q_EMIT historyChanged();
}

void Document::trimHistory() {
    if (int(m_undo.size()) > m_undoLimit) m_undo.erase(m_undo.begin(), m_undo.begin() + (int(m_undo.size()) - m_undoLimit));
}

void Document::restore(DocState&& s) {
    const bool sizeChange = s.size != m_state.size;
    m_state = std::move(s);
    if (sizeChange) {
        m_projection->reset();
        if (m_recProjection) m_recProjection->reset();
        Q_EMIT sizeChanged();
    } else {
        m_projection->invalidateAll();
        if (m_recProjection) m_recProjection->invalidateAll();
    }
    touch();
    Q_EMIT layersChanged();
    Q_EMIT currentLayerChanged();
    Q_EMIT selectionChanged();
    Q_EMIT imageChanged(bounds());
}

void Document::pixelsChanged(const QRect& r, Layer* l) {
    if (r.isEmpty()) return;
    if (l) {
        l->version++;
        invalidateEffects(*l, r);
        for (Layer* p = l->parent; p; p = p->parent) p->version++;
    }
    m_projection->invalidate(r);
    if (m_recProjection) m_recProjection->invalidate(r);
    touch();
    Q_EMIT imageChanged(r);
}

void Document::layerChanged(Layer* l) {
    // Property changes can affect clipped layers and adjustments anywhere, so the whole
    // canvas is marked; only tiles on screen get recomputed.
    QRect r = bounds();
    m_projection->invalidate(r);
    if (m_recProjection) m_recProjection->invalidate(r);
    touch();
    if (l) Q_EMIT layerPropertiesChanged(l->id);
    Q_EMIT imageChanged(r);
}

void Document::structureChanged() {
    m_projection->invalidateAll();
    if (m_recProjection) m_recProjection->invalidateAll();
    touch();
    Q_EMIT layersChanged();
    Q_EMIT currentLayerChanged();
    Q_EMIT imageChanged(bounds());
}

void Document::selectionModified() {
    Q_EMIT selectionChanged();
}

QRect Document::layerBounds(const Layer* l) const {
    if (!l) return {};
    if (l->isGroup()) {
        QRect b;
        for (const auto& c : static_cast<const GroupLayer*>(l)->children) b |= layerBounds(c.get());
        return b;
    }
    if (l->type() == LayerType::Adjustment) return bounds();
    const RasterSurface* s = l->displaySurface();
    if (!s) return {};
    QRect b = s->tileBounds();
    if (l->fx.anyActive()) {
        int m = l->fx.margin();
        b.adjust(-m, -m, m, m);
    }
    return b.translated(l->previewOffset);
}

}  // namespace gx
