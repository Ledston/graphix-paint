#pragma once
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <memory>
#include <optional>
#include <QPointF>

class QMdiArea;
class QMdiSubWindow;
class QLabel;
class QComboBox;
class QSpinBox;
class QToolButton;
class QMenu;
class QDockWidget;

namespace gx {

class CanvasView;
class Document;
class LayerPanel;
class ReferencePanel;
class ColorPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    CanvasView* addDocument(std::shared_ptr<Document> doc);
    CanvasView* activeView() const;
    Document* activeDocument() const;
    bool openFile(const QString& path);
    void checkRecovery();

    // File > Video recording
    void startRecording();
    void pauseRecording();
    bool stopRecording(Document* d = nullptr);  // asks for the speed and the file, saves the video

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    // setup
    void createTools();
    void createActions();
    void createMenus();
    void createToolbar();
    void createDocks();
    void createStatusBar();
    QAction* act(const QString& id, const QString& text, const QString& category, const QKeySequence& key,
                 std::function<void()> fn, const QString& icon = {}, bool needsDoc = true);
    void updateActions();
    void updateTitle();
    void updateRecentMenu();
    void addRecent(const QString& path);

    // commands
    void newDocument();
    void openDialog();
    bool save(Document* d, bool saveAs);
    void exportDialog();
    void importAsLayer();
    bool maybeSave(Document* d);
    bool closeDocumentViews(Document* d);
    void newView();
    void copy(bool merged, bool cut);
    void paste(bool asNewDocument);
    void fill();
    // Window > Materials, Edit > Fill with pattern / Stroke selection
    void placeMaterial(const QString& kind, const QString& value, std::optional<QPointF> at = {});
    void fillPattern();
    void strokeSelectionDialog();
    void saveSelectionAsMaterial(bool asPattern);
    void applyGradientMap(int index);
    void adjustment(int type, bool asLayer);
    void filter(const QString& id);
    void effectFilter(const QString& key);  // Filter > Effects
    void commandPalette();                  // Help > Find a command (Ctrl+F)
    void layerEffects(quint32 id, const QString& focus = {});
    void editAdjustmentLayer(quint32 id);
    void canvasSize();
    void imageSize();
    void preferences(int page = 0);
    void switchLanguage(const QString& code);
    void autoFlatsDialog();
    bool keepOrSaveRecording(Document* d);  // when a picture is closed
    void updateRecordingIndicator();
    void restartApp();
    void shortcutsDialog();
    void togglePanels();
    void setDockFolded(QDockWidget* d, bool folded);  // the arrow on a panel title
    void applyDefaultDockSizes();
    void applySaiKeysOnce();  // W Q R E H [ ] F, A S D Z C B and the digits, once over older keys
public:
    Q_INVOKABLE void resetInterface();  // Window > Reset the interface
private:
    void about();

    QMdiArea* m_mdi;
    QHash<QString, QAction*> m_actions;
    QList<QAction*> m_docActions;
    QMenu* m_recentMenu = nullptr;
    LayerPanel* m_layers = nullptr;
    ReferencePanel* m_refPanel = nullptr;
    ColorPanel* m_colors = nullptr;
    QList<QDockWidget*> m_docks;
    QByteArray m_hiddenState;
    QByteArray m_defaultState;
    bool m_panelsHidden = false;
    QLabel* m_statusPos = nullptr;
    QLabel* m_statusZoom = nullptr;
    QLabel* m_statusSize = nullptr;
    QLabel* m_statusMsg = nullptr;
    QLabel* m_recLabel = nullptr;
    bool m_recBlink = false;
    QComboBox* m_stabMode = nullptr;
    QSpinBox* m_stabLevel = nullptr;
    QSpinBox* m_pressSmooth = nullptr;
    QToolButton* m_snapBtn = nullptr;
    QComboBox* m_symBox = nullptr;
    QToolButton* m_rulerBtn = nullptr;
    void syncGuides();  // toolbar buttons follow the rulers of the active picture
};

}  // namespace gx
