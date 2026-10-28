#pragma once
#include <QColor>
#include <QDialog>
#include <QSize>
#include <QWidget>

#include "core/Gradient.h"
#include "io/Recovery.h"

class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;
class QListWidget;
class QButtonGroup;

namespace gx {

namespace Dialogs {
// Returns -1 when cancelled.
int askInt(QWidget* parent, const QString& title, const QString& label, int value, int min, int max);
bool editGradient(QWidget* parent, Gradient& g);
}  // namespace Dialogs

class NewDocumentDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewDocumentDialog(QWidget* parent = nullptr);
    QSize canvasSize() const;
    double dpi() const;
    QColor background() const;
    void accept() override;

private:
    void updateInfo();
    void setPixels(int w, int h);
    QComboBox* m_preset;
    QDoubleSpinBox *m_w, *m_h;
    QComboBox* m_unit;
    QDoubleSpinBox* m_dpi;
    QComboBox* m_bg;
    QColor m_custom = Qt::white;
    QLabel* m_info;
    int m_lastUnit = 0;
};

class CanvasSizeDialog : public QDialog {
    Q_OBJECT
public:
    CanvasSizeDialog(const QSize& current, QWidget* parent = nullptr);
    QSize newSize() const;
    QPoint offset() const;  // where the old (0,0) goes

private:
    QSize m_cur;
    QSpinBox *m_w, *m_h;
    QButtonGroup* m_anchor;
};

class ImageSizeDialog : public QDialog {
    Q_OBJECT
public:
    ImageSizeDialog(const QSize& current, double dpi, QWidget* parent = nullptr);
    QSize newSize() const;
    double dpi() const;
    bool smooth() const;

private:
    QSize m_cur;
    QSpinBox *m_w, *m_h;
    QDoubleSpinBox* m_pct;
    QDoubleSpinBox* m_dpi;
    QCheckBox* m_keep;
    QComboBox* m_quality;
    bool m_updating = false;
};

class RecoveryDialog : public QDialog {
    Q_OBJECT
public:
    RecoveryDialog(const QList<Recovery::Item>& items, QWidget* parent = nullptr);
    QList<Recovery::Item> selected() const;
    bool discardRequested() const { return m_discard; }

private:
    QList<Recovery::Item> m_items;
    QListWidget* m_list;
    bool m_discard = false;
};

// Gradient stops editor
class GradientEditor : public QWidget {
    Q_OBJECT
public:
    explicit GradientEditor(QWidget* parent = nullptr);
    void setGradient(const Gradient& g);
    Gradient gradient() const { return m_g; }
    int selectedStop() const { return m_sel; }
    void setStopColor(const QColor& c);
    void setStopOpacity(double o);
    void removeSelected();
    QSize sizeHint() const override { return QSize(360, 64); }

Q_SIGNALS:
    void changed();
    void selectionChanged(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    QRectF bar() const;
    int stopAt(const QPointF& p) const;
    Gradient m_g;
    int m_sel = 0;
    bool m_drag = false;
};

}  // namespace gx
