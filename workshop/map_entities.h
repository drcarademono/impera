#pragma once
#include "map_editor.h"
namespace Workshop {
struct ContentsTarget;
enum class MapItemKind {
    Npc = 0,
    Monster = 1,
    Sign = 2,
    Container = 3,
    HiddenContents = 4,
    Horse = 5,
    Skiff = 6,
    Frigate = 7,
    Carpet = 8,
    Trigger = 9,
    Corpse = 10,
    Gold = 11,
    Food = 12,
    Moonstone = 13,
    Gem = 14,
    Key = 15,
    SkeletonKey = 16,
    Torch = 17,
    Potion = 18,
    Scroll = 19,
    Equipment = 20,
    SandalwoodBox = 21,
    BloodSplatter = 22,
    MoldyCorpse = 23,
};
struct MapItemEdit {
    QMap<QString, QByteArray> changes;
    int slot = -1;
    QString mode;
};
QString mapItemName(MapItemKind kind);
int mapItemShape(MapItemKind kind, int variant = -1);
QList<QPair<QString, int>> mapItemVariants(MapItemKind kind);
QList<MapItemKind> availableMapItems(const Project &, const QString &resource);
MapItemEdit addMapItem(const Project &, const QString &resource, int page, QPoint cell,
                       MapItemKind kind, int appearance = -1, int conversation = -1,
                       const QString &name = "New NPC");
QMap<QString, QByteArray> deleteMapItem(const Project &, const QString &resource, int page,
                                        const QString &mode, int slot, QPoint cell);
QMap<QString, QByteArray> moveSignTerrain(const Project &, const QString &, int page, QPoint from,
                                          QPoint to);
QByteArray addContentsAt(const Project &, ContentsTarget target, QPoint cell, bool container);
void validateActorPlacement(const Project &, const QString &, int page, QPoint, unsigned type,
                            int ignoreSlot = -1, int schedule = -1);
QMap<QString, QByteArray> createNpcConversation(const Project &, const QString &npcResource,
                                                int town, int slot, const QString &name);
QString locationObjectName(const Project &, const QString &, int town, int slot);
bool locationObject(const Project &, const QString &, int town, int slot);
QString actorName(int shape);
QList<int> monsterShapes();
} // namespace Workshop
