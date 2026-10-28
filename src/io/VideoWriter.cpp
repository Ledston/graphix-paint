#include "io/VideoWriter.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QPainter>
#include <algorithm>
#include <cstring>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

namespace gx {

namespace {
// The frame at exactly the video size, as 0xffRRGGBB.
QImage fitFrame(const QImage& frame, int w, int h, const QColor& bars) {
    if (frame.size() == QSize(w, h) && frame.format() == QImage::Format_RGB32) return frame;
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(bars);
    QPainter p(&img);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QSizeF s = QSizeF(frame.size()).scaled(QSizeF(w, h), Qt::KeepAspectRatio);
    p.drawImage(QRectF((w - s.width()) / 2, (h - s.height()) / 2, s.width(), s.height()), frame);
    p.end();
    return img;
}
}  // namespace

#ifdef Q_OS_WIN

namespace {
template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// Media Foundation is loaded only when needed (the program also starts on Windows "N"
// editions, which come without it).
bool mfPresent() {
    static int ok = -1;
    if (ok < 0) ok = (LoadLibraryW(L"mfplat.dll") && LoadLibraryW(L"mfreadwrite.dll")) ? 1 : 0;
    return ok == 1;
}

struct MfSession {
    bool com = false, mf = false;
    bool start(QString* error) {
        if (!mfPresent()) {
            if (error) *error = "Windows video support (Media Foundation) is missing. On Windows N, install the Media Feature Pack.";
            return false;
        }
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com = SUCCEEDED(hr);  // S_FALSE or RPC_E_CHANGED_MODE: COM is already set up on this thread
        if (FAILED(MFStartup(MF_VERSION))) {
            if (error) *error = "Windows video support (Media Foundation) could not start.";
            return false;
        }
        mf = true;
        return true;
    }
    void stop() {
        if (mf) MFShutdown();
        if (com) CoUninitialize();
        mf = com = false;
    }
};
}  // namespace

struct VideoWriter::Impl {
    IMFSinkWriter* writer = nullptr;
    DWORD stream = 0;
    MfSession mf;
};

VideoWriter::VideoWriter() : d(new Impl) {}
VideoWriter::~VideoWriter() { close(); }

bool VideoWriter::available() { return mfPresent(); }
bool VideoWriter::isOpen() const { return d->writer != nullptr; }

bool VideoWriter::open(const QString& path, int width, int height, int fps, QString* error) {
    close();
    m_w = std::max(16, width & ~1);
    m_h = std::max(16, height & ~1);
    m_fps = std::clamp(fps, 1, 60);
    m_frame = 0;
    m_time = 0;
    if (!d->mf.start(error)) return false;
    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 2);
    if (attrs) {
        attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);  // MP4 whatever the file is called
    }
    std::wstring wpath = path.toStdWString();
    HRESULT hr = MFCreateSinkWriterFromURL(wpath.c_str(), nullptr, attrs, &d->writer);
    release(attrs);
    if (FAILED(hr)) {
        if (error) *error = "Could not create the video file.";
        d->mf.stop();
        QFile::remove(path);
        return false;
    }
    IMFMediaType* out = nullptr;
    IMFMediaType* in = nullptr;
    hr = MFCreateMediaType(&out);
    if (SUCCEEDED(hr)) hr = out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = out->SetUINT32(MF_MT_AVG_BITRATE, UINT32(std::clamp<qint64>(qint64(m_w) * m_h * m_fps / 5, 2000000, 24000000)));
    if (SUCCEEDED(hr)) hr = out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(out, MF_MT_FRAME_SIZE, UINT32(m_w), UINT32(m_h));
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(out, MF_MT_FRAME_RATE, UINT32(m_fps), 1);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = d->writer->AddStream(out, &d->stream);
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&in);
    if (SUCCEEDED(hr)) hr = in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(m_w * 4));  // top-down rows
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(in, MF_MT_FRAME_SIZE, UINT32(m_w), UINT32(m_h));
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(in, MF_MT_FRAME_RATE, UINT32(m_fps), 1);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = d->writer->SetInputMediaType(d->stream, in, nullptr);
    if (SUCCEEDED(hr)) hr = d->writer->BeginWriting();
    release(out);
    release(in);
    if (FAILED(hr)) {
        if (error) *error = "This computer could not start the H.264 video encoder.";
        release(d->writer);
        d->mf.stop();
        QFile::remove(path);
        return false;
    }
    return true;
}

