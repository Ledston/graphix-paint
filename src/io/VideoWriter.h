#pragma once
#include <QImage>
#include <QString>
#include <memory>

namespace gx {

// Writes an H.264 MP4 video (Windows Media Foundation). On other systems open() fails.
class VideoWriter {
public:
    VideoWriter();
    ~VideoWriter();
    static bool available();
    // width and height are rounded down to even numbers
    bool open(const QString& path, int width, int height, int fps, QString* error);
    bool addFrame(const QImage& frame);  // any size: fitted into the video with black bars
    bool close();
    int width() const { return m_w; }
    int height() const { return m_h; }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    int m_w = 0, m_h = 0, m_fps = 30;
    long long m_frame = 0;
};

}  // namespace gx
