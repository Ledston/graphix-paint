#include "ui/MainWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QDir>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QActionGroup>
#include <QProcess>
#include <QLineEdit>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QDoubleSpinBox>
#include <atomic>

#include "app/App.h"
#include "app/TabletInput.h"
#include "app/Recorder.h"
#include "io/VideoWriter.h"
#include "app/BrushLibrary.h"
#include "app/I18n.h"
#include "app/Shortcuts.h"
#include "core/Compositor.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"
#include "core/Textures.h"
#include "core/LayerOps.h"
#include "io/ImageIO.h"
#include "io/NativeFormat.h"
#include "io/Recovery.h"
#include "tools/BrushTool.h"
#include "tools/FillTools.h"
#include "tools/MoveTool.h"
#include "tools/NavTools.h"
#include "tools/RulerTool.h"
#include "tools/FocusLinesTool.h"
#include "tools/SelectTools.h"
#include "tools/TextTool.h"
#include "tools/ToolManager.h"
#include "tools/TransformTool.h"
#include "tools/VectorTools.h"
#include "ui/AdjustmentDialog.h"
#include "ui/CanvasView.h"
#include "ui/ColorPanel.h"
#include "ui/Dialogs.h"
#include "ui/CommandPalette.h"
#include "ui/EffectFilterDialog.h"
#include "ui/EffectsDialog.h"
#include "ui/HistoryPanel.h"
#include "ui/LayerPanel.h"
#include "ui/LineartDialog.h"
#include "ui/PreviewPanel.h"
#include "ui/ColorRangeDialog.h"
#include "ui/NavigatorPanel.h"
#include "ui/PreferencesDialog.h"
#include "ui/ReferencePanel.h"
#include "ui/MaterialsPanel.h"
#include "app/MaterialActions.h"
#include "app/Materials.h"
#include "ui/ScratchpadPanel.h"
#include "ui/ShortcutsDialog.h"
#include "ui/Theme.h"
#include "ui/ToolPanel.h"

