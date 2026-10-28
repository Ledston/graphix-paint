#pragma once
#include <QImage>
#include <QString>
#include <memory>

namespace gx {

// Writes an H.264 MP4 video with Windows Media Foundation.
// Other systems (only used for development and tests) get a simple JPEG stream
// in the same file, which VideoReader can read back.
// Use one object from one thread only.
class VideoWriter {
public:
    VideoWriter();
    ~VideoWriter();
    // false when this computer cannot write videos (for example Windows N without the Media Feature Pack)
    static bool available();
    // width and height are rounded down to even numbers
    bool open(const QString& path, int width, int height, int fps, QString* error);
    // Adds a frame shown for 'duration' (in 100 ns units; 0 = one frame at the frame rate).
    // A frame of another size is fitted into the video with bars of the given color.
    bool addFrame(const QImage& frame, qint64 duration = 0, const QColor& bars = Qt::black);
    bool close();
    bool isOpen() const;
    int width() const { return m_w; }
    int height() const { return m_h; }
    int fps() const { return m_fps; }
    qint64 written() const { return m_time; }  // total length so far, 100 ns units
    qint64 frames() const { return m_frame; }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    int m_w = 0, m_h = 0, m_fps = 30;
    long long m_frame = 0;
    qint64 m_time = 0;
};

// Reads the frames of a video written by VideoWriter (or any MP4 Windows can play).
class VideoReader {
public:
    VideoReader();
    ~VideoReader();
    bool open(const QString& path, QString* error);
    // The next frame and how long it is shown (100 ns units). false at the end.
    bool readFrame(QImage& out, qint64* duration);
    void close();
    int width() const { return m_w; }
    int height() const { return m_h; }

private:
    bool decodeNext(QImage& img, qint64& ts, qint64& dur);  // Windows
    struct Impl;
    std::unique_ptr<Impl> d;
    int m_w = 0, m_h = 0;
    double m_fps = 30;
    qint64 m_lastTime = -1;
};

// 100 ns units per second (the unit Media Foundation uses)
constexpr qint64 kVideoSecond = 10000000LL;

}  // namespace gx
