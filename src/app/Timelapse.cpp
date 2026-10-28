#include "app/Timelapse.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QPainter>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include "core/Compositor.h"
#include "core/Document.h"
#include "io/VideoWriter.h"

namespace gx {

Timelapse& Timelapse::instance() {
    static Timelapse t;
    return t;
}

Timelapse::Timelapse(QObject* parent) : QObject(parent) {}

void Timelapse::setRoot(const QString& dir) {
    m_root = dir;
    QDir().mkpath(dir);
}

QString Timelapse::folderFor(Document* d) const {
    if (!d->filePath().isEmpty()) {
        QByteArray h = QCryptographicHash::hash(QFileInfo(d->filePath()).absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
        return m_root + "/" + QFileInfo(d->filePath()).completeBaseName().left(40) + "-" + QString::fromLatin1(h);
    }
    return m_root + "/unsaved-" + QString::number(d->uid(), 16);
}

static int countFrames(const QString& dir) {
    return int(QDir(dir).entryList({"frame_*.jpg"}, QDir::Files).size());
}

void Timelapse::watch(Document* d) {
    if (!d || m_docs.contains(d) || m_root.isEmpty()) return;
    State s;
    s.dir = folderFor(d);
    s.frames = countFrames(s.dir);
    QFile info(s.dir + "/timelapse.json");
    if (info.open(QIODevice::ReadOnly)) s.recording = QJsonDocument::fromJson(info.readAll()).object().value("recording").toBool();
    m_docs.insert(d, s);
    connect(d, &Document::historyChanged, this, [this, d] { scheduleCapture(d); });
    connect(d, &Document::filePathChanged, this, [this, d] {
        auto it = m_docs.find(d);
        if (it == m_docs.end()) return;
        const QString to = folderFor(d);
        if (to == it->dir) return;
        if (QFileInfo::exists(it->dir) && !QFileInfo::exists(to)) QDir().rename(it->dir, to);
        it->dir = to;
        it->frames = countFrames(to);
        Q_EMIT changed();
    });
    connect(d, &QObject::destroyed, this, [this, d] { m_docs.remove(d); });
}

void Timelapse::forget(Document* d) { m_docs.remove(d); }

bool Timelapse::isRecording(Document* d) const {
    auto it = m_docs.constFind(d);
    return it != m_docs.cend() && it->recording;
}

int Timelapse::frameCount(Document* d) const {
    auto it = m_docs.constFind(d);
    return it == m_docs.cend() ? 0 : it->frames;
}

QString Timelapse::folder(Document* d) const {
    auto it = m_docs.constFind(d);
    return it == m_docs.cend() ? QString() : it->dir;
}

void Timelapse::writeInfo(Document* d) {
    auto it = m_docs.find(d);
    if (it == m_docs.end()) return;
    QDir().mkpath(it->dir);
    QFile f(it->dir + "/timelapse.json");
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(QJsonObject{{"recording", it->recording}, {"file", d->filePath()}}).toJson());
}

void Timelapse::setRecording(Document* d, bool on) {
    watch(d);
    auto it = m_docs.find(d);
    if (it == m_docs.end()) return;
    it->recording = on;
    writeInfo(d);
    if (on) capture(d);  // the starting point
    Q_EMIT changed();
}

void Timelapse::scheduleCapture(Document* d) {
    auto it = m_docs.find(d);
    if (it == m_docs.end() || !it->recording || it->pending) return;
    // at most about two frames a second; the last change is always caught
    const qint64 wait = it->last.isValid() ? std::max<qint64>(0, 500 - it->last.elapsed()) : 0;
    it->pending = true;
    QPointer<Document> guard(d);
    QTimer::singleShot(int(wait) + 60, this, [this, guard] {
        if (!guard) return;
        auto it2 = m_docs.find(guard.data());
        if (it2 == m_docs.end()) return;
        it2->pending = false;
        capture(guard.data());
    });
}

int Timelapse::hiddenLayerCount(Document* d) {
    int n = 0;
    if (!d) return 0;
    forEachLayer(d->root(), [&](Layer* l) {
        if (l->hideInTimelapse && l->isEffectivelyVisible()) ++n;
        return true;
    });
    return n;
}

namespace {
QImage frameImage(const QImage& img) {
    // about 1600 px across, flattened on white paper
    QImage scaled = std::max(img.width(), img.height()) > 1600 ? img.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation) : img;
    QImage out(scaled.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    p.drawImage(0, 0, scaled);
    p.end();
    return out;
}
}  // namespace

void Timelapse::capture(Document* d) {
    auto it = m_docs.find(d);
    if (it == m_docs.end() || !it->recording) return;
    if (hiddenLayerCount(d) > 0) {
        // Some layers stay out of the video: draw a copy of the picture without them,
        // in the background (the copy shares the pixels, so it costs almost no memory).
        if (it->rendering) {
            it->again = true;
            return;
        }
        auto root = std::static_pointer_cast<GroupLayer>(d->root()->clone());
        forEachLayer(root.get(), [](Layer* l) {
            if (l->hideInTimelapse) l->visible = false;
            return true;
        });
        const QRect bounds = d->bounds();
        QDir().mkpath(it->dir);
        const QString file = QStringLiteral("%1/frame_%2.jpg").arg(it->dir).arg(it->frames, 6, 10, QChar('0'));
        it->frames++;
        it->last.restart();
        it->rendering = true;
        QPointer<Document> guard(d);
        QtConcurrent::run([root, bounds, file] {
            // Draw in strips and shrink each one, so a huge canvas never needs one huge image.
            const QSize outSize = bounds.size().width() > 1600 || bounds.size().height() > 1600
                                      ? bounds.size().scaled(1600, 1600, Qt::KeepAspectRatio)
                                      : bounds.size();
            QImage out(outSize.expandedTo(QSize(1, 1)), QImage::Format_RGB32);
            out.fill(Qt::white);
            QPainter p(&out);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            const double sx = double(out.width()) / bounds.width(), sy = double(out.height()) / bounds.height();
            const int stripH = std::max(128, int(16'000'000 / std::max(1, bounds.width())) / 128 * 128);
            for (int y = bounds.top(); y <= bounds.bottom(); y += stripH) {
                QRect strip(bounds.left(), y, bounds.width(), std::min(stripH, bounds.bottom() + 1 - y));
                QImage part = Compositor::renderImage(root.get(), strip);
                if (part.isNull()) continue;
                p.drawImage(QRectF((strip.left() - bounds.left()) * sx, (strip.top() - bounds.top()) * sy,
                                   strip.width() * sx, strip.height() * sy),
                            part);
            }
            p.end();
            out.save(file, "JPG", 90);
        }).then(this, [this, guard] {
            if (!guard) return;
            auto it2 = m_docs.find(guard.data());
            if (it2 == m_docs.end()) return;
            it2->rendering = false;
            if (it2->again) {
                it2->again = false;
                scheduleCapture(guard.data());
            }
        });
        Q_EMIT changed();
        return;
    }
    QImage img;
    // the whole picture, about 1600 px across
    for (int i = 0; i < 60 && !d->projection()->thumbnail(1600, 50.0, img); ++i) {}
    if (img.isNull()) return;
    QDir().mkpath(it->dir);
    const QString file = QStringLiteral("%1/frame_%2.jpg").arg(it->dir).arg(it->frames, 6, 10, QChar('0'));
    it->frames++;
    it->last.restart();
    // flatten on white and write in the background
    (void)QtConcurrent::run([img, file] { frameImage(img).save(file, "JPG", 90); });
    Q_EMIT changed();
}

void Timelapse::deleteFrames(Document* d) {
    auto it = m_docs.find(d);
    if (it == m_docs.end()) return;
    QDir(it->dir).removeRecursively();
    it->frames = 0;
    it->recording = false;
    Q_EMIT changed();
}

bool Timelapse::exportVideo(Document* d, const QString& path, double seconds, QString* error) {
    auto it = m_docs.find(d);
    if (it == m_docs.end() || it->frames == 0) {
        if (error) *error = "Nothing was recorded yet. Turn on File > Timelapse > Record first.";
        return false;
    }
    QThreadPool::globalInstance()->waitForDone(5000);  // frames still being written
    QStringList files = QDir(it->dir).entryList({"frame_*.jpg"}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        if (error) *error = "Nothing was recorded yet. Turn on File > Timelapse > Record first.";
        return false;
    }
    // video size: the last frame (the finished picture), at most Full HD
    QImageReader probe(it->dir + "/" + files.last());
    QSize s = probe.size().scaled(QSize(1920, 1080), Qt::KeepAspectRatio);
    if (s.width() > probe.size().width()) s = probe.size();
    const double frames = files.size();
    const int fps = int(std::clamp(frames / std::max(1.0, seconds), 2.0, 60.0));
    VideoWriter vw;
    if (!vw.open(path, s.width(), s.height(), fps, error)) return false;
    QImage last;
    for (const QString& f : files) {
        QImage img(it->dir + "/" + f);
        if (img.isNull()) continue;
        if (!vw.addFrame(img)) {
            if (error) *error = "Could not write the video.";
            vw.close();
            return false;
        }
        last = img;
    }
    for (int i = 0; i < fps * 2 && !last.isNull(); ++i) vw.addFrame(last);  // hold the finished picture for 2 s
    if (!vw.close()) {
        if (error) *error = "Could not finish the video file.";
        return false;
    }
    return true;
}

}  // namespace gx
