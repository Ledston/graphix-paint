#include "core/Document.h"

#include <QFileInfo>
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

void Document::setSizeNoUndo(const QSize& s) {
    m_state.size = s.expandedTo(QSize(1, 1));
    m_projection->reset();
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
    if (m_state.currentId == id) return;
    m_state.currentId = id;
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
        QString n = QStringLiteral("%1 %2").arg(base).arg(i);
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
        Q_EMIT sizeChanged();
    } else {
        m_projection->invalidateAll();
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
    touch();
    Q_EMIT imageChanged(r);
}

void Document::layerChanged(Layer* l) {
    // Property changes can affect clipped layers and adjustments anywhere, so the whole
    // canvas is marked; only tiles on screen get recomputed.
    QRect r = bounds();
    m_projection->invalidate(r);
    touch();
    if (l) Q_EMIT layerPropertiesChanged(l->id);
    Q_EMIT imageChanged(r);
}

void Document::structureChanged() {
    m_projection->invalidateAll();
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
