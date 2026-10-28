#include "app/I18n.h"

#include <QFile>
#include <QHash>
#include <QLocale>
#include <QTextStream>

namespace gx {

namespace {
QHash<QString, QString>& table() {
    static QHash<QString, QString> t;
    return t;
}
QString& current() {
    static QString c = "en";
    return c;
}

void loadTable(const QString& code) {
    table().clear();
    if (code == "en") return;
    QFile f(QStringLiteral(":/i18n/%1.tsv").arg(code));
    if (!f.open(QIODevice::ReadOnly)) return;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    while (!ts.atEnd()) {
        QString line = ts.readLine();
        if (line.isEmpty() || line.startsWith('#')) continue;
        int tab = line.indexOf('\t');
        if (tab <= 0) continue;
        QString key = line.left(tab);
        QString val = line.mid(tab + 1);
        key.replace("\\n", "\n");
        val.replace("\\n", "\n");
        table().insert(key, val);
    }
}
}  // namespace

QString T(const char* english) { return T(QString::fromUtf8(english)); }

QString T(const QString& english) {
    if (table().isEmpty()) return english;
    auto it = table().constFind(english);
    if (it != table().cend()) return it.value();
    if (english.endsWith(QLatin1String("..."))) {  // "Curves..." from "Curves"
        auto base = table().constFind(english.chopped(3));
        if (base != table().cend()) return base.value() + QStringLiteral("...");
    }
    return english;
}

void setLanguage(const QString& code) {
    QString c = code;
    if (c.isEmpty() || c == "auto") c = QLocale::system().language() == QLocale::Russian ? "ru" : "en";
    current() = c;
    loadTable(c);
}

QString language() { return current(); }

}  // namespace gx
