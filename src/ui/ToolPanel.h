#pragma once
#include <QHash>
#include <QPointer>
#include <QWidget>

#include "brush/BrushSettings.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QLabel;

class QGridLayout;

namespace gx {

class Tool;
class ValueBar;
class BrushControls;
class Section;

// Grid of brush slots, like SAI's custom tool palette, sorted into tabs
// (Basic, Paint, Hatching, Effects, Erase and the user's own).
class BrushSlots : public QWidget {
    Q_OBJECT
public:
    explicit BrushSlots(QWidget* parent = nullptr);
    int heightForWidth(int w) const override;
    bool hasHeightForWidth() const override { return true; }
    QSize sizeHint() const override { return QSize(220, heightForWidth(220)); }
    QString currentGroup() const { return m_group; }
    void setGroup(const QString& g);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent*) override;
    bool event(QEvent* e) override;
private:
    struct Layout {
        QList<QPair<QString, QRect>> tabs;
        QList<int> cells;  // brush indices shown, in order
        int top = 0;       // where the cells start
        int height = 0;
    };
    Layout layoutFor(int w) const;
    QStringList groupsShown() const;
    int cols(int w) const;
    QRect cell(const Layout& l, int pos, int w) const;
    int indexAt(const QPoint& p, bool* addCell, QString* tab) const;
    void menu(int index, const QPoint& global);
    QImage strokePreview(const BrushSettings& b, const QSize& size);
    QString m_group;
    QHash<QByteArray, QImage> m_previews;
};

// The settings of the current brush slot: the main ones, then the rest in sections
// that fold away. "All settings" opens the brush settings window with a preview.
class BrushSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit BrushSettingsWidget(QWidget* parent = nullptr);
private:
    void load();
    void store();
    void updateVisibility();
    BrushControls* m_controls;
    QList<QPair<QWidget*, std::function<bool(const BrushSettings&)>>> m_sections;
    QLabel* m_title;
    bool m_storing = false;
    qsizetype m_textureCount = -1;
};

class ToolPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolPanel(QWidget* parent = nullptr);
private:
    void showOptionsFor(Tool* t);
    QButtonGroup* m_group;
    QHash<QString, QToolButton*> m_buttons;
    QList<QToolButton*> m_buttonOrder;
    QGridLayout* m_grid = nullptr;
    void layoutToolButtons();
    QStackedWidget* m_stack;
    BrushSettingsWidget* m_brush;
    QHash<Tool*, QWidget*> m_options;
    QScrollArea* m_scroll;
};

}  // namespace gx