namespace gx {

namespace {

// A sub window that asks before closing the last view of a modified document.
class CanvasWindow : public QMdiSubWindow {
public:
    CanvasWindow(MainWindow* mw, CanvasView* v) : m_mw(mw), m_view(v) {
        setWidget(v);
        setAttribute(Qt::WA_DeleteOnClose);
    }
    CanvasView* view() const { return m_view; }
    std::function<bool(CanvasView*)> canClose;

protected:
    void closeEvent(QCloseEvent* e) override {
        if (canClose && !canClose(m_view)) {
            e->ignore();
            return;
        }
        QMdiSubWindow::closeEvent(e);
    }

private:
    MainWindow* m_mw;
    CanvasView* m_view;
};

QString formatSize(const QSize& s) { return QStringLiteral("%1 x %2 px").arg(s.width()).arg(s.height()); }

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("GraphiX Paint");
    setAcceptDrops(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
    setDockNestingEnabled(true);

    m_mdi = new QMdiArea(this);
    m_mdi->setViewMode(QMdiArea::TabbedView);
    m_mdi->setTabsClosable(true);
    m_mdi->setTabsMovable(true);
    m_mdi->setDocumentMode(true);
    m_mdi->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_mdi->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setCentralWidget(m_mdi);
    connect(m_mdi, &QMdiArea::subWindowActivated, this, [this](QMdiSubWindow* w) {
        auto* cw = dynamic_cast<CanvasWindow*>(w);
        app()->setActiveView(cw ? cw->view() : nullptr);
        updateActions();
        updateTitle();
    });

    createTools();
    createActions();
    createMenus();
    createToolbar();
    createDocks();
    createStatusBar();
    Shortcuts::instance().load(app()->settings());
    Recorder::instance().setWindow(this);

    QSettings& s = app()->settings();
    if (!restoreGeometry(s.value("window/geometry").toByteArray())) {
        QRect avail = screen() ? screen()->availableGeometry() : QRect(0, 0, 1600, 900);
        resize(avail.size() * 0.9);
        move(avail.center() - rect().center());
    }
    m_defaultState = saveState(2);  // for Window > Reset the interface
    restoreState(s.value("window/state").toByteArray(), 2);
    const QStringList folded = s.value("window/folded").toStringList();
    for (QDockWidget* d : m_docks)
        if (folded.contains(d->objectName())) setDockFolded(d, true);

    connect(app(), &App::statusMessage, this, [this](const QString& t, int ms) {
        m_statusMsg->setText(t);
        QTimer::singleShot(ms, m_statusMsg, [l = QPointer<QLabel>(m_statusMsg), t] { if (l && l->text() == t) l->clear(); });
    });
    connect(&Recovery::instance(), &Recovery::saved, this, [](const QString& title) {
        app()->showStatus(T("Recovery copy saved: %1").arg(title), 2500);
    });
    connect(app(), &App::prefsChanged, this, [this] {
        const Preferences& p = app()->prefs();
        Recovery::instance().setInterval(p.autosaveMinutes);
        if (TabletInput::active() != p.tabletApi) TabletInput::apply(p.tabletApi);
        for (QToolBar* tb : findChildren<QToolBar*>()) tb->setIconSize(QSize(p.iconSize - 2, p.iconSize - 2));
        while (m_recentMenu && app()->settings().value("recent").toStringList().size() > p.recentCount) {
            QStringList r = app()->settings().value("recent").toStringList();
            r.removeLast();
            app()->settings().setValue("recent", r);
        }
        updateRecentMenu();
        for (QMdiSubWindow* sw : m_mdi->subWindowList())
            if (auto* cw = dynamic_cast<CanvasWindow*>(sw)) cw->view()->doc()->setUndoLimit(app()->prefs().undoLimit);
    });
    qApp->installEventFilter(this);
    updateActions();
}

MainWindow::~MainWindow() = default;

// ============================================================================ tools

void MainWindow::createTools() {
    ToolManager* tm = app()->tools();
    tm->addTool(new BrushTool);
    tm->addTool(new HandTool);
    tm->addTool(new ZoomTool);
    tm->addTool(new RotateTool);
    tm->addTool(new BrushSizeTool);
    tm->addTool(new EyedropperTool);
    tm->addTool(new MoveTool);
    tm->addTool(new RectSelectTool);
    tm->addTool(new EllipseSelectTool);
    tm->addTool(new LassoSelectTool);
    tm->addTool(new MagicWandTool);
    tm->addTool(new SelectionBrushTool);
    tm->addTool(new FillTool);
    tm->addTool(new LassoFillTool);
    tm->addTool(new GradientTool);
    tm->addTool(new TransformTool);
    tm->addTool(new WarpTool);
    tm->addTool(new LiquifyTool);
    tm->addTool(new TextTool);
    tm->addTool(new VectorPenTool);
    tm->addTool(new CurveTool);
    tm->addTool(new VectorEditTool);
    tm->addTool(new VectorPressureTool);
    tm->addTool(new VectorWeightTool);
    tm->addTool(new VectorColorTool);
    tm->addTool(new VectorEraserTool);
    tm->addTool(new RulerTool);
    tm->addTool(new FocusLinesTool(FocusLines::Focus));
    tm->addTool(new FocusLinesTool(FocusLines::Speed));
    for (const char* id : {"select-rect", "select-ellipse", "lasso", "magic-wand", "transform", "warp", "gradient", "vedit",
                           "vpressure", "vweight", "ruler", "curve", "liquify", "text", "select-brush"})
        tm->setModifierTool(id, true);
    tm->setCurrent("brush");

    auto& sc = Shortcuts::instance();
    sc.registerHold("hold.pan", "Canvas (hold)", "Pan the canvas", QKeySequence(Qt::Key_Space));
    sc.registerHold("hold.zoom", "Canvas (hold)", "Zoom by dragging", QKeySequence(Qt::CTRL | Qt::Key_Space));
    sc.registerHold("hold.rotate", "Canvas (hold)", "Rotate the canvas by dragging", QKeySequence(Qt::ALT | Qt::Key_Space));
    sc.registerHold("hold.eyedropper", "Canvas (hold)", "Eyedropper", QKeySequence(Qt::Key_Alt));
    sc.registerHold("hold.move", "Canvas (hold)", "Move layer", QKeySequence(Qt::Key_Control));
    sc.registerHold("hold.brushsize", "Canvas (hold)", "Change brush size by dragging", QKeySequence(Qt::CTRL | Qt::Key_Alt));
}

// ============================================================================ actions

QAction* MainWindow::act(const QString& id, const QString& text, const QString& category, const QKeySequence& key,
                         std::function<void()> fn, const QString& icon, bool needsDoc) {
    QString label = T(text);
    if (label == text && text.endsWith("...")) label = T(text.chopped(3)) + "...";  // "Curves..." from "Curves"
    auto* a = new QAction(label, this);
    a->setProperty("en", text);
    if (!icon.isEmpty()) a->setIcon(Theme::icon(icon));
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    Shortcuts::instance().registerAction(a, id, category, key);
    addAction(a);
    m_actions.insert(id, a);
    if (needsDoc) m_docActions << a;
    return a;
}

void MainWindow::createActions() {
    auto doc = [this] { return activeDocument(); };
    auto view = [this] { return activeView(); };
    auto cur = [this]() -> Layer* { return activeDocument() ? activeDocument()->currentLayer() : nullptr; };
    auto commit = [] { app()->tools()->commitPending(); };

    // File
    act("file.new", "New...", "File", QKeySequence::New, [this] { newDocument(); }, "plus", false);
    act("file.open", "Open...", "File", QKeySequence::Open, [this] { openDialog(); }, {}, false);
    act("file.save", "Save", "File", QKeySequence::Save, [this, commit] { commit(); save(activeDocument(), false); });
    act("file.saveas", "Save as...", "File", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), [this, commit] { commit(); save(activeDocument(), true); });
    act("file.export", "Export...", "File", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S), [this, commit] { commit(); exportDialog(); });
    act("file.import", "Import as layer...", "File", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), [this] { importAsLayer(); });
    // ---- video recording
    act("rec.start", "Start recording", "Recording", {}, [this] { startRecording(); }, "record");
    act("rec.pause", "Pause recording", "Recording", {}, [this] { pauseRecording(); }, "pause", false);
    act("rec.toggle", "Record / pause", "Recording", QKeySequence(Qt::Key_F9), [this] {
        if (Recorder::instance().recordingDocument()) pauseRecording();
        else startRecording();
    }, {}, false);
    act("rec.stop", "Stop and save the video...", "Recording", QKeySequence(Qt::SHIFT | Qt::Key_F9), [this] { stopRecording(); }, "stop", false);
    act("rec.discard", "Delete the recording", "Recording", {}, [this] {
        Document* d = Recorder::instance().recordingDocument();
        if (!d) d = activeDocument();
        if (!d || Recorder::instance().state(d) == Recorder::State::None) return;
        if (QMessageBox::question(this, T("Video recording"), T("Delete the recording of this picture (%1)? The picture itself stays.")
                                                                  .arg(Recorder::formatLength(Recorder::instance().length(d)))) == QMessageBox::Yes)
            Recorder::instance().discard(d);
    }, "trash", false);
    act("rec.folder", "Open the recordings folder", "Recording", {}, [] {
        QString f = app()->dataDir() + "/recordings";
        QDir().mkpath(f);
        QDesktopServices::openUrl(QUrl::fromLocalFile(f));
    }, {}, false);
    {
        auto* group = new QActionGroup(this);
        QAction* canvas = act("rec.mode.canvas", "Record only the canvas", "Recording", {}, [] { Recorder::instance().setNewMode(Recorder::Mode::Canvas); }, {}, false);
        QAction* window = act("rec.mode.window", "Record the program window", "Recording", {}, [] { Recorder::instance().setNewMode(Recorder::Mode::Window); }, {}, false);
        for (QAction* a : {canvas, window}) {
            a->setCheckable(true);
            group->addAction(a);
        }
        auto syncMode = [this, canvas, window] {
            Document* d = Recorder::instance().recordingDocument();
            if (!d) d = activeDocument();
            Recorder& r = Recorder::instance();
            // a started recording keeps its mode
            const bool fixed = d && r.state(d) != Recorder::State::None;
            const Recorder::Mode m = fixed ? r.mode(d) : r.newMode();
            canvas->setChecked(m == Recorder::Mode::Canvas);
            window->setChecked(m == Recorder::Mode::Window);
            canvas->setEnabled(!fixed);
            window->setEnabled(!fixed);
        };
        connect(&Recorder::instance(), &Recorder::changed, this, syncMode);
        connect(app(), &App::activeViewChanged, this, syncMode);
        syncMode();
    }
    act("file.close", "Close", "File", QKeySequence(Qt::CTRL | Qt::Key_W), [this] { if (m_mdi->activeSubWindow()) m_mdi->activeSubWindow()->close(); });
    act("file.quit", "Exit", "File", QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { close(); }, {}, false);

    // Edit
    act("edit.undo", "Undo", "Edit", QKeySequence::Undo, [doc, commit] { commit(); if (auto* d = doc()) d->undo(); }, "undo");
    act("edit.redo", "Redo", "Edit", QKeySequence(Qt::CTRL | Qt::Key_Y), [doc] { if (auto* d = doc()) d->redo(); }, "redo");
    act("edit.redo2", "Redo (alternative)", "Edit", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [doc] { if (auto* d = doc()) d->redo(); });
    act("edit.cut", "Cut", "Edit", QKeySequence::Cut, [this] { copy(false, true); });
    act("edit.copy", "Copy", "Edit", QKeySequence::Copy, [this] { copy(false, false); });
    act("edit.copymerged", "Copy merged", "Edit", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), [this] { copy(true, false); });
    act("edit.paste", "Paste as new layer", "Edit", QKeySequence::Paste, [this] { paste(false); }, {}, false);
    act("edit.pastenew", "Paste as new image", "Edit", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), [this] { paste(true); }, {}, false);
    act("edit.clear", "Clear", "Edit", QKeySequence(Qt::Key_Delete), [doc, cur] { if (auto* l = cur()) ops::clearLayer(*doc(), l->id); }, "clear");
    act("edit.fill", "Fill with main color", "Edit", QKeySequence(Qt::ALT | Qt::Key_Backspace), [this] { fill(); }, "fill");
    act("edit.fillpattern", "Fill with pattern", "Edit", QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Backspace), [this] { fillPattern(); }, "pattern");
    act("edit.stroke", "Stroke selection...", "Edit", {}, [this] { strokeSelectionDialog(); }, "stroke");
    act("edit.transform", "Free transform", "Edit", QKeySequence(Qt::CTRL | Qt::Key_T), [] { app()->tools()->setCurrent("transform"); }, "transform");
    act("edit.warp", "Mesh warp", "Edit", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), [] { app()->tools()->setCurrent("warp"); }, "warp");
    act("edit.liquify", "Liquify", "Edit", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X), [] { app()->tools()->setCurrent("liquify"); }, "liquify");
    act("edit.prefs", "Program settings...", "Settings", QKeySequence(Qt::CTRL | Qt::Key_K), [this] { preferences(); }, "settings", false);
    act("edit.shortcuts", "Keyboard shortcuts...", "Settings", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_K), [this] { shortcutsDialog(); }, "keyboard", false);
    act("settings.pen", "Pen and tablet...", "Settings", {}, [this] { preferences(int(PreferencesDialog::Pen)); }, "tablet", false);

    // Canvas
    act("canvas.size", "Canvas size...", "Canvas", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C), [this] { canvasSize(); });
    act("canvas.imagesize", "Image size...", "Canvas", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I), [this] { imageSize(); });
    act("canvas.rotcw", "Rotate 90 degrees clockwise", "Canvas", {}, [doc, commit] { commit(); ops::rotateCanvas(*doc(), 1); });
    act("canvas.rotccw", "Rotate 90 degrees counter-clockwise", "Canvas", {}, [doc, commit] { commit(); ops::rotateCanvas(*doc(), 3); });
    act("canvas.rot180", "Rotate 180 degrees", "Canvas", {}, [doc, commit] { commit(); ops::rotateCanvas(*doc(), 2); });
    act("canvas.fliph", "Flip canvas horizontally", "Canvas", {}, [doc, commit] { commit(); ops::flipCanvas(*doc(), true); });
    act("canvas.flipv", "Flip canvas vertically", "Canvas", {}, [doc, commit] { commit(); ops::flipCanvas(*doc(), false); });
    act("canvas.crop", "Crop to selection", "Canvas", {}, [doc, commit] { commit(); ops::cropToSelection(*doc()); });
    act("canvas.trim", "Trim transparent edges", "Canvas", {}, [doc, commit] { commit(); ops::trimCanvas(*doc()); });

    // Layer
    act("layer.new", "New layer", "Layer", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [doc] { ops::addRasterLayer(*doc()); }, "layer-new");
    act("layer.newvector", "New linework layer", "Layer", {}, [doc] { ops::addVectorLayer(*doc()); }, "linework-new");
    act("layer.newfolder", "New folder", "Layer", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), [doc] { ops::addGroup(*doc()); }, "folder-new");
    act("layer.duplicate", "Duplicate layer", "Layer", QKeySequence(Qt::CTRL | Qt::Key_J), [doc] { ops::duplicateLayers(*doc(), doc()->selectedLayerIds()); }, "duplicate");
    act("layer.delete", "Delete layer", "Layer", QKeySequence(Qt::CTRL | Qt::Key_Delete), [doc] { ops::deleteLayers(*doc(), doc()->selectedLayerIds()); }, "trash");
    act("layer.mergedown", "Merge down", "Layer", QKeySequence(Qt::CTRL | Qt::Key_E), [doc, cur] {
        QString err;
        if (doc()->selectedCount() > 1) {
            if (!ops::mergeLayers(*doc(), doc()->selectedLayerIds(), &err) && !err.isEmpty()) app()->showStatus(T(err));
        } else if (auto* l = cur()) {
            if (!ops::mergeDown(*doc(), l->id, &err) && !err.isEmpty()) app()->showStatus(T(err));
        }
    }, "merge");
    act("layer.mergeselected", "Merge selected layers", "Layer", {}, [doc] {
        QString err;
        if (!ops::mergeLayers(*doc(), doc()->selectedLayerIds(), &err) && !err.isEmpty()) app()->showStatus(T(err));
    });
    act("layer.mergevisible", "Merge visible", "Layer", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), [doc] { ops::mergeVisible(*doc()); });
    act("layer.flatten", "Flatten image", "Layer", {}, [doc] { ops::flatten(*doc()); });
    act("layer.group", "Put into a new folder", "Layer", QKeySequence(Qt::CTRL | Qt::Key_G), [doc] { ops::groupLayers(*doc(), doc()->selectedLayerIds()); });
    act("layer.rasterize", "Rasterize layer", "Layer", {}, [doc, cur] { if (auto* l = cur()) ops::rasterize(*doc(), l->id); });
    act("layer.up", "Move layer up", "Layer", QKeySequence(Qt::CTRL | Qt::Key_BracketRight), [doc, cur] { if (auto* l = cur()) ops::moveLayerUp(*doc(), l->id); }, "up");
    act("layer.down", "Move layer down", "Layer", QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), [doc, cur] { if (auto* l = cur()) ops::moveLayerDown(*doc(), l->id); }, "down");
    act("layer.clip", "Toggle clipping", "Layer", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G), [doc, cur] {
        if (auto* l = cur()) {
            const bool on = !l->clipping;
            doc()->pushUndo(T("Clipping"));
            for (quint32 id : doc()->selectedLayerIds())
                if (Layer* x = doc()->layer(id)) { x->clipping = on; doc()->layerChanged(x); }
            doc()->layersChanged();
        }
    }, "clip");
    act("layer.alphalock", "Toggle preserve opacity", "Layer", QKeySequence(Qt::Key_Slash), [doc, cur] {
        if (auto* l = cur()) { doc()->pushUndo(T("Preserve opacity")); l = cur(); l->alphaLock = !l->alphaLock; doc()->layerChanged(l); doc()->layersChanged(); }
    }, "alpha-lock");
    act("layer.hide", "Show / hide layer", "Layer", {}, [doc, cur] {
        if (auto* l = cur()) ops::setLayersVisible(*doc(), doc()->selectedLayerIds(), !l->visible);
    }, "eye");
    act("layer.fliph", "Flip layer horizontally", "Layer", {}, [doc, cur] { if (auto* l = cur()) ops::flipLayer(*doc(), l->id, true); });
    act("layer.flipv", "Flip layer vertically", "Layer", {}, [doc, cur] { if (auto* l = cur()) ops::flipLayer(*doc(), l->id, false); });
    act("layer.mask", "Add layer mask", "Layer", {}, [doc, cur] { if (auto* l = cur()) ops::addMask(*doc(), l->id, doc()->hasSelection()); }, "mask");
    act("layer.applymask", "Apply layer mask", "Layer", {}, [doc, cur] { if (auto* l = cur()) ops::applyMask(*doc(), l->id); });
    act("layer.deletemask", "Delete layer mask", "Layer", {}, [doc, cur] { if (auto* l = cur()) ops::deleteMask(*doc(), l->id); });
    auto curRaster = [cur] { return dynamic_cast<RasterLayer*>(cur()); };
    act("layer.properties", "Layer properties...", "Layer", {}, [this, cur] {
        if (auto* l = cur()) QMetaObject::invokeMethod(m_layers, "openPropertiesSlot", Q_ARG(quint32, l->id));
    }, "settings");
    act("layer.reference", "Reference layer (fills take borders from it)", "Layer", {}, [doc, cur] {
        if (auto* l = cur()) {
            const bool on = !l->reference;
            doc()->pushUndo(T("Reference layer"));
            for (quint32 id : doc()->selectedLayerIds())
                if (Layer* x = doc()->layer(id)) x->reference = on;
            doc()->layersChanged();
            app()->showStatus(on ? T("Fills can now take their borders from this layer.") : T("Not a reference layer any more."));
        }
    }, "reference");
    act("layer.chromatic", "Chromatic aberration", "Layer", {}, [doc, cur] {
        if (auto* l = cur(); l && !l->isGroup() && l->type() != LayerType::Adjustment) {
            doc()->pushUndo(T("Chromatic aberration"));
            l->fx.chromatic = !l->fx.chromatic;
            doc()->layerChanged(l);
            doc()->layersChanged();
        }
    }, "channels");
    act("layer.alphamask", "Transparency to layer mask", "Layer", {}, [doc, curRaster] { if (auto* l = curRaster()) ops::transparencyToMask(*doc(), l->id); });
    act("layer.alphalayer", "Alpha channel as a gray layer", "Layer", {}, [doc, curRaster] { if (auto* l = curRaster()) ops::alphaAsLayer(*doc(), l->id); });
    act("layer.invertalpha", "Invert alpha", "Layer", {}, [doc, curRaster] { if (auto* l = curRaster()) ops::invertAlpha(*doc(), l->id); });
    act("layer.split", "Split into red, green and blue layers", "Layer", {}, [doc, curRaster] { if (auto* l = curRaster()) ops::splitChannels(*doc(), l->id); });
    act("layer.removewhite", "Remove white background", "Layer", {}, [doc, curRaster] { if (auto* l = curRaster()) ops::removeWhite(*doc(), l->id); });
    act("layer.recolor", "Recolor with the main color", "Layer", {}, [doc, curRaster] { if (auto* l = curRaster()) ops::recolorLayer(*doc(), l->id, app()->foreground()); });
    act("layer.copysel", "New layer from selection (copy)", "Layer", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_J), [doc] { ops::layerFromSelection(*doc(), false); });
    act("layer.cutsel", "New layer from selection (cut)", "Layer", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J), [doc] { ops::layerFromSelection(*doc(), true); });
    act("layer.flats", "Auto-fill colors (flats)...", "Layer", {}, [this] { autoFlatsDialog(); }, "fill");
    act("layer.center", "Center on canvas", "Layer", {}, [doc, cur] { if (auto* l = cur()) ops::centerLayer(*doc(), l->id); });
    act("layer.effects", "Layer effects...", "Layer", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), [this, cur] { if (auto* l = cur()) layerEffects(l->id, {}); }, "fx");

    // Select
    act("select.all", "Select all", "Select", QKeySequence::SelectAll, [doc] { ops::selectAll(*doc()); });
    act("select.colorrange", "Color range...", "Select", {}, [this, doc] {
        app()->tools()->commitPending();
        ColorRangeDialog dlg(doc(), this);
        dlg.exec();
    }, "magic-wand");
    act("select.none", "Deselect", "Select", QKeySequence(Qt::CTRL | Qt::Key_D), [doc, commit] { commit(); ops::deselect(*doc()); }, "deselect");
    act("select.reselect", "Reselect", "Select", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), [doc] { ops::reselect(*doc()); });
    act("select.invert", "Invert selection", "Select", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), [doc] { ops::invertSelection(*doc()); }, "invert");
    act("select.alpha", "Select layer opacity", "Select", {}, [doc, cur] { if (auto* l = cur()) ops::selectLayerAlpha(*doc(), l->id); });
    act("select.grow", "Expand selection...", "Select", {}, [this, doc] {
        int px = Dialogs::askInt(this, T("Expand selection"), T("Pixels:"), 4, 1, 500);
        if (px > 0) ops::growSelection(*doc(), px);
    });
    act("select.shrink", "Contract selection...", "Select", {}, [this, doc] {
        int px = Dialogs::askInt(this, T("Contract selection"), T("Pixels:"), 4, 1, 500);
        if (px > 0) ops::growSelection(*doc(), -px);
    });
    act("select.feather", "Feather selection...", "Select", {}, [this, doc] {
        int px = Dialogs::askInt(this, T("Feather selection"), T("Radius in pixels:"), 6, 1, 500);
        if (px > 0) ops::featherSelection(*doc(), px);
    });
    act("select.border", "Border selection...", "Select", {}, [this, doc] {
        int px = Dialogs::askInt(this, T("Border selection"), T("Width in pixels:"), 4, 1, 500);
        if (px > 0) ops::borderSelection(*doc(), px);
    });
    act("select.show", "Show selection edges", "Select", QKeySequence(Qt::CTRL | Qt::Key_H), [doc] {
        if (auto* d = doc()) { d->showSelection = !d->showSelection; d->selectionModified(); }
    });

    // Filter & adjustments
    const QList<QPair<int, QKeySequence>> adj = {
        {int(AdjustmentType::BrightnessContrast), {}},
        {int(AdjustmentType::HueSaturation), QKeySequence(Qt::CTRL | Qt::Key_U)},
        {int(AdjustmentType::Levels), QKeySequence(Qt::CTRL | Qt::Key_L)},
        {int(AdjustmentType::Curves), QKeySequence(Qt::CTRL | Qt::Key_M)},
        {int(AdjustmentType::ColorBalance), QKeySequence(Qt::CTRL | Qt::Key_B)},
        {int(AdjustmentType::Exposure), {}},
        {int(AdjustmentType::Vibrance), {}},
        {int(AdjustmentType::GradientMap), {}},
        {int(AdjustmentType::Posterize), {}},
        {int(AdjustmentType::Threshold), {}},
        {int(AdjustmentType::Desaturate), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U)},
        {int(AdjustmentType::Invert), QKeySequence(Qt::CTRL | Qt::Key_I)},
    };
    for (const auto& a : adj) {
        int t = a.first;
        act(QStringLiteral("adjust.%1").arg(t), adjustmentTypeName(AdjustmentType(t)) + "...", "Adjustments", a.second,
            [this, t] { adjustment(t, false); });
        act(QStringLiteral("adjustlayer.%1").arg(t), adjustmentTypeName(AdjustmentType(t)), "Adjustment layers", {},
            [this, t] { adjustment(t, true); });
    }
    for (const auto& f : AdjustmentDialog::filterIds()) {
        QString id = f.first;
        act("filter." + id, f.second + "...", "Filters", {}, [this, id] { filter(id); });
    }
    // layer effects as filters: set up with sliders, then drawn into the pixels
    for (const char* key : {"chromatic", "outline", "shadow", "outerGlow", "innerShadow", "innerGlow", "overlay", "gradient", "texture"}) {
        QString k = key, name;
        for (const auto& e : effectKeys())
            if (e.first == k) name = e.second;
        act("filter.fx." + k, name + "...", "Filters", {}, [this, k] { effectFilter(k); });
    }
    act("filter.lineart", "Make line art...", "Filters", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), [this] {
        Document* d = activeDocument();
        if (!d) return;
        app()->tools()->commitPending();
        LineartDialog dlg(d, this);
        dlg.exec();
    }, "lineart");

    // View
    act("view.newview", "New view window", "View", {}, [this] { newView(); });
    act("view.zoomin", "Zoom in", "View", QKeySequence(Qt::CTRL | Qt::Key_Plus), [view] { view()->zoomIn(); }, "plus");
    act("view.zoomin2", "Zoom in (=)", "View", QKeySequence(Qt::CTRL | Qt::Key_Equal), [view] { view()->zoomIn(); });
    act("view.zoomout", "Zoom out", "View", QKeySequence(Qt::CTRL | Qt::Key_Minus), [view] { view()->zoomOut(); }, "minus");
    act("view.fit", "Fit to window", "View", QKeySequence(Qt::CTRL | Qt::Key_0), [view] { view()->fitToWindow(); }, "zoom");
    act("view.actual", "Actual pixels", "View", QKeySequence(Qt::CTRL | Qt::Key_1), [view] { view()->actualPixels(); }, "image");
    act("view.rotl", "Rotate view left", "View", QKeySequence(Qt::Key_Minus), [view] { view()->rotateBy(-app()->prefs().rotateStepDeg); });
    act("view.rotr", "Rotate view right", "View", QKeySequence(Qt::Key_Equal), [view] { view()->rotateBy(app()->prefs().rotateStepDeg); });
    act("view.rotreset", "Reset rotation", "View", QKeySequence(Qt::Key_Backspace), [view] { view()->setRotation(0); }, "rotate");
    act("view.reset", "Reset view", "View", QKeySequence(Qt::Key_Home), [view] { view()->resetView(); }, "reset");
    act("view.flip", "Flip view horizontally", "View", QKeySequence(Qt::Key_H), [view] { view()->setFlipped(!view()->isFlipped()); }, "flip");
    auto* checker = act("view.checker", "Transparent background (checkerboard)", "View", {}, [] {
        app()->prefs().checkerboard = !app()->prefs().checkerboard;
        app()->preferencesChanged();
    }, {}, false);
    checker->setCheckable(true);
    checker->setChecked(app()->prefs().checkerboard);
    connect(app(), &App::prefsChanged, checker, [checker] { checker->setChecked(app()->prefs().checkerboard); });
    auto* grid = act("view.pixelgrid", "Pixel grid", "View", {}, [] {
        app()->prefs().showPixelGrid = !app()->prefs().showPixelGrid;
        app()->preferencesChanged();
    }, "grid", false);
    grid->setCheckable(true);
    grid->setChecked(app()->prefs().showPixelGrid);
    auto toggle = [this](const QString& id, const QString& text, const QKeySequence& key, bool Preferences::*field, const QString& icon) {
        QAction* a = act(id, text, "View", key, [field] {
            app()->prefs().*field = !(app()->prefs().*field);
            app()->preferencesChanged();
        }, icon, false);
        a->setCheckable(true);
        a->setChecked(app()->prefs().*field);
        connect(app(), &App::prefsChanged, a, [a, field] { a->setChecked(app()->prefs().*field); });
    };
    toggle("view.grid", "Grid", QKeySequence(Qt::CTRL | Qt::Key_Apostrophe), &Preferences::showGrid, "grid");
    toggle("view.thirds", "Rule of thirds", {}, &Preferences::showThirds, {});
    toggle("view.golden", "Golden ratio", {}, &Preferences::showGoldenRatio, {});
    toggle("view.center", "Center lines", {}, &Preferences::showCenterLines, {});
    {
        QAction* g = act("view.gray", "Check values (grays)", "View", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y), [] {
            app()->setGrayscaleView(!app()->grayscaleView());
            app()->showStatus(app()->grayscaleView() ? T("Showing the canvas in grays. The picture is not changed.") : QString(), 3000);
        }, "gray", false);
        g->setCheckable(true);
        connect(app(), &App::viewOptionsChanged, g, [g] { g->setChecked(app()->grayscaleView()); });
    }
    {
        // the reference picture over everything (hidden from screen recordings)
        QAction* o = act("view.overlay", "Reference over everything", "View", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R), [this] {
            if (m_refPanel) m_refPanel->setOverlayVisible(!m_refPanel->overlayVisible());
        }, "overlay", false);
        o->setCheckable(true);
        QAction* t = act("view.overlaythrough", "Draw through the reference", "View", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D), [this] {
            if (m_refPanel) m_refPanel->setClickThrough(!m_refPanel->clickThrough());
        }, "cursor", false);
        t->setCheckable(true);
    }
    act("view.gridsettings", "Grid settings...", "View", {}, [this] { preferences(int(PreferencesDialog::Canvas)); }, {}, false);
    act("view.panels", "Hide / show panels", "View", QKeySequence(Qt::Key_Tab), [this] { togglePanels(); }, {}, false);
    act("window.reset", "Reset the interface", "Window", {}, [this] {
        if (QMessageBox::question(this, T("Reset the interface"),
                                  T("Put all panels back where they were at the start, unfold them and show the hidden tools, tabs and parts "
                                    "of the color panel? Brushes, colors and shortcuts stay as they are.")) == QMessageBox::Yes)
            resetInterface();
    }, "reset", false);
    act("view.fullscreen", "Full screen", "View", QKeySequence(Qt::Key_F11), [this] { isFullScreen() ? showNormal() : showFullScreen(); }, {}, false);
    for (const QString& th : {QString("dark"), QString("gray"), QString("light")}) {
        QString label = th == "dark" ? "Dark theme" : th == "gray" ? "Gray theme" : "Light theme";
        act("view.theme." + th, label, "Settings", {}, [th] {
            app()->prefs().theme = th;
            Theme::apply(th, app()->prefs().uiFontSize);
            app()->preferencesChanged();
        }, {}, false);
    }

    // Tools
    const QList<QPair<QString, QKeySequence>> toolKeys = {
        {"brush", QKeySequence(Qt::Key_B)}, {"eyedropper", QKeySequence(Qt::Key_I)}, {"move", QKeySequence(Qt::Key_V)},
        {"select-rect", QKeySequence(Qt::Key_M)}, {"select-ellipse", QKeySequence(Qt::SHIFT | Qt::Key_M)},
        {"lasso", QKeySequence(Qt::Key_L)}, {"magic-wand", QKeySequence(Qt::Key_W)}, {"select-brush", QKeySequence(Qt::Key_Q)},
        {"fill", QKeySequence(Qt::Key_G)}, {"gradient", QKeySequence(Qt::SHIFT | Qt::Key_G)}, {"lasso-fill", QKeySequence(Qt::Key_F)},
        {"text", QKeySequence(Qt::Key_T)}, {"hand", {}}, {"zoom", QKeySequence(Qt::Key_Z)}, {"rotate", QKeySequence(Qt::Key_R)},
        {"vpen", QKeySequence(Qt::Key_P)}, {"curve", QKeySequence(Qt::SHIFT | Qt::Key_P)}, {"vedit", QKeySequence(Qt::Key_A)},
        {"vpressure", {}}, {"vweight", {}}, {"vcolor", {}}, {"veraser", {}}, {"ruler", QKeySequence(Qt::Key_U)},
        {"transform", {}}, {"warp", {}}, {"liquify", {}}};
    for (const auto& tk : toolKeys) {
        Tool* t = app()->tools()->tool(tk.first);
        if (!t) continue;
        QString id = tk.first;
        act("tool." + id, t->name(), "Tools", tk.second, [id] { app()->tools()->setCurrent(id); }, t->iconName(), false);
    }
    act("tool.eraser", "Eraser (toggle)", "Tools", QKeySequence(Qt::Key_E), [] {
        BrushLibrary* b = app()->brushes();
        static int last = 0;
        int er = b->eraserIndex();
        if (er < 0) return;
        if (b->currentIndex() == er && app()->tools()->current()->id() == "brush") app()->tools()->selectBrushSlot(last);
        else {
            last = b->currentIndex();
            app()->tools()->selectBrushSlot(er);
        }
    }, "eraser", false);
    act("brush.smaller", "Smaller brush", "Tools", QKeySequence(Qt::Key_BracketLeft), [] {
        BrushLibrary* b = app()->brushes();
        BrushSettings s = b->current();
        s.size = std::max(0.5, s.size / (1.0 + app()->prefs().brushStepPercent / 100.0));
        b->update(b->currentIndex(), s);
    }, {}, false);
    act("brush.bigger", "Bigger brush", "Tools", QKeySequence(Qt::Key_BracketRight), [] {
        BrushLibrary* b = app()->brushes();
        BrushSettings s = b->current();
        s.size = std::min(5000.0, s.size * (1.0 + app()->prefs().brushStepPercent / 100.0));
        b->update(b->currentIndex(), s);
    }, {}, false);
    act("color.swap", "Swap main and sub color", "Tools", QKeySequence(Qt::Key_X), [] { app()->swapColors(); }, "swap", false);
    act("color.reset", "Reset colors (black / white)", "Tools", QKeySequence(Qt::Key_D), [] { app()->resetColors(); }, {}, false);
    act("color.transparent", "Transparent color", "Tools", {}, [] { app()->setTransparentColor(!app()->transparentColor()); }, {}, false);
    for (int i = 0; i < 9; ++i)
        act(QStringLiteral("brush.slot%1").arg(i + 1), T("Brush slot %1").arg(i + 1), "Tools", QKeySequence(Qt::Key_1 + i),
            [i] { if (i < app()->brushes()->count()) app()->tools()->selectBrushSlot(i); }, {}, false);

    // Help
    act("help.textures", "Open textures folder", "Help", {}, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(app()->texturesDir())); }, {}, false);
    act("help.reloadtextures", "Reload textures", "Help", {}, [] {
        TextureLibrary::instance().reload();
        Q_EMIT app()->brushes()->slotsChanged();
        app()->showStatus(T("Textures reloaded."));
    }, {}, false);
    act("help.data", "Open settings folder", "Help", {}, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(app()->dataDir())); }, {}, false);
    act("help.about", "About GraphiX Paint", "Help", {}, [this] { about(); }, {}, false);
    act("help.commands", "Find a command...", "Help", QKeySequence(Qt::CTRL | Qt::Key_F), [this] { commandPalette(); }, "zoom", false);
}

