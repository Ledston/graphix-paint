#pragma once
#include <QDialog>
#include <QImage>
#include <QPointer>

class QComboBox;
class QCheckBox;

namespace gx {

class Document;
class ColorButton;
class ValueBar;

// Select > Color range: selects every pixel close to a color (click the preview to pick it).
class ColorRangeDialog : public QDialog {
    Q_OBJECT
public:
    ColorRangeDialog(Document* doc, QWidget* parent = nullptr);
    void accept() override;

private:
    void loadSource();
    void updatePreview();
    float weight(quint32 premultipliedPixel) const;

    QPointer<Document> m_doc;
    QImage m_full;     // straight colors of the picture
    QImage m_small;    // preview copy
    QImage m_mask;     // preview of the selection
    QColor m_color;
    double m_fuzz = 40;
    bool m_invert = false;
    QComboBox* m_source = nullptr;
    QComboBox* m_mode = nullptr;
    ColorButton* m_colorBtn = nullptr;
    QWidget* m_view = nullptr;
};

}  // namespace gx
