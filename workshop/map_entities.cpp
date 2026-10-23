#include "extended_maps.h"
#include "mod/npcs.h"
#include "map_entities.h"
#include "actor_names.h"
#include "contents_editor.h"
#include "custom_tiles.h"
#include "dialogue.h"
#include "inventory_names.h"
#include "location_text.h"
#include "map_positions.h"
#include "world_objects.h"
#include <QRandomGenerator>
#include <algorithm>
#include <cstring>
using U5::require;
namespace Workshop {
QMap<QString, QByteArray> createNpcConversation(const Project &p, const QString &npc, int town,
                                                int slot, const QString &name) {
    auto bytes = p.data(npc);
    require(NPCS_Validate(bytes.constData(), bytes.size()), "Invalid NPC resource");
    require(town >= 0 && town < 8 && slot > 0 &&
                slot < int(NPCS_Count(bytes.constData(), bytes.size(), town)),
            "Choose an NPC");
    auto dialogue = [&](int t, int n) {
        return U5::byte(bytes, NPCS_DialogueOffset(bytes.constData(), bytes.size(), t, n));
    };
    const QString talk = npc.left(npc.size() - 4) + ".TLK";
    require(p.resources.contains(talk), "Conversation resource unavailable");
    auto entries = U5::readDialogue(p.data(talk));
    for (const auto &entry : entries)
        require(int(entry.id) != dialogue(town, slot), "NPC already has a conversation");
    QSet<int> used;
    for (const auto &entry : entries)
        used.insert(entry.id);
    for (int t = 0; t < 8; ++t)
        for (int n = 0; n < int(NPCS_Count(bytes.constData(), bytes.size(), t)); ++n)
            used.insert(dialogue(t, n));
    int id = 1;
    while (id < 128 && used.contains(id))
        ++id;
    require(id < 128, "No free conversation numbers remain");
    auto text = Dialogue::text(name);
    text += U5::encodeText("<Entry>A traveler.<Entry>Greetings.<Entry>I live "
                           "here.<Entry>Farewell.\"<End Conversation><Entry><Any><Label 15>@");
    require(Dialogue::parse(text).structuralError.isEmpty(), "Invalid new conversation");
    entries.append({unsigned(id), text});
    bytes[NPCS_DialogueOffset(bytes.constData(), bytes.size(), town, slot)] = char(id);
    return {{npc, bytes}, {talk, U5::writeDialogue(entries)}};
}
QString actorName(int shape) {
    // Combat group names do not always describe the NPC appearance artwork.
    if (shape == 96)
        return "person in stocks";
    if (shape >= 64 && shape <= 252) {
        QString name = QString::fromLatin1(engineActorNames[(shape - 64) / 4]).toLower();
        // This table names combat groups; the picker places one creature at a time.
        if (name == "headlesses")
            return "headless";
        if (name.endsWith('s'))
            name.chop(1);
        return name;
    }
    return QString("Appearance %1").arg(shape);
}
QList<int> monsterShapes() {
    QList<int> out;
    for (int shape = 128; shape <= 248; shape += 4)
        if (shape != 180 && shape != 232 && shape != 236 && actorName(shape) != "x")
            out.append(shape);
    return out;
}
QString mapItemName(MapItemKind k) {
    switch (k) {
    case MapItemKind::Gem:
        return "Gem";
    case MapItemKind::Key:
        return "Key";
    case MapItemKind::SkeletonKey:
        return "Skeleton key";
    case MapItemKind::Torch:
        return "Torch";
    case MapItemKind::Potion:
        return "Potion";
    case MapItemKind::Scroll:
        return "Scroll";
    case MapItemKind::Equipment:
        return "Equipment";
    case MapItemKind::SandalwoodBox:
        return "Sandalwood box";
    case MapItemKind::Corpse:
        return "Corpse";
    case MapItemKind::Gold:
        return "Gold";
    case MapItemKind::Food:
        return "Food";
    case MapItemKind::Moonstone:
        return "Moonstone";
    case MapItemKind::SleepingFigure:
        return "Sleeping figure";
    case MapItemKind::Npc:
        return "NPC";
    case MapItemKind::Monster:
        return "Monster";
    case MapItemKind::Sign:
        return "Sign";
    case MapItemKind::Container:
        return "Chest";
    case MapItemKind::HiddenContents:
        return "Container";
    case MapItemKind::Horse:
        return "Horse";
    case MapItemKind::Skiff:
        return "Skiff";
    case MapItemKind::Frigate:
        return "Frigate";
    case MapItemKind::Carpet:
        return "Magic carpet";
    case MapItemKind::Trigger:
        return "Trigger";
    }
    return {};
}
int mapItemShape(MapItemKind kind, int variant) {
    switch (kind) {
    case MapItemKind::Gold:
        return 2;
    case MapItemKind::Food:
        return 15;
    case MapItemKind::Moonstone:
        return 25;
    case MapItemKind::Gem:
        return 8;
    case MapItemKind::Key:
    case MapItemKind::SkeletonKey:
        return 7;
    case MapItemKind::Torch:
        return 13;
    case MapItemKind::Potion:
        return 3;
    case MapItemKind::Scroll:
        return 4;
    case MapItemKind::Equipment:
        variant = variant < 0 ? 16 : variant;
        return variant < 4    ? 9
               : variant < 9  ? 6
               : variant < 16 ? 11
               : variant < 42 ? 5
               : variant < 45 ? 10
                              : 12;
    case MapItemKind::SandalwoodBox:
        return 14;
    case MapItemKind::Corpse:
        return 31;
    case MapItemKind::SleepingFigure:
        return 30;
    case MapItemKind::Horse:
        return 16;
    case MapItemKind::Skiff:
        return 40;
    case MapItemKind::Frigate:
        return 32;
    case MapItemKind::Carpet:
        return 27;
    default:
        return variant < 0 ? 144 : variant;
    }
}
QList<QPair<QString, int>> mapItemVariants(MapItemKind kind) {
    QList<QPair<QString, int>> out;
    if (kind == MapItemKind::Moonstone)
        for (auto name :
             QStringList{"New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
                         "Full moon", "Waning gibbous", "Last quarter", "Waning crescent"})
            out.append({name, out.size()});
    if (kind == MapItemKind::Equipment)
        for (int i = 0; i < 48; ++i)
            out.append({QString::fromUtf8(engineEquipmentNames[i]), i});
    if (kind == MapItemKind::Potion)
        for (auto name :
             QStringList{"Blue", "Yellow", "Red", "Green", "Orange", "Purple", "Black", "White"})
            out.append({name, out.size()});
    if (kind == MapItemKind::Scroll) {
        for (auto name : QStringList{"Vas Lor", "Rel Hur", "In Sanct", "In An", "In Quas Wis",
                                     "Kal Xen", "In Bet Xen", "An Tym"})
            out.append({name, out.size()});
        out.append({"HMS Cape plans", 255});
    }
    return out;
}
QList<MapItemKind> availableMapItems(const Project &p, const QString &r) {
    QList<MapItemKind> out;
    if (!MapDocument::supported(r))
        return out;
    const auto npc = r.left(r.size() - 4) + ".NPC";
    const bool worldObjectsAvailable =
        (r == "BRIT.DAT" || r == "UNDER.DAT") && p.resources.contains("INIT.OOL") &&
        p.resources.contains("BRIT.OOL") && p.resources.contains("UNDER.OOL");
    if (p.resources.contains(npc))
        out << MapItemKind::Npc;
    if (worldObjectsAvailable || r.endsWith(".CBT") || p.resources.contains(npc))
        out << MapItemKind::Monster;
    if (p.resources.contains("SIGNS.DAT") &&
        (MAPS_LocationResource(r.toLatin1().constData()) ||
         QStringList{"BRIT.DAT", "UNDER.DAT", "TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"}
             .contains(r)))
        out << MapItemKind::Sign;
    out << MapItemKind::Container << MapItemKind::HiddenContents;
    if (worldObjectsAvailable)
        out << MapItemKind::Horse << MapItemKind::Skiff << MapItemKind::Frigate
            << MapItemKind::Carpet << MapItemKind::Gold << MapItemKind::Food
            << MapItemKind::SleepingFigure << MapItemKind::Gem << MapItemKind::Key
            << MapItemKind::SkeletonKey << MapItemKind::Torch << MapItemKind::Potion
            << MapItemKind::Scroll << MapItemKind::Equipment << MapItemKind::SandalwoodBox
            << MapItemKind::Corpse;
    if ((r == "BRIT.DAT" || r == "UNDER.DAT") && p.resources.contains("INIT.GAM") &&
        p.data("INIT.GAM").size() == 4192)
        out << MapItemKind::Moonstone;
    if (r == "DUNGEON.CBT")
        out << MapItemKind::Trigger;
    return out;
}
QByteArray addContentsAt(const Project &p, ContentsTarget target, QPoint cell, bool container) {
    auto size = mapSize(p, target.map, target.floor, target.kind);
    require(cell.x() >= 0 && cell.y() >= 0 && cell.x() < size.width() && cell.y() < size.height(),
            "Placement outside map");
    require(!contentsHit(p, target, cell),
            "This tile already has contents; select it to edit them");
    auto defs = readContents(p.resources.value("CONTENTS.OVR").edited);
    ContentsDef d{};
    d.kind = target.kind;
    d.map = target.map;
    d.floor = target.floor;
    d.x = cell.x();
    d.y = cell.y();
    d.slot = 255;
    d.original = 65535;
    do {
        d.id = QRandomGenerator::global()->generate() | 0x80000000u;
    } while (std::any_of(defs.begin(), defs.end(), [&](auto o) { return o.id == d.id; }));
    d.action = container;
    defs.append(d);
    return writeContents(defs, p.data("CONTENTS.OVR"));
}
QMap<QString, QByteArray> moveSignTerrain(const Project &p, const QString &r, int page, QPoint from,
                                          QPoint to) {
    MapDocument doc(&p, r);
    require(page >= 0 && page < doc.pages.size(), "Invalid sign floor");
    auto terrain = doc.terrain(page);
    if (from != to) {
        auto custom = CustomTiles::moveScenery(p, r, page, from, to);
        if (!custom.isEmpty())
            return custom;
    }
    int side = doc.pages[page].side;
    require(from.x() >= 0 && from.y() >= 0 && from.x() < side &&
                from.y() < doc.pages[page].height(),
            "Sign source outside map");
    require(to.x() >= 0 && to.y() >= 0 && to.x() < side && to.y() < doc.pages[page].height(),
            "Sign destination outside map");
    int tile = U5::byte(terrain, from.y() * side + from.x());
    if (tile != 0x89 && tile != 0x8a && tile != 0xf8)
        return {};
    int ground = 5;
    for (QPoint delta : {QPoint(0, 1), QPoint(1, 0), QPoint(-1, 0), QPoint(0, -1)}) {
        auto n = from + delta;
        if (n.x() >= 0 && n.y() >= 0 && n.x() < side && n.y() < doc.pages[page].height()) {
            int t = U5::byte(terrain, n.y() * side + n.x());
            if (t == 5 || t == 0x40 || t == 0x44 || t == 0x45) {
                ground = t;
                break;
            }
        }
    }
    terrain[from.y() * side + from.x()] = char(ground);
    if (to != from)
        terrain[to.y() * side + to.x()] = char(tile);
    return doc.changes(page, terrain);
}
static int freeWorldSlot(const Project &p, int world) {
    auto b = WorldObjects::effective(p, world);
    // OUTSUBS_0566 uses Underworld slots 28–31 for the amulet and shards.
    for (int slot = 1; slot < (world ? 28 : 32); ++slot)
        if (!U5::byte(b, slot * 8))
            return slot;
    require(false, "No free world object slots remain");
    return -1;
}
void validateActorPlacement(const Project &p, const QString &r, int index, QPoint cell,
                            unsigned type, int ignoreSlot, int schedule) {
    MapDocument doc(&p, r);
    const auto page = doc.pages.at(index);
    bool world = r == "BRIT.DAT" || r == "UNDER.DAT";
    auto d = CustomTiles::definition(p, type);
    QSize size(world ? 1 : TYPES_Width(&d), world ? 1 : TYPES_Height(&d));
    QRect footprint(cell, size);
    bool large = size != QSize(1, 1);
    require(QRect(0, 0, page.side, page.height()).contains(footprint),
            "The whole creature must fit inside the map");
    if (large) {
        unsigned shape = d.base;
        for (unsigned a = 0; a < 256; a += 4) {
            TypeDef native;
            TYPES_Vanilla(1, a, &native);
            if (native.moveProfile == d.moveProfile) {
                shape = a;
                break;
            }
        }
        auto terrain = doc.terrain(index);
        auto expanded = CustomTiles::expanded(p, r, page, index, terrain);
        for (int y = footprint.top(); y <= footprint.bottom(); ++y)
            for (int x = footprint.left(); x <= footprint.right(); ++x) {
                unsigned id = TYPES_U32(expanded.constData() + (y * page.side + x) * 4);
                if (id) {
                    auto tile = CustomTiles::definition(p, id);
                    require(TYPES_DefWalkable(shape, &tile),
                            "Part of the creature is on blocked terrain");
                } else
                    require(TYPES_Walkable(shape, U5::byte(terrain, y * page.side + x)),
                            "Part of the creature is on blocked terrain");
            }
    }
    auto check = [&](QPoint at, unsigned otherType) {
        auto other = CustomTiles::definition(p, otherType);
        QSize otherSize(world ? 1 : TYPES_Width(&other), world ? 1 : TYPES_Height(&other));
        if (large || otherSize != QSize(1, 1))
            require(!footprint.intersects(QRect(at, otherSize)), "Creature footprints overlap");
    };
    auto npcResource = r.left(r.size() - 4) + ".NPC";
    if (!world && p.resources.contains(npcResource) && page.settlement >= 0) {
        Project view = p;
        MapNpcDocument npcs(&view, npcResource);
        for (int slot = 1; slot < npcs.count(page.settlement); ++slot) {
            if (slot == ignoreSlot ||
                !U5::byte(p.data(npcResource), npcs.shapeOffset(page.settlement, slot)))
                continue;
            for (int hour = schedule < 0 ? 0 : schedule; hour < (schedule < 0 ? 4 : schedule + 1);
                 ++hour) {
                auto pos = npcs.location(page.settlement, slot, hour);
                if (pos.floor == page.floor && pos.x < page.side && pos.y < page.height())
                    check({pos.x, pos.y}, npcs.sprite(page.settlement, slot));
            }
        }
    } else if (r.endsWith(".CBT")) {
        auto bytes = p.data(r);
        int base = page.offset;
        for (int slot = 0; slot < 16; ++slot) {
            if (slot == ignoreSlot)
                continue;
            int x = U5::byte(bytes, base + 6 * 32 + 11 + slot),
                y = U5::byte(bytes, base + 7 * 32 + 11 + slot);
            if (x >= page.side || y >= page.height() ||
                !mapMarkerActive(p, r, index, 24 + slot, x, y) ||
                (r == "DUNGEON.CBT" && !U5::byte(bytes, base + 5 * 32 + 11 + slot)))
                continue;
            unsigned fallback =
                r == "DUNGEON.CBT" ? 256 + U5::byte(bytes, base + 5 * 32 + 11 + slot) : 400;
            check({x, y}, CustomTiles::actorType(p, r, page, index, slot, fallback));
        }
    }
}
MapItemEdit addMapItem(const Project &p, const QString &r, int index, QPoint cell, MapItemKind kind,
                       int appearance, int conversation, const QString &name) {
    require(availableMapItems(p, r).contains(kind), "This item is not supported on this map");
    MapDocument doc(&p, r);
    require(index >= 0 && index < doc.pages.size(), "Invalid map floor");
    const auto page = doc.pages[index];
    require(cell.x() >= 0 && cell.y() >= 0 && cell.x() < page.side && cell.y() < page.height(),
            "Placement outside map");
    unsigned customType = appearance >= 512 ? unsigned(appearance) : 0;
    if (customType) {
        auto type = CustomTiles::definition(p, customType);
        require(type.kind && (kind == MapItemKind::Npc || kind == MapItemKind::Monster),
                "Choose a creature type");
        appearance = type.base;
    }
    if (kind == MapItemKind::Npc || kind == MapItemKind::Monster)
        validateActorPlacement(
            p, r, index, cell,
            customType
                ? customType
                : 256 + (appearance < 0 ? (kind == MapItemKind::Monster ? 144 : 80) : appearance));
    MapItemEdit out;
    auto target = contentsTarget(r, page, index);
    if (kind == MapItemKind::Moonstone) {
        int phase = appearance < 0 ? 0 : appearance;
        require(phase >= 0 && phase < 8, "Choose a moonstone phase");
        out.changes = moveMapPosition(p, phase + 1, 0, r == "UNDER.DAT" ? -1 : 0, cell);
        out.slot = phase + 1;
        out.mode = "Positions";
    } else if (kind == MapItemKind::Npc || (kind == MapItemKind::Monster &&
                                            p.resources.contains(r.left(r.size() - 4) + ".NPC"))) {
        const auto npc = r.left(r.size() - 4) + ".NPC", talk = r.left(r.size() - 4) + ".TLK";
        auto b = p.data(npc);
        require(b.size() >= 4608 && page.settlement >= 0, "No valid NPC records on this map");
        int start = page.settlement * 576;
        const int count = NPCS_Count(b.constData(), b.size(), page.settlement);
        for (int i = 1; i < count; i++)
            if (!U5::byte(b, NPCS_ShapeOffset(b.constData(), b.size(), page.settlement, i))) {
                out.slot = i;
                break;
            }
        if (out.slot <= 0)
            out.slot = count;
        MapNpcDocument::ensureSlot(b, page.settlement, out.slot);
        require(customType || appearance == -1 ||
                    (kind == MapItemKind::Monster
                         ? monsterShapes().contains(appearance)
                         : (appearance >= 64 && appearance < 128 && appearance % 4 == 0)),
                "Choose a supported appearance");
        if (kind == MapItemKind::Monster)
            conversation = 0;
        else if (conversation < 0) {
            require(p.resources.contains(talk), "Conversation resource unavailable");
            auto entries = U5::readDialogue(p.data(talk));
            QSet<int> used;
            for (auto e : entries)
                used.insert(e.id);
            for (int town = 0; town < 8; town++)
                for (int i = 0; i < int(NPCS_Count(b.constData(), b.size(), town)); i++)
                    used.insert(U5::byte(b, NPCS_DialogueOffset(b.constData(), b.size(), town, i)));
            for (int id = 1; id < 128; id++)
                if (!used.contains(id)) {
                    conversation = id;
                    break;
                }
            require(conversation > 0, "No free conversation numbers remain");
            auto bytes = Dialogue::text(name);
            bytes +=
                U5::encodeText("<Entry>A traveler.<Entry>Greetings.<Entry>I live "
                               "here.<Entry>Farewell.\"<End Conversation><Entry><Any><Label 15>@");
            require(Dialogue::parse(bytes).structuralError.isEmpty(), "Invalid new conversation");
            entries.append({unsigned(conversation), bytes});
            out.changes[talk] = U5::writeDialogue(entries);
        } else {
            require(conversation > 0 && conversation < 128, "Choose an editable conversation");
            bool found = false;
            for (auto e : U5::readDialogue(p.data(talk)))
                found |= int(e.id) == conversation;
            require(found, "Conversation unavailable");
        }
        int base = NPCS_Offset(b.constData(), b.size(), page.settlement, out.slot);
        b.replace(base, 16, QByteArray(16, 0));
        for (int loc = 0; loc < 3; loc++) {
            b[base + loc] = char(kind == MapItemKind::Monster ? 5 : 0);
            b[base + 3 + loc] = char(cell.x());
            b[base + 6 + loc] = char(cell.y());
            b[base + 9 + loc] = char(page.floor);
        }
        for (int h = 0; h < 4; h++)
            b[base + 12 + h] = char(h * 6);
        b[NPCS_ShapeOffset(b.constData(), b.size(), page.settlement, out.slot)] =
            char(appearance < 0 ? (kind == MapItemKind::Monster ? 144 : 80) : appearance);
        b[NPCS_DialogueOffset(b.constData(), b.size(), page.settlement, out.slot)] =
            char(conversation);
        out.changes[npc] = b;
        out.mode = "NPCs / Schedules";
        if (out.slot < 32 && target.map <= 32 && p.resources.contains("INIT.GAM")) {
            auto initial = p.data("INIT.GAM");
            int flag = 0x5b4 + (target.map - 1) * 4 + out.slot / 8;
            require(flag < initial.size(), "Initial NPC state is truncated");
            initial[flag] = char(U5::byte(initial, flag) & ~(1u << (out.slot % 8)));
            if (initial != p.data("INIT.GAM"))
                out.changes["INIT.GAM"] = initial;
        }
    } else if (kind == MapItemKind::Sign) {
        require(page.floor <= 3, "Signs support basement through upper floor 3");
        auto signs = readSigns(p.data("SIGNS.DAT"));
        for (auto s : signs)
            require(s.map != target.map || s.floor != target.floor || s.x != cell.x() ||
                        s.y != cell.y(),
                    "A sign already exists here");
        signs.append({target.map, target.floor, cell.x(), cell.y(), encodeSign("New sign")});
        out.changes["SIGNS.DAT"] = writeSigns(p.data("SIGNS.DAT"), signs);
        auto terrain = doc.terrain(index);
        terrain[cell.y() * page.side + cell.x()] = char(0xf8);
        auto changes = doc.changes(index, terrain);
        for (auto it = changes.begin(); it != changes.end(); ++it)
            out.changes[it.key()] = it.value();
        out.slot = signs.size() - 1;
        out.mode = "Signs";
    } else if (kind == MapItemKind::Container || kind == MapItemKind::HiddenContents) {
        out.changes["CONTENTS.OVR"] =
            addContentsAt(p, target, cell, kind == MapItemKind::Container);
        out.mode = "Contents";
    } else if (r.endsWith(".CBT")) {
        auto b = p.data(r);
        int base = page.offset;
        if (kind == MapItemKind::Trigger) {
            for (int i = 0; i < 8; i++)
                if (!mapMarkerActive(p, r, index, 40 + i, U5::byte(b, base + 8 * 32 + 11 + i),
                                     U5::byte(b, base + 8 * 32 + 19 + i))) {
                    out.slot = 22 + i;
                    break;
                }
            require(out.slot >= 22, "No free trigger slots remain");
            int i = out.slot - 22;
            b[base + 8 * 32 + 11 + i] = char(cell.x());
            b[base + 8 * 32 + 19 + i] = char(cell.y());
            b[base + 11 + i] = char(5);
            for (int row : {9, 10}) {
                b[base + row * 32 + 11 + i] = char(cell.x());
                b[base + row * 32 + 19 + i] = char(cell.y());
            }
        } else {
            for (int i = 0; i < 16; i++)
                if (r == "DUNGEON.CBT" ? !U5::byte(b, base + 5 * 32 + 11 + i)
                                       : (!mapMarkerActive(p, r, index, 24 + i,
                                                           U5::byte(b, base + 6 * 32 + 11 + i),
                                                           U5::byte(b, base + 7 * 32 + 11 + i)))) {
                    out.slot = 6 + i;
                    break;
                }
            require(out.slot >= 6, "No free monster positions remain");
            int i = out.slot - 6;
            require(appearance < 0 || monsterShapes().contains(appearance),
                    "Choose a supported monster");
            if (r == "DUNGEON.CBT")
                b[base + 5 * 32 + 11 + i] = char(appearance < 0 ? 144 : appearance);
            b[base + 6 * 32 + 11 + i] = char(cell.x());
            b[base + 7 * 32 + 11 + i] = char(cell.y());
        }
        auto set = [&](int bit) {
            Project view = p;
            for (auto it = out.changes.begin(); it != out.changes.end(); ++it)
                view.resources[it.key()].edited = it.value();
            auto edits = setMapMarker(view, r, index, bit, true);
            for (auto it = edits.begin(); it != edits.end(); ++it)
                out.changes[it.key()] = it.value();
        };
        if (out.slot >= 22) {
            set(40 + out.slot - 22);
            set(48 + out.slot - 22);
            set(56 + out.slot - 22);
        } else
            set(24 + out.slot - 6);
        out.changes[r] = b;
        out.mode = "Encounter";
    } else {
        int world = r == "UNDER.DAT";
        out.slot = freeWorldSlot(p, world);
        QByteArray record(8, 0);
        int shape = mapItemShape(kind, appearance);
        require(kind != MapItemKind::Monster || monsterShapes().contains(shape),
                "Choose a supported monster");
        auto variants = mapItemVariants(kind);
        if (!variants.isEmpty() && appearance >= 0)
            require(std::any_of(variants.begin(), variants.end(),
                                [=](auto v) { return v.second == appearance; }),
                    "Choose a supported item type");
        record[0] = record[1] = char(shape);
        record[2] = char(cell.x());
        record[3] = char(cell.y());
        record[4] = char(world ? 255 : 0);
        record[5] =
            char(kind == MapItemKind::Frigate       ? 99
                 : kind == MapItemKind::SkeletonKey ? 129
                 : kind == MapItemKind::Equipment   ? (appearance < 0 ? 16 : appearance)
                 : kind == MapItemKind::Potion || kind == MapItemKind::Scroll ? qMax(0, appearance)
                 : shape == 2 || shape == 15 || shape == 7 || shape == 8 || shape == 13 ? 1
                                                                                        : 0);
        record[7] = char(255);
        out.changes[WorldObjects::resource(world)] =
            WorldObjects::change(p, world, out.slot, record);
        out.mode = "Starting Objects";
    }
    if (customType) {
        auto typeChanges = CustomTiles::assignActor(
            p, r, page, index, r.endsWith(".CBT") ? out.slot - 6 : out.slot, customType);
        for (auto it = typeChanges.begin(); it != typeChanges.end(); ++it)
            out.changes[it.key()] = it.value();
    }
    Project candidate = p;
    for (auto it = out.changes.begin(); it != out.changes.end(); ++it) {
        candidate.resources[it.key()].edited = it.value();
        if (it.key().endsWith(".OOL"))
            candidate.resources[it.key()].included = true;
    }
    candidate.validate();
    return out;
}
QMap<QString, QByteArray> deleteMapItem(const Project &p, const QString &r, int index,
                                        const QString &mode, int slot, QPoint cell) {
    MapDocument doc(&p, r);
    const auto page = doc.pages.at(index);
    QMap<QString, QByteArray> out;
    if (mode == "NPCs / Schedules") {
        require(slot > 0, "Select an NPC");
        auto npc = r.left(r.size() - 4) + ".NPC";
        auto b = p.data(npc);
        int base = NPCS_Offset(b.constData(), b.size(), page.settlement, slot);
        require(base + 16 <= b.size(), "Invalid NPC slot");
        b.replace(base, 16, QByteArray(16, 0));
        b[NPCS_ShapeOffset(b.constData(), b.size(), page.settlement, slot)] = 0;
        b[NPCS_DialogueOffset(b.constData(), b.size(), page.settlement, slot)] = 0;
        out[npc] = b;
        // Conversations can be shared: deleting an NPC never deletes their
        // conversation.
    } else if (mode == "Encounter") {
        require(slot >= 6 && slot < 30, "Party entry points cannot be deleted");
        auto b = p.data(r);
        int base = page.offset;
        if (slot < 22) {
            int i = slot - 6;
            b[base + 5 * 32 + 11 + i] = 0;
            b[base + 6 * 32 + 11 + i] = b[base + 7 * 32 + 11 + i] = char(255);
        } else {
            int i = slot - 22;
            b[base + 8 * 32 + 11 + i] = b[base + 8 * 32 + 19 + i] = char(255);
        }
        out[r] = b;
        auto flags = setMapMarker(p, r, index, slot < 22 ? 24 + slot - 6 : 40 + slot - 22, false);
        for (auto it = flags.begin(); it != flags.end(); ++it)
            out[it.key()] = it.value();
    } else if (mode == "Signs") {
        auto signs = readSigns(p.data("SIGNS.DAT"));
        auto target = contentsTarget(r, page, index);
        bool removed = false;
        for (int i = signs.size() - 1; i >= 0; i--)
            if (signs[i].map == target.map && signs[i].floor == target.floor &&
                signs[i].x == cell.x() && signs[i].y == cell.y()) {
                signs.removeAt(i);
                removed = true;
            }
        require(removed, "Select an editable sign");
        out["SIGNS.DAT"] = writeSigns(p.data("SIGNS.DAT"), signs);
        auto changes = moveSignTerrain(p, r, index, cell, cell);
        for (auto it = changes.begin(); it != changes.end(); ++it)
            out[it.key()] = it.value();
    } else if (mode == "Starting Objects" || mode == "Contents") {
        auto target = contentsTarget(r, page, index);
        auto defs = readContents(p.data("CONTENTS.OVR"));
        if (mode == "Starting Objects") {
            int world = r == "UNDER.DAT";
            require(slot > 0 && slot < (world ? 28 : 32), "Select an editable world object");
            auto record = WorldObjects::effective(p, world).mid(slot * 8, 8);
            record[0] = record[1] = 0;
            out[WorldObjects::resource(world)] = WorldObjects::change(p, world, slot, record);
            for (int i = defs.size() - 1; i >= 0; i--)
                if (!defs[i].kind && !defs[i].map && defs[i].floor == target.floor &&
                    defs[i].slot == slot)
                    defs.removeAt(i);
        } else {
            ContentsDef selected{};
            for (auto d : mapContents(p, target))
                if (d.x == cell.x() && d.y == cell.y())
                    selected = d;
            require(selected.id, "Select contents to delete");
            for (int i = defs.size() - 1; i >= 0; i--)
                if (defs[i].id == selected.id)
                    defs.removeAt(i);
            if (selected.slot != 255) {
                int world = selected.floor < 0;
                auto record = WorldObjects::effective(p, world).mid(selected.slot * 8, 8);
                record[0] = record[1] = 0;
                out[WorldObjects::resource(world)] =
                    WorldObjects::change(p, world, selected.slot, record);
            } else if (selected.original != 65535) {
                selected.count = 0;
                selected.flags = 0;
                selected.refill = 0;
                memset(selected.items, 0, sizeof(selected.items));
                defs.append(selected); // Suppress inherited rewards instead of restoring them.
            }
        }
        out["CONTENTS.OVR"] = writeContents(defs, p.data("CONTENTS.OVR"));
    } else
        require(false, "Select a map item to delete");
    if (mode == "NPCs / Schedules" || mode == "Starting Objects" ||
        (mode == "Encounter" && slot >= 6 && slot < 22)) {
        auto changes =
            CustomTiles::assignActor(p, r, page, index, r.endsWith(".CBT") ? slot - 6 : slot, 0);
        for (auto it = changes.begin(); it != changes.end(); ++it)
            out[it.key()] = it.value();
    }
    return out;
}
} // namespace Workshop
