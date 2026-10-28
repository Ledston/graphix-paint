#pragma once
#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QToolButton;

namespace gx {

class CanvasView;

// Always shows the whole picture, optionally mirrored and in grays, to check the
// drawing with fresh eyes while working zoomed in.
class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;

private:
    void setView(CanvasView* v);
    void refresh();
    QRect area() const;

    QPointer<CanvasView> m_view;
    QImage m_img;
    QTimer m_timer;
    QToolButton* m_flip = nullptr;
    QToolButton* m_gray = nullptr;
};

}  // namespace gx
