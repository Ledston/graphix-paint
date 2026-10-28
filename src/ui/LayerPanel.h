#pragma once
#include <QAbstractItemModel>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QWidget>

class QComboBox;
class QToolButton;

namespace gx {

class Document;
class Layer;
class ValueBar;

class LayerModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit LayerModel(QObject* parent = nullptr);
    void setDocument(Document* d);
    Document* document() const { return m_doc; }
    Layer* layerAt(const QModelIndex& i) const;
    QModelIndex indexOf(quint32 id) const;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return 1; }
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

    void reset();

private:
    QPointer<Document> m_doc;
};

class LayerDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit LayerDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    bool editorEvent(QEvent* e, QAbstractItemModel* model, const QStyleOptionViewItem& opt, const QModelIndex& idx) override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;

    static QRect eyeRect(const QRect& r);
    static QRect thumbRect(const QRect& r);
    static QRect maskRect(const QRect& r);
    QImage thumbnail(Layer* l, Document* d, bool mask) const;

private:
    struct Thumb {
        quint64 version = ~0ull;
        QImage img;
    };
    mutable QHash<quint64, Thumb> m_thumbs;
};

class LayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);
    void setDocument(Document* d);
    QTreeView* tree() const { return m_tree; }

Q_SIGNALS:
    void requestEffects(quint32 layerId, const QString& focus);
    void requestAdjustmentEdit(quint32 layerId);
    void requestNewAdjustment(int type);

public Q_SLOTS:
    void openPropertiesSlot(quint32 id) { openProperties(id); }

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QList<quint32> targets() const;  // selected layers, or the current one
    void rebuild();
    void syncControls();
    void selectCurrent();
    void contextMenu(const QPoint& pos);
    void openProperties(quint32 id);
    Layer* current() const;

    QPointer<Document> m_doc;
    QTreeView* m_tree;
    LayerModel* m_model;
    LayerDelegate* m_delegate;
    QComboBox* m_blend;
    ValueBar* m_opacity;
    QToolButton *m_alphaLock, *m_clip, *m_lock, *m_maskEdit, *m_pass;
    bool m_updating = false;
    quint32 m_pendingSingle = 0;
};

}  // namespace gx