void MainWindow::createMenus() {
    auto* mb = menuBar();
    auto add = [this](QMenu* m, const QString& id) {
        if (QAction* a = m_actions.value(id)) m->addAction(a);
    };
    QMenu* file = mb->addMenu(T("&File"));
    add(file, "file.new");
    add(file, "file.open");
    m_recentMenu = file->addMenu(T("Open recent"));
    updateRecentMenu();
    file->addSeparator();
    add(file, "file.save");
    add(file, "file.saveas");
    add(file, "file.export");
    add(file, "file.import");
    file->addSeparator();
    QMenu* tl = file->addMenu(Theme::icon("record"), T("Video recording"));
    for (const char* id : {"rec.start", "rec.pause", "rec.stop", "", "rec.toggle", "", "rec.mode.canvas", "rec.mode.window", "", "rec.discard",
                           "rec.folder"}) {
        if (!*id) tl->addSeparator();
        else add(tl, id);
    }
    file->addSeparator();
    add(file, "file.close");
    add(file, "file.quit");

    QMenu* edit = mb->addMenu(T("&Edit"));
    for (const char* id : {"edit.undo", "edit.redo", "", "edit.cut", "edit.copy", "edit.copymerged", "edit.paste", "edit.pastenew",
                           "", "edit.clear", "edit.fill", "edit.fillpattern", "edit.stroke", "", "edit.transform", "edit.warp", "edit.liquify"}) {
        if (!*id) edit->addSeparator();
        else add(edit, id);
    }

    QMenu* canvas = mb->addMenu(T("&Canvas"));
    for (const char* id : {"canvas.size", "canvas.imagesize", "", "canvas.rotcw", "canvas.rotccw", "canvas.rot180", "",
                           "canvas.fliph", "canvas.flipv", "", "canvas.crop", "canvas.trim"}) {
        if (!*id) canvas->addSeparator();
        else add(canvas, id);
    }

    QMenu* layer = mb->addMenu(T("&Layer"));
    for (const char* id : {"layer.new", "layer.newvector", "layer.newfolder"}) add(layer, id);
    QMenu* adjLayer = layer->addMenu(Theme::icon("adjustment"), T("New adjustment layer"));
    for (int t = 0; t < int(AdjustmentType::Count); ++t) add(adjLayer, QStringLiteral("adjustlayer.%1").arg(t));
    for (const char* id : {"", "layer.duplicate", "layer.delete", "", "layer.mergedown", "layer.mergeselected", "layer.mergevisible", "layer.flatten",
                           "layer.group", "layer.rasterize", "", "layer.up", "layer.down", "", "layer.clip", "layer.alphalock",
                           "layer.hide", "layer.reference", "layer.flats", "", "layer.properties", "layer.effects"}) {
        if (!*id) layer->addSeparator();
        else add(layer, id);
    }
    QMenu* chM = layer->addMenu(Theme::icon("channels"), T("Channels"));
    for (const char* id : {"layer.chromatic", "", "select.alpha", "layer.alphamask", "layer.alphalayer", "layer.invertalpha", "",
                           "layer.split", "", "layer.removewhite", "layer.recolor"}) {
        if (!*id) chM->addSeparator();
        else add(chM, id);
    }
    QMenu* maskM = layer->addMenu(Theme::icon("mask"), T("Layer mask"));
    for (const char* id : {"layer.mask", "layer.applymask", "layer.deletemask"}) add(maskM, id);
    QMenu* pxM = layer->addMenu(Theme::icon("image"), T("Pixels"));
    for (const char* id : {"layer.copysel", "layer.cutsel", "", "layer.center", "layer.fliph", "layer.flipv"}) {
        if (!*id) pxM->addSeparator();
        else add(pxM, id);
    }

    QMenu* sel = mb->addMenu(T("&Select"));
    for (const char* id : {"select.all", "select.none", "select.reselect", "select.invert", "", "select.alpha", "select.colorrange", "", "select.grow",
                           "select.shrink", "select.feather", "select.border", "", "select.show"}) {
        if (!*id) sel->addSeparator();
        else add(sel, id);
    }

    QMenu* filt = mb->addMenu(T("Fi&lter"));
    add(filt, "filter.lineart");
    filt->addSeparator();
    QMenu* adjM = filt->addMenu(Theme::icon("adjustment"), T("Adjustments"));
    for (int t = 0; t < int(AdjustmentType::Count); ++t) add(adjM, QStringLiteral("adjust.%1").arg(t));
    QMenu* fxM = filt->addMenu(Theme::icon("fx"), T("Effects"));
    fxM->setToolTipsVisible(true);
    for (const char* id : {"filter.fx.chromatic", "", "filter.fx.outline", "filter.fx.shadow", "filter.fx.outerGlow", "", "filter.fx.innerShadow",
                           "filter.fx.innerGlow", "", "filter.fx.overlay", "filter.fx.gradient", "filter.fx.texture"}) {
        if (!*id) fxM->addSeparator();
        else add(fxM, id);
    }
    filt->addSeparator();
    for (const char* id : {"filter.gaussian", "filter.motion", "", "filter.unsharp", "filter.sharpen", "", "filter.noise",
                           "filter.pixelate", "filter.screentone", "", "filter.glow"}) {
        if (!*id) filt->addSeparator();
        else add(filt, id);
    }

    QMenu* viewM = mb->addMenu(T("&View"));
    for (const char* id : {"view.newview", "", "view.zoomin", "view.zoomout", "view.fit", "view.actual", "", "view.rotl", "view.rotr",
                           "view.rotreset", "view.flip", "view.reset", "", "view.gray", "view.checker", "view.pixelgrid"}) {
        if (!*id) viewM->addSeparator();
        else add(viewM, id);
    }
    QMenu* gridM = viewM->addMenu(Theme::icon("grid"), T("Grid and guides"));
    for (const char* id : {"view.grid", "view.thirds", "view.golden", "view.center", "", "view.gridsettings"}) {
        if (!*id) gridM->addSeparator();
        else add(gridM, id);
    }
    viewM->addSeparator();
    add(viewM, "view.overlay");
    add(viewM, "view.overlaythrough");
    viewM->addSeparator();
    add(viewM, "view.panels");
    add(viewM, "view.fullscreen");

    QMenu* win = mb->addMenu(T("&Window"));
    win->setObjectName("windowMenu");

    // ---- Settings: everything about the program itself
    QMenu* setM = mb->addMenu(T("&Settings"));
    add(setM, "edit.prefs");
    add(setM, "edit.shortcuts");
    add(setM, "settings.pen");
    setM->addSeparator();
    QMenu* langM = setM->addMenu(Theme::icon("language"), T("Language"));
    auto* langGroup = new QActionGroup(langM);
    for (const auto& l : {qMakePair(QStringLiteral("auto"), T("Automatic (as Windows)")), qMakePair(QStringLiteral("en"), QStringLiteral("English")),
                          qMakePair(QStringLiteral("ru"), QString::fromUtf8("Русский"))}) {
        QAction* a = langM->addAction(l.second);
        a->setCheckable(true);
        a->setChecked(app()->prefs().language == l.first);
        langGroup->addAction(a);
        const QString code = l.first;
        connect(a, &QAction::triggered, this, [this, code] { switchLanguage(code); });
    }
    QMenu* themeM = setM->addMenu(Theme::icon("theme"), T("Theme"));
    auto* themeGroup = new QActionGroup(themeM);
    for (const char* id : {"view.theme.dark", "view.theme.gray", "view.theme.light"}) {
        if (QAction* a = m_actions.value(id)) {
            a->setCheckable(true);
            a->setChecked(QString(id).endsWith(app()->prefs().theme));
            themeGroup->addAction(a);
            themeM->addAction(a);
        }
    }
    connect(app(), &App::prefsChanged, themeM, [this] {
        for (const char* id : {"view.theme.dark", "view.theme.gray", "view.theme.light"})
            if (QAction* a = m_actions.value(id)) a->setChecked(QString(id).endsWith(app()->prefs().theme));
    });
    QMenu* tabM = setM->addMenu(Theme::icon("tablet"), T("Pen input"));
    auto* tabGroup = new QActionGroup(tabM);
    for (const auto& t : {qMakePair(QStringLiteral("wintab"), T("Wintab (like SAI, recommended)")), qMakePair(QStringLiteral("ink"), T("Windows Ink"))}) {
        QAction* a = tabM->addAction(t.second);
        a->setCheckable(true);
        tabGroup->addAction(a);
        const QString api = t.first;
        connect(a, &QAction::triggered, this, [api] {
            app()->prefs().tabletApi = api;
            app()->preferencesChanged();
            app()->showStatus(TabletInput::describe());
        });
        connect(tabM, &QMenu::aboutToShow, a, [a, api] { a->setChecked(TabletInput::active() == api); });
    }

    QMenu* help = mb->addMenu(T("&Help"));
    for (const char* id : {"help.commands", "", "help.textures", "help.reloadtextures", "help.data", "", "help.about"}) {
        if (!*id) help->addSeparator();
        else add(help, id);
    }
}

