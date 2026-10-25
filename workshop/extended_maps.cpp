#include "world_objects.h"
#include "dungeon_editor.h"
#include "mod/npcs.h"
#include "extended_maps.h"
#include "gates_editor.h"
#include "map_editor.h"
#include "map_positions.h"
#include "contents_editor.h"
#include "custom_tiles.h"
#include "location_text.h"
#include <algorithm>
#include <tuple>
using U5::require;
namespace Workshop {
QVector<ExtendedMap> extendedMaps(const Project &p) {
    QVector<ExtendedMap> out;
    const auto b = p.resources.value("MAPS.OVR").edited;
    if (b.isEmpty())
        return out;
    require(MAPS_Validate(b.constData(), b.size()), "Invalid extended map catalog");
    for (int off = MAPS_HEADER; off < b.size(); off += MAPS_U32(b.constData() + off)) {
        const auto r = reinterpret_cast<const unsigned char *>(b.constData() + off);
        ExtendedMap m;
        m.kind = r[4];
        m.id = MAPS_Id(r);
        m.page = MAPS_Word(r + 6);
        m.floor = int8_t(r[8]);
        m.background = r[9];
        m.width = MAPS_Word(r + 10);
        m.height = MAPS_Word(r + 12);
        m.world = int8_t(r[14]);
        m.entrance = {r[15], r[16]};
        m.arrival = {r[17], r[18]};
        m.underlay = r[19];
        m.markerMask = r[20];
        m.roomBase = MAPS_Word(r + 21);
        m.markers = quint64(MAPS_U32(r + 120)) | (quint64(MAPS_U32(r + 124)) << 32);
        m.name = QString::fromLatin1(reinterpret_cast<const char *>(r + 24));
        m.resource = QString::fromLatin1(reinterpret_cast<const char *>(r + 88));
        m.cells = b.mid(off + MAPS_RECORD, m.width * m.height);
        out.append(m);
    }
    return out;
}
QByteArray writeExtendedMaps(const QVector<ExtendedMap> &maps) {
    require(maps.size() <= int(MAPS_MAX), "Too many extended maps");
    auto sorted = maps;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return std::make_tuple(a.kind, a.id, a.floor) < std::make_tuple(b.kind, b.id, b.floor);
    });
    QByteArray b(MAPS_HEADER, 0);
    memcpy(b.data(), "IMAPS001", 8);
    TYPES_Put32(b.data() + 8, sorted.size());
    for (const auto &m : sorted) {
        require(m.kind <= 4 && m.id <= ((m.kind == 2 || m.kind == 3) ? 65535u : 255u) &&
                    m.page >= 0 && m.page <= 65535 && m.floor >= -128 && m.floor <= 127 &&
                    m.underlay >= 0 && m.underlay <= 255 && m.background >= 0 &&
                    m.background <= 255 && m.world >= -2 && m.world <= 0 && m.entrance.x() >= 0 &&
                    m.entrance.x() <= 255 && m.entrance.y() >= 0 && m.entrance.y() <= 255 &&
                    m.arrival.x() >= 0 && m.arrival.x() <= 255 && m.arrival.y() >= 0 &&
                    m.arrival.y() <= 255,
                "Invalid map identity or position");
        require(m.width >= 1 && m.width <= 256 && m.height >= 1 && m.height <= 256 &&
                    m.cells.size() == m.width * m.height,
                "Map dimensions must be 1–256 tiles on each axis");
        require(!m.name.isEmpty() && m.name.toLatin1().size() < 64 && !m.resource.isEmpty() &&
                    m.resource.size() < 32,
                "Invalid map name or resource");
        for (auto ch : m.name)
            require(ch.unicode() >= 32 && ch.unicode() <= 126, "Map names use printable ASCII");
        QByteArray r(MAPS_RECORD, 0);
        TYPES_Put32(r.data(), MAPS_RECORD + m.cells.size());
        r[4] = m.kind;
        r[5] = m.id;
        if (m.kind == 2 || m.kind == 3)
            r[23] = m.id >> 8;
        r[6] = m.page;
        r[7] = m.page >> 8;
        r[8] = m.floor;
        r[9] = m.background;
        r[10] = m.width;
        r[11] = m.width >> 8;
        r[12] = m.height;
        r[13] = m.height >> 8;
        r[14] = m.world;
        r[15] = m.entrance.x();
        r[16] = m.entrance.y();
        r[17] = m.arrival.x();
        r[18] = m.arrival.y();
        r[19] = m.underlay;
        r[20] = m.markerMask;
        r[21] = m.roomBase;
        r[22] = m.roomBase >> 8;
        TYPES_Put32(r.data() + 120, m.markers);
        TYPES_Put32(r.data() + 124, m.markers >> 32);
        memcpy(r.data() + 24, m.name.toLatin1().constData(), m.name.toLatin1().size());
        memcpy(r.data() + 88, m.resource.toLatin1().constData(), m.resource.size());
        require(b.size() + r.size() + m.cells.size() <= MAPS_MAX_BYTES,
                "Extended map data exceeds 8 MiB");
        b += r;
        b += m.cells;
    }
    require(b.size() <= MAPS_MAX_BYTES && MAPS_Validate(b.constData(), b.size()),
            "Invalid map catalog or map data exceeds 8 MiB");
    return b;
}
const ExtendedMap *findExtendedMap(const QVector<ExtendedMap> &maps, const QString &resource,
                                   int page) {
    for (const auto &m : maps)
        if (m.resource == resource && m.page == page)
            return &m;
    return nullptr;
}
bool isDungeon(const Project &p, int map) {
    if (map >= 33 && map <= 40)
        return true;
    for (const auto &m : extendedMaps(p))
        if (m.kind == 4 && m.id == unsigned(map))
            return true;
    return false;
}
QVector<int> dungeonIds(const Project &p) {
    QVector<int> ids;
    if (p.resources.contains("DUNGEON.DAT"))
        for (int id = 33; id <= 40; ++id)
            ids.append(id);
    for (const auto &m : extendedMaps(p))
        if (m.kind == 4 && !m.floor && m.id > 40)
            ids.append(m.id);
    return ids;
}
QSize mapSize(const Project &p, int id, int floor, int kind) {
    auto maps = extendedMaps(p);
    for (const auto &m : maps)
        if (m.kind == unsigned(kind               ? kind + 1
                               : isDungeon(p, id) ? 4
                               : id               ? 1
                                                  : 0) &&
            m.id == unsigned(kind ? id
                             : id ? id
                                  : floor != 0) &&
            m.floor == (kind || !id ? 0 : floor))
            return {m.width, m.height};
    require(!MAPS_NewLocation(id) || kind, "Missing location map");
    int side = kind ? 11 : !id ? 256 : isDungeon(p, id) ? 8 : 32;
    return {side, side};
}
bool mapMarkerActive(const Project &p, const QString &r, int page, int bit, int x, int y) {
    auto maps = extendedMaps(p);
    const auto m = findExtendedMap(maps, r, page);
    return m && m->markerMask ? bool(m->markers & (quint64(1) << bit))
                              : x >= 0 && y >= 0 && x < 11 && y < 11;
}
QMap<QString, QByteArray> setMapMarker(const Project &p, const QString &r, int page, int bit,
                                       bool active) {
    auto maps = extendedMaps(p);
    for (auto &m : maps)
        if (m.resource == r && m.page == page && m.markerMask) {
            require(bit >= 0 && bit < 64, "Invalid encounter marker");
            if (active)
                m.markers |= quint64(1) << bit;
            else
                m.markers &= ~(quint64(1) << bit);
            return {{"MAPS.OVR", writeExtendedMaps(maps)}};
        }
    return {};
}
void hydrateExtendedMaps(Project &p) {
    for (const auto &m : extendedMaps(p))
        if (m.kind == 1 && MAPS_NewLocation(m.id)) {
            if (!p.resources.contains(m.resource))
                p.resources[m.resource] = {{}, {}};
            for (const auto &ext : {QString(".NPC"), QString(".TLK")}) {
                auto name = m.resource.section('.', 0, 0) + ext;
                if (!p.resources.contains(name))
                    p.resources[name] = {{}, {}};
            }
        }
}
QMap<QString, QByteArray> resizeMap(const Project &p, const QString &resource, int page, int width,
                                    int height, int fill) {
    require(width >= 1 && width <= 256 && height >= 1 && height <= 256,
            "Map dimensions must be 1–256 tiles on each axis");
    MapDocument doc(&p, resource);
    require(page >= 0 && page < doc.pages.size(), "Invalid map page");
    const auto old = doc.pages[page];
    require(fill >= 0 && fill <= 255 && (resource != "DUNGEON.DAT" || (fill >> 4) != 9),
            "Invalid background feature");
    auto maps = extendedMaps(p);
    ExtendedMap m;
    if (auto previous = findExtendedMap(maps, resource, page))
        m = *previous;
    else {
        m.resource = resource;
        m.page = page;
        m.name = old.name.section(" — ", 0, 0);
        m.floor = old.floor;
        m.kind = resource == "BRIT.DAT" || resource == "UNDER.DAT" ? 0
                 : resource == "BRIT.CBT"                          ? 2
                 : resource == "DUNGEON.CBT"                       ? 3
                 : resource == "DUNGEON.DAT"                       ? 4
                                                                   : 1;
        m.id = m.kind == 4   ? 33 + old.settlement
               : m.kind == 0 ? (resource == "UNDER.DAT")
               : m.kind == 1 ? positionMap(resource, old)
                             : page;
        m.background = fill;
        if (m.kind == 2 || m.kind == 3) {
            m.markerMask = true;
            const auto bytes = p.data(resource);
            auto bit = [&](int index, int row, int xc, int yc, bool enabled = true) {
                int x = U5::byte(bytes, old.offset + row * 32 + xc),
                    y = U5::byte(bytes, old.offset + row * 32 + yc);
                if (enabled && x < old.side && y < old.height())
                    m.markers |= quint64(1) << index;
            };
            for (int row = 1; row <= 4; row++)
                for (int i = 0; i < 6; i++)
                    bit((row - 1) * 6 + i, row, 11 + i, 17 + i);
            for (int i = 0; i < 16; i++) {
                int x = U5::byte(bytes, old.offset + 6 * 32 + 11 + i),
                    y = U5::byte(bytes, old.offset + 7 * 32 + 11 + i);
                if (x < old.side && y < old.height() &&
                    (resource == "BRIT.CBT" || U5::byte(bytes, old.offset + 5 * 32 + 11 + i)))
                    m.markers |= quint64(1) << (24 + i);
            }
            if (resource == "DUNGEON.CBT")
                for (int row = 8; row <= 10; row++)
                    for (int i = 0; i < 8; i++)
                        bit(40 + (row - 8) * 8 + i, row, 11 + i, 19 + i);
        }
    }
    m.width = width;
    m.height = height;
    m.cells = QByteArray(width * height, char(fill));
    const auto terrain = doc.rawTerrain(page);
    for (int y = 0; y < qMin(height, old.height()); ++y)
        memcpy(m.cells.data() + y * width, terrain.constData() + y * old.side,
               qMin(width, old.side));
    // Shrinking cannot silently delete authored positions or actors.
    auto check = [&](int x, int y, const QString &what) {
        require(x >= 0 && y >= 0 && x < width && y < height,
                what + " is outside the new dimensions; move it before resizing");
    };
    if (m.kind == 1)
        for (const auto &r : readGates(p.resources.value("GATES.OVR").edited))
            if (U5::byte(r, 0) == unsigned(m.id) && int(int8_t(r[1])) == old.floor)
                check(U5::byte(r, 2), U5::byte(r, 3), "Town gate / drawbridge");
    if (m.kind == 0)
        for (const auto &entry : maps)
            if (entry.world == (resource == "UNDER.DAT" ? -1 : 0))
                check(entry.entrance.x(), entry.entrance.y(), "Location entrance");
    for (const auto &pos : mapPositions(p))
        if (!pos.carried && pos.map == positionMap(resource, old) &&
            (old.settlement >= 0 ? pos.floor == old.floor
                                 : pos.floor == (resource == "UNDER.DAT" ? -1 : old.floor)))
            check(pos.cell.x(), pos.cell.y(), pos.name);
    if (m.kind == 0 && p.resources.contains("INIT.OOL")) {
        auto objects = WorldObjects::effective(p, resource == "UNDER.DAT" ? 1 : 0);
        for (int slot = 1; slot < 32; slot++)
            if (U5::byte(objects, slot * 8))
                check(U5::byte(objects, slot * 8 + 2), U5::byte(objects, slot * 8 + 3),
                      "Starting object");
    }
    if (resource.endsWith(".CBT")) {
        const auto bytes = p.data(resource);
        int base = old.offset;
        auto marker = [&](int row, int xcol, int ycol, int bit, const QString &name) {
            int x = U5::byte(bytes, base + row * 32 + xcol),
                y = U5::byte(bytes, base + row * 32 + ycol);
            if ((m.markers & (quint64(1) << bit)) && x < old.side && y < old.height())
                check(x, y, name);
        };
        for (int row = 1; row <= 4; row++)
            for (int i = 0; i < 6; i++)
                marker(row, 11 + i, 17 + i, (row - 1) * 6 + i, "Party start");
        for (int i = 0; i < 16; i++)
            if (resource == "BRIT.CBT" || U5::byte(bytes, base + 5 * 32 + 11 + i)) {
                int x = U5::byte(bytes, base + 6 * 32 + 11 + i),
                    y = U5::byte(bytes, base + 7 * 32 + 11 + i);
                if ((m.markers & (quint64(1) << (24 + i))) && x < old.side && y < old.height())
                    check(x, y, "Monster start");
            }
        if (resource == "DUNGEON.CBT")
            for (int row = 8; row <= 10; row++)
                for (int i = 0; i < 8; i++)
                    marker(row, 11 + i, 19 + i, 40 + (row - 8) * 8 + i, "Encounter trigger");
    }
    const auto npc = resource.section('.', 0, 0) + ".NPC";
    if (old.settlement >= 0 && p.resources.contains(npc) && !p.data(npc).isEmpty()) {
        MapNpcDocument actors(const_cast<Project *>(&p), npc);
        for (int id = 1; id < actors.count(old.settlement); ++id)
            if (U5::byte(p.data(npc), actors.shapeOffset(old.settlement, id)) ||
                actors.sprite(old.settlement, id) >= 512)
                for (int slot = 0; slot < 4; ++slot) {
                    auto dest = actors.location(old.settlement, id, slot);
                    if (dest.floor == old.floor && dest.x < old.side && dest.y < old.height()) {
                        check(dest.x, dest.y, "NPC schedule destination");
                        auto def = CustomTiles::definition(p, actors.sprite(old.settlement, id));
                        check(dest.x + TYPES_Width(&def) - 1, dest.y + TYPES_Height(&def) - 1,
                              "NPC footprint");
                    }
                }
    }
    for (const auto &c : CustomTiles::placements(p))
        if (resource != "DUNGEON.DAT" && c.kind != 3 && c.kind != 4 && c.kind != 5 && c.kind != 6) {
            auto key = CustomTiles::cell(resource, old, page, c.x, c.y);
            if (TYPES_CellKey(&key) == TYPES_CellKey(&c))
                check(c.x, c.y, "Custom tile");
        }
    for (const auto &c : mapContents(p, contentsTarget(resource, old, page)))
        check(c.x, c.y, "Container or discovery");
    if (p.resources.contains("SIGNS.DAT"))
        for (const auto &sign : readSigns(p.data("SIGNS.DAT")))
            if (sign.map == positionMap(resource, old) &&
                sign.floor == (resource == "UNDER.DAT" ? -1 : old.floor))
                check(sign.x, sign.y, "Sign");
    if (m.world != -2)
        check(m.arrival.x(), m.arrival.y(), "Arrival position");
    bool replaced = false;
    for (auto &entry : maps)
        if (entry.resource == resource && entry.page == page) {
            entry = m;
            replaced = true;
        }
    if (!replaced)
        maps.append(m);
    return {{"MAPS.OVR", writeExtendedMaps(maps)}};
}
QMap<QString, QByteArray> addLocation(const Project &p, const QString &world, const QString &name,
                                      QPoint entrance, int width, int height, int fill,
                                      int entranceTile) {
    require(width >= 1 && width <= 256 && height >= 1 && height <= 256 && fill >= 0 && fill <= 255,
            "Invalid location dimensions or background tile");
    require(world == "BRIT.DAT" || world == "UNDER.DAT",
            "Place a location entrance on a world map");
    auto maps = extendedMaps(p);
    const auto entranceDefinition = CustomTiles::definition(p, entranceTile);
    const int entranceProfile = entranceTile >= 512 ? entranceDefinition.base : entranceTile;
    require(!entranceDefinition.kind && ((entranceProfile >= 0x10 && entranceProfile <= 0x1b) ||
                                         entranceProfile == 0x39 || entranceProfile == 0x3e),
            "Choose a location entrance tile");
    unsigned id = 42;
    for (; id < 128; ++id)
        if (MAPS_NewLocation(id) && std::none_of(maps.begin(), maps.end(), [&](const auto &m) {
                return (m.kind == 1 || m.kind == 4) && m.id == id;
            }))
            break;
    require(id < 128, "The location identity range is full");
    const auto worldPage = p.mapPages(world).first();
    require(entrance.x() >= 0 && entrance.y() >= 0 && entrance.x() < worldPage.side &&
                entrance.y() < worldPage.height(),
            "Entrance is outside the world map");
    for (const auto &pos : mapPositions(p))
        if (pos.id >= 100 && pos.map == 0 && pos.floor == (world == "BRIT.DAT" ? 0 : -1))
            require(pos.cell != entrance, "There is already a location entrance here");
    for (const auto &m : maps)
        require(m.world != (world == "BRIT.DAT" ? 0 : -1) || m.entrance != entrance,
                "There is already a location entrance here");
    ExtendedMap m;
    m.kind = 1;
    m.id = id;
    m.resource = QString("LOC%1.DAT").arg(id, 3, 10, QChar('0'));
    m.name = name.trimmed();
    m.width = width;
    m.height = height;
    m.world = world == "BRIT.DAT" ? 0 : -1;
    m.entrance = entrance;
    m.arrival = {width / 2, height - 1};
    m.background = fill;
    m.cells = QByteArray(width * height, char(fill));
    MapDocument originalWorld(&p, world);
    m.underlay = U5::byte(originalWorld.terrain(0), entrance.y() * worldPage.side + entrance.x());
    maps.append(m);
    Project candidate = p;
    candidate.resources["MAPS.OVR"].edited = writeExtendedMaps(maps);
    MapDocument worldDoc(&candidate, world);
    auto cells = worldDoc.terrain(0);
    const int index = entrance.y() * worldPage.side + entrance.x();
    cells[index] = char(entranceProfile);
    auto expanded = CustomTiles::expanded(candidate, world, worldDoc.pages[0], 0, cells);
    TYPES_Put32(expanded.data() + index * 4, entranceTile >= 512 ? entranceTile : 0);
    auto changes = worldDoc.changes(0, cells, expanded);
    if (!changes.contains("MAPS.OVR"))
        changes["MAPS.OVR"] = candidate.data("MAPS.OVR");
    changes[m.resource.section('.', 0, 0) + ".NPC"] = QByteArray(4608, 0);
    changes[m.resource.section('.', 0, 0) + ".TLK"] = U5::writeDialogue({});
    return changes;
}
QMap<QString, QByteArray> addDungeon(const Project &p, const QString &world, const QString &name,
                                     QPoint entrance, int width, int height, int fill,
                                     int entranceTile) {
    require(p.resources.contains("DUNGEON.DAT") && p.resources.contains("DUNGEON.CBT"),
            "Dungeon resources are missing");
    require(width >= 1 && width <= 256 && height >= 1 && height <= 256 && fill >= 0 &&
                fill <= 255 && (fill >> 4) != 9,
            "Invalid dungeon dimensions or feature");
    // Reuse entrance validation and terrain painting, then replace the town records atomically.
    const auto base = addLocation(p, world, name, entrance, width, height, 5, entranceTile);
    Project candidate = p;
    for (auto it = base.begin(); it != base.end(); ++it)
        candidate.resources[it.key()].edited = it.value();
    auto maps = extendedMaps(candidate);
    const auto town = std::find_if(maps.begin(), maps.end(), [&](const auto &m) {
        return m.kind == 1 && m.world == (world == "UNDER.DAT" ? -1 : 0) && m.entrance == entrance;
    });
    require(town != maps.end(), "Missing dungeon entrance");
    const auto entranceRecord = *town;
    maps.erase(town);
    auto rooms = p.data("DUNGEON.CBT");
    const int roomBase = qMax(112, (rooms.size() / 352 + 15) / 16 * 16);
    require(roomBase <= 65520, "Dungeon combat-room identity range is full");
    const int pageBase = p.mapPages("DUNGEON.DAT").size();
    for (int floor = 0; floor < 8; ++floor) {
        ExtendedMap m = entranceRecord;
        m.kind = 4;
        m.floor = floor;
        m.page = pageBase + floor;
        m.roomBase = roomBase;
        m.resource = "DUNGEON.DAT";
        m.background = fill;
        m.cells = QByteArray(width * height, char(fill));
        m.arrival = {width / 2, height / 2};
        // Every new floor starts with a usable ladder connection.
        m.cells[m.arrival.y() * width + m.arrival.x()] = char(floor == 7 ? 0x10 : 0x30);
        if (floor) {
            m.world = -2;
            m.underlay = 0;
        }
        maps.append(m);
    }
    QByteArray room(352, char(255));
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 11; ++x)
            room[y * 32 + x] = 68;
    for (int i = 0; i < 16; ++i)
        room[5 * 32 + 11 + i] = 0;
    // Native directional party entry positions, with empty monster and trigger lists.
    for (int direction = 0; direction < 4; ++direction)
        for (int i = 0; i < 6; ++i) {
            room[(direction + 1) * 32 + 11 + i] = direction % 2 ? (direction == 1 ? 9 : 1) : 3 + i;
            room[(direction + 1) * 32 + 17 + i] = direction % 2 ? 3 + i : (direction == 0 ? 1 : 9);
        }
    if (rooms.size() < roomBase * 352)
        rooms.append(QByteArray(roomBase * 352 - rooms.size(), 0));
    for (int i = 0; i < 16; ++i)
        rooms.append(room);
    QMap<QString, QByteArray> changes = base;
    changes.remove(entranceRecord.resource.section('.', 0, 0) + ".NPC");
    changes.remove(entranceRecord.resource.section('.', 0, 0) + ".TLK");
    changes["MAPS.OVR"] = writeExtendedMaps(maps);
    changes["DUNGEON.CBT"] = rooms;
    return changes;
}
void validateExtendedMaps(const Project &p) {
    const auto maps = extendedMaps(p);
    if (maps.isEmpty())
        return;
    auto check = [&](int map, int floor, int kind, int x, int y, const QString &what) {
        auto size = mapSize(p, map, floor, kind);
        require(x >= 0 && y >= 0 && x < size.width() && y < size.height(),
                what + " is outside its map");
    };
    for (const auto &m : maps) {
        require(p.resources.contains(m.resource), "Missing map resource: " + m.resource);
        if (m.world != -2)
            check(0, m.world, 0, m.entrance.x(), m.entrance.y(), "Entrance");
        if (m.kind == 4 && MAPS_NewLocation(m.id)) {
            for (int f = 0; f < 8; ++f)
                require(std::any_of(maps.begin(), maps.end(),
                                    [&](const auto &other) {
                                        return other.kind == 4 && other.id == m.id &&
                                               other.floor == f;
                                    }),
                        "New dungeon is missing a floor");
            if (!m.floor)
                check(0, m.world, 0, m.entrance.x(), m.entrance.y(), "Dungeon entrance");
            require(m.roomBase + 16 <= unsigned(p.data("DUNGEON.CBT").size() / 352),
                    "Dungeon combat rooms are missing");
        } else if (m.kind == 1 && MAPS_NewLocation(m.id)) {
            if (!m.floor)
                check(0, m.world, 0, m.entrance.x(), m.entrance.y(), "Entrance");
            const auto pages = p.mapPages(m.resource);
            require(std::any_of(pages.begin(), pages.end(),
                                [](const auto &page) { return page.floor == 0; }),
                    "Location is missing its ground floor");
            const auto stem = m.resource.section('.', 0, 0);
            require(p.resources.contains(stem + ".NPC") && p.resources.contains(stem + ".TLK"),
                    "New location needs NPC and dialogue resources");
            require(NPCS_Validate(p.data(stem + ".NPC").constData(), p.data(stem + ".NPC").size()),
                    "Invalid location NPCs");
            U5::readDialogue(p.data(stem + ".TLK"));
        } else {
            const auto native = U5::mapPages(m.resource, p.data(m.resource));
            require(m.page >= 0 && m.page < native.size() &&
                        (m.kind != 1 || (positionMap(m.resource, native[m.page]) == int(m.id) &&
                                         native[m.page].floor == m.floor)),
                    "Map override has an invalid native page identity");
        }
    }
    if (std::any_of(maps.begin(), maps.end(), [](const auto &m) { return m.kind == 4; }))
        DungeonDocument::validate(p);
    for (auto pos : mapPositions(p))
        if (!pos.carried)
            check(pos.map, pos.floor, 0, pos.cell.x(), pos.cell.y(), pos.name);
    if (p.resources.contains("SIGNS.DAT"))
        for (auto sign : readSigns(p.data("SIGNS.DAT")))
            check(sign.map, sign.floor, 0, sign.x, sign.y, "Sign");
    for (auto c : readContents(p.resources.value("CONTENTS.OVR").edited))
        check(c.map, c.floor, c.kind, c.x, c.y, "Container or discovery");
}

} // namespace Workshop
