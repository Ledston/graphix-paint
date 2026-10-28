#include "app/Recorder.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QPainter>
#include <QSemaphore>
#include <QThread>
#include <QWaitCondition>
#include <QWidget>
#include <cstring>
#include <deque>

#include "app/App.h"
#include "core/Document.h"
#include "core/Layer.h"
#include "core/Projection.h"
#include "io/VideoWriter.h"
#include "ui/CanvasView.h"

namespace gx {

namespace {
constexpr qint64 kIdleCap = kVideoSecond / 2;          // a pause in drawing shows for at most half a second
constexpr qint64 kSegmentLength = 10 * 60 * kVideoSecond;  // a new file every 10 minutes (less to lose on a crash)
constexpr int kMaxQueue = 8;

int round16(double v) { return std::max(64, int(std::lround(v / 16.0)) * 16); }

// Video size for a picture of size s: at most about Full HD (as many pixels), sides divisible by 16.
QSize videoSizeFor(QSize s) {
    s = s.expandedTo(QSize(1, 1));
    const double k = std::min({1.0, 1920.0 / std::max(s.width(), s.height()), std::sqrt(1920.0 * 1080.0 / (double(s.width()) * s.height()))});
    return QSize(round16(s.width() * k), round16(s.height() * k));
}

QColor barsFor(Recorder::Mode m) { return m == Recorder::Mode::Canvas ? QColor(Qt::white) : QColor(24, 24, 26); }
}  // namespace

// ---------------------------------------------------------------------------------------
// Encodes frames on its own thread, so drawing never waits for the video encoder.
class Encoder : public QThread {
public:
    Encoder(QString path, int w, int h, int fps, QColor bars) : m_path(std::move(path)), m_w(w), m_h(h), m_fps(fps), m_bars(bars) {}
    ~Encoder() override { finish(); }

    bool begin(QString* error) {
        start();
        m_opened.acquire();
        if (!m_ok) {
            wait();
            if (error) *error = m_err;
            return false;
        }
        return true;
    }
    void push(const QImage& img, qint64 dur) {
        QMutexLocker lock(&m_mx);
        if (int(m_q.size()) >= kMaxQueue && !m_q.empty()) {
            m_q.back().dur += dur;  // the encoder is behind: keep the time, skip this picture
            return;
        }
        m_q.push_back({img, dur});
        m_cv.wakeOne();
    }
    bool finish() {
        if (!isRunning()) return m_closeOk;
        {
            QMutexLocker lock(&m_mx);
            m_stop = true;
            m_cv.wakeOne();
        }
        wait();
        return m_closeOk;
    }
    qint64 frames() const { return m_frames; }
    qint64 length() const { return m_length; }
    bool hadError() const { return m_writeError; }

protected:
    void run() override {
        VideoWriter w;
        m_ok = w.open(m_path, m_w, m_h, m_fps, &m_err);
        m_opened.release();
        if (!m_ok) return;
        for (;;) {
            Item it;
            {
                QMutexLocker lock(&m_mx);
                while (m_q.empty() && !m_stop) m_cv.wait(&m_mx);
                if (m_q.empty()) break;
                it = std::move(m_q.front());
                m_q.pop_front();
            }
            if (w.addFrame(it.img, it.dur, m_bars)) {
                ++m_frames;
                m_length += it.dur;
            } else {
                m_writeError = true;
            }
        }
        m_closeOk = w.close();
    }

private:
    struct Item {
        QImage img;
        qint64 dur = 0;
    };
    QString m_path;
    int m_w, m_h, m_fps;
    QColor m_bars;
    QMutex m_mx;
    QWaitCondition m_cv;
    std::deque<Item> m_q;
    bool m_stop = false;
    QSemaphore m_opened;
    bool m_ok = false, m_closeOk = false, m_writeError = false;
    QString m_err;
    qint64 m_frames = 0, m_length = 0;
};

// ---------------------------------------------------------------------------------------

Recorder& Recorder::instance() {
    static Recorder r;
    return r;
}

Recorder::Recorder(QObject* parent) : QObject(parent) {
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Recorder::tick);
}

void Recorder::setRoot(const QString& dir) {
    m_root = dir;
    QDir().mkpath(dir);
    // recordings of pictures that were never saved cannot be continued after a restart
    for (const QFileInfo& fi : QDir(dir).entryInfoList({"unsaved-*"}, QDir::Dirs | QDir::NoDotAndDotDot))
        QDir(fi.absoluteFilePath()).removeRecursively();
    if (app()) m_newMode = app()->settings().value("recording/mode", "canvas").toString() == "window" ? Mode::Window : Mode::Canvas;
}

