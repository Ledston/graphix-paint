#pragma once
#include <QDialog>
#include <QPointer>

#include "core/Layer.h"

namespace gx {

class Document;

// Non-destructive layer effects with live preview.
class EffectsDialog : public QDialog {
    Q_OBJECT
public:
    // focus: an effect to scroll to ("chromatic", "outline", ...)
    EffectsDialog(Document* doc, quint32 layerId, QWidget* parent = nullptr, const QString& focus = {});
    void accept() override;
    void reject() override;

private:
    void apply();
    QPointer<Document> m_doc;
    quint32 m_layer;
    LayerEffects m_fx;
    bool m_done = false;
};

}  // namespace gx
