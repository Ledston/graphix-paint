#pragma once
#include <QIcon>
#include <QWidget>
#include <functional>

namespace gx {

// A ring of brushes and tools that opens under the pointer (hold the quick menu key, point
// at an item, let go; or tap the key and click an item). Esc or a click in the middle closes it.
class RadialMenu : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QIcon icon;
        QString label;
        std::function<void()> run;
    };
    explicit RadialMenu(QWidget* parent);
    void setItems(const QList<Item>& items);
    // opens centered on `center` (parent coordinates)
    void popup(const QPointF& center);
    int highlighted() const { return m_hi; }
    int count() const { return int(m_items.size()); }
    QPointF itemCenter(int i) const;  // in this widget's coordinates
    void choose(int i);               // runs item i and closes
    void chooseHighlighted();         // the key was let go: runs the item pointed at, or closes

    static constexpr double kOuter = 158, kInner = 46;

Q_SIGNALS:
    void closed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    int itemAt(const QPointF& p) const;
    void pointAt(const QPointF& p);
    QList<Item> m_items;
    int m_hi = -1;
};

}  // namespace gx