bool VideoWriter::addFrame(const QImage& frame, qint64 duration, const QColor& bars) {
    if (!d->writer) return false;
    const QImage img = fitFrame(frame, m_w, m_h, bars);
    const LONGLONG dur = duration > 0 ? duration : kVideoSecond / m_fps;
    const DWORD stride = DWORD(m_w * 4), bytes = stride * DWORD(m_h);
    IMFMediaBuffer* buffer = nullptr;
    IMFSample* sample = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);
    BYTE* data = nullptr;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        hr = MFCopyImage(data, LONG(stride), img.constBits(), LONG(img.bytesPerLine()), stride, DWORD(m_h));
        buffer->Unlock();
    }
    if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(bytes);
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(m_time);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(dur);
    if (SUCCEEDED(hr)) hr = d->writer->WriteSample(d->stream, sample);
    release(sample);
    release(buffer);
    if (SUCCEEDED(hr)) {
        ++m_frame;
        m_time += dur;
    }
    return SUCCEEDED(hr);
}

bool VideoWriter::close() {
    bool ok = true;
    if (d->writer) {
        ok = m_frame > 0 && SUCCEEDED(d->writer->Finalize());
        release(d->writer);
    }
    d->mf.stop();
    return ok;
}

struct VideoReader::Impl {
    IMFSourceReader* reader = nullptr;
    MfSession mf;
    LONG stride = 0;
    int cropX = 0, cropY = 0, fullW = 0, fullH = 0;
    // one frame read ahead (its duration is the time to the next one)
    bool havePending = false, ended = false;
    QImage pendImg;
    qint64 pendTs = 0, pendDur = 0;
};

VideoReader::VideoReader() : d(new Impl) {}
VideoReader::~VideoReader() { close(); }

namespace {
bool readFormat(IMFSourceReader* r, int& w, int& h, double& fps, LONG& stride, int& cx, int& cy, int& fw, int& fh) {
    IMFMediaType* t = nullptr;
    if (FAILED(r->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &t))) return false;
    UINT32 W = 0, H = 0;
    MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &W, &H);
    fw = int(W);
    fh = int(H);
    w = fw;
    h = fh;
    cx = cy = 0;
    MFVideoArea area{};
    if (SUCCEEDED(t->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), nullptr))) {
        cx = std::clamp(int(area.OffsetX.value), 0, fw - 1);
        cy = std::clamp(int(area.OffsetY.value), 0, fh - 1);
        w = std::clamp(int(area.Area.cx), 1, fw - cx);
        h = std::clamp(int(area.Area.cy), 1, fh - cy);
    }
    UINT32 num = 0, den = 0;
    if (SUCCEEDED(MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den)) && num && den) fps = double(num) / den;
    UINT32 st = 0;
    if (SUCCEEDED(t->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) {
        stride = LONG(INT32(st));
    } else {
        LONG s = 0;
        MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, W, &s);
        stride = s ? s : LONG(W * 4);
    }
    t->Release();
    return w > 0 && h > 0;
}
}  // namespace

bool VideoReader::open(const QString& path, QString* error) {
    close();
    if (!d->mf.start(error)) return false;
    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 1);
    if (attrs) attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    std::wstring wpath = path.toStdWString();
    HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), attrs, &d->reader);
    release(attrs);
    if (SUCCEEDED(hr)) hr = d->reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (SUCCEEDED(hr)) hr = d->reader->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
    IMFMediaType* t = nullptr;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&t);
    if (SUCCEEDED(hr)) hr = t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = d->reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, t);
    release(t);
    if (SUCCEEDED(hr) && !readFormat(d->reader, m_w, m_h, m_fps, d->stride, d->cropX, d->cropY, d->fullW, d->fullH)) hr = E_FAIL;
    if (FAILED(hr)) {
        if (error) *error = "Could not read the video file.";
        close();
        return false;
    }
    return true;
}

bool VideoReader::decodeNext(QImage& img, qint64& ts, qint64& dur) {
    while (d->reader) {
        DWORD flags = 0;
        LONGLONG t = 0;
        IMFSample* s = nullptr;
        if (FAILED(d->reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, &t, &s))) return false;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            release(s);
            return false;
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
            readFormat(d->reader, m_w, m_h, m_fps, d->stride, d->cropX, d->cropY, d->fullW, d->fullH);
        if (!s) continue;
        LONGLONG sd = 0;
        s->GetSampleDuration(&sd);
        IMFMediaBuffer* buf = nullptr;
        bool ok = SUCCEEDED(s->ConvertToContiguousBuffer(&buf));
        BYTE* data = nullptr;
        DWORD len = 0;
        if (ok) ok = SUCCEEDED(buf->Lock(&data, nullptr, &len));
        if (ok) {
            const LONG as = d->stride < 0 ? -d->stride : d->stride;
            if (qint64(len) >= qint64(as) * d->fullH && as >= LONG(d->fullW * 4)) {
                const int w = m_w, h = m_h;
                img = QImage(w, h, QImage::Format_RGB32);
                for (int y = 0; y < h; ++y) {
                    const int sy = y + d->cropY;
                    const BYTE* src = d->stride > 0 ? data + qint64(sy) * as : data + qint64(d->fullH - 1 - sy) * as;
                    quint32* dst = reinterpret_cast<quint32*>(img.scanLine(y));
                    const quint32* s32 = reinterpret_cast<const quint32*>(src) + d->cropX;
                    for (int x = 0; x < w; ++x) dst[x] = s32[x] | 0xff000000u;
                }
            } else {
                ok = false;
            }
            buf->Unlock();
        }
        release(buf);
        release(s);
        if (!ok) return false;
        ts = t;
        dur = sd;
        return true;
    }
    return false;
}

