#pragma once
#include <QColor>
#include <QIcon>
#include <QJsonObject>
#include <QStringList>
#include <QString>

namespace gx {

struct ThemeColors {
    QColor window, panel, base, alt, text, dim, button, border, accent, accentText, danger;
    QColor workspace;            // around the canvas (the classic themes take it from the settings)
    int radius = 3;              // how round buttons, fields and panels are, px
    QString texture;             // a picture laid over the panels (custom theme)
    double textureOpacity = 0;   // 0..1
    double windowTextureOpacity = 0;  // the same picture over the window background, 0..1

    QJsonObject toJson() const;
    static ThemeColors fromJson(const QJsonObject& o);
};

class Theme {
public:
    // "dark", "gray", "light" (the classic ones), "soft", "lavender", "midnight", "custom"
    static QStringList names();
    static QString displayName(const QString& name);  // English, for T()
    static ThemeColors colorsFor(const QString& name);
    // the custom theme, kept in the settings (theme/custom)
    static ThemeColors customColors();
    static void saveCustom(const ThemeColors& c);
    // a theme from colors, without saving (the editor's live preview)
    static void applyColors(const ThemeColors& c, int fontSize = 0);
    static void apply(const QString& name, int fontSize = 0);
    static const ThemeColors& colors();
    static QString current();
    static QIcon icon(const QString& name);
    static QPixmap iconPixmap(const QString& name, int size, const QColor& color);
    static void clearIconCache();
};

}  // namespace gx
