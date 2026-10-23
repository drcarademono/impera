#pragma once
#include "project.h"
#include "mod/maps.h"
namespace Workshop {
struct ExtendedMap {
    unsigned kind = 0, id = 0;
    int page = 0, floor = 0, width = 32, height = 32, background = 5, world = -2, underlay = 0;
    bool markerMask = false;
    quint64 markers = 0;
    QPoint entrance, arrival;
    QString name, resource;
    QByteArray cells;
};
QVector<ExtendedMap> extendedMaps(const Project &);
QByteArray writeExtendedMaps(const QVector<ExtendedMap> &);
const ExtendedMap *findExtendedMap(const QVector<ExtendedMap> &, const QString &, int page);
QMap<QString, QByteArray> resizeMap(const Project &, const QString &, int page, int width,
                                    int height, int fill);
QMap<QString, QByteArray> addLocation(const Project &, const QString &, const QString &,
                                      QPoint entrance, int width, int height, int fill,
                                      int entranceTile = 0x12);
QSize mapSize(const Project &, int map, int floor, int kind = 0);
bool mapMarkerActive(const Project &, const QString &, int page, int bit, int x, int y);
QMap<QString, QByteArray> setMapMarker(const Project &, const QString &, int page, int bit,
                                       bool active);
void hydrateExtendedMaps(Project &);
void validateExtendedMaps(const Project &);
} // namespace Workshop