void MainWindow::createToolbar() {
    QToolBar* tb = addToolBar(T("Main"));
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    tb->setIconSize(QSize(app()->prefs().iconSize - 2, app()->prefs().iconSize - 2));
    for (const char* id : {"edit.undo", "edit.redo"}) tb->addAction(m_actions.value(id));
    tb->addSeparator();
    for (const char* id : {"select.none", "select.invert"}) tb->addAction(m_actions.value(id));
    tb->addSeparator();
    for (const char* id : {"view.zoomout", "view.zoomin", "view.fit", "view.rotreset", "view.flip"}) tb->addAction(m_actions.value(id));
    tb->addSeparator();
    // video recording: ● record (its arrow chooses canvas / window), ❚❚ pause, ■ stop and save
    for (const char* id : {"rec.start", "rec.pause", "rec.stop"}) tb->addAction(m_actions.value(id));
    if (auto* recBtn = qobject_cast<QToolButton*>(tb->widgetForAction(m_actions.value("rec.start")))) {
        auto* m = new QMenu(recBtn);
        m->addAction(m_actions.value("rec.mode.canvas"));
        m->addAction(m_actions.value("rec.mode.window"));
        m->addSeparator();
        m->addAction(m_actions.value("rec.discard"));
        m->addAction(m_actions.value("rec.folder"));
        recBtn->setMenu(m);
        recBtn->setPopupMode(QToolButton::MenuButtonPopup);
    }
    tb->addSeparator();

    auto* stabLabel = new QLabel(T("Stabilizer"), tb);
    stabLabel->setContentsMargins(6, 0, 2, 0);
    tb->addWidget(stabLabel);
    m_stabMode = new QComboBox(tb);
    m_stabMode->addItem(T("Off"), 0);
    m_stabMode->addItem(T("Average"), 1);
    m_stabMode->addItem(T("Speed (S)"), 2);
    m_stabMode->addItem(T("Rope"), 3);
    m_stabMode->setToolTip(T("Stabilizer: Average = SAI 1-15 levels, Speed = SAI S-levels, Rope = pulled string"));
    tb->addWidget(m_stabMode);
    m_stabLevel = new QSpinBox(tb);
    m_stabLevel->setRange(0, 30);
    m_stabLevel->setToolTip(T("Stabilizer strength"));
    tb->addWidget(m_stabLevel);
    auto* psLabel = new QLabel(T("Pressure smoothing"), tb);
    psLabel->setContentsMargins(8, 0, 2, 0);
    tb->addWidget(psLabel);
    m_pressSmooth = new QSpinBox(tb);
    m_pressSmooth->setRange(0, 20);
    tb->addWidget(m_pressSmooth);
    auto syncStab = [this] {
        m_stabMode->setCurrentIndex(std::max(0, m_stabMode->findData(app()->stabilizerMode())));
        m_stabLevel->setValue(app()->stabilizerLevel());
        m_pressSmooth->setValue(app()->pressureSmoothing());
    };
    syncStab();
    connect(m_stabMode, &QComboBox::activated, this, [this](int i) { app()->setStabilizer(m_stabMode->itemData(i).toInt(), m_stabLevel->value()); });
    connect(m_stabLevel, &QSpinBox::valueChanged, this, [this](int v) { app()->setStabilizer(m_stabMode->currentData().toInt(), v); });
    connect(m_pressSmooth, &QSpinBox::valueChanged, this, [](int v) { app()->setPressureSmoothing(v); });
    tb->addSeparator();

    m_snapBtn = new QToolButton(tb);
    m_snapBtn->setIcon(Theme::icon("perspective"));
    m_snapBtn->setCheckable(true);
    m_snapBtn->setToolTip(T("Perspective ruler: show and snap strokes (edit with the Ruler tool)"));
    tb->addWidget(m_snapBtn);
    connect(m_snapBtn, &QToolButton::toggled, this, [this](bool on) {
        if (Document* d = activeDocument()) {
            d->perspective.enabled = on;
            d->perspective.snap = on;
            d->guidesModified();
        }
    });
    // special ruler: the button turns snapping on and off, its menu chooses the ruler
    m_rulerBtn = new QToolButton(tb);
    m_rulerBtn->setIcon(Theme::icon("ruler"));
    m_rulerBtn->setCheckable(true);
    m_rulerBtn->setPopupMode(QToolButton::MenuButtonPopup);
    m_rulerBtn->setToolTip(T("Snap strokes to the ruler. The arrow chooses the ruler: straight, curve, ellipse, concentric, parallel, fan "
                             "(shape it with the Ruler tool, U)"));
    auto* rulerMenu = new QMenu(m_rulerBtn);
    auto* rulerGroup = new QActionGroup(rulerMenu);
    const QStringList kinds = Ruler::kindNames();
    for (int i = 0; i < kinds.size(); ++i) {
        QAction* a = rulerMenu->addAction(T(kinds[i]));
        a->setCheckable(true);
        a->setData(i);
        rulerGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] {
            Document* d = activeDocument();
            if (!d) return;
            d->ruler.kind = i;
            d->ruler.snap = true;
            d->ruler.resetFor(d->size());
            d->guidesModified();
            if (i != Ruler::None) app()->tools()->setCurrent("ruler");
        });
    }
    m_rulerBtn->setMenu(rulerMenu);
    tb->addWidget(m_rulerBtn);
    connect(m_rulerBtn, &QToolButton::toggled, this, [this](bool on) {
        Document* d = activeDocument();
        if (!d) return;
        if (on && !d->ruler.isOn()) {
            d->ruler.kind = Ruler::Straight;
            d->ruler.resetFor(d->size());
        }
        d->ruler.snap = on;
        d->guidesModified();
    });
    m_symBox = new QComboBox(tb);
    m_symBox->addItem(Theme::icon("symmetry"), T("No symmetry"), int(Symmetry::Off));
    m_symBox->addItem(Theme::icon("symmetry"), T("Mirror left/right"), int(Symmetry::Vertical));
    m_symBox->addItem(Theme::icon("symmetry"), T("Mirror top/bottom"), int(Symmetry::Horizontal));
    m_symBox->addItem(Theme::icon("symmetry"), T("Mirror both"), int(Symmetry::Both));
    m_symBox->addItem(Theme::icon("symmetry"), T("Radial"), int(Symmetry::Radial));
    m_symBox->setToolTip(T("Symmetry painting (move the center with the Ruler tool)"));
    tb->addWidget(m_symBox);
    connect(m_symBox, &QComboBox::activated, this, [this](int i) {
        if (Document* d = activeDocument()) {
            d->symmetry.mode = m_symBox->itemData(i).toInt();
            d->guidesModified();
        }
    });
    connect(app(), &App::activeViewChanged, this, [this, syncStab] {
        syncStab();
        Document* d = activeDocument();
        QSignalBlocker b1(m_snapBtn), b2(m_symBox);
        m_snapBtn->setChecked(d && d->perspective.enabled);
        m_symBox->setCurrentIndex(d ? std::max(0, m_symBox->findData(d->symmetry.mode)) : 0);
        syncGuides();
        if (d) connect(d, &Document::guidesChanged, this, &MainWindow::syncGuides, Qt::UniqueConnection);
    });
}

