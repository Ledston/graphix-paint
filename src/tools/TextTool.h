#pragma once
#include <QPointer>

#include "tools/Tool.h"

class QPlainTextEdit;
class QFontComboBox;
class QCheckBox;
class QComboBox;

namespace gx {

class ValueBar;
class TextLayer;

// Click to add text, click on a text layer to edit it, drag to move it.
// Fonts come from the system. Text stays editable until the layer is rasterized.
class TextTool : public Tool {
    Q_OBJECT
public:
    explicit TextTool(QObject* p = nullptr) : Tool("text", "Text", "text", p) {}
    void press(const PointerEvent& e) override;
    void move(const PointerEvent& e) override;
    void release(const PointerEvent& e) override;
    void drawOverlay(QPainter& p, CanvasView* v) override;
    QCursor cursor(CanvasView*) const override { return Qt::IBeamCursor; }
    QWidget* createOptions(QWidget* parent) override;
    bool isBusy() const override { return m_drag; }

private:
    TextLayer* currentText() const;
    void applyEdit(const std::function<void(TextLayer*)>& fn, const QString& key);
    void syncFromLayer();

    bool m_drag = false;
    QPointF m_press, m_origin;
    QPointer<CanvasView> m_view;
    QPointer<QWidget> m_opts;
    QPlainTextEdit* m_edit = nullptr;
    QFontComboBox* m_font = nullptr;
    ValueBar* m_size = nullptr;
    ValueBar* m_line = nullptr;
    ValueBar* m_letter = nullptr;
    QCheckBox *m_bold = nullptr, *m_italic = nullptr, *m_under = nullptr, *m_vertical = nullptr;
    QComboBox* m_align = nullptr;
    bool m_syncing = false;
    QString m_family;
    double m_sizePx = 48;
};

}  // namespace gx
