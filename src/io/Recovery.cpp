#include "io/Recovery.h"

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include "core/Document.h"
#include "io/NativeFormat.h"

namespace gx {

Recovery& Recovery::instance() {
    static Recovery r;
    return r;
}

void Recovery::start(const QString& baseDir, int minutes) {
    m_base = baseDir;
    QDir().mkpath(m_base);
    m_session = m_base + "/session-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(m_session);
    m_lock = std::make_unique<QLockFile>(m_session + "/lock");
    m_lock->setStaleLockTime(0);
    m_lock->tryLock(100);
    connect(&m_timer, &QTimer::timeout, this, &Recovery::tick, Qt::UniqueConnection);
    setInterval(minutes);
}

void Recovery::setInterval(int minutes) {
    if (minutes <= 0) {
        m_timer.stop();
        return;
    }
    m_timer.start(minutes * 60 * 1000);
}

void Recovery::watch(Document* doc) {
    if (!doc || m_docs.contains(doc)) return;
    State s;
    s.doc = doc;
    s.savedChange = doc->lastChange();
    s.file = m_session + QStringLiteral("/doc-%1.gxp").arg(doc->uid(), 16, 16, QChar('0'));
    m_docs.insert(doc, s);
}

void Recovery::unwatch(Document* doc) {
    auto it = m_docs.find(doc);
    if (it == m_docs.end()) return;
    QString f = it->file;
    m_docs.erase(it);
    QFile::remove(f);
    QFile::remove(f + ".json");
}

void Recovery::markSaved(Document* doc) {
    auto it = m_docs.find(doc);
    if (it == m_docs.end()) return;
    it->savedChange = doc->lastChange();
    QFile::remove(it->file);
    QFile::remove(it->file + ".json");
}

void Recovery::tick() {
    for (auto it = m_docs.begin(); it != m_docs.end(); ++it) {
        Document* d = it->doc;
        if (!d || it->running) continue;
        if (!d->isModified() || d->lastChange() <= it->savedChange) continue;
        saveDoc(d);
    }
}

void Recovery::saveDoc(Document* doc) {
    auto it = m_docs.find(doc);
    if (it == m_docs.end()) return;
    it->running = true;
    it->savedChange = doc->lastChange();
    DocSnapshot snap = takeSnapshot(*doc);
    QString file = it->file;
    QJsonObject meta{{"title", doc->displayName()}, {"path", doc->filePath()},
                     {"time", QDateTime::currentDateTime().toString(Qt::ISODate)}};
    QString title = doc->displayName();
    QPointer<Recovery> self(this);
    auto future = QtConcurrent::run([snap = std::move(snap), file, meta]() mutable {
        QString err;
        bool ok = saveNative(snap, file, &err);
        if (ok) {
            QFile m(file + ".json");
            if (m.open(QIODevice::WriteOnly)) m.write(QJsonDocument(meta).toJson());
        }
        return ok;
    });
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, doc, title]() {
        auto it2 = m_docs.find(doc);
        if (it2 != m_docs.end()) it2->running = false;
        if (watcher->result()) Q_EMIT saved(title);
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void Recovery::shutdown() {
    m_timer.stop();
    if (m_session.isEmpty()) return;
    m_lock.reset();
    QDir(m_session).removeRecursively();
}

QList<Recovery::Item> Recovery::pendingFromCrashedSessions() const {
    QList<Item> items;
    QDir base(m_base);
    for (const QString& s : base.entryList({"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString dir = base.filePath(s);
        if (dir == m_session) continue;
        QLockFile lock(dir + "/lock");
        lock.setStaleLockTime(0);
        if (!lock.tryLock(10)) continue;  // another running instance
        lock.unlock();
        QDir d(dir);
        for (const QString& f : d.entryList({"*.gxp"}, QDir::Files)) {
            Item it;
            it.file = d.filePath(f);
            it.sessionDir = dir;
            QFile m(it.file + ".json");
            if (m.open(QIODevice::ReadOnly)) {
                QJsonObject o = QJsonDocument::fromJson(m.readAll()).object();
                it.title = o.value("title").toString();
                it.originalPath = o.value("path").toString();
                it.time = QDateTime::fromString(o.value("time").toString(), Qt::ISODate);
            }
            if (it.title.isEmpty()) it.title = f;
            items << it;
        }
        if (d.entryList({"*.gxp"}, QDir::Files).isEmpty()) d.removeRecursively();
    }
    return items;
}

void Recovery::discard(const QList<Item>& items) {
    QSet<QString> dirs;
    for (const auto& it : items) {
        QFile::remove(it.file);
        QFile::remove(it.file + ".json");
        dirs.insert(it.sessionDir);
    }
    for (const QString& d : dirs) {
        QDir dir(d);
        if (dir.entryList({"*.gxp"}, QDir::Files).isEmpty()) dir.removeRecursively();
    }
}

}  // namespace gx
