#include "gates_editor.h"
#include "../src/mod/gates.h"
#include "extended_maps.h"
#include "map_positions.h"
#include <algorithm>
namespace Workshop {
QList<QByteArray> readGates(const QByteArray &bytes) {
    QList<QByteArray> records;
    if (bytes.isEmpty())
        return records;
    U5::require(GATES_Validate(bytes.constData(), size_t(bytes.size())), "Invalid town gates");
    for (int offset = GATES_HEADER; offset < bytes.size(); offset += GATES_RECORD)
        records.append(bytes.mid(offset, GATES_RECORD));
    return records;
}
QByteArray writeGates(QList<QByteArray> records) {
    if (records.isEmpty())
        return {};
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
        return GATES_Key(reinterpret_cast<const unsigned char *>(a.constData())) <
               GATES_Key(reinterpret_cast<const unsigned char *>(b.constData()));
    });
    QByteArray bytes("IGAT0001", 8);
    unsigned n = unsigned(records.size());
    for (int i = 0; i < 4; ++i)
        bytes.append(char(n >> (i * 8)));
    for (const auto &r : records)
        bytes.append(r);
    U5::require(GATES_Validate(bytes.constData(), size_t(bytes.size())), "Invalid town gates");
    return bytes;
}
QList<QByteArray> mapGates(const Project &p, int map, int floor, const QByteArray &terrain,
                           QSize size) {
    QList<QByteArray> records;
    QSet<int> explicitCells;
    for (const auto &r : readGates(p.resources.value("GATES.OVR").edited))
        if (U5::byte(r, 0) == unsigned(map) && int(int8_t(r[1])) == floor) {
            records.append(r);
            explicitCells.insert(U5::byte(r, 3) * size.width() + U5::byte(r, 2));
        }
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x) {
            int offset = y * size.width() + x;
            if (offset >= terrain.size() || explicitCells.contains(offset))
                continue;
            int tile = U5::byte(terrain, offset);
            bool bridge = (tile & 254) == 72;
            bool gate = y && U5::byte(terrain, offset - size.width()) == 135;
            if (!bridge && !gate)
                continue;
            QByteArray r(GATES_RECORD, 0);
            r[0] = char(map);
            r[1] = char(floor);
            r[2] = char(x);
            r[3] = char(y);
            r[4] = bridge ? 2 : 1;
            r[5] = 1;
            r[6] = char(tile);
            r[7] = char(bridge ? 3 : tile ^ 221);
            r[8] = 5;
            r[9] = 20;
            records.append(r);
        }
    return records;
}
QByteArray setGate(const Project &p, const QByteArray &record, bool remove) {
    auto records = readGates(p.resources.value("GATES.OVR").edited);
    for (int i = records.size() - 1; i >= 0; --i)
        if (records[i].left(4) == record.left(4))
            records.removeAt(i);
    if (!remove)
        records.append(record);
    return writeGates(records);
}
void validateGates(const Project &p) {
    const auto records = readGates(p.resources.value("GATES.OVR").edited);
    if (records.isEmpty())
        return;
    QMap<QPair<int, int>, QSize> sizes;
    for (const auto &name : p.resources.keys()) {
        if (name.endsWith(".CBT") || name == "DUNGEON.DAT" || name == "BRIT.DAT" ||
            name == "UNDER.DAT" || !MapDocument::supported(name))
            continue;
        for (const auto &page : p.mapPages(name))
            sizes.insert({positionMap(name, page), page.floor}, QSize(page.side, page.height()));
    }
    for (const auto &r : records) {
        const auto key = qMakePair(int(U5::byte(r, 0)), int(int8_t(r[1])));
        U5::require(sizes.contains(key), "Town gate has no location floor");
        const auto size = sizes[key];
        U5::require(U5::byte(r, 2) < unsigned(size.width()) &&
                        U5::byte(r, 3) < unsigned(size.height()),
                    "Town gate is outside the location floor");
    }
}
} // namespace Workshop
