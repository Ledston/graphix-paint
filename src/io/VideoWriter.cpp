#include "io/VideoWriter.h"

#include <QPainter>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#endif

namespace gx {

#ifdef Q_OS_WIN
struct VideoWriter::Impl {
    IMFSinkWriter* writer = nullptr;
    DWORD stream = 0;
    bool started = false;
    bool comInit = false;
};

namespace {
template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
}  // namespace

VideoWriter::VideoWriter() : d(new Impl) {}
VideoWriter::~VideoWriter() { close(); }

bool VideoWriter::available() { return true; }

bool VideoWriter::open(const QString& path, int width, int height, int fps, QString* error) {
    close();
    m_w = std::max(16, width & ~1);
    m_h = std::max(16, height & ~1);
    m_fps = std::clamp(fps, 1, 60);
    m_frame = 0;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    d->comInit = SUCCEEDED(hr);
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        if (error) *error = "Windows video encoding (Media Foundation) is not available.";
        return false;
    }
    d->started = true;
    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 1);
    if (attrs) attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    std::wstring wpath = path.toStdWString();
    hr = MFCreateSinkWriterFromURL(wpath.c_str(), nullptr, attrs, &d->writer);
    release(attrs);
    if (FAILED(hr)) {
        if (error) *error = "Could not create the video file.";
        return false;
    }
    IMFMediaType* out = nullptr;
    IMFMediaType* in = nullptr;
    hr = MFCreateMediaType(&out);
    if (SUCCEEDED(hr)) hr = out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = out->SetUINT32(MF_MT_AVG_BITRATE, UINT32(std::min<qint64>(20000000, qint64(m_w) * m_h * m_fps / 6)));
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
        return false;
    }
    return true;
}

bool VideoWriter::addFrame(const QImage& frame) {
    if (!d->writer) return false;
    QImage img(m_w, m_h, QImage::Format_RGB32);
    img.fill(Qt::black);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QSizeF s = QSizeF(frame.size()).scaled(QSizeF(m_w, m_h), Qt::KeepAspectRatio);
        p.drawImage(QRectF((m_w - s.width()) / 2, (m_h - s.height()) / 2, s.width(), s.height()), frame);
    }
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
    const LONGLONG dur = 10000000LL / m_fps;
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(m_frame * dur);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(dur);
    if (SUCCEEDED(hr)) hr = d->writer->WriteSample(d->stream, sample);
    release(sample);
    release(buffer);
    if (SUCCEEDED(hr)) ++m_frame;
    return SUCCEEDED(hr);
}

bool VideoWriter::close() {
    bool ok = true;
    if (d->writer) {
        ok = SUCCEEDED(d->writer->Finalize());
        release(d->writer);
    }
    if (d->started) {
        MFShutdown();
        d->started = false;
    }
    if (d->comInit) {
        CoUninitialize();
        d->comInit = false;
    }
    return ok;
}

#else

struct VideoWriter::Impl {};
VideoWriter::VideoWriter() : d(new Impl) {}
VideoWriter::~VideoWriter() = default;
bool VideoWriter::available() { return false; }
bool VideoWriter::open(const QString&, int, int, int, QString* error) {
    if (error) *error = "Video export works on Windows.";
    return false;
}
bool VideoWriter::addFrame(const QImage&) { return false; }
bool VideoWriter::close() { return true; }

#endif

}  // namespace gx
