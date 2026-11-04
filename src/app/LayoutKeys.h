#pragma once
#include <QObject>

class QKeyEvent;

namespace gx {

// Keyboard shortcuts by the physical key, whatever the keyboard layout is.
// With a Russian (or any non-Latin) layout Qt sees "Х" instead of "[", "Ц" instead of "W"...
// and the shortcuts stop working. This filter maps such keys back to the Latin key on the
// same place and runs the shortcut (or gives the canvas the Latin key for held keys).
class LayoutKeys : public QObject {
    Q_OBJECT
public:
    explicit LayoutKeys(QObject* parent = nullptr) : QObject(parent) {}
    // The Latin key on the same physical place, or 0 when the key is already Latin / unknown.
    static int latinKey(const QKeyEvent* e);
    static int latinKeyFor(int qtKey, quint32 nativeVirtualKey = 0);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    int m_eaten = 0;  // the key whose press / release this filter swallows
};

}  // namespace gx
