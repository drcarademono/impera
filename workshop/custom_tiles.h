#pragma once
#include "location_text.h"
#include "mod/types.h"
#include "project.h"
namespace CustomTiles {
struct Frame {
    QImage image;
    unsigned duration = 150, variant = 0;
};
QSize artworkSize(unsigned variant);
QMap<unsigned, QByteArray> records(const Project &);
TypeDef definition(const Project &, unsigned);
void repairActorNames(Project &);
QVector<Frame> frames(const QByteArray &);
QVector<Frame> importedFrames(const QVector<Frame> &, const QVector<Frame> &, unsigned variant,
                              int selected);
QByteArray record(TypeDef, const QVector<Frame> &);
QByteArray library(const Project &, const QMap<unsigned, QByteArray> &);
QImage image(const Project &, unsigned, unsigned variant = 0, quint64 ms = 0);
void validate(const Project &);
TypeCell cell(const QString &, const U5::MapPage &, int page, int x, int y);
TypeCell actorCell(const QString &, const U5::MapPage &, int page, int slot);
unsigned actorType(const Project &, const QString &, const U5::MapPage &, int page, int slot,
                   unsigned fallback);
QMap<QString, QByteArray> assignActor(const Project &, const QString &, const U5::MapPage &,
                                      int page, int slot, unsigned type);
QMap<quint64, TypeCell> placements(const Project &);
QByteArray mapBytes(const Project &, const QMap<quint64, TypeCell> &);
QByteArray expanded(const Project &, const QString &, const U5::MapPage &, int page,
                    const QByteArray &);
QMap<QString, QByteArray> paint(const Project &, const QString &, const U5::MapPage &, int page,
                                const QByteArray &bytes, const QByteArray &expanded);
QMap<QString, QByteArray> moveScenery(const Project &, const QString &, int page, QPoint from,
                                      QPoint to);
QWidget *editor(Project *, Workshop::TextCommit, QMap<QString, int> *);
} // namespace CustomTiles
