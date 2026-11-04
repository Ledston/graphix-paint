#include "app/LayoutKeys.h"

#include <QAction>
#include <QApplication>
#include <QHash>
#include <QKeyEvent>
#include <QKeySequence>
#include <QWidget>

#include "app/Shortcuts.h"

namespace gx {

int LayoutKeys::latinKeyFor(int qtKey, quint32 nativeVirtualKey) {
    // only letters of other alphabets; Latin keys, digits and special keys stay as they are
    if (qtKey <= 0x7f || qtKey >= 0x01000000) return 0;
#ifdef Q_OS_WIN
    // Windows tells which physical key it was: the virtual key code does not depend on the layout
    if (nativeVirtualKey >= 0x41 && nativeVirtualKey <= 0x5a) return int(Qt::Key_A) + int(nativeVirtualKey - 0x41);
    if (nativeVirtualKey >= 0x30 && nativeVirtualKey <= 0x39) return int(Qt::Key_0) + int(nativeVirtualKey - 0x30);
    switch (nativeVirtualKey) {
    case 0xba: return Qt::Key_Semicolon;
    case 0xbb: return Qt::Key_Equal;
    case 0xbc: return Qt::Key_Comma;
    case 0xbd: return Qt::Key_Minus;
    case 0xbe: return Qt::Key_Period;
    case 0xbf: return Qt::Key_Slash;
    case 0xc0: return Qt::Key_QuoteLeft;
    case 0xdb: return Qt::Key_BracketLeft;
    case 0xdc: return Qt::Key_Backslash;
    case 0xdd: return Qt::Key_BracketRight;
    case 0xde: return Qt::Key_Apostrophe;
    default: break;
    }
#else
    Q_UNUSED(nativeVirtualKey);
#endif
    // Russian (and Ukrainian) layout on a QWERTY keyboard
    static const QHash<int, int> ru = {
        {0x0419, Qt::Key_Q}, {0x0426, Qt::Key_W}, {0x0423, Qt::Key_E}, {0x041a, Qt::Key_R}, {0x0415, Qt::Key_T},
        {0x041d, Qt::Key_Y}, {0x0413, Qt::Key_U}, {0x0428, Qt::Key_I}, {0x0429, Qt::Key_O}, {0x0417, Qt::Key_P},
        {0x0425, Qt::Key_BracketLeft}, {0x042a, Qt::Key_BracketRight}, {0x0424, Qt::Key_A}, {0x042b, Qt::Key_S},
        {0x0412, Qt::Key_D}, {0x0410, Qt::Key_F}, {0x041f, Qt::Key_G}, {0x0420, Qt::Key_H}, {0x041e, Qt::Key_J},
        {0x041b, Qt::Key_K}, {0x0414, Qt::Key_L}, {0x0416, Qt::Key_Semicolon}, {0x042d, Qt::Key_Apostrophe},
        {0x042f, Qt::Key_Z}, {0x0427, Qt::Key_X}, {0x0421, Qt::Key_C}, {0x041c, Qt::Key_V}, {0x0418, Qt::Key_B},
        {0x0422, Qt::Key_N}, {0x042c, Qt::Key_M}, {0x0411, Qt::Key_Comma}, {0x042e, Qt::Key_Period},
        {0x0401, Qt::Key_QuoteLeft},
        // Ukrainian letters on the Russian places
        {0x0406, Qt::Key_S}, {0x0407, Qt::Key_BracketRight}, {0x0404, Qt::Key_Apostrophe}, {0x0490, Qt::Key_QuoteLeft},
    };
    return ru.value(qtKey, 0);
}

int LayoutKeys::latinKey(const QKeyEvent* e) { return latinKeyFor(e->key(), e->nativeVirtualKey()); }

namespace {
bool typingInto(QWidget* w) {
    return w && (w->inherits("QLineEdit") || w->inherits("QTextEdit") || w->inherits("QPlainTextEdit") ||
                 w->inherits("QAbstractSpinBox") || w->inherits("QKeySequenceEdit"));
}
}  // namespace

bool LayoutKeys::eventFilter(QObject* o, QEvent* e) {
    const QEvent::Type t = e->type();
    if (t != QEvent::ShortcutOverride && t != QEvent::KeyPress && t != QEvent::KeyRelease) return false;
    auto* k = static_cast<QKeyEvent*>(e);
    if (t == QEvent::KeyRelease && m_eaten && k->key() == m_eaten) {
        if (!k->isAutoRepeat()) m_eaten = 0;
        return k->isAutoRepeat();  // the last release goes on: held keys (quick menu, hold-to-return) wait for it
    }
    if (t == QEvent::KeyPress && m_eaten && k->key() == m_eaten) return true;
    const int latin = latinKey(k);
    if (!latin) return false;
    if (typingInto(QApplication::focusWidget()) || QApplication::activePopupWidget()) return false;
    const Qt::KeyboardModifiers mods = k->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);

    if (t == QEvent::ShortcutOverride) {
        if (QApplication::activeModalWidget()) return false;
        // the same press asked again (Qt tries the shortcuts once more when the key event is delivered)
        if (m_eaten == k->key() && !k->isAutoRepeat()) {
            e->accept();
            return true;
        }
        const QKeySequence seq(QKeyCombination(mods, Qt::Key(latin)));
        for (const ShortcutInfo& s : Shortcuts::instance().all()) {
            if (s.hold || !s.action || s.seq != seq || !s.action->isEnabled()) continue;
            m_eaten = k->key();
            e->accept();
            // keys that pick a brush or a tool do not repeat while held
            if (!(k->isAutoRepeat() && s.action->property("noRepeat").toBool())) s.action->trigger();
            return true;
        }
        return false;
    }
    // not a command: the canvas (held keys, tool keys) gets the Latin key
    if (o->inherits("gx::CanvasView")) {
        QKeyEvent latinEvent(t, latin, k->modifiers(), k->nativeScanCode(), k->nativeVirtualKey(), k->nativeModifiers(), k->text(),
                             k->isAutoRepeat(), ushort(k->count()));
        QCoreApplication::sendEvent(o, &latinEvent);
        return true;
    }
    return false;
}

}  // namespace gx
