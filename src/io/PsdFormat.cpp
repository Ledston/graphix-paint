#include "io/PsdFormat.h"

#include <QFile>
#include <QImage>
#include <QSaveFile>
#include <algorithm>
#include <cstring>

#include "core/Compositor.h"
#include "core/Effects.h"
#include "core/ImageOps.h"
#include "core/Pixel.h"

namespace gx {

namespace {

// ============================================================================ writing helpers

struct Out {
    QByteArray b;
    void u8(quint32 v) { b.append(char(v & 0xff)); }
    void u16(quint32 v) { u8(v >> 8); u8(v); }
    void u32(quint32 v) { u16(v >> 16); u16(v); }
    void u64(quint64 v) { u32(quint32(v >> 32)); u32(quint32(v)); }
    void i16(qint32 v) { u16(quint32(qint16(v)) & 0xffff); }
    void i32(qint32 v) { u32(quint32(v)); }
    void raw(const char* s, int n) { b.append(s, n); }
    void raw(const QByteArray& a) { b.append(a); }
    void len(quint64 v, bool big) { if (big) u64(v); else u32(quint32(v)); }
    void padTo(int m) { while (b.size() % m) u8(0); }
};

void packBits(const quint8* row, int n, QByteArray& out) {
    int i = 0;
    while (i < n) {
        int j = i;
        while (j + 1 < n && row[j + 1] == row[i] && j - i < 127) ++j;
        int run = j - i + 1;
        if (run >= 3) {
            out.append(char(257 - run));
            out.append(char(row[i]));
            i = j + 1;
            continue;
        }
        int start = i;
        while (i < n && i - start < 128) {
            if (i + 2 < n && row[i] == row[i + 1] && row[i] == row[i + 2]) break;
            ++i;
        }
        out.append(char(i - start - 1));
        out.append(reinterpret_cast<const char*>(row + start), i - start);
    }
}

// Compression marker + RLE rows for one plane
QByteArray encodePlane(const quint8* plane, int w, int h, bool big) {
    Out o;
    o.u16(1);
    if (w <= 0 || h <= 0) return o.b;
    QByteArray rows;
    std::vector<quint32> counts(static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        int before = int(rows.size());
        packBits(plane + size_t(y) * w, w, rows);
        counts[size_t(y)] = quint32(rows.size() - before);
    }
    for (quint32 c : counts) big ? o.u32(c) : o.u16(c);
    o.raw(rows);
    return o.b;
}

struct Rec {
    QString name;
    QRect rect;
    int lsct = -1;  // -1 none, 1 open folder, 2 closed folder, 3 divider
    QByteArray blendKey = "norm";
    quint8 opacity = 255;
    bool clipping = false, visible = true, alphaLock = false;
    QByteArray ch[5];  // A, R, G, B, mask
    bool hasMask = false, maskEnabled = true;
    QRect maskRect;
    quint8 maskDefault = 255;
};

void planesFromSurface(const RasterSurface& s, const QRect& r, Rec& rec, bool big) {
    const int w = r.width(), h = r.height();
    const size_t n = size_t(w) * h;
    std::vector<quint32> px(n);
    s.readRect(r, px.data(), w);
    std::vector<quint8> planes[4];
    for (auto& p : planes) p.resize(n);
    for (size_t i = 0; i < n; ++i) {
        QColor c = colorFromPremul(px[i]);
        planes[0][i] = quint8(c.alpha());
        planes[1][i] = quint8(c.red());
        planes[2][i] = quint8(c.green());
        planes[3][i] = quint8(c.blue());
    }
    for (int k = 0; k < 4; ++k) rec.ch[k] = encodePlane(planes[k].data(), w, h, big);
}

void emptyChannels(Rec& rec, bool big) {
    for (int k = 0; k < 4; ++k) rec.ch[k] = encodePlane(nullptr, 0, 0, big);
}

void collect(Document& doc, const GroupLayer* g, std::vector<Rec>& out, bool big, QStringList* warn) {
    for (const auto& cp : g->children) {
        Layer* l = cp.get();
        Rec r;
        r.name = l->name;
        r.blendKey = blendModePsdKey(l->blend);
        r.opacity = quint8(std::clamp(int(l->opacity * 255 + 0.5f), 0, 255));
        r.clipping = l->clipping;
        r.visible = l->visible;
        r.alphaLock = l->alphaLock;
        if (l->isGroup()) {
            Rec div;
            div.name = "</Layer group>";
            div.lsct = 3;
            emptyChannels(div, big);
            out.push_back(div);
            collect(doc, static_cast<GroupLayer*>(l), out, big, warn);
            auto* gl = static_cast<GroupLayer*>(l);
            r.lsct = gl->expanded ? 1 : 2;
            if (gl->passThrough) r.blendKey = "pass";
            emptyChannels(r, big);
        } else if (l->type() == LayerType::Adjustment) {
            if (warn) *warn << QStringLiteral("Adjustment layer \"%1\" was not exported (PSD export keeps pixel layers only).").arg(l->name);
            continue;
        } else {
            const RasterSurface* s = l->displaySurface();
            bool baked = false;
            if (l->fx.anyActive()) {
                ensureEffects(*l, doc.bounds());
                if (l->fxCache) { s = &l->fxCache->tiles; baked = true; }
                if (warn) *warn << QStringLiteral("Effects of \"%1\" were merged into its pixels.").arg(l->name);
            }
            QRect b = s ? (s->exactBounds() & doc.bounds()) : QRect();
            r.rect = b;
            if (s && !b.isEmpty()) planesFromSurface(*s, b, r, big);
            else emptyChannels(r, big);
            if (l->hasMask && !baked) {
                QRect mb = l->mask.tileBounds() & doc.bounds();
                if (!mb.isEmpty()) {
                    std::vector<quint8> m(size_t(mb.width()) * mb.height());
                    l->mask.readRect(mb, m.data(), mb.width());
                    r.ch[4] = encodePlane(m.data(), mb.width(), mb.height(), big);
                    r.hasMask = true;
                    r.maskRect = mb;
                    r.maskDefault = 255;
                    r.maskEnabled = l->maskEnabled;
                }
            }
        }
        out.push_back(r);
    }
}

void writeRecord(Out& o, const Rec& r, bool big) {
    o.i32(r.rect.isEmpty() ? 0 : r.rect.top());
    o.i32(r.rect.isEmpty() ? 0 : r.rect.left());
    o.i32(r.rect.isEmpty() ? 0 : r.rect.bottom() + 1);
    o.i32(r.rect.isEmpty() ? 0 : r.rect.right() + 1);
    int nch = r.hasMask ? 5 : 4;
    o.u16(quint32(nch));
    const qint32 ids[5] = {-1, 0, 1, 2, -2};
    for (int k = 0; k < nch; ++k) {
        o.i16(ids[k]);
        o.len(quint64(r.ch[k].size()), big);
    }
    o.raw("8BIM", 4);
    o.raw(r.blendKey.left(4).leftJustified(4, ' '));
    o.u8(r.opacity);
    o.u8(r.clipping ? 1 : 0);
    quint8 flags = 0;
    if (r.alphaLock) flags |= 1;
    if (!r.visible) flags |= 2;
    if (r.lsct >= 0) flags |= 0x08 | 0x10;
    o.u8(flags);
    o.u8(0);
    Out extra;
    if (r.hasMask) {
        extra.u32(20);
        extra.i32(r.maskRect.top());
        extra.i32(r.maskRect.left());
        extra.i32(r.maskRect.bottom() + 1);
        extra.i32(r.maskRect.right() + 1);
        extra.u8(r.maskDefault);
        extra.u8(r.maskEnabled ? 0 : 2);
        extra.u16(0);
    } else {
        extra.u32(0);
    }
    extra.u32(0);  // blending ranges
    QByteArray name = r.name.toLatin1().left(255);
    extra.u8(quint32(name.size()));
    extra.raw(name);
    while ((extra.b.size() - 8 - (r.hasMask ? 20 : 0)) % 4) extra.u8(0);
    // unicode name
    {
        Out u;
        u.u32(quint32(r.name.size()));
        for (QChar c : r.name) u.u16(c.unicode());
        u.padTo(4);
        extra.raw("8BIMluni", 8);
        extra.u32(quint32(u.b.size()));
        extra.raw(u.b);
    }
    if (r.lsct >= 0) {
        extra.raw("8BIMlsct", 8);
        extra.u32(12);
        extra.u32(quint32(r.lsct));
        extra.raw("8BIM", 4);
        extra.raw(r.blendKey.left(4).leftJustified(4, ' '));
    }
    o.u32(quint32(extra.b.size()));
    o.raw(extra.b);
}

// ============================================================================ reading helpers

class In {
public:
    explicit In(QIODevice* d) : m_d(d) {}
    bool ok() const { return m_ok; }
    void fail() { m_ok = false; }
    QByteArray bytes(qint64 n) {
        if (!m_ok || n < 0) { m_ok = false; return {}; }
        QByteArray b = m_d->read(n);
        if (b.size() != n) m_ok = false;
        return b;
    }
    quint8 u8() { QByteArray b = bytes(1); return b.size() == 1 ? quint8(b[0]) : 0; }
    quint16 u16() { QByteArray b = bytes(2); return b.size() == 2 ? quint16((quint8(b[0]) << 8) | quint8(b[1])) : 0; }
    quint32 u32() { return (quint32(u16()) << 16) | u16(); }
    quint64 u64() { return (quint64(u32()) << 32) | u32(); }
    qint16 i16() { return qint16(u16()); }
    qint32 i32() { return qint32(u32()); }
    quint64 len(bool big) { return big ? u64() : u32(); }
    void skip(qint64 n) {
        if (n <= 0) return;
        if (!m_d->seek(m_d->pos() + n)) m_ok = false;
    }
    qint64 pos() const { return m_d->pos(); }
    void seek(qint64 p) { if (!m_d->seek(p)) m_ok = false; }

private:
    QIODevice* m_d;
    bool m_ok = true;
};

bool unpackBits(const char* src, int srcLen, quint8* dst, int n) {
    int i = 0, o = 0;
    while (o < n && i < srcLen) {
        int c = qint8(src[i++]);
        if (c >= 0) {
            int cnt = c + 1;
            if (i + cnt > srcLen || o + cnt > n) return false;
            std::memcpy(dst + o, src + i, size_t(cnt));
            i += cnt;
            o += cnt;
        } else if (c != -128) {
            int cnt = 1 - c;
            if (i >= srcLen || o + cnt > n) return false;
            std::memset(dst + o, quint8(src[i++]), size_t(cnt));
            o += cnt;
        }
    }
    return o == n;
}

// Reads one channel's image data (compression + data) into an 8-bit plane.
bool readChannel(In& in, quint64 length, int w, int h, int depth, bool big, std::vector<quint8>& plane) {
    plane.assign(size_t(std::max(0, w)) * std::max(0, h), 0);
    if (length < 2) return true;
    quint16 comp = in.u16();
    qint64 rest = qint64(length) - 2;
    if (w <= 0 || h <= 0) { in.skip(rest); return in.ok(); }
    const int bps = depth / 8;
    const size_t rowBytes = size_t(w) * bps;
    std::vector<quint8> raw(rowBytes * h);
    if (comp == 0) {
        QByteArray d = in.bytes(rest);
        if (!in.ok()) return false;
        std::memcpy(raw.data(), d.constData(), std::min(raw.size(), size_t(d.size())));
    } else if (comp == 1) {
        int cb = big ? 4 : 2;
        std::vector<quint32> counts(static_cast<size_t>(h));
        for (int y = 0; y < h; ++y) counts[size_t(y)] = big ? in.u32() : in.u16();
        QByteArray d = in.bytes(rest - qint64(h) * cb);
        if (!in.ok()) return false;
        qint64 off = 0;
        for (int y = 0; y < h; ++y) {
            qint64 c = counts[size_t(y)];
            if (off + c > d.size()) return false;
            if (!unpackBits(d.constData() + off, int(c), raw.data() + rowBytes * y, int(rowBytes))) return false;
            off += c;
        }
    } else if (comp == 2 || comp == 3) {
        QByteArray d = in.bytes(rest);
        if (!in.ok()) return false;
        quint32 expected = quint32(raw.size());
        QByteArray pre(4, 0);
        pre[0] = char(expected >> 24); pre[1] = char(expected >> 16); pre[2] = char(expected >> 8); pre[3] = char(expected);
        QByteArray un = qUncompress(pre + d);
        if (size_t(un.size()) != raw.size()) return false;
        std::memcpy(raw.data(), un.constData(), raw.size());
        if (comp == 3) {
            for (int y = 0; y < h; ++y) {
                quint8* row = raw.data() + rowBytes * y;
                if (bps == 1) {
                    for (int x = 1; x < w; ++x) row[x] = quint8(row[x] + row[x - 1]);
                } else if (bps == 2) {
                    quint16 prev = quint16((row[0] << 8) | row[1]);
                    for (int x = 1; x < w; ++x) {
                        quint16 v = quint16(((row[x * 2] << 8) | row[x * 2 + 1]) + prev);
                        row[x * 2] = quint8(v >> 8);
                        row[x * 2 + 1] = quint8(v);
                        prev = v;
                    }
                }
            }
        }
    } else {
        in.skip(rest);
        return false;
    }
    if (bps == 1) plane.assign(raw.begin(), raw.end());
    else
        for (size_t i = 0; i < plane.size(); ++i) plane[i] = raw[i * bps];  // big-endian high byte
    return true;
}

struct ReadRec {
    QRect rect;
    std::vector<std::pair<int, quint64>> chans;
    QByteArray blendKey;
    quint8 opacity = 255, clipping = 0, flags = 0;
    QString name;
    int lsct = -1;
    QByteArray lsctBlend;
    bool hasMask = false;
    QRect maskRect;
    quint8 maskDefault = 0, maskFlags = 0;
    float fillOpacity = 1.f;
    std::vector<quint8> planes[5];  // A R G B Mask
};

QImage planesToImage(const ReadRec& r, int mode) {
    const int w = r.rect.width(), h = r.rect.height();
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) return img;
    const bool hasA = !r.planes[0].empty();
    for (int y = 0; y < h; ++y) {
        quint32* row = reinterpret_cast<quint32*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            size_t i = size_t(y) * w + x;
            int a = hasA ? r.planes[0][i] : 255;
            int R, G, B;
            if (mode == 1) R = G = B = r.planes[1].empty() ? 0 : r.planes[1][i];
            else {
                R = r.planes[1].empty() ? 0 : r.planes[1][i];
                G = r.planes[2].empty() ? 0 : r.planes[2][i];
                B = r.planes[3].empty() ? 0 : r.planes[3][i];
            }
            row[x] = qPremultiply(qRgba(R, G, B, a));
        }
    }
    return img;
}

}  // namespace

