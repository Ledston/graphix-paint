#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

#include "core/Adjustments.h"
#include "core/Tile.h"

class QVBoxLayout;
class QComboBox;
class QCheckBox;

namespace gx {

class Document;
class CurveEditor;

// Dialog for image adjustments (applied to a layer or edited on an adjustment layer)
// and for filters. Shows a live preview on the canvas.
class AdjustmentDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { ApplyToLayer, EditLayer, Filter };
    AdjustmentDialog(Document* doc, quint32 layerId, const Adjustment& adj, Mode mode, QWidget* parent = nullptr);
    AdjustmentDialog(Document* doc, quint32 layerId, const QString& filterId, QWidget* parent = nullptr);
    ~AdjustmentDialog() override;

    static QList<QPair<QString, QString>> filterIds();

    void accept() override;
    void reject() override;

private:
    void buildAdjustment();
    void buildFilter();
    void schedulePreview();
    void computePreview();
    void finish(bool ok);
    QWidget* slider(const QString& label, const QString& key, double min, double max, int dec, const QString& suffix = {},
                    double scale = 1);
    QWidget* checkbox(const QString& label, const QString& key);
    QVector<float> histogram(int channel) const;

    QPointer<Document> m_doc;
    quint32 m_layer;
    Mode m_mode;
    Adjustment m_adj;
    QString m_filter;
    QJsonObject m_params;  // filter params
    QVBoxLayout* m_body = nullptr;
    QCheckBox* m_preview = nullptr;
    QTimer m_timer;
    bool m_done = false;
    RasterSurface m_original;
    QRect m_area;
};

}  // namespace gx