void MainWindow::syncGuides() {
    Document* d = activeDocument();
    if (!m_rulerBtn) return;
    QSignalBlocker b1(m_snapBtn), b2(m_symBox), b3(m_rulerBtn);
    m_snapBtn->setChecked(d && d->perspective.enabled);
    m_symBox->setCurrentIndex(d ? std::max(0, m_symBox->findData(d->symmetry.mode)) : 0);
    m_rulerBtn->setChecked(d && d->ruler.snaps());
    for (QAction* a : m_rulerBtn->menu()->actions()) a->setChecked(d ? a->data().toInt() == d->ruler.kind : a->data().toInt() == 0);
}

namespace {
// Scrolls a panel vertically when it is made shorter than its contents, but keeps the
// panel's own minimum width (a plain scroll area would let the panel be squeezed thin).
class DockScroll : public QScrollArea {
public:
    using QScrollArea::QScrollArea;
    QSize minimumSizeHint() const override {
        QSize s = QScrollArea::minimumSizeHint();
        if (QWidget* w = widget()) {
            const int sb = verticalScrollBar() ? verticalScrollBar()->sizeHint().width() : 12;
            s.setWidth(std::max(s.width(), std::max(w->minimumSizeHint().width(), w->minimumWidth()) + sb));
        }
        s.setHeight(std::min(s.height(), 40));
        return s;
    }
};
}  // namespace

void MainWindow::createDocks() {
    auto dock = [this](const QString& id, const QString& title, QWidget* w, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(T(title), this);
        d->setObjectName(id);
        // The panel scrolls when it is made smaller than its contents, so every panel can be
        // made shorter or taller by dragging the line between panels.
        auto* scroll = new DockScroll(d);
        scroll->setObjectName("dockScroll");
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(w);
        d->setWidget(scroll);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        // Title without the small float / close buttons: an arrow folds the panel, drag the title
        // to move the panel out, double-click it to put it back. Panels are shown and hidden in Window.
        auto* head = new QWidget(d);
        head->setObjectName("dockTitleBar");
        auto* hl = new QHBoxLayout(head);
        hl->setContentsMargins(2, 0, 4, 0);
        hl->setSpacing(2);
        auto* arrow = new QToolButton(head);
        arrow->setObjectName("dockFold");
        arrow->setArrowType(Qt::DownArrow);
        arrow->setAutoRaise(true);
        arrow->setFixedSize(16, 16);
        arrow->setToolTip(T("Fold or unfold the panel"));
        auto* label = new QLabel(d->windowTitle(), head);
        label->setObjectName("dockTitle");
        hl->addWidget(arrow);
        hl->addWidget(label, 1);
        head->setToolTip(T("Drag to move the panel. Double-click to put it back into the window."));
        connect(d, &QDockWidget::windowTitleChanged, label, &QLabel::setText);
        connect(arrow, &QToolButton::clicked, this, [this, d] { setDockFolded(d, !d->property("folded").toBool()); });
        d->setTitleBarWidget(head);
        addDockWidget(area, d);
        m_docks << d;
        if (QMenu* wm = menuBar()->findChild<QMenu*>("windowMenu")) wm->addAction(d->toggleViewAction());
        return d;
    };
    m_colors = new ColorPanel;
    auto* colorDock = dock("colorDock", "Color", m_colors, Qt::LeftDockWidgetArea);
    auto* toolDock = dock("toolDock", "Tools", new ToolPanel, Qt::LeftDockWidgetArea);
    splitDockWidget(colorDock, toolDock, Qt::Vertical);
    auto* navDock = dock("navDock", "Navigator", new NavigatorPanel, Qt::RightDockWidgetArea);
    m_layers = new LayerPanel;
    auto* layerDock = dock("layerDock", "Layers", m_layers, Qt::RightDockWidgetArea);
    splitDockWidget(navDock, layerDock, Qt::Vertical);
    auto* histDock = dock("historyDock", "History", new HistoryPanel, Qt::RightDockWidgetArea);
    m_refPanel = new ReferencePanel;
    auto* refDock = dock("referenceDock", "Reference", m_refPanel, Qt::RightDockWidgetArea);
    if (QAction* o = m_actions.value("view.overlay")) {
        connect(m_refPanel, &ReferencePanel::overlayStateChanged, o, [this, o] {
            o->setChecked(m_refPanel->overlayVisible());
            if (QAction* t = m_actions.value("view.overlaythrough")) t->setChecked(m_refPanel->clickThrough());
        });
    }
    auto* scratchDock = dock("scratchDock", "Scratchpad", new ScratchpadPanel, Qt::RightDockWidgetArea);
    auto* previewDock = dock("previewDock", "Preview", new PreviewPanel, Qt::RightDockWidgetArea);
    auto* materials = new MaterialsPanel;
    auto* matDock = dock("materialsDock", "Materials", materials, Qt::RightDockWidgetArea);
    connect(materials, &MaterialsPanel::place, this, [this](const QString& k, const QString& v) { placeMaterial(k, v); });
    connect(materials, &MaterialsPanel::applyGradient, this, &MainWindow::applyGradientMap);
    connect(materials, &MaterialsPanel::saveSelection, this, &MainWindow::saveSelectionAsMaterial);
    tabifyDockWidget(histDock, matDock);
    tabifyDockWidget(matDock, refDock);
    tabifyDockWidget(refDock, scratchDock);
    tabifyDockWidget(scratchDock, previewDock);
    histDock->raise();
    applyDefaultDockSizes();

    if (QMenu* wm = menuBar()->findChild<QMenu*>("windowMenu")) {
        wm->addSeparator();
        wm->addAction(m_actions.value("window.reset"));
    }
    connect(m_layers, &LayerPanel::requestEffects, this, &MainWindow::layerEffects);
    connect(m_layers, &LayerPanel::requestAdjustmentEdit, this, &MainWindow::editAdjustmentLayer);
    connect(m_layers, &LayerPanel::requestNewAdjustment, this, [this](int t) { adjustment(t, true); });
}

void MainWindow::applyDefaultDockSizes() {
    auto find = [this](const char* id) { return findChild<QDockWidget*>(QString::fromLatin1(id)); };
    QDockWidget *colorDock = find("colorDock"), *toolDock = find("toolDock"), *navDock = find("navDock"), *layerDock = find("layerDock"),
                *histDock = find("historyDock");
    if (!colorDock || !toolDock || !navDock || !layerDock || !histDock) return;
    histDock->raise();
    resizeDocks({colorDock, toolDock}, {320, 560}, Qt::Vertical);
    resizeDocks({colorDock}, {270}, Qt::Horizontal);
    resizeDocks({layerDock}, {300}, Qt::Horizontal);
    resizeDocks({navDock, layerDock, histDock}, {180, 480, 200}, Qt::Vertical);
}

void MainWindow::createStatusBar() {
    m_statusMsg = new QLabel(this);
    m_statusSize = new QLabel(this);
    m_statusZoom = new QLabel(this);
    m_statusPos = new QLabel(this);
    statusBar()->addWidget(m_statusMsg, 1);
    // ● REC 00:12:34 while recording, ❚❚ Paused when a recording waits
    m_recLabel = new QLabel(this);
    m_recLabel->setTextFormat(Qt::RichText);
    m_recLabel->setContentsMargins(8, 0, 8, 0);
    m_recLabel->hide();
    statusBar()->addPermanentWidget(m_recLabel);
    auto* recTimer = new QTimer(this);
    recTimer->setInterval(500);
    connect(recTimer, &QTimer::timeout, this, &MainWindow::updateRecordingIndicator);
    recTimer->start();
    connect(&Recorder::instance(), &Recorder::changed, this, [this] {
        updateRecordingIndicator();
        updateTitle();
    });
    connect(&Recorder::instance(), &Recorder::failed, this, [this](const QString& msg) {
        QMessageBox::warning(this, T("Video recording"), T("The recording stopped.") + "\n\n" + T(msg) + "\n\n" + T("What was recorded so far is kept."));
    });
    connect(app(), &App::activeViewChanged, this, &MainWindow::updateRecordingIndicator);
    updateRecordingIndicator();
    statusBar()->addPermanentWidget(m_statusPos);
    statusBar()->addPermanentWidget(m_statusSize);
    statusBar()->addPermanentWidget(m_statusZoom);
}

// ============================================================================ documents

CanvasView* MainWindow::addDocument(std::shared_ptr<Document> doc) {
    Recorder::instance().watch(doc.get());
    doc->setUndoLimit(app()->prefs().undoLimit);
    auto* v = new CanvasView(doc);
    auto* w = new CanvasWindow(this, v);
    w->canClose = [this](CanvasView* view) {
        Document* d = view->doc();
        int views = 0;
        for (QMdiSubWindow* sw : m_mdi->subWindowList())
            if (auto* cw = dynamic_cast<CanvasWindow*>(sw); cw && cw->view()->doc() == d) ++views;
        if (views <= 1) {
            if (!maybeSave(d)) return false;
            if (!keepOrSaveRecording(d)) return false;
            Recovery::instance().unwatch(d);
        }
        if (app()->activeView() == view) app()->setActiveView(nullptr);
        return true;
    };
    m_mdi->addSubWindow(w);
    w->setWindowTitle(doc->displayName());
    w->showMaximized();
    connect(doc.get(), &Document::modifiedChanged, this, [this] { updateTitle(); });
    connect(doc.get(), &Document::filePathChanged, this, [this] { updateTitle(); });
    connect(doc.get(), &Document::historyChanged, this, [this] { updateActions(); });
    connect(doc.get(), &Document::sizeChanged, this, [this] { updateTitle(); });
    connect(v, &CanvasView::cursorMoved, this, [this](const QPointF& p) {
        m_statusPos->setText(QStringLiteral("%1, %2").arg(int(std::floor(p.x()))).arg(int(std::floor(p.y()))));
    });
    connect(v, &CanvasView::viewChanged, this, [this, v] {
        if (v == activeView()) m_statusZoom->setText(QStringLiteral("%1%  %2%3").arg(v->zoom() * 100, 0, 'f', 1).arg(v->rotation(), 0, 'f', 0).arg(QChar(0xB0)));
    });
    Recovery::instance().watch(doc.get());
    app()->setActiveView(v);
    QTimer::singleShot(0, v, [v] { v->fitToWindow(); });
    updateActions();
    updateTitle();
    return v;
}

CanvasView* MainWindow::activeView() const {
    auto* cw = dynamic_cast<CanvasWindow*>(m_mdi->activeSubWindow());
    return cw ? cw->view() : app()->activeView();
}

Document* MainWindow::activeDocument() const {
    CanvasView* v = activeView();
    return v ? v->doc() : nullptr;
}

void MainWindow::updateActions() {
    bool has = activeDocument() != nullptr;
    for (QAction* a : m_docActions) a->setEnabled(has);
    if (has) {
        m_actions.value("edit.undo")->setEnabled(activeDocument()->canUndo());
        m_actions.value("edit.redo")->setEnabled(activeDocument()->canRedo());
    }
}

void MainWindow::updateTitle() {
    QStringList seen;
    for (QMdiSubWindow* sw : m_mdi->subWindowList()) {
        auto* cw = dynamic_cast<CanvasWindow*>(sw);
        if (!cw) continue;
        Document* d = cw->view()->doc();
        QString t = d->displayName() + (d->isModified() ? " *" : "");
        int n = int(seen.count(d->displayName()));
        seen << d->displayName();
        if (n > 0) t += QStringLiteral(" (%1)").arg(n + 1);
        cw->setWindowTitle(t);
    }
    Document* d = activeDocument();
    // the taskbar also shows that a recording is running
    const QString rec = Recorder::instance().recordingDocument() ? QStringLiteral("\u25CF REC - ") : QString();
    setWindowTitle(rec + (d ? QStringLiteral("%1%2 - GraphiX Paint").arg(d->displayName(), d->isModified() ? " *" : "") : QStringLiteral("GraphiX Paint")));
    m_statusSize->setText(d ? formatSize(d->size()) : QString());
}

