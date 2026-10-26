#pragma once
#include "engine_tables.h"
namespace Workshop {
struct MapPosition {
    int id;
    QString name;
    int map, floor;
    QPoint cell;
    bool carried = false;
};
QVector<MapPosition> mapPositions(const Project &);
int positionMap(const QString &resource, const U5::MapPage &page);
QMap<QString, QByteArray> moveMapPosition(const Project &, int id, int map, int floor, QPoint cell,
                                          bool moveLandmark = true);
QWidget *mapPositionEditor(Project *, const QString &, const U5::MapPage &, MapCanvas *,
                           std::function<bool(const QMap<QString, QByteArray> &, const QString &)>,
                           QMap<QString, int> *, std::function<void(QPoint)>,
                           std::function<void(const QString &)> navigate = {});
} // namespace Workshop
