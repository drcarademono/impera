#include "extended_maps.h"
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
        m.id = r[5];
        m.page = MAPS_Word(r + 6);
        m.floor = int8_t(r[8]);
        m.background = r[9];
        m.width = MAPS_Word(r + 10);
        m.height = MAPS_Word(r + 12);
        m.world = int8_t(r[14]);
        m.entrance = {r[15], r[16]};
        m.arrival = {r[17], r[18]};
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
        require(m.kind <= 3 && m.id <= 255 && m.page >= 0 && m.page <= 65535 && m.floor >= -128 &&
                    m.floor <= 127 && m.background >= 0 && m.background <= 255 && m.world >= -2 &&
                    m.world <= 0 && m.entrance.x() >= 0 && m.entrance.x() <= 255 &&
                    m.entrance.y() >= 0 && m.entrance.y() <= 255 && m.arrival.x() >= 0 &&
                    m.arrival.x() <= 255 && m.arrival.y() >= 0 && m.arrival.y() <= 255,
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
    require(fill >= 0 && fill <= 255, "Invalid background tile");
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
                                                                   : 1;
        m.id = m.kind == 0   ? (resource == "UNDER.DAT")
               : m.kind == 1 ? positionMap(resource, old)
                             : page;
        m.background = fill;
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
    if (m.kind == 0)
        for (const auto &entry : maps)
            if (entry.world == (resource == "UNDER.DAT" ? -1 : 0))
                check(entry.entrance.x(), entry.entrance.y(), "Location entrance");
    for (const auto &pos : mapPositions(p))
        if (!pos.carried && pos.map == positionMap(resource, old) &&
            (old.settlement >= 0 ? pos.floor == old.floor
                                 : pos.floor == (resource == "UNDER.DAT" ? -1 : old.floor)))
            check(pos.cell.x(), pos.cell.y(), pos.name);
    const auto npc = resource.section('.', 0, 0) + ".NPC";
    if (old.settlement >= 0 && p.resources.contains(npc) && !p.data(npc).isEmpty()) {
        MapNpcDocument actors(const_cast<Project *>(&p), npc);
        for (int id = 1; id < actors.count(old.settlement); ++id)
            if (U5::byte(p.data(npc), actors.shapeOffset(old.settlement, id)) ||
                actors.sprite(old.settlement, id) >= 512)
                for (int slot = 0; slot < 4; ++slot) {
                    auto dest = actors.location(old.settlement, id, slot);
                    if (dest.floor == old.floor && dest.x < old.side && dest.y < old.height())
                        check(dest.x, dest.y, "NPC schedule destination");
                }
    }
    for (const auto &c : CustomTiles::placements(p))
        if (c.kind != 3 && c.kind != 4 && c.kind != 5 && c.kind != 6) {
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
                                      QPoint entrance, int width, int height, int fill) {
    require(width >= 1 && width <= 256 && height >= 1 && height <= 256 && fill >= 0 && fill <= 255,
            "Invalid location dimensions or background tile");
    require(world == "BRIT.DAT" || world == "UNDER.DAT",
            "Place a location entrance on a world map");
    auto maps = extendedMaps(p);
    unsigned id = 42;
    for (; id < 128; ++id)
        if (MAPS_NewLocation(id) && std::none_of(maps.begin(), maps.end(), [&](const auto &m) {
                return m.kind == 1 && m.id == id;
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
    maps.append(m);
    return {{"MAPS.OVR", writeExtendedMaps(maps)},
            {m.resource.section('.', 0, 0) + ".NPC", QByteArray(4608, 0)},
            {m.resource.section('.', 0, 0) + ".TLK", U5::writeDialogue({})}};
}
void validateExtendedMaps(const Project &p) {
    if (!p.resources.value("MAPS.OVR").edited.isEmpty())
        extendedMaps(p);
}
} // namespace Workshop
