#pragma once
#include <QElapsedTimer>
#include <QString>

class QTabletEvent;

namespace gx {

// Chooses how the pen talks to the program on Windows:
//  - "wintab": the classic tablet driver interface used by PaintTool SAI. Works with
//    Windows Ink switched off in the tablet driver.
//  - "ink":    Windows Ink (pointer messages).
// Wintab is switched on and off while the program runs; if it is not available
// (no Wintab driver installed) the program stays on Windows Ink.
class TabletInput {
public:
    // Applies the wanted interface and returns the one that is really in use.
    static QString apply(const QString& wanted);
    static QString active();          // "wintab" or "ink"
    static bool wintabAvailable();    // the driver provides Wintab
    static QString describe();        // human readable state for the settings window

    // When both interfaces deliver the same pen, only one may be used. Returns true
    // if this event comes from the other one and must be ignored.
    static bool shouldIgnore(const QTabletEvent* e);

private:
    static QString s_active;
    static QElapsedTimer s_lastWintab;
};

}  // namespace gx
