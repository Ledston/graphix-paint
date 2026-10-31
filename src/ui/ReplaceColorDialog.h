#pragma once
#include <QDialog>
#include <QPointer>
#include <QTimer>

#include "core/ReplaceColor.h"

class QCheckBox;

namespace gx {

class Document;
class ColorButton;

// Edit > Replace color: a live preview on the canvas; OK changes the layer (inside the selection).
class ReplaceColorDialog : public QDialog {
    Q_OBJECT
public:
    ReplaceColorDialog(Document* doc, quint32 layerId, QWidget* parent = nullptr);
    ~ReplaceColorDialog() override;
    ReplaceColorParams& params() { return m_p; }
    void accept() override;
    void reject() override;
    void updatePreview();

private:
    void clearPreview();
    QPointer<Document> m_doc;
    quint32 m_layer;
    ReplaceColorParams m_p;
    QTimer m_timer;
    QCheckBox* m_preview = nullptr;
};

}  // namespace gx