bool VideoReader::readFrame(QImage& out, qint64* duration) {
    if (!d->reader || d->ended) return false;
    if (!d->havePending) {
        if (!decodeNext(d->pendImg, d->pendTs, d->pendDur)) {
            d->ended = true;
            return false;
        }
        d->havePending = true;
    }
    QImage next;
    qint64 nts = 0, ndur = 0;
    const bool more = decodeNext(next, nts, ndur);
    out = d->pendImg;
    qint64 dur = more ? nts - d->pendTs : d->pendDur;
    if (dur <= 0) dur = d->pendDur > 0 ? d->pendDur : qint64(kVideoSecond / std::max(1.0, m_fps));
    if (duration) *duration = dur;
    if (more) {
        d->pendImg = next;
        d->pendTs = nts;
        d->pendDur = ndur;
    } else {
        d->havePending = false;
        d->ended = true;
    }
    return true;
}

void VideoReader::close() {
    release(d->reader);
    d->mf.stop();
    d->havePending = d->ended = false;
    d->pendImg = QImage();
}

#else  // ---- other systems: a simple JPEG stream (development and tests only) ----

namespace {
const char kMagic[4] = {'G', 'X', 'V', '1'};
}

struct VideoWriter::Impl {
    QFile file;
    QDataStream out;
};

VideoWriter::VideoWriter() : d(new Impl) {}
VideoWriter::~VideoWriter() { close(); }
bool VideoWriter::available() { return true; }
bool VideoWriter::isOpen() const { return d->file.isOpen(); }

bool VideoWriter::open(const QString& path, int width, int height, int fps, QString* error) {
    close();
    m_w = std::max(16, width & ~1);
    m_h = std::max(16, height & ~1);
    m_fps = std::clamp(fps, 1, 60);
    m_frame = 0;
    m_time = 0;
    d->file.setFileName(path);
    if (!d->file.open(QIODevice::WriteOnly)) {
        if (error) *error = "Could not create the video file.";
        return false;
    }
    d->out.setDevice(&d->file);
    d->out.writeRawData(kMagic, 4);
    d->out << qint32(m_w) << qint32(m_h) << qint32(m_fps);
    return true;
}

bool VideoWriter::addFrame(const QImage& frame, qint64 duration, const QColor& bars) {
    if (!d->file.isOpen()) return false;
    const qint64 dur = duration > 0 ? duration : kVideoSecond / m_fps;
    QByteArray jpg;
    QBuffer b(&jpg);
    b.open(QIODevice::WriteOnly);
    fitFrame(frame, m_w, m_h, bars).save(&b, "JPG", 85);
    d->out << qint64(dur) << jpg;
    ++m_frame;
    m_time += dur;
    return d->out.status() == QDataStream::Ok;
}

bool VideoWriter::close() {
    if (!d->file.isOpen()) return true;
    d->out.setDevice(nullptr);
    d->file.close();
    return m_frame > 0;
}

struct VideoReader::Impl {
    QFile file;
    QDataStream in;
};

VideoReader::VideoReader() : d(new Impl) {}
VideoReader::~VideoReader() { close(); }

bool VideoReader::open(const QString& path, QString* error) {
    close();
    d->file.setFileName(path);
    char magic[4] = {};
    if (!d->file.open(QIODevice::ReadOnly) || d->file.read(magic, 4) != 4 || std::memcmp(magic, kMagic, 4) != 0) {
        if (error) *error = "Could not read the video file.";
        d->file.close();
        return false;
    }
    d->in.setDevice(&d->file);
    qint32 w = 0, h = 0, fps = 30;
    d->in >> w >> h >> fps;
    m_w = w;
    m_h = h;
    m_fps = fps;
    return w > 0 && h > 0;
}

bool VideoReader::readFrame(QImage& out, qint64* duration) {
    if (!d->file.isOpen() || d->in.atEnd()) return false;
    qint64 dur = 0;
    QByteArray jpg;
    d->in >> dur >> jpg;
    if (d->in.status() != QDataStream::Ok) return false;
    out = QImage::fromData(jpg, "JPG").convertToFormat(QImage::Format_RGB32);
    if (duration) *duration = dur;
    return !out.isNull();
}

void VideoReader::close() {
    d->in.setDevice(nullptr);
    d->file.close();
}

#endif

}  // namespace gx
