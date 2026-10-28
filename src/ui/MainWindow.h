#pragma once
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <memory>

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
    void adjustment(int type, bool asLayer);
    void filter(const QString& id);
    void layerEffects(quint32 id);
    void editAdjustmentLayer(quint32 id);
    void canvasSize();
    void imageSize();
    void preferences(int page = 0);
    void switchLanguage(const QString& code);
    void restartApp();
    void shortcutsDialog();
    void togglePanels();
    void about();

    QMdiArea* m_mdi;
    QHash<QString, QAction*> m_actions;
    QList<QAction*> m_docActions;
    QMenu* m_recentMenu = nullptr;
    LayerPanel* m_layers = nullptr;
    ColorPanel* m_colors = nullptr;
    QList<QDockWidget*> m_docks;
    QByteArray m_hiddenState;
    bool m_panelsHidden = false;
    QLabel* m_statusPos = nullptr;
    QLabel* m_statusZoom = nullptr;
    QLabel* m_statusSize = nullptr;
    QLabel* m_statusMsg = nullptr;
    QComboBox* m_stabMode = nullptr;
    QSpinBox* m_stabLevel = nullptr;
    QSpinBox* m_pressSmooth = nullptr;
    QToolButton* m_snapBtn = nullptr;
    QComboBox* m_symBox = nullptr;
};

}  // namespace gx
