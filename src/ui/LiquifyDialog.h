#pragma once
#include <QDialog>
#include <QImage>
#include <QPointer>
#include <vector>

#include "core/LiquifyField.h"

class QButtonGroup;
class QCheckBox;

namespace gx {

class ValueBar;
class LiquifyCanvas;

// Filter > Liquify: the layer in its own window with Photoshop's liquify tools
// (forward warp, reconstruct, smooth, twirl, pucker, bloat, push left, freeze / thaw).
// OK puts the result into the layer as one undo step; Cancel leaves the layer as it was.
class LiquifyDialog : public QDialog {
    Q_OBJECT
public:
    // src: the layer's pixels (premultiplied); frozen: optional, 1 = cannot move (outside the selection)
    LiquifyDialog(const QImage& src, const std::vector<float>* frozen, QWidget* parent = nullptr);
    ~LiquifyDialog() override;

    LiquifyField& field() { return m_field; }
    const QImage& result() const { return m_field.output(); }
    void setMode(int mode);
    int mode() const { return m_mode; }
    LiquifyField::Brush& brush() { return m_brush; }
    QWidget* canvasWidget() const;
    QPointF canvasPoint(QPointF imagePos) const;  // where a picture point is on the canvas widget

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    friend class LiquifyCanvas;
    void saveSettings() const;
    LiquifyField m_field;
    LiquifyField::Brush m_brush;
    int m_mode = LiquifyField::Forward;
    bool m_penPressure = true;
    bool m_showMesh = false;
    bool m_showFrozen = true;
    LiquifyCanvas* m_canvas = nullptr;
    QButtonGroup* m_tools = nullptr;
    ValueBar* m_size = nullptr;
};

}  // namespace gx
