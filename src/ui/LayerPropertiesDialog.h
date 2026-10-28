#pragma once
#include <QDialog>
#include <QPointer>

namespace gx {

class Document;

// Double-click on a layer: name, opacity, blending and all switches in one window.
class LayerPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    LayerPropertiesDialog(Document* doc, quint32 layerId, QWidget* parent = nullptr);
    void accept() override;
    void reject() override;

Q_SIGNALS:
    void requestEffects(quint32 layerId);

private:
    QPointer<Document> m_doc;
    quint32 m_id;
    bool m_done = false;
    bool m_changed = false;
};

}  // namespace gx
