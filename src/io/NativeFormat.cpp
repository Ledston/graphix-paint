#include "io/NativeFormat.h"

#include "core/ImageOps.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentMap>
#include <atomic>
#include <cstring>

#include "vector/VectorRender.h"

namespace gx {

namespace {

const char kMagic[8] = {'G', 'X', 'P', 'A', 'I', 'N', 'T', '1'};
constexpr quint32 kVersion = 1;

struct TileBlob {
    qint32 tx = 0, ty = 0;
    bool uniform = false;
    quint32 value = 0;
    QByteArray data;  // compressed
};

struct SurfaceBlob {
    quint8 kind = 0;  // 0 rgba, 1 mask
    quint32 defaultValue = 0;
    std::vector<TileBlob> tiles;
};

template <class T>
SurfaceBlob encodeSurface(const TileGrid<T>& s, quint8 kind) {
    SurfaceBlob b;
    b.kind = kind;
    b.defaultValue = quint32(s.defaultValue());
    std::vector<std::pair<qint64, typename TileGrid<T>::Ptr>> items;
    for (auto it = s.tiles().cbegin(); it != s.tiles().cend(); ++it) items.push_back({it.key(), it.value()});
    b.tiles.resize(items.size());
    std::vector<int> idx(items.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = int(i);
    parallelFor(int(idx.size()), [&](int i) {
        const auto& [k, t] = items[size_t(i)];
        TileBlob& tb = b.tiles[size_t(i)];
        tb.tx = keyTx(k);
        tb.ty = keyTy(k);
        if (!t->px) {
            tb.uniform = true;
            tb.value = quint32(t->uniform);
        } else {
            QByteArray raw(reinterpret_cast<const char*>(t->px.get()), int(sizeof(T) * TILE_PIXELS));
            tb.data = qCompress(raw, 3);
        }
    });
    return b;
}

template <class T>
bool decodeSurface(const SurfaceBlob& b, TileGrid<T>& out) {
    out = TileGrid<T>(T(b.defaultValue));
    std::vector<typename TileGrid<T>::Ptr> res(b.tiles.size());
    std::atomic<bool> ok{true};
    std::vector<int> idx(b.tiles.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = int(i);
    parallelFor(int(idx.size()), [&](int i) {
        const TileBlob& tb = b.tiles[size_t(i)];
        auto t = std::make_shared<Tile<T>>();
        if (tb.uniform) t->uniform = T(tb.value);
        else {
            QByteArray raw = qUncompress(tb.data);
            if (raw.size() != int(sizeof(T) * TILE_PIXELS)) { ok = false; return; }
            t->px.reset(new T[TILE_PIXELS]);
            std::memcpy(t->px.get(), raw.constData(), size_t(raw.size()));
        }
        res[size_t(i)] = t;
    });
    if (!ok) return false;
    for (size_t i = 0; i < res.size(); ++i) out.setTile(tileKey(b.tiles[i].tx, b.tiles[i].ty), res[i]);
    return true;
}

struct Writer {
    std::vector<SurfaceBlob> blobs;
    int add(const RasterSurface& s) { blobs.push_back(encodeSurface(s, 0)); return int(blobs.size()) - 1; }
    int add(const MaskSurface& s) { blobs.push_back(encodeSurface(s, 1)); return int(blobs.size()) - 1; }
};

QJsonObject layerToJson(const Layer& l, Writer& w) {
    QJsonObject o;
    o["type"] = int(l.type());
    o["id"] = qint64(l.id);
    o["name"] = l.name;
    o["visible"] = l.visible;
    o["opacity"] = l.opacity;
    o["blend"] = int(l.blend);
    o["clipping"] = l.clipping;
    o["locked"] = l.locked;
    o["alphaLock"] = l.alphaLock;
    if (l.labelColor.isValid()) o["label"] = l.labelColor.name();
    if (l.reference) o["reference"] = true;
    if (l.hideInTimelapse) o["hideInTimelapse"] = true;
    if (l.fx.anyActive() || !(l.fx == LayerEffects())) o["fx"] = l.fx.toJson();
    if (l.hasMask) {
        o["mask"] = w.add(l.mask);
        o["maskEnabled"] = l.maskEnabled;
    }
    switch (l.type()) {
    case LayerType::Raster: {
        const auto& rl = static_cast<const RasterLayer&>(l);
        o["pixels"] = w.add(rl.pixels);
        if (rl.generatorEditable()) o["generator"] = rl.generator;  // lines that can still be changed
        break;
    }
    case LayerType::Vector: {
        const auto& v = static_cast<const VectorLayer&>(l);
        QJsonArray strokes;
        for (const auto& s : v.strokes) strokes.append(s.toJson());
        o["strokes"] = strokes;
        o["nextStrokeId"] = qint64(v.nextStrokeId);
        break;
    }
    case LayerType::Text: o["text"] = static_cast<const TextLayer&>(l).text.toJson(); break;
    case LayerType::Adjustment: o["adjustment"] = static_cast<const AdjustmentLayer&>(l).adjustment.toJson(); break;
    case LayerType::Group: {
        const auto& g = static_cast<const GroupLayer&>(l);
        o["expanded"] = g.expanded;
        o["passThrough"] = g.passThrough;
        QJsonArray ch;
        for (const auto& c : g.children) ch.append(layerToJson(*c, w));
        o["children"] = ch;
        break;
    }
    }
    return o;
}

struct Reader {
    std::vector<SurfaceBlob> blobs;
    QString error;
    bool raster(int idx, RasterSurface& out) {
        if (idx < 0 || idx >= int(blobs.size())) return true;
        if (!decodeSurface(blobs[size_t(idx)], out)) { error = "Damaged pixel data."; return false; }
        return true;
    }
    bool mask(int idx, MaskSurface& out) {
        if (idx < 0 || idx >= int(blobs.size())) return true;
        if (!decodeSurface(blobs[size_t(idx)], out)) { error = "Damaged mask data."; return false; }
        return true;
    }
};

LayerPtr layerFromJson(const QJsonObject& o, Reader& r, int depth) {
    if (depth > 64) return nullptr;
    LayerPtr l;
    int type = o.value("type").toInt();
    switch (LayerType(type)) {
    case LayerType::Raster: {
        auto rl = std::make_shared<RasterLayer>();
        if (!r.raster(o.value("pixels").toInt(-1), rl->pixels)) return nullptr;
        rl->generator = o.value("generator").toObject();
        rl->generatorVersion = rl->version;
        l = rl;
        break;
    }
    case LayerType::Vector: {
        auto v = std::make_shared<VectorLayer>();
        for (const auto& s : o.value("strokes").toArray()) v->strokes.push_back(VectorStroke::fromJson(s.toObject()));
        v->nextStrokeId = quint32(o.value("nextStrokeId").toInteger(1));
        for (const auto& s : v->strokes) v->nextStrokeId = std::max(v->nextStrokeId, s.id + 1);
        renderVectorLayer(*v);
        l = v;
        break;
    }
    case LayerType::Text: {
        auto t = std::make_shared<TextLayer>();
        t->text = TextParams::fromJson(o.value("text").toObject());
        renderTextLayer(*t);
        l = t;
        break;
    }
    case LayerType::Adjustment: {
        auto a = std::make_shared<AdjustmentLayer>();
        a->adjustment = Adjustment::fromJson(o.value("adjustment").toObject());
        l = a;
        break;
    }
    case LayerType::Group: {
        auto g = std::make_shared<GroupLayer>();
        g->expanded = o.value("expanded").toBool(true);
        g->passThrough = o.value("passThrough").toBool(false);
        for (const auto& c : o.value("children").toArray()) {
            LayerPtr cl = layerFromJson(c.toObject(), r, depth + 1);
            if (!cl) {
                if (!r.error.isEmpty()) return nullptr;
                continue;
            }
            g->insert(int(g->children.size()), cl);
        }
        l = g;
        break;
    }
    default: return nullptr;
    }
    l->id = quint32(o.value("id").toInteger());
    l->name = o.value("name").toString();
    l->visible = o.value("visible").toBool(true);
    l->opacity = float(std::clamp(o.value("opacity").toDouble(1), 0.0, 1.0));
    int b = o.value("blend").toInt();
    l->blend = (b >= 0 && b < int(BlendMode::Count)) ? BlendMode(b) : BlendMode::Normal;
    l->clipping = o.value("clipping").toBool();
    l->locked = o.value("locked").toBool();
    l->alphaLock = o.value("alphaLock").toBool();
    if (o.contains("label")) l->labelColor = QColor(o.value("label").toString());
    l->reference = o.value("reference").toBool();
    l->hideInTimelapse = o.value("hideInTimelapse").toBool();
    if (o.contains("fx")) l->fx = LayerEffects::fromJson(o.value("fx").toObject());
    if (o.contains("mask")) {
        l->hasMask = true;
        l->maskEnabled = o.value("maskEnabled").toBool(true);
        if (!r.mask(o.value("mask").toInt(-1), l->mask)) return nullptr;
    }
    return l;
}

}  // namespace

DocSnapshot takeSnapshot(const Document& doc) {
    DocSnapshot s;
    DocState st;
    st.root = std::static_pointer_cast<GroupLayer>(doc.root()->clone());
    st.selection = doc.selection();
    st.hasSelection = doc.hasSelection();
    st.currentId = doc.currentLayerId();
    st.size = doc.size();
    s.state = std::move(st);
    s.dpi = doc.dpi();
    s.perspective = doc.perspective;
    s.symmetry = doc.symmetry;
    s.ruler = doc.ruler;
    s.seamless = doc.seamless;
    return s;
}

bool saveNative(const DocSnapshot& snap, const QString& path, QString* error, const QImage& thumbnail) {
    Writer w;
    QJsonObject head;
    head["app"] = "GraphiX Paint";
    head["appVersion"] = GX_VERSION;
    head["width"] = snap.state.size.width();
    head["height"] = snap.state.size.height();
    head["dpi"] = snap.dpi;
    head["current"] = qint64(snap.state.currentId);
    head["perspective"] = snap.perspective.toJson();
    head["symmetry"] = snap.symmetry.toJson();
    if (snap.ruler.isOn()) head["ruler"] = snap.ruler.toJson();
    if (snap.seamless) head["seamless"] = true;
    QJsonArray layers;
    for (const auto& c : snap.state.root->children) layers.append(layerToJson(*c, w));
    head["layers"] = layers;
    if (snap.state.hasSelection) head["selection"] = w.add(snap.state.selection);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    ds.writeRawData(kMagic, 8);
    ds << kVersion;
    ds << qCompress(QJsonDocument(head).toJson(QJsonDocument::Compact), 6);
    ds << quint32(w.blobs.size());
    for (const auto& b : w.blobs) {
        ds << b.kind << b.defaultValue << quint32(b.tiles.size());
        for (const auto& t : b.tiles) {
            ds << t.tx << t.ty << quint8(t.uniform ? 1 : 0);
            if (t.uniform) ds << t.value;
            else ds << t.data;
        }
    }
    QByteArray thumb;
    if (!thumbnail.isNull()) {
        QBuffer buf(&thumb);
        buf.open(QIODevice::WriteOnly);
        thumbnail.save(&buf, "PNG");
    }
    ds << thumb;
    if (ds.status() != QDataStream::Ok) {
        if (error) *error = "Could not write the file.";
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

std::unique_ptr<Document> loadNative(const QString& path, QString* error) {
    auto fail = [&](const QString& e) -> std::unique_ptr<Document> {
        if (error) *error = e;
        return nullptr;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(f.errorString());
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    char magic[8];
    if (ds.readRawData(magic, 8) != 8 || std::memcmp(magic, kMagic, 8) != 0) return fail("This is not a GraphiX file.");
    quint32 version = 0;
    ds >> version;
    if (version == 0 || version > kVersion) return fail("The file was made by a newer version of GraphiX.");
    QByteArray headZ;
    ds >> headZ;
    QJsonParseError pe;
    QJsonDocument jd = QJsonDocument::fromJson(qUncompress(headZ), &pe);
    if (pe.error != QJsonParseError::NoError || !jd.isObject()) return fail("The file header is damaged.");
    QJsonObject head = jd.object();
    Reader r;
    quint32 nb = 0;
    ds >> nb;
    if (nb > 1000000) return fail("The file is damaged.");
    r.blobs.resize(nb);
    for (quint32 i = 0; i < nb; ++i) {
        SurfaceBlob& b = r.blobs[i];
        quint32 nt = 0;
        ds >> b.kind >> b.defaultValue >> nt;
        if (nt > 50000000u || ds.status() != QDataStream::Ok) return fail("The file is damaged.");
        b.tiles.resize(nt);
        for (auto& t : b.tiles) {
            quint8 u = 0;
            ds >> t.tx >> t.ty >> u;
            t.uniform = u != 0;
            if (t.uniform) ds >> t.value;
            else ds >> t.data;
            if (ds.status() != QDataStream::Ok) return fail("The file is truncated.");
        }
    }
    int w = head.value("width").toInt(), h = head.value("height").toInt();
    if (w <= 0 || h <= 0 || w > 200000 || h > 200000) return fail("The canvas size in the file is invalid.");
    auto doc = std::make_unique<Document>(QSize(w, h));
    doc->setDpi(head.value("dpi").toDouble(350));
    doc->perspective = Perspective::fromJson(head.value("perspective").toObject());
    if (head.contains("symmetry")) doc->symmetry = Symmetry::fromJson(head.value("symmetry").toObject());
    if (head.contains("ruler")) doc->ruler = Ruler::fromJson(head.value("ruler").toObject());
    doc->seamless = head.value("seamless").toBool();
    for (const auto& lv : head.value("layers").toArray()) {
        LayerPtr l = layerFromJson(lv.toObject(), r, 0);
        if (!l) {
            if (!r.error.isEmpty()) return fail(r.error);
            continue;
        }
        doc->root()->insert(int(doc->root()->children.size()), l);
    }
    // Make sure every id is unique (older or hand-edited files)
    QSet<quint32> seen;
    forEachLayer(doc->root(), [&](Layer* l) {
        if (l->id == 0 || seen.contains(l->id)) l->id = 0;
        else seen.insert(l->id);
        return true;
    });
    forEachLayer(doc->root(), [&](Layer* l) {
        if (l->id == 0) l->id = doc->newLayerId();
        return true;
    });
    if (head.contains("selection")) {
        MaskSurface sel(0);
        if (r.mask(head.value("selection").toInt(-1), sel)) {
            doc->selection() = sel;
            doc->setHasSelection(!sel.isEmpty());
        }
    }
    if (doc->root()->children.empty()) {
        auto l = std::make_shared<RasterLayer>();
        l->id = doc->newLayerId();
        l->name = "Layer 1";
        doc->root()->insert(0, l);
    }
    quint32 cur = quint32(head.value("current").toInteger());
    doc->setCurrentLayer(doc->layer(cur) ? cur : doc->root()->children.back()->id);
    doc->setFilePath(path);
    doc->setModified(false);
    doc->clearHistory();
    return doc;
}

}  // namespace gx
