#include "map_json.h"
#include "map_editor.h"
#include "dungeon_editor.h"
#include "extended_maps.h"
#include "custom_tiles.h"
#include "contents_editor.h"
#include "map_entities.h"
#include "map_positions.h"
#include "location_text.h"
#include "world_objects.h"
#include "mod/npcs.h"
#include "mod/tileset_resources.h"
#include <QtWidgets>
#include <cmath>
using U5::require;
namespace Workshop {
namespace {
int number(QJsonValue v, int lo, int hi, const QString &field) {
    double d = v.toDouble(NAN);
    require(std::isfinite(d) && d == std::floor(d) && d >= lo && d <= hi,
            "Invalid map JSON: " + field);
    return int(d);
}
QJsonArray bytes(const QByteArray &b) {
    QJsonArray a;
    for (auto c : b)
        a.append(uchar(c));
    return a;
}
QByteArray bytes(QJsonValue v, int size) {
    require(v.isArray() && v.toArray().size() == size, "Invalid map JSON byte record");
    QByteArray b;
    for (auto x : v.toArray())
        b.append(char(number(x, 0, 255, "byte")));
    return b;
}
QVector<int> pagesFor(const Project &p, const QString &r, int page) {
    auto pages = p.mapPages(r);
    require(page >= 0 && page < pages.size(), "Choose a map");
    QVector<int> out;
    for (int i = 0; i < pages.size(); ++i)
        if ((r == "DUNGEON.DAT" || mapJsonKind(r) == "location")
                ? pages[i].settlement == pages[page].settlement
                : i == page)
            out.append(i);
    std::sort(out.begin(), out.end(),
              [&](int a, int b) { return pages[a].floor < pages[b].floor; });
    return out;
}
QJsonObject contentsJson(const ContentsDef &d) {
    QJsonArray items;
    for (int i = 0; i < d.count; ++i)
        items.append(QJsonObject{{"type", d.items[i].type},
                                 {"variant", d.items[i].variant},
                                 {"quantity", d.items[i].quantity}});
    return {{"x", d.x},
            {"y", d.y},
            {"action", d.action},
            {"refill", d.refill},
            {"original", d.original},
            {"slot", d.slot},
            {"flags", d.flags},
            {"lockDifficulty", d.lockDifficulty},
            {"trapDifficulty", d.trapDifficulty},
            {"items", items}};
}
QPair<QString, int> destination(const Project &p, int id) {
    QString r = isDungeon(p, id) ? "DUNGEON.DAT"
                : MAPS_NewLocation(id)
                    ? QString("LOC%1.DAT").arg(id, 3, 10, QChar('0'))
                    : QStringList{"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"}.value(
                          (id - 1) / 8);
    require(p.resources.contains(r), "A linked map is missing from the project");
    auto pages = p.mapPages(r);
    for (int i = 0; i < pages.size(); ++i)
        if (positionMap(r, pages[i]) == id && !pages[i].floor)
            return {r, i};
    throw std::runtime_error("A linked map has no ground floor");
}
struct Exporter {
    const Project &p;
    QSet<unsigned> types, vanillaTypes, artwork;
    void type(unsigned id) {
        auto records = CustomTiles::records(p);
        if (id < 512) {
            if (!records.contains(id) || vanillaTypes.contains(id))
                return;
            vanillaTypes.insert(id);
        } else {
            if (types.contains(id))
                return;
            require(records.contains(id), "Missing custom tile definition");
            types.insert(id);
        }
        auto d = CustomTiles::definition(p, id);
        type(d.ground);
        for (auto result : d.result)
            type(result);
    }
    void actor(unsigned id) {
        type(id);
        if (id >= 512) {
            type(id);
            return;
        }
        if (id >= 256) {
            int base = (id - 256) & 252;
            for (int frame = 0; frame < 4; ++frame)
                artwork.insert(256 + base + frame);
        }
    }
    QJsonObject node(const QString &r, int page, bool rooms = true) {
        auto pages = p.mapPages(r);
        const auto selected = pages.at(page);
        const QString kind = mapJsonKind(r);
        QJsonObject out{{"kind", kind}, {"name", selected.name.section(" — ", 0, 0)}};
        if (kind == "combat")
            out["encounterMode"] = r == "BRIT.CBT" ? "outdoor" : "dungeon";
        QJsonArray floors;
        MapDocument doc(&p, r);
        for (int index : pagesFor(p, r, page)) {
            const auto mp = pages[index];
            const auto target = contentsTarget(r, mp, index);
            QByteArray terrain = r == "DUNGEON.DAT"
                                     ? DungeonDocument(&p).level(mp.settlement, mp.floor)
                                     : doc.rawTerrain(index);
            auto expanded =
                r == "DUNGEON.DAT" ? QByteArray() : CustomTiles::expanded(p, r, mp, index, terrain);
            QJsonArray rows;
            for (int y = 0; y < mp.height(); ++y) {
                QJsonArray row;
                for (int x = 0; x < mp.side; ++x) {
                    unsigned id = U5::byte(terrain, y * mp.side + x);
                    unsigned custom = expanded.isEmpty()
                                          ? 0
                                          : TYPES_U32(expanded.constData() + (y * mp.side + x) * 4);
                    if (custom) {
                        id = custom;
                        type(id);
                    } else if (r != "DUNGEON.DAT") {
                        artwork.insert(id);
                        type(id);
                    }
                    row.append(int(id));
                }
                rows.append(row);
            }
            const auto catalog = extendedMaps(p);
            const auto previous = findExtendedMap(catalog, r, index);
            const int background = previous ? previous->background : kind == "combat" ? 255
                                   : kind == "dungeon" ? 0xb0 : terrain.isEmpty() ? 0 : uchar(terrain.back());
            QJsonObject f{{"floor", mp.floor}, {"width", mp.side}, {"height", mp.height()},
                          {"background", background}, {"tiles", rows}};
            if (kind != "dungeon") { artwork.insert(background); type(background); }
            if (kind == "combat") {
                f["encounterData"] = bytes(p.data(r).mid(mp.offset, 352));
                QJsonArray mask;
                for (int bit = 0; bit < 64; ++bit) {
                    const auto catalog = extendedMaps(p);
                    const auto m = findExtendedMap(catalog, r, index);
                    mask.append(m && m->markerMask ? bool(m->markers & (quint64(1) << bit))
                                                   : false);
                }
                bool explicitMask = false;
                for (const auto &m : extendedMaps(p))
                    if (m.resource == r && m.page == index)
                        explicitMask = m.markerMask;
                if (explicitMask)
                    f["markers"] = mask;
                QJsonArray actors;
                for (int slot = 0; slot < 16; ++slot) {
                    unsigned id = CustomTiles::actorType(p, r, mp, index, slot, 0);
                    if (id >= 512) {
                        type(id);
                        actors.append(QJsonObject{{"slot", slot}, {"type", int(id)}});
                    }
                    if (r == "DUNGEON.CBT")
                        actor(256 + U5::byte(p.data(r), mp.offset + 5 * 32 + 11 + slot));
                }
                f["actors"] = actors;
            }
            QJsonArray signs;
            if (p.resources.contains("SIGNS.DAT"))
                for (const auto &sign : readSigns(p.data("SIGNS.DAT")))
                    if (sign.map == target.map && sign.floor == target.floor && !target.kind)
                        signs.append(QJsonObject{{"x", sign.x},
                                                 {"y", sign.y},
                                                 {"text", QString::fromLatin1(sign.text)}});
            f["signs"] = signs;
            QJsonArray contents;
            for (const auto &d : mapContents(p, target))
                if (d.x < mp.side && d.y < mp.height())
                    contents.append(contentsJson(d));
            f["contents"] = contents;
            for (const auto &pos : mapPositions(p))
                if (!pos.id && pos.map == target.map && pos.floor == target.floor && !target.kind)
                    f["partyStart"] = QJsonArray{pos.cell.x(), pos.cell.y()};
            floors.append(f);
        }
        out["floors"] = floors;
        if (kind == "location") {
            const QString npc = r.section('.', 0, 0) + ".NPC", talk = r.section('.', 0, 0) + ".TLK";
            const auto b = p.data(npc);
            QJsonArray actors;
            QJsonObject dialogueData;
            auto conversations = U5::readDialogue(p.data(talk));
            const int town = selected.settlement;
            for (int slot = 1; slot < int(NPCS_Count(b.constData(), b.size(), town)); ++slot) {
                int shape = U5::byte(b, NPCS_ShapeOffset(b.constData(), b.size(), town, slot));
                if (!shape)
                    continue;
                const int off = NPCS_Offset(b.constData(), b.size(), town, slot);
                int speech = U5::byte(b, NPCS_DialogueOffset(b.constData(), b.size(), town, slot));
                QJsonObject a{{"slot", slot},
                              {"schedule", bytes(b.mid(off, 16))},
                              {"appearance", shape},
                              {"conversation", speech}};
                unsigned id = CustomTiles::actorType(p, r, selected, page, slot, 256 + shape);
                actor(id);
                if (id >= 512)
                    a["type"] = int(id);
                if (!locationObject(p, npc, town, slot))
                    for (const auto &entry : conversations)
                        if (entry.id == unsigned(speech))
                            dialogueData[QString::number(speech)] =
                                QString::fromLatin1(entry.bytes.toBase64());
                actors.append(a);
            }
            out["npcs"] = actors;
            out["dialogues"] = dialogueData;
        }
        if (kind == "world") {
            const int world = r == "UNDER.DAT";
            out["world"] = world;
            auto b = WorldObjects::effective(p, world);
            QJsonArray objects;
            for (int slot = 1; slot < 32; ++slot) {
                unsigned id =
                    CustomTiles::actorType(p, r, selected, page, slot, 256 + U5::byte(b, slot * 8));
                QJsonObject o{{"slot", slot}, {"data", bytes(b.mid(slot * 8, 8))}};
                if (U5::byte(b, slot * 8))
                    actor(id);
                if (id >= 512) {
                    o["type"] = int(id);
                    type(id);
                }
                objects.append(o);
            }
            out["objects"] = objects;
            QJsonArray moonstones;
            const auto init = p.resources.value("INIT.GAM").edited;
            if (init.size() == 4192)
                for (int i = 0; i < 8; ++i)
                    if (U5::byte(init, 0x29a + i) != 255 &&
                        int8_t(init[0x2a2 + i]) == (world ? -1 : 0))
                        moonstones.append(QJsonObject{{"phase", i},
                                                      {"x", int(U5::byte(init, 0x28a + i))},
                                                      {"y", int(U5::byte(init, 0x292 + i))}});
            out["moonstones"] = moonstones;
            QJsonArray links;
            QSet<int> seen;
            QSet<unsigned> entranceCells;
            for (const auto &pos : mapPositions(p)) {
                if (pos.id < 100 || pos.id >= 2000 || pos.map || pos.floor != (world ? -1 : 0))
                    continue;
                int id = pos.id >= 1000 ? pos.id - 1000 : pos.id - 99;
                bool explicitPortal = false;
                for (const auto &m : extendedMaps(p))
                    if ((m.kind == 1 || m.kind == 4) && m.id == unsigned(id) && !m.floor &&
                        m.world == (world ? -1 : 0))
                        explicitPortal = true;
                if (pos.id < 1000 && explicitPortal)
                    continue;
                if (seen.contains(id) || pos.cell.x() >= selected.side ||
                    pos.cell.y() >= selected.height())
                    continue;
                // Vanilla links resolve to the same base-game destinations in the other project.
                // Authored destinations are bundled because their identities are project-specific.
                if (id <= 40 &&
                    !p.resources.contains(id >= 33 ? "DUNGEON.DAT"
                                                   : QStringList{"TOWNE.DAT", "DWELLING.DAT",
                                                                 "CASTLE.DAT", "KEEP.DAT"}
                                                         .value((id - 1) / 8)))
                    continue;
                if (pos.id < 1000) {
                    auto terrain = MapDocument(&p, r).rawTerrain(0);
                    int tile = U5::byte(terrain, pos.cell.y() * selected.side + pos.cell.x());
                    bool dungeonTile = tile >= 0x16 && tile <= 0x18;
                    bool townTile = tile == 0x10 || (tile >= 0x12 && tile <= 0x15) ||
                                    tile == 0x1b || tile == 0x39 || tile == 0x3e;
                    if (id >= 33 ? !dungeonTile : !townTile)
                        continue;
                }
                unsigned cellKey = pos.cell.y() * 256 + pos.cell.x();
                if (entranceCells.contains(cellKey))
                    continue;
                entranceCells.insert(cellKey);
                seen.insert(id);
                auto target = destination(p, id);
                auto targetPage = p.mapPages(target.first)[target.second];
                QPoint arrival(id <= 32 ? 15 : 1, id <= 32 ? 30 : 1);
                for (const auto &m : extendedMaps(p))
                    if ((m.kind == 1 || m.kind == 4) && m.id == unsigned(id) && !m.floor &&
                        m.world != -2)
                        arrival = m.arrival;
                QJsonObject link{
                    {"x", pos.cell.x()},
                    {"y", pos.cell.y()},
                    {"id", id},
                    {"arrival", QJsonArray{qMin(arrival.x(), targetPage.side - 1),
                                           qMin(arrival.y(), targetPage.height() - 1)}}};
                if (MAPS_NewLocation(id))
                    link["destination"] = node(target.first, target.second);
                links.append(link);
            }
            out["entrances"] = links;
        }
        if (kind == "dungeon" && rooms) {
            QJsonArray linked;
            DungeonDocument dungeon(&p);
            for (int i = 0; i < 16; ++i)
                linked.append(
                    node("DUNGEON.CBT", dungeon.room(selected.settlement, 0xa0 + i), false));
            out["rooms"] = linked;
            for (int art : {0x44, 0xc8, 0xc9, 257, 0xd8, 0x8c, 0, 0x45, 0xb8, 0x4f, 0x4d, 0x4e})
                artwork.insert(art);
        }
        return out;
    }
};
} // namespace
QString mapJsonKind(const QString &r) {
    if (r == "BRIT.DAT" || r == "UNDER.DAT")
        return "world";
    if (r == "DUNGEON.DAT")
        return "dungeon";
    if (r.endsWith(".CBT"))
        return "combat";
    require(MapDocument::supported(r), "This resource is not a map");
    return "location";
}
QByteArray exportMapJson(const Project &p, const QString &r, int page) {
    Exporter e{p};
    auto map = e.node(r, page);
    QJsonArray types;
    const auto records = CustomTiles::records(p);
    QList<unsigned> ids = e.types.values();
    std::sort(ids.begin(), ids.end());
    for (unsigned id : ids)
        types.append(
            QJsonObject{{"id", int(id)}, {"record", QString::fromLatin1(records[id].toBase64())}});
    QJsonArray slotIds;
    auto art = e.artwork.values();
    std::sort(art.begin(), art.end());
    for (unsigned id : art)
        slotIds.append(int(id));
    QJsonArray vanilla;
    auto nativeIds = e.vanillaTypes.values();
    std::sort(nativeIds.begin(), nativeIds.end());
    for (unsigned id : nativeIds)
        vanilla.append(
            QJsonObject{{"id", int(id)}, {"record", QString::fromLatin1(records[id].toBase64())}});
    QJsonArray sheets;
    for (auto it = p.resources.begin(); it != p.resources.end(); ++it)
        if (MOD_TilesetSheet(it.key().toLatin1().constData()) && !it->edited.isEmpty())
            sheets.append(QJsonObject{{"resource", it.key()},
                                      {"png", QString::fromLatin1(it->edited.toBase64())}});
    QJsonObject out{
        {"format", "impera-map"},
        {"version", 1},
        {"map", map},
        {"customTypes", types},
        {"vanillaTypes", vanilla},
        {"artwork", QJsonObject{{"slots", slotIds},
                                {"dos", QString::fromLatin1(p.data("TILES.16").toBase64())}}}};
    auto artwork = out["artwork"].toObject();
    artwork["sheets"] = sheets;
    out["artwork"] = artwork;
    auto result = QJsonDocument(out).toJson(QJsonDocument::Compact);
    require(result.size() <= 128 * 1024 * 1024, "Map JSON exceeds 128 MiB");
    return result;
}
namespace {
struct Importer {
    Project p;
    QMap<unsigned, unsigned> ids;
    explicit Importer(const Project &source) : p(source) {}
    void apply(const QMap<QString, QByteArray> &changes) {
        for (auto it = changes.begin(); it != changes.end(); ++it)
            p.resources[it.key()].edited = it.value();
    }
    unsigned type(unsigned id) const {
        if (id < 512)
            return id;
        require(ids.contains(id), "Map JSON is missing a custom type");
        return ids[id];
    }
    void assets(const QJsonObject &root) {
        require(root["customTypes"].isArray(), "Map JSON is missing its custom types");
        auto records = CustomTiles::records(p);
        QMap<unsigned, QByteArray> incoming;
        unsigned next = 512;
        for (auto value : root["customTypes"].toArray()) {
            auto obj = value.toObject();
            unsigned old = number(obj["id"], 512, 65535, "custom type ID");
            require(!incoming.contains(old), "Duplicate custom type in map JSON");
            auto b = QByteArray::fromBase64(obj["record"].toString().toLatin1(),
                                            QByteArray::AbortOnBase64DecodingErrors);
            require(b.size() >= 256 && b.size() < 16 * 1024 * 1024 &&
                        TYPES_U32(b.constData()) == unsigned(b.size()),
                    "Invalid custom artwork in map JSON");
            unsigned id = old;
            if (records.contains(id) || ids.values().contains(id)) {
                while (next <= 65535 && (records.contains(next) || ids.values().contains(next)))
                    ++next;
                require(next <= 65535, "No free custom type IDs remain");
                id = next++;
            }
            ids[old] = id;
            incoming[old] = b;
        }
        auto fingerprint = [](QByteArray b) {
            TypeDef d;
            TYPES_Decode(reinterpret_cast<const unsigned char *>(b.constData()), &d);
            const unsigned own = d.id;
            d.id = TYPES_FIRST_CUSTOM;
            if (d.ground == own)
                d.ground = 0xffffffffu;
            for (auto &result : d.result)
                if (result == own)
                    result = 0xffffffffu;
            TYPES_Encode(reinterpret_cast<unsigned char *>(b.data()), &d);
            TYPES_Put32(b.data(), b.size());
            return b;
        };
        QHash<QByteArray, unsigned> existing;
        for (auto it = records.begin(); it != records.end(); ++it)
            if (it.key() >= 512)
                existing.insert(fingerprint(it.value()), it.key());
        // Reuse matching artwork and behavior, including self-referencing actor transforms.
        for (int pass = 0; pass < qMin(incoming.size(), qsizetype(16)); ++pass) {
            bool changed = false;
            for (auto it = incoming.begin(); it != incoming.end(); ++it) {
                auto b = it.value();
                TypeDef d;
                TYPES_Decode(reinterpret_cast<const unsigned char *>(b.constData()), &d);
                d.id = ids[it.key()];
                if (d.ground >= 512)
                    d.ground = type(d.ground);
                for (auto &result : d.result)
                    if (result >= 512)
                        result = type(result);
                TYPES_Encode(reinterpret_cast<unsigned char *>(b.data()), &d);
                TYPES_Put32(b.data(), b.size());
                auto found = existing.constFind(fingerprint(b));
                if (found != existing.cend() && ids[it.key()] != found.value()) {
                    ids[it.key()] = found.value();
                    changed = true;
                }
            }
            if (!changed)
                break;
        }
        for (auto it = incoming.begin(); it != incoming.end(); ++it) {
            auto b = it.value();
            TypeDef d;
            TYPES_Decode(reinterpret_cast<const unsigned char *>(b.constData()), &d);
            require(d.id == it.key(), "Custom type ID does not match its artwork");
            d.id = ids[it.key()];
            if (d.ground >= 512)
                d.ground = type(d.ground);
            for (auto &result : d.result)
                if (result >= 512)
                    result = type(result);
            TYPES_Encode(reinterpret_cast<unsigned char *>(b.data()), &d);
            TYPES_Put32(b.data(), b.size());
            records[d.id] = b;
        }
        QSet<unsigned> nativeIds;
        for (auto value : root["vanillaTypes"].toArray()) {
            auto obj = value.toObject();
            unsigned id = number(obj["id"], 0, 511, "vanilla type ID");
            require(!nativeIds.contains(id), "Duplicate vanilla type definition");
            nativeIds.insert(id);
            auto b = QByteArray::fromBase64(obj["record"].toString().toLatin1(),
                                            QByteArray::AbortOnBase64DecodingErrors);
            require(b.size() >= 256 && TYPES_U32(b.constData()) == unsigned(b.size()),
                    "Invalid vanilla type record");
            TypeDef d;
            TYPES_Decode(reinterpret_cast<const unsigned char *>(b.constData()), &d);
            require(d.id == id, "Vanilla type ID mismatch");
            if (d.ground >= 512)
                d.ground = type(d.ground);
            for (auto &result : d.result)
                if (result >= 512)
                    result = type(result);
            TYPES_Encode(reinterpret_cast<unsigned char *>(b.data()), &d);
            TYPES_Put32(b.data(), b.size());
            records[id] = b;
        }
        if (!incoming.isEmpty() || !nativeIds.isEmpty())
            p.resources["TYPES.OVR"].edited = CustomTiles::library(p, records);
        auto artwork = root["artwork"].toObject();
        auto source = QByteArray::fromBase64(artwork["dos"].toString().toLatin1(),
                                             QByteArray::AbortOnBase64DecodingErrors);
        auto art = U5::readGraphics("TILES.16", source);
        auto target = U5::readGraphics("TILES.16", p.data("TILES.16"));
        for (auto value : artwork["slots"].toArray()) {
            int slot = number(value, 0, 511, "artwork slot");
            require(slot < art.images.size() && slot < target.images.size(),
                    "Missing vanilla artwork");
            target.images[slot] = art.images[slot];
        }
        p.resources["TILES.16"].edited = U5::writeGraphics(target);
        for (auto value : artwork["sheets"].toArray()) {
            auto sheet = value.toObject();
            const QString resource = sheet["resource"].toString();
            require(MOD_TilesetSheet(resource.toLatin1().constData()) &&
                        p.resources.contains(resource),
                    "Unknown alternate tileset in map JSON");
            const auto b = QByteArray::fromBase64(sheet["png"].toString().toLatin1(),
                                                  QByteArray::AbortOnBase64DecodingErrors);
            auto from = U5::readGraphics(resource, b),
                 to = U5::readGraphics(resource, p.data(resource));
            for (auto slot : artwork["slots"].toArray()) {
                int index = number(slot, 0, 511, "artwork slot") - from.tileOffset;
                if (index >= 0 && index < from.images.size())
                    to.images[index] = from.images[index];
            }
            p.resources[resource].edited = U5::writeGraphics(to);
        }
    }
    void rename(const QString &r, int page, const QString &name) {
        if (name.isEmpty() || mapJsonKind(r) == "combat")
            return;
        auto mp = p.mapPages(r)[page];
        int map = r == "UNDER.DAT" ? 41 : positionMap(r, mp);
        if (MAPS_NewLocation(map)) {
            auto maps = extendedMaps(p);
            for (auto &m : maps)
                if (int(m.id) == map && (m.kind == 1 || m.kind == 4))
                    m.name = name;
            p.resources["MAPS.OVR"].edited = writeExtendedMaps(maps);
        } else
            p.resources["LOCATIONS.OVR"].edited = p.renameLocation(map, name);
    }
    void replaceContents(ContentsTarget target, QJsonValue values, int width, int height) {
        require(values.isArray(), "Map JSON is missing its contents");
        auto defs = readContents(p.resources.value("CONTENTS.OVR").edited);
        auto before = mapContents(p, target);
        auto belongs = [&](const ContentsDef &d) {
            return d.kind == target.kind && d.map == target.map && d.floor == target.floor;
        };
        for (int i = defs.size() - 1; i >= 0; --i)
            if (belongs(defs[i]))
                defs.removeAt(i);
        unsigned next = 1;
        for (const auto &d : defs)
            next = qMax(next, d.id + 1);
        for (auto d : before)
            if (d.original != 65535 && d.x < width && d.y < height) {
                d.id = next++;
                d.count = 0;
                d.flags = 0;
                d.refill = 0;
                memset(d.items, 0, sizeof(d.items));
                defs.append(d);
            }
        for (auto value : values.toArray()) {
            require(value.isObject(), "Invalid contents entry");
            auto o = value.toObject();
            ContentsDef d{};
            d.id = next++;
            d.map = target.map;
            d.floor = target.floor;
            d.kind = target.kind;
            d.original = 65535;
            d.x = number(o["x"], 0, width - 1, "contents X");
            d.y = number(o["y"], 0, height - 1, "contents Y");
            d.action = number(o["action"], 0, 1, "contents action");
            d.refill = number(o["refill"], 0, 4, "refill");
            if (d.refill == 2) {
                int original = number(o["original"], 13, 15, "conditional discovery");
                d.refill = original == 14 ? 1 : original == 13 ? 3 : 4;
            }
            d.slot = number(o["slot"], 1, 255, "container slot");
            d.flags = number(o["flags"], 0, 3, "contents flags");
            d.lockDifficulty = number(o["lockDifficulty"], 0, 30, "lock difficulty");
            d.trapDifficulty = number(o["trapDifficulty"], 0, 30, "trap difficulty");
            require(o["items"].isArray() && o["items"].toArray().size() <= 16,
                    "Invalid contents items");
            for (auto item : o["items"].toArray()) {
                auto i = item.toObject();
                auto &v = d.items[d.count++];
                v.type = number(i["type"], 1, 9, "item type");
                v.variant = number(i["variant"], 0, 47, "item variant");
                v.quantity = number(i["quantity"], 1, 9999, "quantity");
            }
            for (int i = defs.size() - 1; i >= 0; --i)
                if (belongs(defs[i]) && defs[i].x == d.x && defs[i].y == d.y) {
                    if (defs[i].original != 65535)
                        d.original = defs[i].original;
                    defs.removeAt(i);
                }
            defs.append(d);
        }
        p.resources["CONTENTS.OVR"].edited =
            writeContents(defs, p.resources.value("CONTENTS.OVR").edited);
    }
    void npcs(const QJsonObject &node, const QString &r, int page, const QMap<int, int> &floors) {
        const auto mp = p.mapPages(r)[page];
        QString npc = r.section('.', 0, 0) + ".NPC", talk = r.section('.', 0, 0) + ".TLK";
        QByteArray b = p.data(npc);
        int town = mp.settlement;
        QSet<unsigned> reclaim, used;
        for (int t = 0; t < 8; ++t)
            for (int slot = 1; slot < int(NPCS_Count(b.constData(), b.size(), t)); ++slot)
                if (U5::byte(b, NPCS_ShapeOffset(b.constData(), b.size(), t, slot)) &&
                    !locationObject(p, npc, t, slot))
                    (t == town ? reclaim : used)
                        .insert(U5::byte(b, NPCS_DialogueOffset(b.constData(), b.size(), t, slot)));
        auto entries = U5::readDialogue(p.data(talk));
        for (auto entry : entries)
            if (!reclaim.contains(entry.id))
                used.insert(entry.id);
        QMap<int, int> conversations;
        require(node["dialogues"].isObject(), "Map JSON is missing its conversations");
        auto dialogueData = node["dialogues"].toObject();
        for (int off = b.size() - 24; off >= IMPERA_NPC_EXTRA_START; off -= 24)
            if (U5::byte(b, off) == unsigned(town))
                b.remove(off, 24);
        if (b.size() >= IMPERA_NPC_EXTRA_START)
            TYPES_Put32(b.data() + 4616, (b.size() - IMPERA_NPC_EXTRA_START) / 24);
        auto party = b.mid(town * 576, 16);
        int partyShape = U5::byte(b, town * 576 + 512), partySpeech = U5::byte(b, town * 576 + 544);
        b.replace(town * 576, 576, QByteArray(576, 0));
        b.replace(town * 576, 16, party);
        b[town * 576 + 512] = char(partyShape);
        b[town * 576 + 544] = char(partySpeech);
        require(node["npcs"].isArray(), "Map JSON is missing its NPCs");
        QSet<int> slotIds;
        for (auto value : node["npcs"].toArray()) {
            auto o = value.toObject();
            int slot = number(o["slot"], 1, IMPERA_NPC_CAPACITY - 1, "NPC slot");
            require(!slotIds.contains(slot), "Duplicate NPC slot");
            slotIds.insert(slot);
            MapNpcDocument::ensureSlot(b, town, slot);
            QByteArray schedule = bytes(o["schedule"], 16);
            for (int loc = 0; loc < 3; ++loc) {
                int floor = int8_t(schedule[9 + loc]);
                if (floors.contains(floor))
                    schedule[9 + loc] = char(floors[floor]);
            }
            int shape = number(o["appearance"], 1, 255, "NPC appearance");
            int speech = number(o["conversation"], 0, 255, "conversation");
            auto speechData = dialogueData.value(QString::number(speech));
            if (!speechData.isUndefined()) {
                require(speechData.isString(), "Invalid conversation data");
                if (!conversations.contains(speech)) {
                    int id = 1;
                    while (id < 128 && used.contains(id))
                        ++id;
                    require(id < 128, "No free conversation numbers remain in this location bank");
                    auto text = QByteArray::fromBase64(speechData.toString().toLatin1(),
                                                       QByteArray::AbortOnBase64DecodingErrors);
                    require(!text.isEmpty(), "Invalid conversation data");
                    for (int i = entries.size() - 1; i >= 0; --i)
                        if (entries[i].id == unsigned(id))
                            entries.removeAt(i);
                    entries.append({unsigned(id), text});
                    used.insert(id);
                    conversations[speech] = id;
                }
                speech = conversations[speech];
            }
            int offset = NPCS_Offset(b.constData(), b.size(), town, slot);
            b.replace(offset, 16, schedule);
            if (o.contains("type"))
                shape = CustomTiles::definition(p, type(number(o["type"], 512, 65535, "NPC type")))
                            .base;
            b[NPCS_ShapeOffset(b.constData(), b.size(), town, slot)] = char(shape);
            b[NPCS_DialogueOffset(b.constData(), b.size(), town, slot)] = char(speech);
            p.resources[npc].edited = b;
            if (o.contains("type"))
                apply(CustomTiles::assignActor(p, r, mp, page, slot, type(o["type"].toInt())));
        }
        p.resources[npc].edited = b;
        p.resources[talk].edited = U5::writeDialogue(entries);
    }
    void node(const QJsonObject &o, const QString &r, int page, int depth = 0) {
        require(depth <= 3, "Map JSON links are nested too deeply");
        const auto kind = mapJsonKind(r);
        require(o["kind"].toString() == kind, "Import a " + kind + " map to replace this map");
        if (kind == "combat")
            require(o["encounterMode"].toString() == (r == "BRIT.CBT" ? "outdoor" : "dungeon"),
                    "Outdoor and dungeon encounters use different actor data");
        auto pages = p.mapPages(r);
        const auto selected = pages.at(page);
        auto indexes = pagesFor(p, r, page);
        auto source = o["floors"].toArray();
        if (kind == "location" && MAPS_LocationResource(r.toLatin1().constData()) &&
            o["floors"].isArray()) {
            require(!source.isEmpty() && source.size() <= 5,
                    "Locations support at most five floors");
            QSet<int> sourceFloors;
            for (auto value : source) {
                int floor = number(value.toObject()["floor"], -1, 3, "location floor");
                require(!sourceFloors.contains(floor), "Duplicate location floor");
                sourceFloors.insert(floor);
            }
            require(sourceFloors.contains(0), "Location JSON needs a ground floor");
            auto catalog = extendedMaps(p);
            auto base = *findExtendedMap(catalog, r, 0);
            for (int i = catalog.size() - 1; i >= 0; --i)
                if (catalog[i].resource == r)
                    catalog.removeAt(i);
            QVector<int> order{0};
            for (int floor : {-1, 1, 2, 3})
                if (sourceFloors.contains(floor))
                    order.append(floor);
            for (int i = 0; i < order.size(); ++i) {
                auto m = base;
                m.page = i;
                m.floor = order[i];
                if (m.floor) {
                    m.world = -2;
                    m.underlay = 0;
                }
                catalog.append(m);
            }
            p.resources["MAPS.OVR"].edited = writeExtendedMaps(catalog);
            pages = p.mapPages(r);
            page = 0;
            indexes = pagesFor(p, r, 0);
        }
        require(o["floors"].isArray() && source.size() == indexes.size(),
                "The source and destination must have the same number of floors");
        QMap<int, int> floors;
        for (int i = 0; i < source.size(); ++i) {
            int floor = number(source[i].toObject()["floor"], -1, 7, "floor");
            require(!floors.contains(floor), "Duplicate floor in map JSON");
            floors[floor] = pages[indexes[i]].floor;
        }
        auto placements = CustomTiles::placements(p);
        for (auto it = placements.begin(); it != placements.end();) {
            bool match = false;
            for (int index : indexes) {
                auto c = CustomTiles::cell(r, pages[index], index, 0, 0);
                if (it->kind == c.kind && it->map == c.map && it->floor == c.floor)
                    match = true;
                auto a = CustomTiles::actorCell(r, pages[index], index, 0);
                if (it->kind == a.kind && it->map == a.map &&
                    (kind != "world" || it->floor == a.floor))
                    match = true;
            }
            if (match)
                it = placements.erase(it);
            else
                ++it;
        }
        if (!p.resources.value("TYPEMAP.OVR").edited.isEmpty())
            p.resources["TYPEMAP.OVR"].edited = CustomTiles::mapBytes(p, placements);
        auto signs = p.resources.contains("SIGNS.DAT") ? readSigns(p.data("SIGNS.DAT"))
                                                       : QVector<SignRecord>();
        auto maps = extendedMaps(p);
        for (int i = 0; i < indexes.size(); ++i) {
            int index = indexes[i];
            auto mp = pages[index];
            auto f = source[i].toObject();
            int w = number(f["width"], 1, 256, "width"), h = number(f["height"], 1, 256, "height");
            auto rows = f["tiles"].toArray();
            require(f["tiles"].isArray() && rows.size() == h, "Map JSON tile row count is wrong");
            QByteArray terrain, expanded(w * h * 4, 0);
            for (int y = 0; y < h; ++y) {
                require(rows[y].isArray() && rows[y].toArray().size() == w,
                        "Map JSON tile column count is wrong");
                auto row = rows[y].toArray();
                for (int x = 0; x < w; ++x) {
                    unsigned id = number(row[x], 0, kind == "dungeon" ? 255 : 65535, "tile ID");
                    require(kind != "dungeon" || (id >> 4) != 9,
                            "Cannot import an unused dungeon feature");
                    id = type(id);
                    terrain.append(char(id >= 512 ? CustomTiles::definition(p, id).base : id));
                    if (id >= 512)
                        TYPES_Put32(expanded.data() + (y * w + x) * 4, id);
                }
            }
            ExtendedMap m;
            if (const auto *previous = findExtendedMap(maps, r, index))
                m = *previous;
            else {
                m.resource = r;
                m.page = index;
                m.floor = mp.floor;
                m.kind = kind == "world"      ? 0
                         : kind == "location" ? 1
                         : kind == "dungeon"  ? 4
                         : r == "BRIT.CBT"    ? 2
                                              : 3;
                m.id = kind == "world"    ? unsigned(r == "UNDER.DAT")
                       : kind == "combat" ? index
                                          : positionMap(r, mp);
                m.name = selected.name.section(" — ", 0, 0);
            }
            m.width = w;
            m.height = h;
            m.background = number(f["background"], 0, 255, "background tile");
            m.cells = terrain;
            if (m.world != -2) {
                m.arrival.setX(qMin(m.arrival.x(), w - 1));
                m.arrival.setY(qMin(m.arrival.y(), h - 1));
            }
            if (kind == "combat") {
                auto record = bytes(f["encounterData"], 352);
                auto b = p.data(r);
                b.replace(mp.offset, 352, record);
                p.resources[r].edited = b;
                require(f["actors"].isArray(), "Map JSON is missing its encounter actors");
                m.markerMask = f.contains("markers");
                m.markers = 0;
                if (m.markerMask) {
                    require(f["markers"].isArray() && f["markers"].toArray().size() == 64,
                            "Invalid encounter markers");
                    int bit = 0;
                    for (auto active : f["markers"].toArray()) {
                        require(active.isBool(), "Invalid encounter marker");
                        if (active.toBool())
                            m.markers |= quint64(1) << bit;
                        ++bit;
                    }
                }
            }
            bool replaced = false;
            for (auto &old : maps)
                if (old.resource == r && old.page == index) {
                    old = m;
                    replaced = true;
                }
            if (!replaced)
                maps.append(m);
            p.resources["MAPS.OVR"].edited = writeExtendedMaps(maps);
            if (kind != "dungeon") {
                auto targetPage = p.mapPages(r)[index];
                apply(CustomTiles::paint(p, r, targetPage, index, terrain, expanded));
                for (auto actor : f["actors"].toArray()) {
                    auto a = actor.toObject();
                    apply(CustomTiles::assignActor(
                        p, r, targetPage, index, number(a["slot"], 0, 15, "actor slot"),
                        type(number(a["type"], 512, 65535, "actor type"))));
                }
            }
            auto target = contentsTarget(r, p.mapPages(r)[index], index);
            replaceContents(target, f["contents"], w, h);
            if (!target.kind) {
                require(f["signs"].isArray(), "Map JSON is missing its signs");
                require(p.resources.contains("SIGNS.DAT") || f["signs"].toArray().isEmpty(),
                        "The destination project is missing SIGNS.DAT");
                for (int j = signs.size() - 1; j >= 0; --j)
                    if (signs[j].map == target.map && signs[j].floor == target.floor)
                        signs.removeAt(j);
                for (auto value : f["signs"].toArray()) {
                    auto sign = value.toObject();
                    require(sign["text"].isString(), "Invalid sign text");
                    signs.append({target.map, target.floor, number(sign["x"], 0, w - 1, "sign X"),
                                  number(sign["y"], 0, h - 1, "sign Y"),
                                  sign["text"].toString().toLatin1()});
                }
                if (f.contains("partyStart")) {
                    auto at = f["partyStart"].toArray();
                    require(at.size() == 2, "Invalid party start");
                    apply(moveMapPosition(
                        p, 0, target.map, target.floor,
                        {number(at[0], 0, w - 1, "start X"), number(at[1], 0, h - 1, "start Y")}));
                }
            }
        }
        if (p.resources.contains("SIGNS.DAT"))
            p.resources["SIGNS.DAT"].edited = writeSigns(p.data("SIGNS.DAT"), signs);
        if (kind == "location")
            npcs(o, r, page, floors);
        if (kind == "world") {
            int world = r == "UNDER.DAT";
            auto data = WorldObjects::effective(p, world);
            require(o["objects"].isArray() && o["objects"].toArray().size() == 31,
                    "Invalid world objects");
            QSet<int> slotIds;
            for (auto value : o["objects"].toArray()) {
                auto obj = value.toObject();
                int slot = number(obj["slot"], 1, 31, "object slot");
                require(!slotIds.contains(slot), "Duplicate object slot");
                slotIds.insert(slot);
                auto record = bytes(obj["data"], 8);
                if (record[0])
                    record[4] = char(world ? -1 : 0);
                data.replace(slot * 8, 8, record);
            }
            apply({{WorldObjects::resource(world), data}});
            p.resources[WorldObjects::resource(world)].included = true;
            for (auto value : o["objects"].toArray()) {
                auto obj = value.toObject();
                if (obj.contains("type"))
                    apply(CustomTiles::assignActor(
                        p, r, p.mapPages(r)[0], 0, obj["slot"].toInt(),
                        type(number(obj["type"], 512, 65535, "world actor type"))));
            }
            require(o["entrances"].isArray(), "Map JSON is missing its entrance links");
            for (auto value : o["entrances"].toArray()) {
                auto link = value.toObject();
                QPoint at(number(link["x"], 0, p.mapPages(r)[0].side - 1, "entrance X"),
                          number(link["y"], 0, p.mapPages(r)[0].height() - 1, "entrance Y"));
                int id = number(link["id"], 1, 127, "destination ID");
                if (link.contains("destination")) {
                    auto child = link["destination"].toObject();
                    require(child["kind"] == "location" || child["kind"] == "dungeon",
                            "Invalid linked destination type");
                    id = -1;
                    for (const auto &m : extendedMaps(p))
                        if (!m.floor && MAPS_NewLocation(m.id) && m.world == (world ? -1 : 0) &&
                            m.name == child["name"].toString() &&
                            m.kind == unsigned(child["kind"] == "dungeon" ? 4 : 1)) {
                            id = m.id;
                            break;
                        }
                    if (id < 0) {
                        require(child["floors"].isArray() && !child["floors"].toArray().isEmpty(),
                                "Linked map has no floors");
                        const auto f = child["floors"].toArray().at(0).toObject();
                        int w = number(f["width"], 1, 256, "linked map width"),
                            h = number(f["height"], 1, 256, "linked map height");
                        const auto tiles = MapDocument(&p, r).rawTerrain(0);
                        int tile = U5::byte(tiles, at.y() * p.mapPages(r)[0].side + at.x());
                        const auto expanded =
                            CustomTiles::expanded(p, r, p.mapPages(r)[0], 0, tiles);
                        unsigned custom = TYPES_U32(expanded.constData() +
                                                    (at.y() * p.mapPages(r)[0].side + at.x()) * 4);
                        if (custom)
                            tile = custom;
                        apply(child["kind"] == "dungeon"
                                  ? addDungeon(p, r, child["name"].toString(), at, w, h, 0, tile)
                                  : addLocation(p, r, child["name"].toString(), at, w, h, 5, tile));
                        hydrateExtendedMaps(p);
                        for (const auto &m : extendedMaps(p))
                            if (!m.floor && m.world == (world ? -1 : 0) && m.entrance == at)
                                id = m.id;
                    }
                    require(id > 0, "Cannot resolve linked destination");
                    auto target = destination(p, id);
                    node(child, target.first, target.second, depth + 1);
                }
                auto target = destination(p, id);
                auto mp = p.mapPages(target.first)[target.second];
                auto catalog = extendedMaps(p);
                ExtendedMap portal;
                if (const auto *old = findExtendedMap(catalog, target.first, target.second))
                    portal = *old;
                else {
                    portal.kind = isDungeon(p, id) ? 4 : 1;
                    portal.id = id;
                    portal.resource = target.first;
                    portal.page = target.second;
                    portal.floor = 0;
                    portal.name = p.locationName(id);
                    portal.width = mp.side;
                    portal.height = mp.height();
                    portal.cells = isDungeon(p, id)
                                       ? DungeonDocument(&p).level(mp.settlement, 0)
                                       : MapDocument(&p, target.first).rawTerrain(target.second);
                }
                auto arrival = link["arrival"].toArray();
                require(arrival.size() == 2, "Invalid map arrival");
                portal.world = world ? -1 : 0;
                portal.entrance = at;
                portal.arrival = {number(arrival[0], 0, portal.width - 1, "arrival X"),
                                  number(arrival[1], 0, portal.height - 1, "arrival Y")};
                bool replaced = false;
                for (auto &m : catalog)
                    if (m.resource == portal.resource && m.page == portal.page) {
                        m = portal;
                        replaced = true;
                    }
                if (!replaced)
                    catalog.append(portal);
                p.resources["MAPS.OVR"].edited = writeExtendedMaps(catalog);
            }
            for (auto value : o["moonstones"].toArray()) {
                auto obj = value.toObject();
                int phase = number(obj["phase"], 0, 7, "moon phase");
                apply(moveMapPosition(
                    p, 1 + phase, 0, world ? -1 : 0,
                    {number(obj["x"], 0, p.mapPages(r)[0].side - 1, "moonstone X"),
                     number(obj["y"], 0, p.mapPages(r)[0].height() - 1, "moonstone Y")}));
            }
        }
        if (kind == "dungeon") {
            require(o["rooms"].isArray() && o["rooms"].toArray().size() == 16,
                    "Dungeon JSON needs sixteen encounter rooms");
            DungeonDocument dungeon(&p);
            for (int room = 0; room < 16; ++room)
                node(o["rooms"].toArray()[room].toObject(), "DUNGEON.CBT",
                     dungeon.room(selected.settlement, 0xa0 + room), depth + 1);
        }
        rename(r, page, o["name"].toString());
    }
};
} // namespace
QMap<QString, QByteArray> importMapJson(const Project &source, const QString &r, int page,
                                        const QByteArray &json) {
    require(json.size() <= 128 * 1024 * 1024, "Map JSON exceeds 128 MiB");
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(json, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid map JSON");
    auto root = document.object();
    require(root["format"] == "impera-map" && root["version"].isDouble() &&
                root["version"].toInt() == 1,
            "Unsupported map JSON format or version");
    require(root["map"].isObject() && root["map"].toObject()["kind"].toString() == mapJsonKind(r),
            "Import a " + mapJsonKind(r) + " map to replace this map");
    Importer importer(source);
    importer.assets(root);
    importer.node(root["map"].toObject(), r, page);
    importer.p.validate();
    QMap<QString, QByteArray> edits;
    for (auto it = importer.p.resources.begin(); it != importer.p.resources.end(); ++it)
        if (it->edited != source.resources.value(it.key()).edited ||
            (it->included && !source.resources.value(it.key()).included))
            edits[it.key()] = it->edited;
    return edits;
}
void saveMapJson(QWidget *owner, const Project &p, const QString &r, int page) {
    auto path = QFileDialog::getSaveFileName(owner, "Export map", {}, "Map JSON (*.json)");
    if (path.isEmpty())
        return;
    require(QFileInfo(path).absolutePath() != p.sourceDirectory,
            "Export outside the original game folder");
    QSaveFile file(path);
    auto b = exportMapJson(p, r, page);
    require(b.size() <= 128 * 1024 * 1024, "Map JSON exceeds 128 MiB");
    require(file.open(QIODevice::WriteOnly) && file.write(b) == b.size() && file.commit(),
            "Cannot export map JSON");
}
QByteArray openMapJson(QWidget *owner) {
    auto path = QFileDialog::getOpenFileName(owner, "Import map", {}, "Map JSON (*.json)");
    if (path.isEmpty())
        return {};
    QFile file(path);
    require(file.size() <= 128 * 1024 * 1024 && file.open(QIODevice::ReadOnly),
            "Cannot read map JSON");
    return file.readAll();
}
} // namespace Workshop
