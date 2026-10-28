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
#include <QActionGroup>
#include <QProcess>
#include <QLineEdit>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

#include "app/App.h"
#include "app/TabletInput.h"
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
#include "tools/SelectTools.h"
#include "tools/TextTool.h"
#include "tools/ToolManager.h"
#include "tools/TransformTool.h"
#include "tools/VectorTools.h"
#include "ui/AdjustmentDialog.h"
#include "ui/CanvasView.h"
#include "ui/ColorPanel.h"
#include "ui/Dialogs.h"
#include "ui/EffectsDialog.h"
#include "ui/HistoryPanel.h"
#include "ui/LayerPanel.h"
#include "ui/NavigatorPanel.h"
#include "ui/PreferencesDialog.h"
#include "ui/ReferencePanel.h"
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

    QSettings& s = app()->settings();
    if (!restoreGeometry(s.value("window/geometry").toByteArray())) {
        QRect avail = screen() ? screen()->availableGeometry() : QRect(0, 0, 1600, 900);
        resize(avail.size() * 0.9);
        move(avail.center() - rect().center());
    }
    restoreState(s.value("window/state").toByteArray(), 2);

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
    auto* a = new QAction(T(text), this);
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
    act("layer.effects", "Layer effects...", "Layer", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), [this, cur] { if (auto* l = cur()) layerEffects(l->id); }, "fx");

    // Select
    act("select.all", "Select all", "Select", QKeySequence::SelectAll, [doc] { ops::selectAll(*doc()); });
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
    act("view.gridsettings", "Grid settings...", "View", {}, [this] { preferences(int(PreferencesDialog::Canvas)); }, {}, false);
    act("view.panels", "Hide / show panels", "View", QKeySequence(Qt::Key_Tab), [this] { togglePanels(); }, {}, false);
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
        act(QStringLiteral("brush.slot%1").arg(i + 1), QStringLiteral("Brush slot %1").arg(i + 1), "Tools", QKeySequence(Qt::Key_1 + i),
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
    add(file, "file.close");
    add(file, "file.quit");

    QMenu* edit = mb->addMenu(T("&Edit"));
    for (const char* id : {"edit.undo", "edit.redo", "", "edit.cut", "edit.copy", "edit.copymerged", "edit.paste", "edit.pastenew",
                           "", "edit.clear", "edit.fill", "", "edit.transform", "edit.warp", "edit.liquify"}) {
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
                           "layer.hide", "", "layer.fliph", "layer.flipv", "", "layer.mask", "layer.applymask",
                           "layer.deletemask", "", "layer.effects"}) {
        if (!*id) layer->addSeparator();
        else add(layer, id);
    }

    QMenu* sel = mb->addMenu(T("&Select"));
    for (const char* id : {"select.all", "select.none", "select.reselect", "select.invert", "", "select.alpha", "", "select.grow",
                           "select.shrink", "select.feather", "select.border", "", "select.show"}) {
        if (!*id) sel->addSeparator();
        else add(sel, id);
    }

    QMenu* filt = mb->addMenu(T("Fi&lter"));
    QMenu* adjM = filt->addMenu(T("Adjustments"));
    for (int t = 0; t < int(AdjustmentType::Count); ++t) add(adjM, QStringLiteral("adjust.%1").arg(t));
    filt->addSeparator();
    for (const auto& f : AdjustmentDialog::filterIds()) add(filt, "filter." + f.first);

    QMenu* viewM = mb->addMenu(T("&View"));
    for (const char* id : {"view.newview", "", "view.zoomin", "view.zoomout", "view.fit", "view.actual", "", "view.rotl", "view.rotr",
                           "view.rotreset", "view.flip", "view.reset", "", "view.checker", "view.pixelgrid"}) {
        if (!*id) viewM->addSeparator();
        else add(viewM, id);
    }
    QMenu* gridM = viewM->addMenu(Theme::icon("grid"), T("Grid and guides"));
    for (const char* id : {"view.grid", "view.thirds", "view.golden", "view.center", "", "view.gridsettings"}) {
        if (!*id) gridM->addSeparator();
        else add(gridM, id);
    }
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
    for (const char* id : {"help.textures", "help.reloadtextures", "help.data", "", "help.about"}) {
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
    });
}

void MainWindow::createDocks() {
    auto dock = [this](const QString& id, const QString& title, QWidget* w, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(T(title), this);
        d->setObjectName(id);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        // Plain title without the small float / close buttons: drag the title to move the
        // panel out, double-click it to put it back. Panels are shown and hidden in Window.
        auto* head = new QLabel(d->windowTitle(), d);
        head->setObjectName("dockTitle");
        head->setToolTip(T("Drag to move the panel. Double-click to put it back into the window."));
        connect(d, &QDockWidget::windowTitleChanged, head, &QLabel::setText);
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
    auto* refDock = dock("referenceDock", "Reference", new ReferencePanel, Qt::RightDockWidgetArea);
    auto* scratchDock = dock("scratchDock", "Scratchpad", new ScratchpadPanel, Qt::RightDockWidgetArea);
    tabifyDockWidget(histDock, refDock);
    tabifyDockWidget(refDock, scratchDock);
    histDock->raise();
    resizeDocks({colorDock, toolDock}, {320, 560}, Qt::Vertical);
    resizeDocks({colorDock}, {270}, Qt::Horizontal);
    resizeDocks({layerDock}, {300}, Qt::Horizontal);
    resizeDocks({navDock, layerDock, histDock}, {180, 480, 200}, Qt::Vertical);

    connect(m_layers, &LayerPanel::requestEffects, this, &MainWindow::layerEffects);
    connect(m_layers, &LayerPanel::requestAdjustmentEdit, this, &MainWindow::editAdjustmentLayer);
    connect(m_layers, &LayerPanel::requestNewAdjustment, this, [this](int t) { adjustment(t, true); });
}

void MainWindow::createStatusBar() {
    m_statusMsg = new QLabel(this);
    m_statusSize = new QLabel(this);
    m_statusZoom = new QLabel(this);
    m_statusPos = new QLabel(this);
    statusBar()->addWidget(m_statusMsg, 1);
    statusBar()->addPermanentWidget(m_statusPos);
    statusBar()->addPermanentWidget(m_statusSize);
    statusBar()->addPermanentWidget(m_statusZoom);
}

// ============================================================================ documents

CanvasView* MainWindow::addDocument(std::shared_ptr<Document> doc) {
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
    setWindowTitle(d ? QStringLiteral("%1%2 - GraphiX Paint").arg(d->displayName(), d->isModified() ? " *" : "") : "GraphiX Paint");
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

void MainWindow::layerEffects(quint32 id) {
    Document* d = activeDocument();
    if (!d || !d->layer(id)) return;
    Layer* l = d->layer(id);
    if (l->isGroup() || l->type() == LayerType::Adjustment) {
        app()->showStatus(T("Effects work on pixel, linework and text layers."));
        return;
    }
    EffectsDialog dlg(d, id, this);
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
        if (!maybeSave(d)) {
            e->ignore();
            return;
        }
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
    if (e->mimeData()->hasUrls() || e->mimeData()->hasImage()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const QMimeData* md = e->mimeData();
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
