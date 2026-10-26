#pragma once
#include "project.h"
#include <QPoint>
#include <QSize>
namespace Workshop {
QList<QByteArray> readGates(const QByteArray &bytes);
QByteArray writeGates(QList<QByteArray> records);
QList<QByteArray> mapGates(const Project &project, int map, int floor, const QByteArray &terrain,
                           QSize size);
QByteArray setGate(const Project &project, const QByteArray &record, bool remove = false);
void validateGates(const Project &project);
} // namespace Workshop
