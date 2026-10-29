#include "app/BrushLibrary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace gx {

BrushLibrary::BrushLibrary(const QString& file, QObject* parent) : QObject(parent), m_file(file) { load(); }

void BrushLibrary::setCurrent(int i) {
    i = std::clamp(i, 0, count() - 1);
    if (i == m_current) return;
    m_current = i;
    Q_EMIT currentChanged(i);
}

int BrushLibrary::eraserIndex() const {
    for (int i = 0; i < count(); ++i)
        if (m_slots[i].engine == BrushEngine::Erase) return i;
    return -1;
}

int BrushLibrary::indexOfName(const QString& name) const {
    for (int i = 0; i < count(); ++i)
        if (m_slots[i].name == name) return i;
    return -1;
}

void BrushLibrary::update(int i, const BrushSettings& s) {
    if (i < 0 || i >= count()) return;
    m_slots[i] = s;
    Q_EMIT brushChanged(i);
}

int BrushLibrary::add(const BrushSettings& s) {
    m_slots.push_back(s);
    Q_EMIT slotsChanged();
    return count() - 1;
}

int BrushLibrary::duplicate(int i) {
    if (i < 0 || i >= count()) return -1;
    BrushSettings s = m_slots[i];
    s.name += " 2";
    m_slots.insert(i + 1, s);
    Q_EMIT slotsChanged();
    return i + 1;
}

void BrushLibrary::remove(int i) {
    if (count() <= 1 || i < 0 || i >= count()) return;
    m_slots.remove(i);
    if (m_current >= count()) m_current = count() - 1;
    Q_EMIT slotsChanged();
    Q_EMIT currentChanged(m_current);
}

void BrushLibrary::move(int from, int to) {
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to) return;
    BrushSettings s = m_slots[from];
    m_slots.remove(from);
    m_slots.insert(to, s);
    if (m_current == from) m_current = to;
    Q_EMIT slotsChanged();
}

void BrushLibrary::resetToDefaults() {
    m_slots = BrushSettings::defaults();
    m_current = 0;
    Q_EMIT slotsChanged();
    Q_EMIT currentChanged(0);
}

void BrushLibrary::resetSlot(int i) {
    if (i < 0 || i >= count()) return;
    for (const auto& d : BrushSettings::defaults())
        if (d.name == m_slots[i].name) {
            m_slots[i] = d;
            Q_EMIT brushChanged(i);
            return;
        }
}

void BrushLibrary::save() const {
    QJsonArray arr;
    for (const auto& s : m_slots) arr.append(s.toJson());
    QJsonObject o{{"version", 2}, {"current", m_current}, {"brushes", arr}};
    QSaveFile f(m_file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        f.commit();
    }
}

void BrushLibrary::load() {
    m_slots.clear();
    QFile f(m_file);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        for (const auto& v : o.value("brushes").toArray()) m_slots.push_back(BrushSettings::fromJson(v.toObject()));
        m_current = o.value("current").toInt(0);
        // brushes added by newer versions are given to an existing palette once
        const int version = o.value("version").toInt(1);
        if (!m_slots.isEmpty() && version < 2)
            for (const BrushSettings& b : BrushSettings::defaultsSince(2))
                if (indexOfName(b.name) < 0) m_slots.push_back(b);
    }
    if (m_slots.isEmpty()) m_slots = BrushSettings::defaults();
    m_current = std::clamp(m_current, 0, count() - 1);
}

bool BrushLibrary::importFile(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    QJsonArray arr = d.isArray() ? d.array() : d.object().value("brushes").toArray();
    if (arr.isEmpty() && d.isObject() && d.object().contains("name")) arr.append(d.object());
    if (arr.isEmpty()) {
        if (error) *error = "No brushes found in the file.";
        return false;
    }
    for (const auto& v : arr) m_slots.push_back(BrushSettings::fromJson(v.toObject()));
    Q_EMIT slotsChanged();
    return true;
}

bool BrushLibrary::exportFile(const QString& path, const QList<int>& indices, QString* error) const {
    QJsonArray arr;
    for (int i : indices)
        if (i >= 0 && i < count()) arr.append(m_slots[i].toJson());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"brushes", arr}}).toJson());
    return f.commit();
}

}  // namespace gx
