#pragma once
#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>

#include "core/Layer.h"
#include "core/Tile.h"

class QCheckBox;
class QLabel;

namespace gx {

class Document;

// A layer effect used as a filter (Filter > Effects): set it up with the sliders while the
// canvas shows the result, OK draws it into the layer's pixels for good.
class EffectFilterDialog : public QDialog {
    Q_OBJECT
public:
    EffectFilterDialog(Document* doc, quint32 layerId, const QString& effect, QWidget* parent = nullptr);
    ~EffectFilterDialog() override;
    void accept() override;
    void reject() override;
    // the area the effect changes (layer content + room for the effect, inside the selection)
    QRect area() const;

private:
    void schedule();
    void startPreview();
    void showPreview(const RasterSurface& s);
    void clearPreview();

    QPointer<Document> m_doc;
    quint32 m_layer;
    QString m_key;
    LayerEffects m_fx;
    QCheckBox* m_previewBox = nullptr;
    QLabel* m_busy = nullptr;
    QTimer m_timer;
    QFutureWatcher<RasterSurface> m_watch;
    bool m_again = false;
    quint64 m_version = 0, m_shownVersion = 0, m_runningVersion = 0;
    RasterSurface m_result;  // the newest finished preview
    bool m_done = false;
};

}  // namespace gx
