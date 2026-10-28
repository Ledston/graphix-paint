#pragma once
#include <QHash>
#include <QObject>
#include <QSet>

#include "tools/Tool.h"

namespace gx {

class ToolManager : public QObject {
    Q_OBJECT
public:
    explicit ToolManager(QObject* parent = nullptr);

    void addTool(Tool* t);
    QList<Tool*> tools() const { return m_list; }
    Tool* tool(const QString& id) const { return m_tools.value(id); }
    Tool* current() const { return m_current; }
    Tool* active() const { return m_drag ? m_drag : (m_temp ? m_temp : m_current); }
    void setCurrent(const QString& id);
    void selectBrushSlot(int index);
    // A tool that ignores held-key tool switching while it is selected
    void setModifierTool(const QString& id, bool usesModifiers) { if (usesModifiers) m_modTools.insert(id); else m_modTools.remove(id); }

    void press(const PointerEvent& e);
    void move(const PointerEvent& e);
    void release(const PointerEvent& e);
    void hover(const PointerEvent& e);
    void doubleClick(const PointerEvent& e);
    bool keyPress(QKeyEvent* e, CanvasView* v);
    bool keyRelease(QKeyEvent* e, CanvasView* v);
    void clearKeys();
    bool isDragging() const { return m_drag != nullptr; }
    const QSet<int>& heldKeys() const { return m_keys; }

    void drawOverlay(QPainter& p, CanvasView* v);
    QCursor cursor(CanvasView* v) const;
    void commitPending();  // applies pending edits of the current tool (before saving etc.)

Q_SIGNALS:
    void currentChanged(Tool* t);
    void activeChanged(Tool* t);
    void overlayChanged();

private:
    void updateTemporary();

    QHash<QString, Tool*> m_tools;
    QList<Tool*> m_list;
    Tool* m_current = nullptr;
    Tool* m_temp = nullptr;
    Tool* m_drag = nullptr;
    QSet<int> m_keys;
    Qt::KeyboardModifiers m_mods;
    QSet<QString> m_modTools;
};

}  // namespace gx
