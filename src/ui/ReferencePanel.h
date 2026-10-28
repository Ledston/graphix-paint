#pragma once
#include <QImage>
#include <QWidget>

class QListWidget;
class QToolButton;

namespace gx {

// Shows reference images next to the canvas; click one to pick its colors.
class ReferenceView : public QWidget {
    Q_OBJECT
public:
    explicit ReferenceView(QWidget* parent = nullptr);
    void setImage(const QImage& img);
    void setGray(bool on) { m_gray = on; update(); }
    void setFlip(bool on) { m_flip = on; update(); }
    bool gray() const { return m_gray; }
    bool flip() const { return m_flip; }
    void fit();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent*) override;
private:
    QTransform xf() const;
    void pick(const QPointF& w);
    QImage m_img, m_grayImg;
    double m_zoom = 1;
    QPointF m_center;
    bool m_gray = false, m_flip = false;
    bool m_pan = false, m_picking = false;
    QPointF m_last;
};

class ReferencePanel : public QWidget {
    Q_OBJECT
public:
    explicit ReferencePanel(QWidget* parent = nullptr);
    ~ReferencePanel() override;
protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
private:
    void addImage(const QImage& img, const QString& path);
    void show(int index);
    void save();
    QListWidget* m_list;
    ReferenceView* m_view;
    QList<QImage> m_images;
    QStringList m_paths;
};

}  // namespace gx
