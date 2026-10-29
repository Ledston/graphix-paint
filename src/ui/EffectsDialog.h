#pragma once
#include <QDialog>
#include <QPointer>
#include <functional>

class QVBoxLayout;

#include "core/Layer.h"

namespace gx {

class Document;

// The layer effects, by key ("outline", "chromatic"...) with their English names.
QList<QPair<QString, QString>> effectKeys();
bool* effectSwitch(LayerEffects& fx, const QString& key);
// Adds the controls of one effect; 'changed' is called after every edit.
void addEffectControls(const QString& key, LayerEffects* fx, QVBoxLayout* layout, QWidget* parent, QObject* context,
                       const std::function<void()>& changed);

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
