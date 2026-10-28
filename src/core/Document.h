#pragma once
#include <QDateTime>
#include <QSet>
#include <QObject>
#include <QSize>
#include <QString>
#include <memory>
#include <vector>

#include "core/Guides.h"
#include "core/Layer.h"
#include "core/Projection.h"

namespace gx {

// Everything that undo/redo restores.
struct DocState {
    std::shared_ptr<GroupLayer> root;
    MaskSurface selection{0};
    bool hasSelection = false;
    quint32 currentId = 0;
    QSize size;
    DocState clone() const;
};

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(const QSize& size, QObject* parent = nullptr);
    ~Document() override;

    // ---- canvas ----
    QSize size() const { return m_state.size; }
    QRect bounds() const { return QRect(QPoint(0, 0), m_state.size); }
    void setSizeNoUndo(const QSize& s);  // used by canvas operations after pushUndo
    double dpi() const { return m_dpi; }
    void setDpi(double d) { m_dpi = d; }

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& p);
    QString displayName() const;
    bool isModified() const { return m_modified; }
    void setModified(bool m);
    quint64 uid() const { return m_uid; }
    QDateTime lastChange() const { return m_lastChange; }

    // ---- layers ----
    GroupLayer* root() const { return m_state.root.get(); }
    Layer* currentLayer() const;
    quint32 currentLayerId() const { return m_state.currentId; }
    void setCurrentLayer(quint32 id);  // also makes it the only selected layer

    // ---- several selected layers (Ctrl / Shift + click in the layer list) ----
    // The current layer is always one of them. Lists are in stacking order, bottom first.
    QList<quint32> selectedLayerIds() const;
    QList<quint32> topLevelSelectedIds() const;  // without layers inside a selected folder
    bool isLayerSelected(quint32 id) const;
    int selectedCount() const { return int(selectedLayerIds().size()); }
    void toggleLayerSelected(quint32 id);
    void selectLayerRange(quint32 from, quint32 to);
    void setLayerSelection(const QList<quint32>& ids, quint32 current);
    Layer* layer(quint32 id) const { return findLayer(root(), id); }
    quint32 newLayerId();
    QString uniqueLayerName(const QString& base) const;

    // ---- selection (0 = unselected, 255 = selected) ----
    MaskSurface& selection() { return m_state.selection; }
    const MaskSurface& selection() const { return m_state.selection; }
    bool hasSelection() const { return m_state.hasSelection; }
    void setHasSelection(bool on);
    QRect selectionBounds() const;
    MaskSurface& savedSelection() { return m_lastSelection; }
    bool showSelection = true;

    // ---- history ----
    // Call BEFORE changing anything. Consecutive calls with the same non-empty
    // mergeKey within a short time collapse into one step (slider drags, etc.).
    void pushUndo(const QString& name, const QString& mergeKey = {});
    // Restores the state captured by the last pushUndo and forgets it (cancelled operation).
    void revertLastUndo();
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    void undo();
    void redo();
    QStringList historyNames() const;  // oldest first, then "current"
    int historyIndex() const { return int(m_undo.size()); }
    void jumpToHistory(int index);
    void setUndoLimit(int n) { m_undoLimit = std::max(5, n); trimHistory(); }
    void clearHistory();

    // ---- change notifications ----
    void pixelsChanged(const QRect& canvasRect, Layer* layer = nullptr);
    void layerChanged(Layer* layer);  // properties (opacity, blend, visibility...)
    void structureChanged();          // layers added / removed / moved
    void selectionModified();
    void guidesModified() { Q_EMIT guidesChanged(); }

    Projection* projection() { return m_projection.get(); }
    // The picture without the layers hidden from screen recordings (made when first asked for).
    Projection* recordingProjection();
    void releaseRecordingProjection();

    // ---- non-layer document data (saved, not undoable) ----
    Perspective perspective;
    Symmetry symmetry;

    // Bounds of a layer's visible content in canvas space.
    QRect layerBounds(const Layer* l) const;

Q_SIGNALS:
    void imageChanged(const QRect& r);
    void layersChanged();
    void layerPropertiesChanged(quint32 id);
    void currentLayerChanged();
    void selectionChanged();
    void historyChanged();
    void modifiedChanged(bool);
    void sizeChanged();
    void guidesChanged();
    void filePathChanged();

private:
    struct HistoryEntry {
        QString name;
        DocState state;
        QString mergeKey;
        qint64 timeMs = 0;
    };
    void restore(DocState&& s);
    void trimHistory();
    void touch();

    DocState m_state;
    QSet<quint32> m_selectedLayers;
    std::vector<HistoryEntry> m_undo, m_redo;
    int m_undoLimit = 200;
    std::unique_ptr<Projection> m_projection;
    std::unique_ptr<Projection> m_recProjection;
    MaskSurface m_lastSelection{0};
    QString m_filePath;
    bool m_modified = false;
    double m_dpi = 350;
    quint32 m_nextId = 1;
    quint64 m_uid;
    QDateTime m_lastChange;
};

}  // namespace gx
