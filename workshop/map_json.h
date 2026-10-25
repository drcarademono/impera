#pragma once
#include "project.h"
class QWidget;
namespace Workshop {
QString mapJsonKind(const QString &resource);
QByteArray exportMapJson(const Project &, const QString &resource, int page);
QMap<QString, QByteArray> importMapJson(const Project &, const QString &resource, int page,
                                        const QByteArray &json);
void saveMapJson(QWidget *, const Project &, const QString &, int page);
QByteArray openMapJson(QWidget *);
} // namespace Workshop
