#pragma once
#include <QCursor>
#include <QKeyEvent>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QPointer>

namespace gx {

class CanvasView;
class Document;
class Layer;

struct PointerEvent {
    QPointF widgetPos;
    QPointF pos;  // canvas coordinates
    double pressure = 1.0;
    double tiltX = 0, tiltY = 0, rotation = 0;
    double timeMs = 0;
    Qt::MouseButton button = Qt::LeftButton;
    Qt::MouseButtons buttons = Qt::LeftButton;
    Qt::KeyboardModifiers mods;
    bool tablet = false;
    bool eraser = false;  // eraser end of the pen
    CanvasView* view = nullptr;
};

class Tool : public QObject {
    Q_OBJECT
public:
    Tool(const QString& id, const QString& name, const QString& icon, QObject* parent = nullptr)
        : QObject(parent), m_id(id), m_name(name), m_icon(icon) {}

    QString id() const { return m_id; }
    QString name() const { return m_name; }  // English, use T()
    QString iconName() const { return m_icon; }

    virtual void activate() {}
    virtual void deactivate() {}
    virtual void press(const PointerEvent&) {}
    virtual void move(const PointerEvent&) {}
    virtual void release(const PointerEvent&) {}
    virtual void hover(const PointerEvent&) {}
    virtual void doubleClick(const PointerEvent&) {}
    // Enter / Escape / tool specific keys. Return true when handled.
    virtual bool keyPress(QKeyEvent*, CanvasView*) { return false; }
    virtual void drawOverlay(QPainter&, CanvasView*) {}
    virtual QCursor cursor(CanvasView*) const { return Qt::CrossCursor; }
    virtual bool showsBrushOutline() const { return false; }
    virtual double outlineDiameter() const { return 0; }  // canvas px
    // True while an operation is in progress (no temporary tool switching).
    virtual bool isBusy() const { return false; }
    // Pending edit (transform, warp, liquify...) that must be applied or dropped.
    virtual bool hasPending() const { return false; }
    virtual void commitPending() {}
    virtual void cancelPending() {}
    // Panel with the tool's settings (owned by the caller)
    virtual QWidget* createOptions(QWidget* parent) { Q_UNUSED(parent); return nullptr; }

Q_SIGNALS:
    void overlayChanged();
    void optionsChanged();

protected:
    QString m_id, m_name, m_icon;
};

}  // namespace gx
