#pragma once
#include <QString>

namespace gx {

// UI text translation. Source strings are English; a Russian table ships with
// the program. T() returns the English text when no translation exists.
QString T(const char* english);
QString T(const QString& english);

void setLanguage(const QString& code);  // "en", "ru" or "auto"
QString language();                     // resolved code

}  // namespace gx
