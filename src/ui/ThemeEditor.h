#pragma once
#include <QDialog>

#include "ui/Theme.h"

class QLabel;

namespace gx {

// Settings > Interface > My theme: the colors of every part of the program, how round
// it is, and a picture over the panels. Every change shows on the whole program at once;
// Cancel brings the theme back as it was.
class ThemeEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThemeEditorDialog(QWidget* parent = nullptr);
    void accept() override;
    void reject() override;
    ThemeColors& colors() { return m_c; }
    void preview();  // shows m_c on the whole program
    static bool exportTheme(const ThemeColors& c, const QString& file, QString* error);
    static bool importTheme(const QString& file, ThemeColors* out, QString* error);

private:
    void rebuild();
    ThemeColors m_c;
    QString m_before;  // the theme before the editor opened
    QWidget* m_body = nullptr;
    QLabel* m_texName = nullptr;
};

}  // namespace gx
