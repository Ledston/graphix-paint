#pragma once
#include <QDialog>

#include "app/Preferences.h"

namespace gx {

class CurveEditor;

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);
    void accept() override;

private:
    Preferences m_p;
    CurveEditor* m_curve = nullptr;
};

}  // namespace gx
