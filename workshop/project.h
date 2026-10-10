#pragma once
#include "formats.h"
#include <QMap>
struct Resource {
    QByteArray original, edited;
};
class Project {
  public:
    QString sourceDirectory, title = "Untitled mod", projectPath;
    QMap<QString, Resource> resources;
    void openGame(const QString &directory);
    void save(const QString &path);
    void load(const QString &path);
    QByteArray package() const;
    void importPackage(const QByteArray &bytes);
    QStringList changed() const;
    const QByteArray &data(const QString &name) const;
    void validate() const;
};