void Recorder::setNewMode(Mode m) {
    m_newMode = m;
    if (app()) app()->settings().setValue("recording/mode", m == Mode::Window ? "window" : "canvas");
    Q_EMIT changed();
}

QString Recorder::folderFor(Document* d) const {
    if (!d->filePath().isEmpty()) {
        const QString abs = QFileInfo(d->filePath()).absoluteFilePath();
        QByteArray h = QCryptographicHash::hash(abs.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
        return m_root + "/" + QFileInfo(d->filePath()).completeBaseName().left(40) + "-" + QString::fromLatin1(h);
    }
    return m_root + "/unsaved-" + QString::number(d->uid(), 16);
}

QString Recorder::folder(Document* d) const {
    const Session* s = findSession(d);
    return s ? s->dir : QString();
}

void Recorder::load(Session& s) {
    s.loaded = true;
    QFile f(s.dir + "/session.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    s.mode = o.value("mode").toString() == "window" ? Mode::Window : Mode::Canvas;
    s.width = o.value("width").toInt();
    s.height = o.value("height").toInt();
    s.fps = std::clamp(o.value("fps").toInt(30), 1, 60);
    s.nextIndex = std::max(1, o.value("next").toInt(1));
    s.segments.clear();
    for (const QJsonValue& v : o.value("segments").toArray()) {
        const QJsonObject so = v.toObject();
        Segment seg;
        seg.file = so.value("file").toString();
        seg.length = qint64(so.value("length").toDouble());
        seg.frames = qint64(so.value("frames").toDouble());
        seg.closed = so.value("closed").toBool();
        // a file that was being written when the program stopped unexpectedly cannot be read
        if (!seg.closed || seg.frames <= 0 || !QFileInfo::exists(s.dir + "/" + seg.file)) continue;
        s.segments << seg;
    }
    if (s.width < 16 || s.height < 16) s.segments.clear();
}

void Recorder::save(Document* d, const Session& s) {
    QDir().mkpath(s.dir);
    QJsonArray segs;
    for (const Segment& seg : s.segments)
        segs.append(QJsonObject{{"file", seg.file}, {"length", double(seg.length)}, {"frames", double(seg.frames)}, {"closed", seg.closed}});
    QJsonObject o{{"version", 1},
                  {"mode", s.mode == Mode::Window ? "window" : "canvas"},
                  {"width", s.width},
                  {"height", s.height},
                  {"fps", s.fps},
                  {"next", s.nextIndex},
                  {"file", d ? d->filePath() : QString()},
                  {"segments", segs}};
    QFile f(s.dir + "/session.json");
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(o).toJson());
}

Recorder::Session& Recorder::session(Document* d) {
    Session& s = m_sessions[d];
    if (!s.loaded) {
        s.dir = folderFor(d);
        load(s);
    }
    return s;
}

const Recorder::Session* Recorder::findSession(Document* d) const {
    auto it = m_sessions.constFind(d);
    return it == m_sessions.cend() ? nullptr : &it.value();
}

void Recorder::watch(Document* d) {
    if (!d || m_root.isEmpty() || m_sessions.contains(d)) return;
    session(d);
    connect(d, &Document::filePathChanged, this, [this, d] {
        auto it = m_sessions.find(d);
        if (it == m_sessions.end()) return;
        const QString to = folderFor(d);
        if (to == it->dir) return;
        const bool wasRecording = m_active == d;
        if (wasRecording) pause();  // the open video file has to be closed before the folder moves
        it = m_sessions.find(d);
        if (QFileInfo::exists(it->dir)) {
            if (QFileInfo::exists(to)) QDir(to).removeRecursively();  // an old recording of the file that was overwritten
            QDir().rename(it->dir, to);
        }
        it->dir = to;
        save(d, *it);
        if (wasRecording) start(d, nullptr);
        Q_EMIT changed();
    });
    connect(d, &QObject::destroyed, this, [this, d] {
        if (m_enc && !m_active) {
            // the picture was closed while recording: finish the video file
            auto it = m_sessions.find(d);
            m_timer.stop();
            qApp->removeEventFilter(this);
            flushPending();
            if (it != m_sessions.end()) closeSegment(nullptr, *it);
            // (only our own picture cache: the canvas views are being destroyed right now)
            m_level = QImage();
            m_levelL = -1;
            m_tileVersions.clear();
            Q_EMIT changed();
        }
        m_sessions.remove(d);
    });
}

Document* Recorder::recordingDocument() const { return m_active.data(); }

Recorder::State Recorder::state(Document* d) const {
    if (d && m_active == d) return State::Recording;
    const Session* s = findSession(d);
    return s && !s->segments.isEmpty() ? State::Paused : State::None;
}

Recorder::Mode Recorder::mode(Document* d) const {
    const Session* s = findSession(d);
    return s && !s->segments.isEmpty() ? s->mode : m_newMode;
}

qint64 Recorder::length(Document* d) const {
    const Session* s = findSession(d);
    if (!s) return 0;
    qint64 t = 0;
    for (const Segment& seg : s->segments)
        if (seg.closed) t += seg.length;
    if (d && m_active == d) t += m_segTime;
    return t;
}

int Recorder::hiddenLayerCount(Document* d) {
    int n = 0;
    if (!d) return 0;
    forEachLayer(d->root(), [&](Layer* l) {
        if (l->hideInTimelapse && l->isEffectivelyVisible()) ++n;
        return true;
    });
    return n;
}

QString Recorder::formatLength(qint64 t) {
    const qint64 sec = t / kVideoSecond;
    const qint64 h = sec / 3600, m = (sec / 60) % 60, s = sec % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
                 : QStringLiteral("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

bool Recorder::openSegment(Document* d, Session& s, QString* error) {
    QDir().mkpath(s.dir);
    Segment seg;
    seg.file = QStringLiteral("part_%1.mp4").arg(s.nextIndex, 4, 10, QChar('0'));
    auto* enc = new Encoder(s.dir + "/" + seg.file, s.width, s.height, s.fps, barsFor(s.mode));
    QString err;
    if (!enc->begin(&err)) {
        delete enc;
        if (error) *error = err;
        return false;
    }
    s.nextIndex++;
    s.segments << seg;
    save(d, s);
    m_enc = enc;
    m_segTime = 0;
    return true;
}

void Recorder::closeSegment(Document* d, Session& s) {
    if (!m_enc) return;
    const bool ok = m_enc->finish();
    const qint64 frames = m_enc->frames(), len = m_enc->length();
    delete m_enc;
    m_enc = nullptr;
    m_segTime = 0;
    if (s.segments.isEmpty()) return;
    Segment& seg = s.segments.last();
    if (!ok || frames <= 0) {
        QFile::remove(s.dir + "/" + seg.file);
        s.segments.removeLast();
    } else {
        seg.frames = frames;
        seg.length = len;
        seg.closed = true;
    }
    save(d, s);
    if (s.segments.isEmpty()) QDir(s.dir).removeRecursively();
}

bool Recorder::start(Document* d, QString* error) {
    if (!d) return false;
    if (m_active == d) return true;
    if (m_active) pause();
    if (!VideoWriter::available()) {
        if (error) *error = "Windows video support (Media Foundation) is missing. On Windows N, install the Media Feature Pack.";
        return false;
    }
    watch(d);
    Session& s = session(d);
    if (s.segments.isEmpty()) {
        s.mode = m_newMode;
        s.fps = fps();
        QSize src = d->size();
        if (s.mode == Mode::Window && m_window) src = m_window->size();
        const QSize vs = videoSizeFor(src);
        s.width = vs.width();
        s.height = vs.height();
        s.nextIndex = 1;
    }
    if (s.mode == Mode::Window && !m_window) {
        if (error) *error = "Nothing to record.";
        return false;
    }
    if (!openSegment(d, s, error)) return false;
    m_active = d;
    m_changed = true;
    m_pending = {};
    m_clock.restart();
    m_sinceInput.restart();
    m_levelL = -1;
    m_docConn = connect(d, &Document::imageChanged, this, [this] { m_changed = true; });
    if (s.mode == Mode::Window) qApp->installEventFilter(this);
    m_timer.start(1000 / std::max(1, s.fps));
    tick();  // the starting picture
    Q_EMIT changed();
    return true;
}

void Recorder::pause() {
    Document* d = m_active;
    if (!d || !m_enc) return;
    m_timer.stop();
    flushPending();
    disconnect(m_docConn);
    qApp->removeEventFilter(this);
    Session& s = session(d);
    m_active = nullptr;
    closeSegment(d, s);
    cleanUpCaches(d);
    Q_EMIT changed();
}

void Recorder::cleanUpCaches(Document* d) {
    m_level = QImage();
    m_levelL = -1;
    m_tileVersions.clear();
    if (d) d->releaseRecordingProjection();
    if (m_window)
        for (CanvasView* v : m_window->findChildren<CanvasView*>()) v->dropRecordingCache();
}

void Recorder::discard(Document* d) {
    if (m_active == d) pause();
    auto it = m_sessions.find(d);
    const QString dir = it != m_sessions.end() ? it->dir : folderFor(d);
    if (it != m_sessions.end()) {
        it->segments.clear();
        it->nextIndex = 1;
    }
    QDir(dir).removeRecursively();
    Q_EMIT changed();
}

bool Recorder::eventFilter(QObject* o, QEvent* e) {
    switch (e->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::TabletMove:
    case QEvent::TabletPress:
    case QEvent::TabletRelease:
    case QEvent::KeyPress:
    case QEvent::Wheel:
        m_sinceInput.restart();
        break;
    default:
        break;
    }
    return QObject::eventFilter(o, e);
}

void Recorder::emitFrame(const QImage& img) {
    if (!m_enc || img.isNull()) return;
    const qint64 now = m_clock.nsecsElapsed() / 100;
    const qint64 frame = kVideoSecond / std::max(1, fps());
    if (m_pending.valid) {
        const qint64 dur = std::clamp(now - m_pending.at, frame, kIdleCap);
        m_enc->push(m_pending.img, dur);
        m_segTime += dur;
    }
    m_pending.img = img;
    m_pending.at = now;
    m_pending.valid = true;
}

void Recorder::flushPending() {
    if (!m_pending.valid || !m_enc) return;
    const qint64 now = m_clock.nsecsElapsed() / 100;
    const qint64 frame = kVideoSecond / std::max(1, fps());
    const qint64 dur = std::clamp(now - m_pending.at, frame, kIdleCap);
    m_enc->push(m_pending.img, dur);
    m_segTime += dur;
    m_pending = {};
}

void Recorder::tick() {
    Document* d = m_active;
    if (!d || !m_enc) return;
    Session& s = session(d);
    if (s.mode == Mode::Canvas) {
        if (!m_changed) return;
        m_changed = false;
        emitFrame(canvasFrame(d));
    } else {
        // the window: while something changes or the pen / mouse moves
        const bool busy = m_changed || m_sinceInput.elapsed() < 250;
        if (!busy) return;
        m_changed = false;
        emitFrame(windowFrame());
    }
    if (m_segTime >= kSegmentLength) {
        flushPending();
        closeSegment(d, s);
        QString err;
        if (!openSegment(d, s, &err)) {
            m_timer.stop();
            disconnect(m_docConn);
            qApp->removeEventFilter(this);
            m_active = nullptr;
            cleanUpCaches(d);
            Q_EMIT changed();
            Q_EMIT failed(err);
        }
    }
}

QImage Recorder::canvasFrame(Document* d) {
    Projection* pr = d->recordingProjection();
    const Session& s = session(d);
    const QSize ds = d->size();
    const QSize fit = ds.scaled(QSize(s.width, s.height), Qt::KeepAspectRatio);
    // the smallest level of the picture pyramid that is still at least the video size
    int L = 0;
    while (L < pr->maxLevel() && (ds.width() >> (L + 1)) >= fit.width() && (ds.height() >> (L + 1)) >= fit.height()) ++L;
    const int lw = std::max(1, (ds.width() + (1 << L) - 1) >> L), lh = std::max(1, (ds.height() + (1 << L) - 1) >> L);
    if (L != m_levelL || ds != m_levelDoc || m_level.size() != QSize(lw, lh)) {
        m_level = QImage(lw, lh, QImage::Format_ARGB32_Premultiplied);
        m_level.fill(0);
        m_levelL = L;
        m_levelDoc = ds;
        m_tileVersions.clear();
    }
    const int tx1 = (lw - 1) / TILE, ty1 = (lh - 1) / TILE;
    QVector<qint64> keys;
    keys.reserve((tx1 + 1) * (ty1 + 1));
    for (int ty = 0; ty <= ty1; ++ty)
        for (int tx = 0; tx <= tx1; ++tx) keys << tileKey(tx, ty);
    // changed parts are computed right away (the first frame of a big picture takes longer)
    for (int i = 0; i < 400 && !pr->update(L, keys, 30.0); ++i) {}
    pr->nextFrame();
    for (qint64 k : keys) {
        pr->touch(L, k);
        const Projection::Entry* e = pr->entry(L, k);
        const quint32 ver = e ? e->version : 0;
        auto it = m_tileVersions.find(k);
        if (it != m_tileVersions.end() && it.value() == ver) continue;
        m_tileVersions[k] = ver;
        const int ox = keyTx(k) * TILE, oy = keyTy(k) * TILE;
        const int w = std::min(TILE, lw - ox), h = std::min(TILE, lh - oy);
        const Tile<quint32>* t = (e && e->tile) ? e->tile.get() : nullptr;
        for (int y = 0; y < h; ++y) {
            quint32* row = reinterpret_cast<quint32*>(m_level.scanLine(oy + y)) + ox;
            if (t && t->px) std::memcpy(row, t->px.get() + y * TILE, sizeof(quint32) * size_t(w));
            else std::fill(row, row + w, t ? t->uniform : 0u);
        }
    }
    return m_level;
}

QImage Recorder::windowFrame() {
    if (!m_window) return {};
    bool hidden = false;
    for (CanvasView* v : m_window->findChildren<CanvasView*>())
        if (v->isVisible() && hiddenLayerCount(v->doc()) > 0) hidden = true;
    CanvasView::setRecordingPaint(hidden);
    QImage img = m_window->grab().toImage().convertToFormat(QImage::Format_RGB32);
    CanvasView::setRecordingPaint(false);
    // the mouse pointer (the pen's brush circle is already drawn by the canvas)
    const double k = img.devicePixelRatio();
    img.setDevicePixelRatio(1.0);  // paint in device pixels
    const QPoint c = m_window->mapFromGlobal(QCursor::pos());
    if (m_window->rect().contains(c)) {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(QPointF(c) * k);
        p.scale(k, k);
        QPolygonF arrow({QPointF(0, 0), QPointF(0, 17), QPointF(4.5, 13), QPointF(7.5, 19.5), QPointF(10, 18.5), QPointF(7, 12), QPointF(12.5, 12)});
        p.setPen(QPen(Qt::black, 1.2));
        p.setBrush(Qt::white);
        p.drawPolygon(arrow);
    }
    return img;
}

Recorder::Job Recorder::job(Document* d) const {
    Job j;
    const Session* s = findSession(d);
    if (!s) return j;
    j.width = s->width;
    j.height = s->height;
    j.fps = s->fps;
    j.mode = s->mode;
    for (const Segment& seg : s->segments) {
        if (!seg.closed) continue;
        j.files << s->dir + "/" + seg.file;
        j.length += seg.length;
    }
    return j;
}

bool Recorder::exportVideo(const Job& job, const QString& path, const ExportOptions& opt, const std::function<bool(double)>& progress,
                           QString* error) {
    if (job.files.isEmpty() || job.length <= 0) {
        if (error) *error = "Nothing was recorded yet.";
        return false;
    }
    const bool realSpeed = opt.targetLength <= 0 || opt.targetLength >= job.length;
    if (realSpeed && job.files.size() == 1 && !opt.holdEnd) {
        // one piece at real speed: the recorded file is already the video
        QFile::remove(path);
        if (QFile::copy(job.files.first(), path)) {
            if (progress) progress(1.0);
            return true;
        }
    }
    VideoWriter w;
    if (!w.open(path, job.width, job.height, job.fps, error)) return false;
    const QColor bars = barsFor(job.mode);
    const qint64 outFrame = kVideoSecond / std::max(1, job.fps);
    // sped up: every output frame stands for 'step' of recorded time
    const double step = realSpeed ? 0.0 : double(job.length) / std::max<qint64>(1, opt.targetLength / outFrame);
    qint64 nextOut = 0;  // index of the next output frame
    qint64 srcTime = 0;
    QImage img, last;
    int unreadable = 0;
    for (const QString& f : job.files) {
        VideoReader r;
        if (!r.open(f, nullptr)) {
            ++unreadable;
            continue;
        }
        qint64 dur = 0;
        while (r.readFrame(img, &dur)) {
            if (realSpeed) {
                w.addFrame(img, dur, bars);
            } else {
                const double end = double(srcTime + dur);
                qint64 n = 0;
                while (double(nextOut) * step < end) {
                    ++nextOut;
                    ++n;
                }
                if (n > 0) w.addFrame(img, n * outFrame, bars);
            }
            last = img;
            srcTime += dur;
            if (progress && !progress(std::min(1.0, double(srcTime) / double(job.length)))) {
                w.close();
                QFile::remove(path);
                if (error) error->clear();  // cancelled
                return false;
            }
        }
    }
    if (opt.holdEnd && !last.isNull()) w.addFrame(last, 2 * kVideoSecond, bars);
    const qint64 frames = w.frames();
    const bool ok = w.close();
    if (!ok || frames == 0) {
        QFile::remove(path);
        if (error) *error = unreadable ? "The recorded video could not be read." : "Could not finish the video file.";
        return false;
    }
    return true;
}

}  // namespace gx
