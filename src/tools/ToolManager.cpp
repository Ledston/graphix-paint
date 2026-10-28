#include "tools/ToolManager.h"

#include "app/App.h"
#include "app/BrushLibrary.h"
#include "app/Shortcuts.h"

namespace gx {

ToolManager::ToolManager(QObject* parent) : QObject(parent) {}

void ToolManager::addTool(Tool* t) {
    t->setParent(this);
    m_tools.insert(t->id(), t);
    m_list << t;
    connect(t, &Tool::overlayChanged, this, &ToolManager::overlayChanged);
    if (!m_current) m_current = t;
}

void ToolManager::setCurrent(const QString& id) {
    Tool* t = m_tools.value(id);
    if (!t || t == m_current) return;
    if (m_drag) return;  // never switch in the middle of a stroke
    if (m_current) {
        if (m_current->hasPending()) m_current->commitPending();
        m_current->deactivate();
    }
    m_current = t;
    m_current->activate();
    updateTemporary();
    Q_EMIT currentChanged(t);
    Q_EMIT activeChanged(active());
    Q_EMIT overlayChanged();
}

void ToolManager::selectBrushSlot(int index) {
    app()->brushes()->setCurrent(index);
    setCurrent("brush");
}

void ToolManager::commitPending() {
    if (m_current && m_current->hasPending()) m_current->commitPending();
}

void ToolManager::updateTemporary() {
    if (m_drag) return;
    Tool* old = m_temp;
    m_temp = nullptr;
    auto& sc = Shortcuts::instance();
    const bool modTool = m_current && (m_modTools.contains(m_current->id()) || m_current->hasPending());
    const bool busy = m_current && m_current->isBusy();
    if (!busy) {
        if (sc.holdActive("hold.rotate", m_keys, m_mods)) m_temp = tool("rotate");
        else if (sc.holdActive("hold.zoom", m_keys, m_mods)) m_temp = tool("zoom");
        else if (sc.holdActive("hold.pan", m_keys, m_mods)) m_temp = tool("hand");
        else if (!modTool && sc.holdActive("hold.brushsize", m_keys, m_mods)) m_temp = tool("brushsize");
        else if (!modTool && sc.holdActive("hold.move", m_keys, m_mods)) m_temp = tool("move");
        else if (!modTool && sc.holdActive("hold.eyedropper", m_keys, m_mods)) m_temp = tool("eyedropper");
    }
    if (m_temp == m_current) m_temp = nullptr;
    if (old != m_temp) {
        Q_EMIT activeChanged(active());
        Q_EMIT overlayChanged();
    }
}

void ToolManager::press(const PointerEvent& e) {
    if (m_drag) return;
    m_mods = e.mods;
    Tool* t = active();
    if (e.button == Qt::MiddleButton) t = tool("hand");
    else if (e.button == Qt::RightButton) {
        if (!app()->prefs().rightClickEyedropper) return;
        if (m_current && m_current->hasPending()) return;
        t = tool("eyedropper");
    }
    if (!t) return;
    m_drag = t;
    t->press(e);
}

void ToolManager::move(const PointerEvent& e) {
    if (m_drag) m_drag->move(e);
    else hover(e);
}

void ToolManager::release(const PointerEvent& e) {
    if (!m_drag) return;
    Tool* t = m_drag;
    m_drag = nullptr;
    t->release(e);
    updateTemporary();
    Q_EMIT activeChanged(active());
}

void ToolManager::hover(const PointerEvent& e) {
    if (Tool* t = active()) t->hover(e);
}

void ToolManager::doubleClick(const PointerEvent& e) {
    if (Tool* t = active()) t->doubleClick(e);
}

bool ToolManager::keyPress(QKeyEvent* e, CanvasView* v) {
    m_mods = e->modifiers();
    if (!e->isAutoRepeat()) m_keys.insert(e->key());
    Tool* kt = m_drag ? m_drag : m_current;
    if (kt && kt->keyPress(e, v)) return true;
    updateTemporary();
    return e->key() == Qt::Key_Space || e->key() == Qt::Key_Alt;
}

bool ToolManager::keyRelease(QKeyEvent* e, CanvasView*) {
    m_mods = e->modifiers();
    if (!e->isAutoRepeat()) m_keys.remove(e->key());
    updateTemporary();
    // Swallow Alt so it never opens the menu bar while picking colors.
    return e->key() == Qt::Key_Space || e->key() == Qt::Key_Alt;
}

void ToolManager::clearKeys() {
    m_keys.clear();
    m_mods = Qt::NoModifier;
    updateTemporary();
}

void ToolManager::drawOverlay(QPainter& p, CanvasView* v) {
    if (m_current && m_current != active()) m_current->drawOverlay(p, v);
    if (Tool* t = active()) t->drawOverlay(p, v);
}

QCursor ToolManager::cursor(CanvasView* v) const {
    Tool* t = active();
    return t ? t->cursor(v) : QCursor(Qt::ArrowCursor);
}

}  // namespace gx
