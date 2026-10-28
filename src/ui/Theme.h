#pragma once
#include <QColor>
#include <QIcon>
#include <QString>

namespace gx {

struct ThemeColors {
    QColor window, panel, base, alt, text, dim, button, border, accent, accentText, danger;
};

class Theme {
public:
    static void apply(const QString& name, int fontSize = 0);
    static const ThemeColors& colors();
    static QString current();
    static QIcon icon(const QString& name);
    static QPixmap iconPixmap(const QString& name, int size, const QColor& color);
    static void clearIconCache();
};

}  // namespace gx
