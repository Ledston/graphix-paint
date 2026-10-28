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
class Section;

// Grid of brush slots, like SAI's custom tool palette.
class BrushSlots : public QWidget {
    Q_OBJECT
public:
    explicit BrushSlots(QWidget* parent = nullptr);
    int heightForWidth(int w) const override;
    bool hasHeightForWidth() const override { return true; }
    QSize sizeHint() const override { return QSize(220, heightForWidth(220)); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
private:
    int cols() const;
    int indexAt(const QPoint& p) const;
    QRect cell(int i) const;
    void menu(int index, const QPoint& global);
};

// Every setting of the current brush slot.
class BrushSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit BrushSettingsWidget(QWidget* parent = nullptr);
private:
    void load();
    void store();
    void updateVisibility();
    ValueBar* bar(QWidget* parent, const QString& label, double min, double max, int dec, double* field,
                  double scale = 1, const QString& suffix = {}, bool log = false);
    QCheckBox* check(QWidget* parent, const QString& label, bool* field);

    BrushSettings m_s;
    bool m_loading = false;
    QList<std::function<void()>> m_loaders;
    QComboBox* m_engine;
    QComboBox* m_mode;
    QComboBox* m_shape;
    QComboBox* m_texture;
    Section* m_mixSection;
    QWidget* m_sizesRow;
    QLabel* m_title;
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
