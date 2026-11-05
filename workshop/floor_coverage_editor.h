#pragma once
#include "mod/floor_coverage.h"
#include "project.h"
#include <QtWidgets>
namespace Workshop {
QByteArray floorCoverage(const Project &, const QString &, int page);
QByteArray defaultFloorCoverage(const Project &, const QString &, int page, bool suggest = false);
QByteArray setFloorCoverage(const Project &, unsigned map, int floor, int width, int height,
                            const QByteArray &flags);
void validateFloorCoverage(const Project &);
QMap<QString, QByteArray> editFloorCoverage(QWidget *, const Project &, const QString &, int page);
} // namespace Workshop
