#pragma once
#include <QDateTime>
#include <QHash>
#include <QLockFile>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>

namespace gx {

class Document;

// "Recover work" like SAI: every few minutes changed documents are written in the
// background to a per-session folder. If the program does not close normally,
// the next start offers to restore them.
class Recovery : public QObject {
    Q_OBJECT
public:
    struct Item {
        QString file;          // recovery .gxp
        QString title;
        QString originalPath;
        QDateTime time;
        QString sessionDir;
    };

    static Recovery& instance();

    void start(const QString& baseDir, int intervalMinutes);
    void setInterval(int minutes);
    void watch(Document* doc);
    void unwatch(Document* doc);
    void markSaved(Document* doc);  // the user saved: recovery copy is no longer needed
    void shutdown();                // clean exit: remove this session's files

    QList<Item> pendingFromCrashedSessions() const;
    void discard(const QList<Item>& items);

Q_SIGNALS:
    void saved(const QString& title);

private:
    void tick();
    void saveDoc(Document* doc);
    struct State {
        QPointer<Document> doc;
        QDateTime savedChange;
        bool running = false;
        QString file;
    };
    QString m_base, m_session;
    std::unique_ptr<QLockFile> m_lock;
    QTimer m_timer;
    QHash<Document*, State> m_docs;
};

}  // namespace gx
