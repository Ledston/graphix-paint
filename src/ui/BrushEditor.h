#pragma once
#include <QDialog>

#include "brush/BrushSettings.h"

class QLineEdit;
class QListWidget;
class QStackedWidget;
class QComboBox;

namespace gx {

class BrushControls;
class BrushPreviewWidget;
class BrushTestPad;

// All settings of one brush slot (double-click a brush): sections on the left, the
// settings in the middle, a sample stroke and a sheet to try the brush on the right.
// Changes apply right away; Cancel brings the brush back as it was.
class BrushEditorDialog : public QDialog {
    Q_OBJECT
public:
    BrushEditorDialog(int slot, QWidget* parent = nullptr);
    // opens (or raises) the window for a slot
    static void open(int slot, QWidget* parent);
    void reject() override;  // puts the brush back as it was

private:
    void apply();
    void updatePages();

    int m_slot;
    BrushSettings m_original;
    BrushControls* m_controls = nullptr;
    QListWidget* m_list = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLineEdit* m_name = nullptr;
    QComboBox* m_group = nullptr;
    BrushPreviewWidget* m_preview = nullptr;
    BrushTestPad* m_pad = nullptr;
    QList<int> m_pageRows;
};

}  // namespace gx
