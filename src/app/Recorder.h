#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>

class QWidget;

namespace gx {

class Document;
class Encoder;

// Records a video of the drawing straight into MP4 (File > Record video).
//  - Record / Pause / Stop. Stop asks for the speed and where to save the video.
//  - Two modes: only the canvas (the picture as it is drawn), or the whole program window.
//  - Only the time you draw is recorded: when nothing changes, the video waits.
//  - Layers marked "Hide from the recording" and the reference over everything are left out.
//  - A paused recording is kept next to the settings (per picture file), so it can go on
//    after the program was closed: open the file and press Record again.
class Recorder : public QObject {
    Q_OBJECT
public:
    enum class Mode { Canvas, Window };
    enum class State { None, Recording, Paused };

    static Recorder& instance();
    void setRoot(const QString& dir);
    void setWindow(QWidget* w) { m_window = w; }  // what the window mode records

    // mode for the next new recording (a started recording keeps its own)
    Mode newMode() const { return m_newMode; }
    void setNewMode(Mode m);

    void watch(Document* d);  // call for every open document
    State state(Document* d) const;
    Mode mode(Document* d) const;
    qint64 length(Document* d) const;  // recorded time, 100 ns units
    Document* recordingDocument() const;
    QString folder(Document* d) const;

    bool start(Document* d, QString* error);  // starts or continues
    void pause();                             // the recording that runs now
    void discard(Document* d);                // deletes the recording of d

    // Saving the video (can run on a worker thread: take the job on the GUI thread first).
    struct Job {
        QStringList files;
        qint64 length = 0;
        int width = 0, height = 0, fps = 30;
        Mode mode = Mode::Canvas;
    };
    Job job(Document* d) const;
    struct ExportOptions {
        qint64 targetLength = 0;  // 0 = real speed, otherwise the video is sped up to this length
        bool holdEnd = true;      // show the finished picture for 2 more seconds
    };
    // progress gets 0..1 and returns false to cancel
    static bool exportVideo(const Job& job, const QString& path, const ExportOptions& opt,
                            const std::function<bool(double)>& progress, QString* error);

    static int hiddenLayerCount(Document* d);
    static QString formatLength(qint64 t);  // 1:02:03 or 12:34
    int fps() const { return 30; }

    // Makes a frame now if something changed (the timer calls it; tests call it directly).
    void tick();

Q_SIGNALS:
    void changed();                       // started, paused, stopped
    void failed(const QString& message);  // the recording had to stop

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    explicit Recorder(QObject* parent = nullptr);
    struct Segment {
        QString file;
        qint64 length = 0;
        qint64 frames = 0;
        bool closed = false;
    };
    struct Session {
        QString dir;
        bool loaded = false;
        Mode mode = Mode::Canvas;
        int width = 0, height = 0, fps = 30;
        QList<Segment> segments;
        int nextIndex = 1;
    };
    Session& session(Document* d);
    const Session* findSession(Document* d) const;
    QString folderFor(Document* d) const;
    void load(Session& s);
    void save(Document* d, const Session& s);
    bool openSegment(Document* d, Session& s, QString* error);
    void closeSegment(Document* d, Session& s);
    void emitFrame(const QImage& img);
    void flushPending();
    QImage canvasFrame(Document* d);
    QImage windowFrame();
    void cleanUpCaches(Document* d);

    QString m_root;
    QPointer<QWidget> m_window;
    Mode m_newMode = Mode::Canvas;
    QHash<Document*, Session> m_sessions;

    // the running recording
    QPointer<Document> m_active;
    Encoder* m_enc = nullptr;
    QTimer m_timer;
    QMetaObject::Connection m_docConn;
    bool m_changed = false;
    QElapsedTimer m_clock, m_sinceInput;
    qint64 m_segTime = 0;  // pushed into the current segment
    struct Pending {
        QImage img;
        qint64 at = 0;
        bool valid = false;
    } m_pending;
    // canvas mode: the picture at one level of the projection, updated tile by tile
    QImage m_level;
    int m_levelL = -1;
    QSize m_levelDoc;
    QHash<qint64, quint32> m_tileVersions;
};

}  // namespace gx
