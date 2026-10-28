#pragma once
#include <QObject>
#include <QVector>

#include "brush/BrushSettings.h"

namespace gx {

// The brush slots of the tool palette (like SAI's custom tools). Saved to brushes.json.
class BrushLibrary : public QObject {
    Q_OBJECT
public:
    explicit BrushLibrary(const QString& file, QObject* parent = nullptr);

    int count() const { return int(m_slots.size()); }
    const BrushSettings& at(int i) const { return m_slots[std::clamp(i, 0, count() - 1)]; }
    int currentIndex() const { return m_current; }
    const BrushSettings& current() const { return at(m_current); }
    void setCurrent(int i);
    int eraserIndex() const;  // slot used by the eraser end of the pen
    int indexOfName(const QString& name) const;

    void update(int i, const BrushSettings& s);  // live edit
    int add(const BrushSettings& s);
    int duplicate(int i);
    void remove(int i);
    void move(int from, int to);
    void resetToDefaults();
    void resetSlot(int i);

    bool importFile(const QString& path, QString* error);
    bool exportFile(const QString& path, const QList<int>& indices, QString* error) const;

    void save() const;
    void load();

Q_SIGNALS:
    void slotsChanged();
    void currentChanged(int index);
    void brushChanged(int index);

private:
    QString m_file;
    QVector<BrushSettings> m_slots;
    int m_current = 0;
};

}  // namespace gx
