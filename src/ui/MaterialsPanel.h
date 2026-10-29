#pragma once
#include <QWidget>

class QListWidget;
class QTabWidget;

namespace gx {

// Window > Materials: pictures, patterns, screentones and gradient maps.
// Drag one onto the canvas (or double-click it) to use it.
class MaterialsPanel : public QWidget {
    Q_OBJECT
public:
    explicit MaterialsPanel(QWidget* parent = nullptr);
    QTabWidget* tabs() const { return m_tabs; }

Q_SIGNALS:
    // kind: "image", "pattern", "tone", "gradient"; value: file, name or index
    void place(const QString& kind, const QString& value);
    void applyGradient(int index);       // gradient map applied to the layer's pixels
    void saveSelection(bool asPattern);  // the selected part of the layer becomes a material

private:
    void refill();
    QTabWidget* m_tabs;
    QListWidget *m_images, *m_patterns, *m_tones, *m_gradients;
};

}  // namespace gx