// ============================================================================ save

bool savePsd(Document& doc, const QString& path, QString* error, QStringList* warnings) {
    const QSize sz = doc.size();
    const bool big = sz.width() > 30000 || sz.height() > 30000;
    std::vector<Rec> recs;
    collect(doc, doc.root(), recs, big, warnings);

    Out o;
    o.raw("8BPS", 4);
    o.u16(big ? 2 : 1);
    o.raw("\0\0\0\0\0\0", 6);
    o.u16(4);
    o.u32(quint32(sz.height()));
    o.u32(quint32(sz.width()));
    o.u16(8);
    o.u16(3);
    o.u32(0);  // color mode data
    // image resources: resolution
    {
        Out res;
        res.raw("8BIM", 4);
        res.u16(0x03ED);
        res.u16(0);  // empty pascal name, padded
        res.u32(16);
        quint32 fixed = quint32(doc.dpi() * 65536.0);
        res.u32(fixed); res.u16(1); res.u16(1);
        res.u32(fixed); res.u16(1); res.u16(1);
        o.u32(quint32(res.b.size()));
        o.raw(res.b);
    }
    // layer and mask info
    Out layerInfo;
    layerInfo.i16(-qint32(recs.size()));
    for (const auto& r : recs) writeRecord(layerInfo, r, big);
    for (const auto& r : recs)
        for (int k = 0; k < (r.hasMask ? 5 : 4); ++k) layerInfo.raw(r.ch[k]);
    layerInfo.padTo(4);
    Out lmi;
    lmi.len(quint64(layerInfo.b.size()), big);
    lmi.raw(layerInfo.b);
    lmi.u32(0);  // global layer mask info
    o.len(quint64(lmi.b.size()), big);
    o.raw(lmi.b);

    // merged image (straight colors)
    QImage merged = Compositor::renderImage(doc.root(), doc.bounds());
    if (merged.isNull()) {
        if (error) *error = "Not enough memory to build the merged image.";
        return false;
    }
    const int w = sz.width(), h = sz.height();
    {
        std::vector<quint8> plane(size_t(w) * h);
        Out cnt;
        QByteArray all;
        for (int c = 0; c < 4; ++c) {
            for (int y = 0; y < h; ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(merged.constScanLine(y));
                for (int x = 0; x < w; ++x) {
                    QRgb p = qUnpremultiply(row[x]);
                    quint8 v = c == 0 ? qRed(p) : c == 1 ? qGreen(p) : c == 2 ? qBlue(p) : qAlpha(p);
                    plane[size_t(y) * w + x] = v;
                }
            }
            for (int y = 0; y < h; ++y) {
                QByteArray r;
                packBits(plane.data() + size_t(y) * w, w, r);
                big ? cnt.u32(quint32(r.size())) : cnt.u16(quint32(r.size()));
                all.append(r);
            }
        }
        o.u16(1);
        o.raw(cnt.b);
        o.raw(all);
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    if (f.write(o.b) != o.b.size() || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

// ============================================================================ load

std::unique_ptr<Document> loadPsd(const QString& path, QString* error, QStringList* warnings) {
    auto fail = [&](const QString& e) -> std::unique_ptr<Document> {
        if (error) *error = e;
        return nullptr;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(f.errorString());
    In in(&f);
    if (in.bytes(4) != "8BPS") return fail("This is not a PSD file.");
    quint16 ver = in.u16();
    if (ver != 1 && ver != 2) return fail("Unsupported PSD version.");
    const bool big = ver == 2;
    in.skip(6);
    quint16 channels = in.u16();
    quint32 H = in.u32(), W = in.u32();
    quint16 depth = in.u16(), mode = in.u16();
    if (!in.ok()) return fail("The file is damaged.");
    if (W == 0 || H == 0 || W > 300000 || H > 300000) return fail("Invalid canvas size.");
    if (depth != 8 && depth != 16) return fail(QStringLiteral("%1-bit PSD files are not supported yet (8 and 16 bit are).").arg(depth));
    if (mode != 3 && mode != 1) return fail("Only RGB and Grayscale PSD files are supported.");
    in.skip(in.u32());  // color mode data
    double dpi = 72;
    {
        quint32 resLen = in.u32();
        qint64 end = in.pos() + resLen;
        while (in.ok() && in.pos() + 12 <= end) {
            if (in.bytes(4) != "8BIM") break;
            quint16 id = in.u16();
            quint8 nl = in.u8();
            in.skip(nl + ((nl + 1) % 2));
            quint32 sz = in.u32();
            qint64 dataStart = in.pos();
            if (id == 0x03ED && sz >= 16) dpi = in.u32() / 65536.0;
            in.seek(dataStart + sz + (sz % 2));
        }
        in.seek(end);
    }
    if (!in.ok()) return fail("The file is damaged.");

    auto doc = std::make_unique<Document>(QSize(int(W), int(H)));
    doc->setDpi(dpi > 1 ? dpi : 72);

    quint64 lmiLen = in.len(big);
    qint64 lmiEnd = in.pos() + qint64(lmiLen);
    std::vector<ReadRec> recs;
    if (lmiLen > 0) {
        quint64 liLen = in.len(big);
        qint64 liEnd = in.pos() + qint64(liLen);
        if (liLen > 0) {
            int count = std::abs(int(in.i16()));
            if (count > 20000) return fail("Too many layers.");
            recs.resize(size_t(count));
            for (auto& r : recs) {
                qint32 t = in.i32(), l = in.i32(), b = in.i32(), rr = in.i32();
                r.rect = QRect(QPoint(l, t), QPoint(rr - 1, b - 1));
                quint16 nc = in.u16();
                if (nc > 64) return fail("The file is damaged.");
                for (int c = 0; c < nc; ++c) {
                    int id = in.i16();
                    quint64 len = in.len(big);
                    r.chans.push_back({id, len});
                }
                if (in.bytes(4) != "8BIM") return fail("The layer data is damaged.");
                r.blendKey = in.bytes(4);
                r.opacity = in.u8();
                r.clipping = in.u8();
                r.flags = in.u8();
                in.u8();
                quint32 extraLen = in.u32();
                qint64 extraEnd = in.pos() + extraLen;
                quint32 maskLen = in.u32();
                qint64 maskEnd = in.pos() + maskLen;
                if (maskLen >= 18) {
                    qint32 mt = in.i32(), ml = in.i32(), mb = in.i32(), mr = in.i32();
                    r.maskRect = QRect(QPoint(ml, mt), QPoint(mr - 1, mb - 1));
                    r.maskDefault = in.u8();
                    r.maskFlags = in.u8();
                    r.hasMask = true;
                }
                in.seek(maskEnd);
                in.skip(in.u32());  // blending ranges
                quint8 nl = in.u8();
                r.name = QString::fromLatin1(in.bytes(nl));
                qint64 used = 1 + nl;
                in.skip((4 - used % 4) % 4);
                while (in.ok() && in.pos() + 12 <= extraEnd) {
                    QByteArray sig = in.bytes(4);
                    if (sig != "8BIM" && sig != "8B64") break;
                    QByteArray key = in.bytes(4);
                    static const QList<QByteArray> longKeys = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                                               "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
                    quint64 len = (big && longKeys.contains(key)) ? in.u64() : in.u32();
                    qint64 dataStart = in.pos();
                    if (key == "luni" && len >= 4) {
                        quint32 n = in.u32();
                        if (n < 10000) {
                            QString s;
                            for (quint32 i = 0; i < n; ++i) s.append(QChar(in.u16()));
                            r.name = s;
                        }
                    } else if ((key == "lsct" || key == "lsdk") && len >= 4) {
                        r.lsct = int(in.u32());
                        if (len >= 12) {
                            in.skip(4);
                            r.lsctBlend = in.bytes(4);
                        }
                    } else if (key == "iOpa" && len >= 1) {
                        r.fillOpacity = in.u8() / 255.f;
                    }
                    in.seek(dataStart + qint64(len));
                }
                in.seek(extraEnd);
                if (!in.ok()) return fail("The layer data is damaged.");
            }
            // channel image data
            for (auto& r : recs) {
                for (const auto& [id, len] : r.chans) {
                    int w = r.rect.width(), h = r.rect.height();
                    int slot = id == -1 ? 0 : (id >= 0 && id <= 2) ? id + 1 : (id == -2 ? 4 : -1);
                    if (slot == 4) { w = r.maskRect.width(); h = r.maskRect.height(); }
                    if (slot < 0) { in.skip(qint64(len)); continue; }
                    qint64 start = in.pos();
                    if (qint64(w) * h > 400ll * 1000 * 1000) return fail("A layer in this file is too large.");
                    if (!readChannel(in, len, w, h, depth, big, r.planes[slot])) {
                        if (warnings) *warnings << QStringLiteral("Could not read some pixels of \"%1\".").arg(r.name);
                        r.planes[slot].clear();
                    }
                    in.seek(start + qint64(len));
                }
            }
        }
        in.seek(liEnd);
    }
    in.seek(lmiEnd);

    // Build the layer tree (records are bottom to top)
    std::vector<std::shared_ptr<GroupLayer>> stack;
    stack.push_back(std::make_shared<GroupLayer>());
    for (auto& r : recs) {
        if (r.lsct == 3) {
            stack.push_back(std::make_shared<GroupLayer>());
            continue;
        }
        LayerPtr layer;
        if ((r.lsct == 1 || r.lsct == 2) && stack.size() > 1) {
            auto g = stack.back();
            stack.pop_back();
            g->expanded = r.lsct == 1;
            g->passThrough = r.lsctBlend == "pass" || r.blendKey == "pass";
            layer = g;
        } else {
            auto rl = std::make_shared<RasterLayer>();
            if (!r.rect.isEmpty() && (!r.planes[0].empty() || !r.planes[1].empty())) {
                QImage img = planesToImage(r, mode);
                if (!img.isNull()) imageToSurface(img, rl->pixels, r.rect.topLeft());
            }
            layer = rl;
        }
        layer->id = doc->newLayerId();
        layer->name = r.name.isEmpty() ? doc->uniqueLayerName("Layer") : r.name;
        bool pass = false;
        layer->blend = blendModeFromPsdKey(r.blendKey, &pass);
        layer->opacity = (r.opacity / 255.f) * r.fillOpacity;
        layer->clipping = r.clipping != 0;
        layer->visible = (r.flags & 2) == 0;
        layer->alphaLock = (r.flags & 1) != 0;
        if (r.hasMask && !r.planes[4].empty() && !r.maskRect.isEmpty()) {
            layer->hasMask = true;
            layer->maskEnabled = (r.maskFlags & 2) == 0;
            layer->mask = MaskSurface(r.maskDefault);
            layer->mask.writeRect(r.maskRect, r.planes[4].data(), r.maskRect.width());
            layer->mask.optimize();
        }
        stack.back()->insert(int(stack.back()->children.size()), layer);
    }
    while (stack.size() > 1) {  // unbalanced folders: keep the content
        auto g = stack.back();
        stack.pop_back();
        g->id = doc->newLayerId();
        g->name = doc->uniqueLayerName("Folder");
        stack.back()->insert(int(stack.back()->children.size()), g);
    }
    for (auto& c : stack[0]->children) doc->root()->insert(int(doc->root()->children.size()), c);

    if (doc->root()->children.empty()) {
        // Flat PSD: use the merged image
        quint16 comp = in.u16();
        int nch = std::min<int>(channels, 4);
        std::vector<std::vector<quint8>> planes(static_cast<size_t>(nch));
        const int bps = depth / 8;
        const size_t rowBytes = size_t(W) * bps;
        bool ok = in.ok();
        std::vector<quint32> counts;
        if (comp == 1) {
            counts.resize(size_t(channels) * H);
            for (auto& c : counts) c = big ? in.u32() : in.u16();
        }
        for (int c = 0; c < nch && ok; ++c) {
            std::vector<quint8> raw(rowBytes * H);
            if (comp == 0) {
                QByteArray d = in.bytes(qint64(raw.size()));
                if (!in.ok()) { ok = false; break; }
                std::memcpy(raw.data(), d.constData(), raw.size());
            } else if (comp == 1) {
                for (quint32 y = 0; y < H && ok; ++y) {
                    QByteArray d = in.bytes(counts[size_t(c) * H + y]);
                    ok = in.ok() && unpackBits(d.constData(), int(d.size()), raw.data() + rowBytes * y, int(rowBytes));
                }
            } else ok = false;
            planes[size_t(c)].resize(size_t(W) * H);
            for (size_t i = 0; i < planes[size_t(c)].size(); ++i) planes[size_t(c)][i] = raw[i * bps];
        }
        auto rl = std::make_shared<RasterLayer>();
        rl->id = doc->newLayerId();
        rl->name = "Background";
        if (ok) {
            QImage img(int(W), int(H), QImage::Format_ARGB32_Premultiplied);
            for (quint32 y = 0; y < H; ++y) {
                quint32* row = reinterpret_cast<quint32*>(img.scanLine(int(y)));
                for (quint32 x = 0; x < W; ++x) {
                    size_t i = size_t(y) * W + x;
                    int R, G, B, A = 255;
                    if (mode == 1) { R = G = B = planes[0][i]; if (nch > 1) A = planes[1][i]; }
                    else {
                        R = planes[0][i]; G = nch > 1 ? planes[1][i] : 0; B = nch > 2 ? planes[2][i] : 0;
                        if (nch > 3) A = planes[3][i];
                    }
                    row[x] = qPremultiply(qRgba(R, G, B, A));
                }
            }
            imageToSurface(img, rl->pixels, QPoint(0, 0));
        } else if (warnings) {
            *warnings << "The merged image could not be read.";
        }
        doc->root()->insert(0, rl);
    }
    doc->setCurrentLayer(doc->root()->children.back()->id);
    doc->setFilePath(path);
    doc->setModified(false);
    doc->clearHistory();
    return doc;
}

}  // namespace gx
