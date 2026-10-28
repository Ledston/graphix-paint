#pragma once
#include <QPointer>
#include <QWidget>
#include <memory>

class QListWidget;

namespace gx {

class Document;

class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(QWidget* parent = nullptr);
private:
    void setDocument(Document* d);
    void rebuild();
    QPointer<Document> m_doc;
    QListWidget* m_list;
    bool m_updating = false;
};

class ScratchpadPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScratchpadPanel(QWidget* parent = nullptr);
    ~ScratchpadPanel() override;
private:
    void save();
    std::shared_ptr<Document> m_doc;
    QWidget* m_view = nullptr;
};

}  // namespace gx