void MainWindow::newDocument() {
    NewDocumentDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    auto doc = std::make_shared<Document>(dlg.canvasSize());
    doc->setDpi(dlg.dpi());
    auto bg = std::make_shared<RasterLayer>();
    bg->id = doc->newLayerId();
    if (dlg.background().alpha() > 0) {
        bg->name = T("Paper");
        bg->pixels.fillRect(doc->bounds(), premulFromColor(dlg.background()));
        doc->root()->insert(0, bg);
        auto l = std::make_shared<RasterLayer>();
        l->id = doc->newLayerId();
        l->name = T("Layer 1");
        doc->root()->insert(1, l);
        doc->setCurrentLayer(l->id);
    } else {
        bg->name = T("Layer 1");
        doc->root()->insert(0, bg);
        doc->setCurrentLayer(bg->id);
    }
    doc->setModified(false);
    addDocument(doc);
}

bool MainWindow::openFile(const QString& path) {
    // already open?
    for (QMdiSubWindow* sw : m_mdi->subWindowList())
        if (auto* cw = dynamic_cast<CanvasWindow*>(sw); cw && QFileInfo(cw->view()->doc()->filePath()) == QFileInfo(path)) {
            m_mdi->setActiveSubWindow(sw);
            return true;
        }
    QString err;
    QStringList warn;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::unique_ptr<Document> doc;
    try {
        doc = io::openFile(path, &err, &warn);
    } catch (const std::bad_alloc&) {
        err = T("Not enough memory to open this file.");
    }
    QApplication::restoreOverrideCursor();
    if (!doc) {
        QMessageBox::warning(this, T("Open"), T("Could not open %1:\n%2").arg(QFileInfo(path).fileName(), T(err)));
        return false;
    }
    addRecent(path);
    addDocument(std::shared_ptr<Document>(doc.release()));
    if (!warn.isEmpty()) app()->showStatus(warn.join(" "), 8000);
    return true;
}

void MainWindow::openDialog() {
    QString dir = app()->settings().value("paths/lastDir").toString();
    QStringList files = QFileDialog::getOpenFileNames(this, T("Open"), dir, io::openFilter());
    for (const QString& f : files) {
        app()->settings().setValue("paths/lastDir", QFileInfo(f).absolutePath());
        openFile(f);
    }
}

bool MainWindow::save(Document* d, bool saveAs) {
    if (!d) return false;
    QString path = d->filePath();
    if (saveAs || path.isEmpty() || !io::canKeepLayers(path)) {
        QString dir = path.isEmpty() ? app()->settings().value("paths/lastDir").toString() : QFileInfo(path).absolutePath();
        QString base = path.isEmpty() ? T("Untitled") : QFileInfo(path).completeBaseName();
        const QString ext = app()->prefs().defaultFormat == "psd" ? ".psd" : ".gxp";
        QString suggested = dir.isEmpty() ? base + ext : dir + "/" + base + ext;
        QString selected = ext == ".psd" ? QStringLiteral("Photoshop (*.psd)") : QString();
        path = QFileDialog::getSaveFileName(this, T("Save as"), suggested, io::saveFilter(), &selected);
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += ext;
        app()->settings().setValue("paths/lastDir", QFileInfo(path).absolutePath());
    }
    QString err;
    QStringList warn;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = false;
    try {
        ok = io::saveFile(*d, path, &err, &warn);
    } catch (const std::bad_alloc&) {
        err = T("Not enough memory to save.");
    }
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, T("Save"), T("Could not save %1:\n%2").arg(QFileInfo(path).fileName(), T(err)));
        return false;
    }
    if (io::canKeepLayers(path)) {
        d->setFilePath(path);
        d->setModified(false);
        Recovery::instance().markSaved(d);
    }
    addRecent(path);
    app()->showStatus(warn.isEmpty() ? T("Saved %1").arg(QFileInfo(path).fileName()) : warn.join(" "), 5000);
    updateTitle();
    return true;
}

