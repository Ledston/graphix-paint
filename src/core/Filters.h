#pragma once
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <vector>

namespace gx {

// Destructive image filters (Filter menu). Work on premultiplied float RGBA buffers.
QList<QPair<QString, QString>> filterList();  // id, English name
QJsonObject filterDefaults(const QString& id);
int filterMargin(const QString& id, const QJsonObject& p);  // pixels of neighbourhood needed
// buf: (w*h*4) premultiplied; ox, oy = canvas position of the buffer (for noise seeds)
void applyFilter(const QString& id, const QJsonObject& p, std::vector<float>& buf, int w, int h, int ox, int oy);

}  // namespace gx
