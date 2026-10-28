#pragma once
#include <QDialog>
#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QTimer>

#include "core/Lineart.h"

class QComboBox;
class QCheckBox;
class QLabel;

namespace gx {

class Document;
class ValueBar;
class ColorButton;

// Filter > Make line art: live preview while the settings change, result on a new
// transparent layer (or as editable linework).
class LineartDialog : public QDialog {
    Q_OBJECT
public:
    LineartDialog(Document* doc, QWidget* parent = nullptr);
    void accept() override;

private:
    void loadSource();
    void schedule();
    void startPreview();
    void applyPreset(int i);
    void syncControls();

    QPointer<Document> m_doc;
    LineartParams m_p;
    QImage m_source;      // full size, premultiplied
    QPoint m_sourceAt;    // where the source starts on the canvas
    QImage m_small;       // preview copy
    double m_scale = 1;   // m_small / m_source
    QImage m_result;
    QTimer m_timer;
    QFutureWatcher<QPair<QImage, int>> m_watch;
    bool m_dirty = false;
    int m_bg = 0;         // preview background: 0 white, 1 checker, 2 original faded

    QComboBox* m_sourceBox = nullptr;
    QComboBox* m_modeBox = nullptr;
    QLabel* m_autoInfo = nullptr;
    QWidget* m_view = nullptr;
    QCheckBox* m_hideSource = nullptr;
    QCheckBox* m_vector = nullptr;
    QList<std::function<void()>> m_loaders;  // refresh controls from m_p
    bool m_loading = false;
};

}  // namespace gx