void MainWindow::exportDialog() {
    Document* d = activeDocument();
    if (!d) return;
    QString dir = app()->settings().value("paths/exportDir", app()->settings().value("paths/lastDir")).toString();
    QString base = d->filePath().isEmpty() ? T("Untitled") : QFileInfo(d->filePath()).completeBaseName();
    QString path = QFileDialog::getSaveFileName(this, T("Export"), dir.isEmpty() ? base + ".png" : dir + "/" + base + ".png", io::exportFilter());
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += ".png";
    app()->settings().setValue("paths/exportDir", QFileInfo(path).absolutePath());
    int quality = 92;
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "jpg" || ext == "jpeg" || ext == "webp") {
        quality = Dialogs::askInt(this, T("Export"), T("Quality (1-100):"), app()->prefs().jpegQuality, 1, 100);
        if (quality <= 0) return;
        app()->prefs().jpegQuality = quality;
        app()->savePrefs();
    }
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = io::saveFile(*d, path, &err, nullptr, quality);
    QApplication::restoreOverrideCursor();
    if (!ok) QMessageBox::warning(this, T("Export"), T("Could not export:\n%1").arg(T(err)));
    else app()->showStatus(T("Exported %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::importAsLayer() {
    Document* d = activeDocument();
    if (!d) return;
    QString f = QFileDialog::getOpenFileName(this, T("Import as layer"), app()->settings().value("paths/lastDir").toString(), io::openFilter());
    if (f.isEmpty()) return;
    QString err;
    QImage img = io::readImage(f, &err);
    if (img.isNull()) {
        QMessageBox::warning(this, T("Import"), T("Could not read the image:\n%1").arg(err));
        return;
    }
    auto l = std::make_shared<RasterLayer>();
    l->name = QFileInfo(f).completeBaseName();
    QPoint at((d->size().width() - img.width()) / 2, (d->size().height() - img.height()) / 2);
    imageToSurface(img, l->pixels, at);
    ops::insertLayer(*d, l, nullptr, true, T("Import layer"));
}

bool MainWindow::maybeSave(Document* d) {
    if (!d || !d->isModified()) return true;
    auto r = QMessageBox::question(this, T("Unsaved changes"), T("Save changes to %1?").arg(d->displayName()),
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return save(d, false);
    return true;
}

void MainWindow::newView() {
    CanvasView* v = activeView();
    if (!v) return;
    auto* nv = new CanvasView(v->docPtr());
    auto* w = new CanvasWindow(this, nv);
    w->canClose = [this](CanvasView* view) {
        int views = 0;
        for (QMdiSubWindow* sw : m_mdi->subWindowList())
            if (auto* cw = dynamic_cast<CanvasWindow*>(sw); cw && cw->view()->doc() == view->doc()) ++views;
        if (views <= 1) {
            if (!maybeSave(view->doc())) return false;
            if (!keepOrSaveRecording(view->doc())) return false;
            Recovery::instance().unwatch(view->doc());
        }
        if (app()->activeView() == view) app()->setActiveView(nullptr);
        return true;
    };
    m_mdi->addSubWindow(w);
    w->show();
    connect(nv, &CanvasView::cursorMoved, this, [this](const QPointF& p) {
        m_statusPos->setText(QStringLiteral("%1, %2").arg(int(std::floor(p.x()))).arg(int(std::floor(p.y()))));
    });
    updateTitle();
    QTimer::singleShot(0, nv, [nv] { nv->fitToWindow(); });
}

void MainWindow::copy(bool merged, bool cut) {
    Document* d = activeDocument();
    if (!d) return;
    app()->tools()->commitPending();
    QRect r = d->hasSelection() ? d->selectionBounds() : d->bounds();
    if (r.isEmpty()) return;
    QImage img;
    if (merged) img = Compositor::renderImage(d->root(), r);
    else {
        Layer* l = d->currentLayer();
        if (!l) return;
        CompositeOptions o;
        o.only = l;
        img = Compositor::renderImage(d->root(), r, o);
    }
    if (d->hasSelection()) {
        std::vector<float> m(size_t(r.width()) * r.height());
        readMask(d->selection(), r, m.data());
        for (int y = 0; y < img.height(); ++y) {
            quint32* row = reinterpret_cast<quint32*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                float f = m[size_t(y) * r.width() + x];
                if (f >= 1) continue;
                float px[4];
                unpack(row[x], px);
                for (float& v : px) v *= f;
                row[x] = pack(px);
            }
        }
    }
    QApplication::clipboard()->setImage(img.convertToFormat(QImage::Format_ARGB32));
    app()->settings().setValue("clipboard/origin", r.topLeft());
    app()->settings().setValue("clipboard/size", r.size());
    if (cut && !merged)
        if (Layer* l = d->currentLayer()) ops::clearLayer(*d, l->id);
    app()->showStatus(cut ? T("Cut to the clipboard.") : T("Copied to the clipboard."));
}

void MainWindow::paste(bool asNew) {
    QImage img = QApplication::clipboard()->image();
    if (img.isNull()) {
        // a copied file path?
        const QMimeData* md = QApplication::clipboard()->mimeData();
        if (md && md->hasUrls() && !md->urls().isEmpty()) img = io::readImage(md->urls().first().toLocalFile(), nullptr);
    }
    if (img.isNull()) {
        app()->showStatus(T("The clipboard has no image."));
        return;
    }
    Document* d = activeDocument();
    if (asNew || !d) {
        auto doc = io::documentFromImage(img, T("Pasted"));
        doc->setModified(true);
        addDocument(std::shared_ptr<Document>(doc.release()));
        return;
    }
    app()->tools()->commitPending();
    auto l = std::make_shared<RasterLayer>();
    l->name = T("Pasted");
    QPoint at;
    QPoint origin = app()->settings().value("clipboard/origin").toPoint();
    QSize size = app()->settings().value("clipboard/size").toSize();
    if (size == img.size() && d->bounds().contains(QRect(origin, size))) at = origin;
    else if (CanvasView* v = activeView()) {
        QPointF c = v->toCanvas(QPointF(v->width() / 2.0, v->height() / 2.0));
        at = QPoint(int(c.x() - img.width() / 2.0), int(c.y() - img.height() / 2.0));
    }
    imageToSurface(img, l->pixels, at);
    ops::insertLayer(*d, l, nullptr, true, T("Paste"));
}

void MainWindow::fill() {
    Document* d = activeDocument();
    if (!d || !d->currentLayer()) return;
    app()->tools()->commitPending();
    ops::fillLayer(*d, d->currentLayer()->id, app()->foreground());
}

void MainWindow::placeMaterial(const QString& kind, const QString& value, std::optional<QPointF> at) {
    Document* d = activeDocument();
    if (!d) {
        app()->showStatus(T("Open or create a picture first."));
        return;
    }
    app()->tools()->commitPending();
    QString err;
    Layer* l = nullptr;
    if (kind == "image") l = materials::placeImage(*d, value, at, &err);
    else if (kind == "pattern") l = materials::placePattern(*d, value, app()->foreground(), &err);
    else if (kind == "tone") l = materials::placeTone(*d, value.toInt(), app()->foreground(), &err);
    else if (kind == "gradient") l = materials::placeGradient(*d, value.toInt(), &err);
    else if (kind == "save-gradient") {
        auto* g = dynamic_cast<AdjustmentLayer*>(d->currentLayer());
        if (!g || g->adjustment.type != AdjustmentType::GradientMap) {
            app()->showStatus(T("Select a gradient map layer first."), 5000);
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, T("Save gradient map"), T("Name:"), QLineEdit::Normal, g->name, &ok);
        if (ok && materials::saveLayerGradient(*d, name, &err)) app()->showStatus(T("Saved to Materials > Gradient maps."));
        return;
    }
    if (!l && !err.isEmpty()) app()->showStatus(err, 5000);
}

void MainWindow::fillPattern() {
    Document* d = activeDocument();
    if (!d) return;
    app()->tools()->commitPending();
    QString err;
    if (!materials::fillWithPattern(*d, app()->foreground(), &err)) app()->showStatus(err, 5000);
}

void MainWindow::strokeSelectionDialog() {
    Document* d = activeDocument();
    if (!d) return;
    app()->tools()->commitPending();
    if (!d->hasSelection()) {
        app()->showStatus(T("Select an area first."), 5000);
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(T("Stroke selection"));
    auto* f = new QFormLayout(&dlg);
    QSettings& s = app()->settings();
    auto* width = new QDoubleSpinBox(&dlg);
    width->setRange(0.5, 500);
    width->setDecimals(1);
    width->setSuffix(T(" px"));
    width->setValue(s.value("stroke/width", 4.0).toDouble());
    f->addRow(T("Width:"), width);
    auto* pos = new QComboBox(&dlg);
    pos->addItem(T("Inside"), 0);
    pos->addItem(T("On the edge"), 1);
    pos->addItem(T("Outside"), 2);
    pos->setCurrentIndex(std::clamp(s.value("stroke/position", 1).toInt(), 0, 2));
    f->addRow(T("Position:"), pos);
    auto* with = new QComboBox(&dlg);
    with->addItem(T("The main color"), 0);
    with->addItem(T("The pattern from Materials"), 1);
    with->setCurrentIndex(std::clamp(s.value("stroke/pattern", 0).toInt(), 0, 1));
    f->addRow(T("Paint with:"), with);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    f->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    s.setValue("stroke/width", width->value());
    s.setValue("stroke/position", pos->currentIndex());
    s.setValue("stroke/pattern", with->currentIndex());
    QString err;
    if (!materials::strokeSelection(*d, width->value(), materials::StrokePosition(pos->currentIndex()), app()->foreground(),
                                    with->currentIndex() == 1, &err))
        app()->showStatus(err, 5000);
}

void MainWindow::saveSelectionAsMaterial(bool asPattern) {
    Document* d = activeDocument();
    if (!d) {
        app()->showStatus(T("Open or create a picture first."));
        return;
    }
    app()->tools()->commitPending();
    bool ok = false;
    const QString name = QInputDialog::getText(this, asPattern ? T("Save as pattern") : T("Save as picture"), T("Name:"),
                                               QLineEdit::Normal, d->currentLayer() ? d->currentLayer()->name : QString(), &ok);
    if (!ok) return;
    QString err;
    if (materials::saveSelection(*d, asPattern, name, &err).isEmpty()) app()->showStatus(err, 5000);
    else app()->showStatus(asPattern ? T("Saved to Materials > Patterns.") : T("Saved to Materials > Pictures."));
}

void MainWindow::applyGradientMap(int index) {
    Document* d = activeDocument();
    if (!d) return;
    app()->tools()->commitPending();
    Layer* l = d->currentLayer();
    const QList<Gradient> grads = Materials::instance().gradients();
    if (!l || !l->isRaster() || index < 0 || index >= grads.size()) {
        app()->showStatus(T("Adjustments change pixels: select a pixel layer, or use an adjustment layer."));
        return;
    }
    Adjustment a = Adjustment::defaults(AdjustmentType::GradientMap);
    a.params["gradient"] = grads[index].toJson();
    AdjustmentDialog dlg(d, l->id, a, AdjustmentDialog::Mode::ApplyToLayer, this);
    dlg.exec();
}

void MainWindow::adjustment(int type, bool asLayer) {
    Document* d = activeDocument();
    if (!d) return;
    app()->tools()->commitPending();
    if (asLayer) {
        AdjustmentLayer* l = ops::addAdjustmentLayer(*d, Adjustment::defaults(AdjustmentType(type)));
        if (l) editAdjustmentLayer(l->id);
        return;
    }
    Layer* l = d->currentLayer();
    if (!l) return;
    if (!l->isRaster()) {
        if (l->type() == LayerType::Adjustment) { editAdjustmentLayer(l->id); return; }
        app()->showStatus(T("Adjustments change pixels: select a pixel layer, or use an adjustment layer."));
        return;
    }
    AdjustmentDialog dlg(d, l->id, Adjustment::defaults(AdjustmentType(type)), AdjustmentDialog::Mode::ApplyToLayer, this);
    dlg.exec();
}

void MainWindow::editAdjustmentLayer(quint32 id) {
    Document* d = activeDocument();
    if (!d) return;
    auto* l = dynamic_cast<AdjustmentLayer*>(d->layer(id));
    if (!l) return;
    AdjustmentDialog dlg(d, id, l->adjustment, AdjustmentDialog::Mode::EditLayer, this);
    dlg.exec();
}

void MainWindow::filter(const QString& id) {
    Document* d = activeDocument();
    if (!d) return;
    app()->tools()->commitPending();
    Layer* l = d->currentLayer();
    if (!l || !l->isRaster()) {
        app()->showStatus(T("Filters work on pixel layers."));
        return;
    }
    AdjustmentDialog dlg(d, l->id, id, this);
    dlg.exec();
}

void MainWindow::commandPalette() {
    auto* p = new CommandPalette(menuBar(), m_actions, this);
    p->setAttribute(Qt::WA_DeleteOnClose);
    p->show();
}

void MainWindow::effectFilter(const QString& key) {
    Document* d = activeDocument();
    if (!d) return;
    app()->tools()->commitPending();
    Layer* l = d->currentLayer();
    if (!l || !l->isRaster()) {
        app()->showStatus(T("Filters work on pixel layers."));
        return;
    }
    EffectFilterDialog dlg(d, l->id, key, this);
    dlg.exec();
}

void MainWindow::layerEffects(quint32 id, const QString& focus) {
    Document* d = activeDocument();
    if (!d || !d->layer(id)) return;
    Layer* l = d->layer(id);
    if (l->isGroup() || l->type() == LayerType::Adjustment) {
        app()->showStatus(T("Effects work on pixel, linework and text layers."));
        return;
    }
    EffectsDialog dlg(d, id, this, focus);
    dlg.exec();
}

void MainWindow::canvasSize() {
    Document* d = activeDocument();
    if (!d) return;
    app()->tools()->commitPending();
    CanvasSizeDialog dlg(d->size(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    ops::resizeCanvas(*d, dlg.newSize(), dlg.offset());
}

void MainWindow::imageSize() {
    Document* d = activeDocument();
    if (!d) return;
    app()->tools()->commitPending();
    ImageSizeDialog dlg(d->size(), d->dpi(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    d->setDpi(dlg.dpi());
    if (dlg.newSize() != d->size()) ops::scaleImage(*d, dlg.newSize(), dlg.smooth());
}

void MainWindow::preferences(int page) {
    PreferencesDialog dlg(this, PreferencesDialog::Page(std::clamp(page, 0, int(PreferencesDialog::PageCount) - 1)));
    connect(&dlg, &PreferencesDialog::restartRequested, this, [this, &dlg] {
        dlg.close();
        QTimer::singleShot(0, this, &MainWindow::restartApp);
    });
    dlg.exec();
}

// ============================================================================ video recording

void MainWindow::startRecording() {
    Document* d = activeDocument();
    if (!d) {
        app()->showStatus(T("Open or create a picture first."), 4000);
        return;
    }
    Recorder& r = Recorder::instance();
    if (r.recordingDocument() == d) {
        app()->showStatus(T("The recording is already running."), 3000);
        return;
    }
    QString err;
    if (!r.start(d, &err)) {
        QMessageBox::warning(this, T("Video recording"), T("Could not start the recording.") + "\n\n" + T(err));
        return;
    }
    const QString toggle = m_actions.value("rec.toggle")->shortcut().toString(QKeySequence::NativeText);
    const QString stop = m_actions.value("rec.stop")->shortcut().toString(QKeySequence::NativeText);
    QString msg = (r.mode(d) == Recorder::Mode::Window ? T("Recording the program window.") : T("Recording the canvas."));
    if (!toggle.isEmpty()) msg += " " + T("%1 pauses.").arg(toggle);
    if (!stop.isEmpty()) msg += " " + T("%1 stops and saves the video.").arg(stop);
    if (const int hidden = Recorder::hiddenLayerCount(d)) msg += " " + T("Layers left out of the video: %1.").arg(hidden);
    app()->showStatus(msg, 6000);
}

void MainWindow::pauseRecording() {
    Recorder& r = Recorder::instance();
    Document* d = r.recordingDocument();
    if (!d) return;
    r.pause();
    const QString toggle = m_actions.value("rec.toggle")->shortcut().toString(QKeySequence::NativeText);
    app()->showStatus(T("Recording paused (%1).").arg(Recorder::formatLength(r.length(d))) +
                          (toggle.isEmpty() ? QString() : " " + T("%1 continues.").arg(toggle)),
                      5000);
}

bool MainWindow::stopRecording(Document* d) {
    Recorder& r = Recorder::instance();
    if (!d) d = r.recordingDocument();
    if (!d) d = activeDocument();
    if (!d || r.state(d) == Recorder::State::None) {
        const QString key = m_actions.value("rec.toggle")->shortcut().toString(QKeySequence::NativeText);
        QMessageBox::information(this, T("Video recording"),
                                 T("Nothing is recorded yet. Press Record on the toolbar%1 to start.").arg(key.isEmpty() ? QString() : " (" + key + ")"));
        return false;
    }
    if (r.recordingDocument() == d) r.pause();
    const qint64 len = r.length(d);
    const qint64 secs = std::max<qint64>(1, len / kVideoSecond);

    QDialog dlg(this);
    dlg.setWindowTitle(T("Save the video"));
    auto* v = new QVBoxLayout(&dlg);
    auto* info = new QLabel(T("Recorded: %1 (only the time you were drawing).").arg(Recorder::formatLength(len)) + "\n" +
                                (r.mode(d) == Recorder::Mode::Window ? T("What is recorded: the program window.") : T("What is recorded: the canvas.")),
                            &dlg);
    info->setWordWrap(true);
    v->addWidget(info);
    auto* real = new QRadioButton(T("Real speed (%1)").arg(Recorder::formatLength(len)), &dlg);
    auto* fast = new QRadioButton(T("Speed up to"), &dlg);
    auto* fastLen = new QSpinBox(&dlg);
    fastLen->setRange(3, int(std::clamp<qint64>(secs - 1, 3, 36000)));
    fastLen->setSuffix(T(" s"));
    fastLen->setValue(int(std::clamp<qint64>(app()->settings().value("recording/targetSeconds", 60).toInt(), 3, fastLen->maximum())));
    auto* fastRow = new QHBoxLayout;
    fastRow->addWidget(fast);
    fastRow->addWidget(fastLen);
    fastRow->addStretch();
    v->addWidget(real);
    v->addLayout(fastRow);
    const bool canSpeedUp = secs > 4;
    fast->setEnabled(canSpeedUp);
    fastLen->setEnabled(canSpeedUp);
    (canSpeedUp && secs > 90 ? fast : real)->setChecked(true);
    connect(fastLen, &QSpinBox::valueChanged, fast, [fast] { fast->setChecked(true); });
    auto* hold = new QCheckBox(T("Show the finished picture 2 more seconds at the end"), &dlg);
    hold->setChecked(app()->settings().value("recording/holdEnd", true).toBool());
    v->addWidget(hold);
    auto* bb = new QDialogButtonBox(&dlg);
    QPushButton* saveBtn = bb->addButton(T("Save video..."), QDialogButtonBox::AcceptRole);
    QPushButton* laterBtn = bb->addButton(T("Continue later"), QDialogButtonBox::RejectRole);
    QPushButton* delBtn = bb->addButton(T("Delete the recording"), QDialogButtonBox::DestructiveRole);
    laterBtn->setToolTip(T("Keep the recording: press Record again to continue it."));
    saveBtn->setDefault(true);
    bool deleteIt = false;
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(delBtn, &QPushButton::clicked, &dlg, [&] {
        if (QMessageBox::question(&dlg, T("Video recording"), T("Delete the recording? This cannot be undone.")) == QMessageBox::Yes) {
            deleteIt = true;
            dlg.reject();
        }
    });
    v->addWidget(bb);
    const int res = dlg.exec();
    if (deleteIt) {
        r.discard(d);
        app()->showStatus(T("The recording was deleted."), 4000);
        return true;
    }
    if (res != QDialog::Accepted) {
        app()->showStatus(T("The recording is kept. Press Record to continue it."), 4000);
        return false;
    }
    app()->settings().setValue("recording/targetSeconds", fastLen->value());
    app()->settings().setValue("recording/holdEnd", hold->isChecked());

    QString dir = app()->settings().value("recording/lastDir").toString();
    if (dir.isEmpty()) dir = d->filePath().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) : QFileInfo(d->filePath()).absolutePath();
    const QString base = d->filePath().isEmpty() ? T("Untitled") : QFileInfo(d->filePath()).completeBaseName();
    QString path = QFileDialog::getSaveFileName(this, T("Save the video"), dir + "/" + base + " - " + T("recording") + ".mp4", T("MP4 video") + " (*.mp4)");
    if (path.isEmpty()) {
        app()->showStatus(T("The recording is kept. Press Record to continue it."), 4000);
        return false;
    }
    if (QFileInfo(path).suffix().compare("mp4", Qt::CaseInsensitive) != 0) path += ".mp4";
    app()->settings().setValue("recording/lastDir", QFileInfo(path).absolutePath());

    const Recorder::Job job = r.job(d);
    Recorder::ExportOptions opt;
    opt.targetLength = fast->isChecked() ? qint64(fastLen->value()) * kVideoSecond : 0;
    opt.holdEnd = hold->isChecked();
    std::atomic<int> progress{0};
    std::atomic<bool> cancel{false};
    QString err;
    QProgressDialog pd(T("Saving the video..."), T("Cancel"), 0, 1000, this);
    pd.setWindowModality(Qt::WindowModal);
    pd.setMinimumDuration(300);
    QFuture<bool> f = QtConcurrent::run([&] {
        return Recorder::exportVideo(job, path, opt, [&](double p) {
            progress = int(p * 1000);
            return !cancel.load();
        }, &err);
    });
    while (!f.isFinished()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        pd.setValue(progress.load());
        if (pd.wasCanceled()) cancel = true;
        QThread::msleep(10);
    }
    const bool ok = f.result();
    pd.reset();
    if (!ok) {
        if (err.isEmpty()) {
            app()->showStatus(T("Cancelled. The recording is kept."), 4000);
        } else {
            QMessageBox::warning(this, T("Video recording"), T("Could not save the video.") + "\n\n" + T(err) + "\n\n" + T("The recording is kept."));
        }
        return false;
    }
    r.discard(d);
    QMessageBox box(QMessageBox::Information, T("Video recording"), T("The video is saved:") + "\n" + QDir::toNativeSeparators(path), QMessageBox::Ok, this);
    QPushButton* openBtn = box.addButton(T("Open folder"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == openBtn) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    return true;
}

bool MainWindow::keepOrSaveRecording(Document* d) {
    Recorder& r = Recorder::instance();
    if (!d || r.state(d) == Recorder::State::None) return true;
    if (r.recordingDocument() == d) r.pause();
    if (!d->filePath().isEmpty()) {
        app()->showStatus(T("The recording of %1 is kept: open the file and press Record to continue it.").arg(d->displayName()), 6000);
        return true;
    }
    QMessageBox box(QMessageBox::Question, T("Video recording"),
                    T("This picture is not saved, so its recording cannot be continued later. Save the video now?"), QMessageBox::NoButton, this);
    QPushButton* saveBtn = box.addButton(T("Save video..."), QMessageBox::AcceptRole);
    QPushButton* delBtn = box.addButton(T("Delete the recording"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(saveBtn);
    box.exec();
    if (box.clickedButton() == saveBtn) return stopRecording(d);
    if (box.clickedButton() == delBtn) {
        r.discard(d);
        return true;
    }
    return false;
}

void MainWindow::updateRecordingIndicator() {
    Recorder& r = Recorder::instance();
    Document* rec = r.recordingDocument();
    Document* d = activeDocument();
    QString text;
    if (rec) {
        m_recBlink = !m_recBlink;
        const QString dot = m_recBlink ? "<span style='color:#e5484d'>&#9679;</span>" : "<span style='color:transparent'>&#9679;</span>";
        text = dot + " <b style='color:#e5484d'>REC</b> " + Recorder::formatLength(r.length(rec));
        if (rec != d) text += " &middot; " + rec->displayName().toHtmlEscaped();
        m_recLabel->setToolTip(T("Recording. Pause: %1, stop and save: %2.")
                                   .arg(m_actions.value("rec.toggle")->shortcut().toString(QKeySequence::NativeText),
                                        m_actions.value("rec.stop")->shortcut().toString(QKeySequence::NativeText)));
    } else if (d && r.state(d) == Recorder::State::Paused) {
        text = "<b>&#10074;&#10074; " + T("Paused").toHtmlEscaped() + "</b> " + Recorder::formatLength(r.length(d));
        m_recLabel->setToolTip(T("The recording is paused. Record continues it, Stop saves the video."));
    }
    m_recLabel->setText(text);
    m_recLabel->setVisible(!text.isEmpty());
    // toolbar buttons and menu
    const bool recording = rec != nullptr;
    const bool hasSession = (rec != nullptr) || (d && r.state(d) != Recorder::State::None);
    if (QAction* a = m_actions.value("rec.start")) {
        a->setIcon(Theme::icon(recording ? "record-on" : "record"));
        a->setText(d && r.state(d) == Recorder::State::Paused ? T("Continue recording") : T("Start recording"));
    }
    if (QAction* a = m_actions.value("rec.pause")) a->setEnabled(recording);
    if (QAction* a = m_actions.value("rec.stop")) a->setEnabled(hasSession);
    if (QAction* a = m_actions.value("rec.discard")) a->setEnabled(hasSession);
}

void MainWindow::autoFlatsDialog() {
    Document* d = activeDocument();
    if (!d) return;
    QDialog dlg(this);
    dlg.setWindowTitle(T("Auto-fill colors (flats)"));
    auto* v = new QVBoxLayout(&dlg);
    auto* info = new QLabel(T("Every closed area of the line art gets its own flat color on a new layer under the lines. "
                              "Then recolor the areas with the Fill tool (\"Borders from the current layer\")."), &dlg);
    info->setWordWrap(true);
    v->addWidget(info);
    auto* f = new QFormLayout;
    auto* gap = new QSpinBox(&dlg);
    gap->setRange(0, 30);
    gap->setValue(app()->settings().value("flats/gap", 4).toInt());
    gap->setSuffix(" px");
    f->addRow(T("Close gaps"), gap);
    auto* bg = new QCheckBox(T("Leave the background empty"), &dlg);
    bg->setChecked(app()->settings().value("flats/skipBg", true).toBool());
    f->addRow(bg);
    auto* colors = new QComboBox(&dlg);
    colors->addItems({T("Soft different colors"), T("Grays"), T("The main color")});
    colors->setCurrentIndex(app()->settings().value("flats/colors", 0).toInt());
    f->addRow(T("Colors"), colors);
    v->addLayout(f);
    bool hasRef = false;
    forEachLayer(d->root(), [&](Layer* l) {
        if (l->reference) hasRef = true;
        return !hasRef;
    });
    auto* src = new QLabel(hasRef ? T("Line art: the reference layers.") : T("Line art: the current layer (no reference layer is marked)."), &dlg);
    src->setObjectName("sectionTitle");
    v->addWidget(src);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    app()->settings().setValue("flats/gap", gap->value());
    app()->settings().setValue("flats/skipBg", bg->isChecked());
    app()->settings().setValue("flats/colors", colors->currentIndex());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    Layer* l = ops::autoFlats(*d, gap->value(), bg->isChecked(), colors->currentIndex(), app()->foreground(), &err);
    QApplication::restoreOverrideCursor();
    if (!l && !err.isEmpty()) QMessageBox::information(this, T("Auto-fill colors (flats)"), T(err));
}

void MainWindow::switchLanguage(const QString& code) {
    if (app()->prefs().language == code) return;
    app()->prefs().language = code;
    app()->preferencesChanged();
    if (QMessageBox::question(this, T("Language"), T("Restart GraphiX Paint now to switch the language?")) == QMessageBox::Yes)
        QTimer::singleShot(0, this, &MainWindow::restartApp);
}

// Closes the program (asking to save pictures) and starts it again.
void MainWindow::restartApp() {
    if (!close()) return;
    app()->shutdown();  // write every setting before the new copy reads them
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
}

void MainWindow::shortcutsDialog() {
    ShortcutsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) Shortcuts::instance().save(app()->settings());
}

void MainWindow::setDockFolded(QDockWidget* d, bool folded) {
    if (!d) return;
    const bool was = d->property("folded").toBool();
    if (folded && !was) d->setProperty("unfoldedHeight", d->height());
    d->setProperty("folded", folded);
    if (auto* arrow = d->titleBarWidget() ? d->titleBarWidget()->findChild<QToolButton*>("dockFold") : nullptr)
        arrow->setArrowType(folded ? Qt::RightArrow : Qt::DownArrow);
    if (QWidget* body = d->widget()) body->setVisible(!folded);
    // a folded panel is only its title; the panels next to it take the room
    const int h = d->titleBarWidget() ? d->titleBarWidget()->sizeHint().height() + 2 : 24;
    d->setMaximumHeight(folded ? h : QWIDGETSIZE_MAX);
    // unfolded: back to the height it had (the neighbours give the room back)
    if (!folded && was && !d->isFloating()) {
        const int back = std::max(120, d->property("unfoldedHeight").toInt());
        resizeDocks({d}, {back}, Qt::Vertical);
    }
    QStringList list;
    for (QDockWidget* x : m_docks)
        if (x->property("folded").toBool()) list << x->objectName();
    app()->settings().setValue("window/folded", list);
}

void MainWindow::resetInterface() {
    m_panelsHidden = false;
    for (QDockWidget* d : m_docks) setDockFolded(d, false);
    restoreState(m_defaultState, 2);  // positions and which panels are shown
    applyDefaultDockSizes();
    Preferences& p = app()->prefs();
    p.hiddenTools.clear();
    p.hiddenBrushTabs.clear();
    p.hiddenMaterialTabs.clear();
    p.hiddenColorParts.clear();
    app()->settings().remove("toolpanel/split");
    app()->preferencesChanged();
    app()->showStatus(T("The interface is back as it was at the start."));
}

void MainWindow::togglePanels() {
    m_panelsHidden = !m_panelsHidden;
    if (m_panelsHidden) {
        m_hiddenState = saveState(2);
        for (QDockWidget* d : m_docks) d->hide();
    } else {
        restoreState(m_hiddenState, 2);
    }
}

void MainWindow::about() {
    QMessageBox::about(this, T("About GraphiX Paint"),
                       QStringLiteral("<h3>GraphiX Paint %1</h3><p>%2</p><p>%3</p>")
                           .arg(GX_VERSION, T("A drawing program in the spirit of PaintTool SAI 2."),
                                T("Textures folder:") + "<br>" + app()->texturesDir().toHtmlEscaped()));
}

void MainWindow::addRecent(const QString& path) {
    QStringList r = app()->settings().value("recent").toStringList();
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > std::max(0, app()->prefs().recentCount)) r.removeLast();
    app()->settings().setValue("recent", r);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    QStringList r = app()->settings().value("recent").toStringList();
    for (const QString& p : r) m_recentMenu->addAction(QFileInfo(p).fileName(), this, [this, p] { openFile(p); })->setToolTip(p);
    if (r.isEmpty()) m_recentMenu->addAction(T("(empty)"))->setEnabled(false);
    else {
        m_recentMenu->addSeparator();
        m_recentMenu->addAction(T("Clear list"), this, [this] {
            app()->settings().remove("recent");
            updateRecentMenu();
        });
    }
}

void MainWindow::checkRecovery() {
    QList<Recovery::Item> items = Recovery::instance().pendingFromCrashedSessions();
    if (items.isEmpty()) return;
    RecoveryDialog dlg(items, this);
    int r = dlg.exec();
    if (r == QDialog::Accepted) {
        for (const auto& it : dlg.selected()) {
            QString err;
            auto doc = loadNative(it.file, &err);
            if (!doc) continue;
            doc->setFilePath(QFileInfo::exists(it.originalPath) && io::canKeepLayers(it.originalPath) ? it.originalPath : QString());
            doc->setModified(true);
            addDocument(std::shared_ptr<Document>(doc.release()));
        }
        Recovery::instance().discard(items);
    } else if (dlg.discardRequested()) {
        Recovery::instance().discard(items);
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    app()->tools()->commitPending();
    QSet<Document*> docs;
    for (QMdiSubWindow* sw : m_mdi->subWindowList())
        if (auto* cw = dynamic_cast<CanvasWindow*>(sw)) docs.insert(cw->view()->doc());
    for (Document* d : docs)
        if (!maybeSave(d) || !keepOrSaveRecording(d)) {
            e->ignore();
            return;
        }
    Recorder::instance().pause();  // finishes the video file
    for (QMdiSubWindow* sw : m_mdi->subWindowList())
        if (auto* cw = dynamic_cast<CanvasWindow*>(sw)) cw->canClose = nullptr;
    QSettings& s = app()->settings();
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", m_panelsHidden ? m_hiddenState : saveState(2));
    if (m_colors) m_colors->savePalette();
    app()->setActiveView(nullptr);
    m_mdi->closeAllSubWindows();
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    const QMimeData* md = e->mimeData();
    if (md->hasFormat(QString::fromLatin1(kMaterialMime)) || md->hasUrls() || md->hasImage()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const QMimeData* md = e->mimeData();
    if (md->hasFormat(QString::fromLatin1(kMaterialMime))) {
        // a material from Window > Materials: placed where it was dropped
        const QJsonObject o = QJsonDocument::fromJson(md->data(QString::fromLatin1(kMaterialMime))).object();
        std::optional<QPointF> at;
        for (QWidget* w = childAt(e->position().toPoint()); w; w = w->parentWidget())
            if (auto* v = qobject_cast<CanvasView*>(w)) {
                for (QMdiSubWindow* sw : m_mdi->subWindowList())
                    if (auto* cw = dynamic_cast<CanvasWindow*>(sw); cw && cw->view() == v) m_mdi->setActiveSubWindow(sw);
                at = v->toCanvas(v->mapFrom(this, e->position()));
                break;
            }
        if (!at) {
            app()->showStatus(T("Drop materials onto the canvas."));
            return;
        }
        placeMaterial(o.value("kind").toString(), o.value("value").toString(), at);
        e->acceptProposedAction();
        return;
    }
    if (md->hasUrls()) {
        Document* d = activeDocument();
        QWidget* target = childAt(e->position().toPoint());
        bool overCanvas = false;
        for (QWidget* w = target; w; w = w->parentWidget())
            if (qobject_cast<CanvasView*>(w)) overCanvas = true;
        for (const QUrl& u : md->urls()) {
            QString f = u.toLocalFile();
            if (f.isEmpty()) continue;
            QString ext = QFileInfo(f).suffix().toLower();
            if (overCanvas && d && ext != "gxp" && ext != "psd" && ext != "psb") {
                QImage img = io::readImage(f, nullptr);
                if (img.isNull()) continue;
                auto l = std::make_shared<RasterLayer>();
                l->name = QFileInfo(f).completeBaseName();
                imageToSurface(img, l->pixels, QPoint((d->size().width() - img.width()) / 2, (d->size().height() - img.height()) / 2));
                ops::insertLayer(*d, l, nullptr, true, T("Import layer"));
            } else {
                openFile(f);
            }
        }
        e->acceptProposedAction();
    } else if (md->hasImage()) {
        QImage img = qvariant_cast<QImage>(md->imageData());
        if (!img.isNull()) {
            auto doc = io::documentFromImage(img, T("Dropped image"));
            addDocument(std::shared_ptr<Document>(doc.release()));
        }
    }
}

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    // Keep held keys (Space, Alt...) working even when the canvas does not have focus
    // but the mouse is over it.
    if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) {
        auto* k = static_cast<QKeyEvent*>(e);
        CanvasView* v = activeView();
        if (v && v->cursorInside() && o != v && !qobject_cast<QLineEdit*>(o) && !o->inherits("QAbstractSpinBox") &&
            !o->inherits("QTextEdit") && !o->inherits("QPlainTextEdit") && !o->inherits("QKeySequenceEdit")) {
            int key = k->key();
            if (key == Qt::Key_Space || key == Qt::Key_Alt || key == Qt::Key_Control || key == Qt::Key_Shift) {
                if (e->type() == QEvent::KeyPress) app()->tools()->keyPress(k, v);
                else app()->tools()->keyRelease(k, v);
                v->refreshCursor();
                if (key == Qt::Key_Space || key == Qt::Key_Alt) return true;
            }
        }
    }
    return QMainWindow::eventFilter(o, e);
}

}  // namespace gx
