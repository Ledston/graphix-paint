#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace gx {

class Document;

// Records the picture after each change while recording is on, and makes a video of it.
// Frames are kept next to the settings, one folder per picture, so recording can go on
// over several days (the folder follows the file when it is saved under a new name).
class Timelapse : public QObject {
    Q_OBJECT
public:
    static Timelapse& instance();
    void setRoot(const QString& dir);

    bool isRecording(Document* d) const;
    void setRecording(Document* d, bool on);
    int frameCount(Document* d) const;
    QString folder(Document* d) const;
    void watch(Document* d);  // call for every open document
    void forget(Document* d);
    void deleteFrames(Document* d);
    // Makes the video. Returns false with an error message.
    bool exportVideo(Document* d, const QString& path, double seconds, QString* error);
    // Layers marked "Hide from the timelapse" that are visible right now.
    static int hiddenLayerCount(Document* d);

Q_SIGNALS:
    void changed();

private:
    explicit Timelapse(QObject* parent = nullptr);
    struct State {
        QString dir;
        bool recording = false;
        int frames = 0;
        QElapsedTimer last;
        bool pending = false;
        bool rendering = false;  // a frame without hidden layers is being drawn in the background
        bool again = false;      // something changed meanwhile
    };
    QString folderFor(Document* d) const;
    void capture(Document* d);
    void scheduleCapture(Document* d);
    void writeInfo(Document* d);
    QString m_root;
    QHash<Document*, State> m_docs;
};

}  // namespace gx
