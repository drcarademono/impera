#include "mod/npcs.h"
#include "map_editor.h"
#include "extended_maps.h"
#include "../src/tiles.h"
#include "contents_editor.h"
#include "content_navigation.h"
#include "custom_tiles.h"
#include "dialogue.h"
#include "location_text.h"
#include "map_entities.h"
#include "map_positions.h"
#include "resource_document.h"
#include "world_objects.h"
#include <cmath>
using U5::require;

bool MapDocument::supported(const QString &r) {
    return Workshop::capability(r).editor == Workshop::EditorKind::Map;
}
MapDocument::MapDocument(const Project *p, const QString &r) : project(p), resource(r) {
    pages = project->mapPages(resource);
}
QByteArray MapDocument::rawTerrain(int index, bool original) const {
    const auto &p = pages.at(index);
    const auto maps = Workshop::extendedMaps(*project);
    const auto *extended = Workshop::findExtendedMap(maps, resource, index);
    if (extended && !original)
        return extended->cells;
    const auto data = original ? project->resources[resource].original : project->data(resource);
    QByteArray native;
    const bool world = resource == "BRIT.DAT" || resource == "UNDER.DAT";
    if (world)
        native = U5::worldMap(
            resource, data,
            resource == "BRIT.DAT"
                ? (original ? project->resources["DATA.OVL"].original : project->data("DATA.OVL"))
                : QByteArray());
    const auto nativePages = MAPS_LocationResource(resource.toLatin1().constData())
                                 ? QVector<U5::MapPage>()
                                 : U5::mapPages(resource, data);
    if (nativePages.isEmpty()) {
        require(extended, "Missing map terrain");
        return QByteArray(p.side * p.height(), char(extended->background));
    }
    const auto &source = nativePages.at(index);
    if (!world)
        for (int y = 0; y < source.height(); ++y)
            native += data.mid(source.offset + y * source.stride, source.side);
    require(native.size() == source.side * source.height(), "Map terrain is truncated");
    QByteArray cells(p.side * p.height(), char(extended ? extended->background : 0));
    for (int y = 0; y < qMin(p.height(), source.height()); ++y)
        memcpy(cells.data() + y * p.side, native.constData() + y * source.side,
               qMin(p.side, source.side));
    return cells;
}
QByteArray MapDocument::terrain(int index, bool original) const {
    auto bytes = rawTerrain(index, original);
    if (original)
        return bytes;
    auto definitions = CustomTiles::records(*project);
    if (definitions.isEmpty())
        return bytes;
    for (const auto &placed : CustomTiles::placements(*project)) {
        auto c = CustomTiles::cell(resource, pages[index], index, placed.x, placed.y);
        if (TYPES_CellKey(&c) == TYPES_CellKey(&placed) && placed.x < pages[index].side &&
            placed.y < pages[index].height())
            bytes[placed.y * pages[index].side + placed.x] =
                char(U5::byte(definitions[placed.type], 9));
    }
    return bytes;
}
QMap<QString, QByteArray> MapDocument::changes(int index, const QByteArray &cells,
                                               const QByteArray &expanded) const {
    const auto &p = pages.at(index);
    require(cells.size() == p.side * p.height(), "Map terrain dimensions changed");
    auto data = project->data(resource);
    auto native = rawTerrain(index);
    auto ext = expanded;
    if (ext.isEmpty()) {
        ext = CustomTiles::expanded(*project, resource, p, index, cells);
        auto current = terrain(index);
        for (int i = 0; i < cells.size(); i++)
            if (cells[i] != current[i])
                TYPES_Put32(ext.data() + i * 4, 0);
    }
    require(ext.size() == cells.size() * 4, "Invalid extended map dimensions");
    QByteArray stored = cells;
    for (int i = 0; i < cells.size(); i++)
        if (TYPES_U32(ext.constData() + i * 4) >= 512)
            stored[i] = native[i];
    QMap<QString, QByteArray> result;
    auto maps = Workshop::extendedMaps(*project);
    if (Workshop::findExtendedMap(maps, resource, index)) {
        for (auto &map : maps)
            if (map.resource == resource && map.page == index)
                map.cells = stored;
        result["MAPS.OVR"] = Workshop::writeExtendedMaps(maps);
    } else if (resource == "BRIT.DAT" || resource == "UNDER.DAT") {
        auto overlay = resource == "BRIT.DAT" ? project->data("DATA.OVL") : QByteArray();
        U5::writeWorld(resource, stored, data, overlay);
        if (resource == "BRIT.DAT")
            result["DATA.OVL"] = overlay;
    } else
        for (int y = 0; y < p.side; ++y)
            data.replace(p.offset + y * p.stride, p.side, stored.mid(y * p.side, p.side));
    if (!result.contains("MAPS.OVR"))
        result[resource] = data;
    auto custom = CustomTiles::paint(*project, resource, p, index, cells, ext);
    for (auto it = custom.begin(); it != custom.end(); it++)
        result[it.key()] = it.value();
    return result;
}

MapNpcDocument::MapNpcDocument(Project *p, QString r) : project(p), resource(r) {
    require(NPCS_Validate(project->data(resource).constData(), project->data(resource).size()),
            "Invalid NPC schedules");
}
int MapNpcDocument::locationIndex(int slot) {
    require(slot >= 0 && slot < 4, "Invalid NPC time slot");
    return slot == 0 ? 0 : slot == 2 ? 2 : 1;
}
int MapNpcDocument::count(int town) const {
    auto b = project->data(resource);
    return NPCS_Count(b.constData(), b.size(), town);
}
int MapNpcDocument::offset(int town, int npc) const {
    auto b = project->data(resource);
    auto off = NPCS_Offset(b.constData(), b.size(), town, npc);
    require(off + 16 <= size_t(b.size()), "Invalid NPC record");
    return int(off);
}
int MapNpcDocument::shapeOffset(int town, int npc) const {
    offset(town, npc);
    auto b = project->data(resource);
    return NPCS_ShapeOffset(b.constData(), b.size(), town, npc);
}
int MapNpcDocument::dialogueOffset(int town, int npc) const {
    offset(town, npc);
    auto b = project->data(resource);
    return NPCS_DialogueOffset(b.constData(), b.size(), town, npc);
}
void MapNpcDocument::ensureSlot(QByteArray &b, int town, int npc) {
    require(town >= 0 && town < 8 && npc > 0 && npc < IMPERA_NPC_CAPACITY, "Invalid NPC slot");
    require(NPCS_Validate(b.constData(), b.size()), "Invalid NPC schedules");
    if (npc < 32 || NPCS_Offset(b.constData(), b.size(), town, npc) < size_t(b.size()))
        return;
    if (b.size() == 4608) {
        b.append("INPC0001", 8);
        b.append(QByteArray(8, 0));
    }
    QByteArray record(24, 0);
    record[0] = char(town);
    record[2] = char(npc);
    record[3] = char(npc >> 8);
    int at = IMPERA_NPC_EXTRA_START;
    for (; at < b.size(); at += 24) {
        int key = U5::byte(b, at) * IMPERA_NPC_CAPACITY +
                  NPCS_Word(reinterpret_cast<const unsigned char *>(b.constData() + at + 2));
        if (key > town * IMPERA_NPC_CAPACITY + npc)
            break;
    }
    b.insert(at, record);
    TYPES_Put32(b.data() + 4616, (b.size() - IMPERA_NPC_EXTRA_START) / 24);
}
NpcLocation MapNpcDocument::location(int settlement, int npc, int slot) const {
    int base = offset(settlement, npc), loc = locationIndex(slot);
    auto data = project->data(resource);
    int z = U5::byte(data, base + 9 + loc);
    return {int(U5::byte(data, base + 3 + loc)), int(U5::byte(data, base + 6 + loc)),
            z >= 128 ? z - 256 : z, int(U5::byte(data, base + loc))};
}
int MapNpcDocument::hour(int settlement, int npc, int slot) const {
    locationIndex(slot);
    return U5::byte(project->data(resource), offset(settlement, npc) + 12 + slot);
}
int MapNpcDocument::sprite(int settlement, int npc) const {
    offset(settlement, npc);
    const int bank = resource == "TOWNE.NPC"      ? 0
                     : resource == "DWELLING.NPC" ? 8
                     : resource == "CASTLE.NPC"   ? 16
                                                  : 24;
    const unsigned customMap = MAPS_LocationResource(resource.toLatin1().constData());
    TypeCell key{3,
                 quint8(customMap ? customMap : bank + settlement + 1),
                 0,
                 quint8(npc),
                 quint8(npc >> 8),
                 0,
                 0};
    unsigned custom = CustomTiles::placements(*project).value(TYPES_CellKey(&key), key).type;
    return custom ? custom : 256 + U5::byte(project->data(resource), shapeOffset(settlement, npc));
}
int MapNpcDocument::dialogue(int settlement, int npc) const {
    offset(settlement, npc);
    return U5::byte(project->data(resource), dialogueOffset(settlement, npc));
}
QByteArray MapNpcDocument::move(int settlement, int npc, int slot, NpcLocation destination) const {
    int base = offset(settlement, npc), loc = locationIndex(slot);
    int width = 32, height = 32;
    if (project->resources.contains(resource.section('.', 0, 0) + ".DAT"))
        for (const auto &page : project->mapPages(resource.section('.', 0, 0) + ".DAT"))
            if (page.settlement == settlement && page.floor == destination.floor) {
                width = page.side;
                height = page.height();
            }
    require(destination.x >= 0 && destination.x < width && destination.y >= 0 &&
                destination.y < height && destination.floor >= -128 && destination.floor <= 127,
            "NPC destination is outside map bounds");
    auto bytes = project->data(resource);
    bytes[base + 3 + loc] = char(destination.x);
    bytes[base + 6 + loc] = char(destination.y);
    bytes[base + 9 + loc] = char(destination.floor);
    return bytes;
}

MapCanvas::MapCanvas(QWidget *parent) : QWidget(parent) {
    setObjectName("mapCanvas");
    setAccessibleName("Map canvas");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}
QImage MapCanvas::tileImageAt(int offset) const {
    unsigned id = tileId(offset);
    auto art = tiles.value(id);
    if (!spriteGround.contains(id))
        return art;
    unsigned ground = spriteGround[id];
    if (ground == TYPES_INFER_GROUND) {
        int x = offset % side, y = offset / side, best = 0;
        int offsets[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
        for (auto &o : offsets) {
            int nx = x + o[0], ny = y + o[1];
            if (nx < 0 || ny < 0 || nx >= side || ny >= rowCount())
                continue;
            unsigned candidate = tileId(ny * side + nx);
            if (!(candidate == 5 || candidate == 0x40 || candidate == 0x44 || candidate == 0x45 ||
                  (candidate >= 512 && !spriteGround.contains(candidate))))
                continue;
            int votes = 0;
            for (auto &n : offsets) {
                int px = x + n[0], py = y + n[1];
                if (px >= 0 && py >= 0 && px < side && py < rowCount())
                    votes += tileId(py * side + px) == candidate;
            }
            if (votes > best) {
                best = votes;
                ground = candidate;
            }
        }
    }
    int edge = qMax(16, art.width());
    QImage result(edge, edge, QImage::Format_ARGB32);
    result.fill(Qt::black);
    QPainter painter(&result);
    if (ground != TYPES_INFER_GROUND && ground < unsigned(tiles.size()))
        painter.drawImage(result.rect(), tiles[ground]);
    painter.drawImage(result.rect(), art);
    return result;
}
unsigned MapCanvas::tileId(int offset) const {
    if (offset < 0 || offset >= ids.size())
        return 0;
    unsigned custom =
        extended.size() == ids.size() * 4 ? TYPES_U32(extended.constData() + offset * 4) : 0;
    return custom ? custom : U5::byte(ids, offset);
}
void MapCanvas::setTileId(int offset, unsigned id) {
    if (offset < 0 || offset >= ids.size())
        return;
    if (actorBrushKinds.contains(id) || specialBrushKinds.contains(id) ||
        (id >= 512 && !legacyTiles.contains(id)))
        return;
    if (!extended.isEmpty() || id >= 512) {
        if (extended.size() != ids.size() * 4)
            extended = QByteArray(ids.size() * 4, 0);
        TYPES_Put32(extended.data() + offset * 4, id >= 512 ? id : 0);
    }
    ids[offset] = char(id >= 512 ? legacyTiles.value(id) : id);
}
QIcon MapCanvas::toolIcon(int i) {
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#e0e8f0"), 2));
    if (i == MapCanvas::Pencil) {
        painter.drawLine(5, 19, 18, 6);
        painter.drawLine(8, 21, 21, 8);
        painter.drawLine(5, 19, 4, 22);
        painter.drawLine(18, 6, 21, 8);
    } else if (i == MapCanvas::Eyedropper) {
        painter.drawLine(5, 19, 17, 7);
        painter.drawLine(8, 21, 20, 9);
        painter.drawLine(14, 5, 22, 13);
        painter.drawLine(5, 19, 8, 21);
    } else if (i == MapCanvas::Pan) {
        painter.drawLine(12, 2, 12, 22);
        painter.drawLine(2, 12, 22, 12);
        painter.drawPolyline(QPolygon{{8, 6}, {12, 2}, {16, 6}});
        painter.drawPolyline(QPolygon{{8, 18}, {12, 22}, {16, 18}});
        painter.drawPolyline(QPolygon{{6, 8}, {2, 12}, {6, 16}});
        painter.drawPolyline(QPolygon{{18, 8}, {22, 12}, {18, 16}});
    } else if (i == MapCanvas::InspectNpc) {
        painter.setBrush(QColor("#a5d8ff"));
        painter.drawPolygon(
            QPolygon{{3, 2}, {3, 20}, {8, 15}, {12, 22}, {16, 20}, {12, 14}, {21, 14}});
    } else if (i == MapCanvas::Select) {
        painter.setPen(QPen(QColor("#e0e8f0"), 2, Qt::DashLine));
        painter.drawRect(3, 3, 18, 18);
    } else if (i == MapCanvas::Rectangle) {
        painter.setBrush(QColor("#729ac4"));
        painter.drawRect(3, 5, 18, 14);
    } else {
        painter.drawPolygon(QPolygon{{3, 12}, {12, 3}, {20, 11}, {11, 20}});
        painter.drawLine(6, 12, 18, 12);
        painter.setBrush(QColor("#729ac4"));
        painter.drawEllipse(18, 17, 4, 5);
    }
    painter.end();
    return QIcon(icon);
}
void MapCanvas::resizeMap() {
    setFixedSize(int(std::ceil(side * 16 * zoom)), int(std::ceil(rowCount() * 16 * zoom)));
    update();
}
QPoint MapCanvas::cellAt(QPointF p) const {
    if (p.x() < 0 || p.y() < 0 || p.x() >= side * 16 * zoom || p.y() >= rowCount() * 16 * zoom)
        return {-1, -1};
    return {int(std::floor(p.x() / (16 * zoom))), int(std::floor(p.y() / (16 * zoom)))};
}
void MapCanvas::paintEvent(QPaintEvent *e) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    double cell = 16 * zoom;
    auto r = e->rect();
    p.fillRect(r, QColor("#141b27"));
    for (int y = qMax(0, int(r.top() / cell)); y <= qMin(rowCount() - 1, int(r.bottom() / cell));
         ++y)
        for (int x = qMax(0, int(r.left() / cell)); x <= qMin(side - 1, int(r.right() / cell));
             ++x) {
            int offset = y * side + x;
            if (offset >= ids.size())
                continue;
            int id = tileId(offset);
            QRectF target(x * cell, y * cell, cell, cell);
            if (id < tiles.size())
                p.drawImage(target, tileImageAt(offset));
            else
                p.fillRect(target, Qt::magenta);
            if (comparison && original.size() == ids.size() &&
                (ids[offset] != original[offset] ||
                 (extended.size() == ids.size() * 4 &&
                  TYPES_U32(extended.constData() + offset * 4)))) {
                p.fillRect(target, QColor(255, 150, 30, 65));
                p.setPen(QColor("#ffae42"));
                p.drawRect(target.adjusted(1, 1, -1, -1));
            }
            if (grid) {
                p.setPen(QColor(255, 255, 255, 45));
                p.drawRect(target.adjusted(0, 0, -1, -1));
            }
        }
    if (hasFocus()) {
        p.setPen(QPen(QColor("#ffea80"), 2, Qt::DashLine));
        p.drawRect(QRectF(keyboardCell.x() * cell, keyboardCell.y() * cell, cell, cell)
                       .adjusted(2, 2, -2, -2));
    }
    for (auto a : actors)
        if (a.x >= 0 && a.y >= 0 && a.x < side && a.y < rowCount() && a.tile >= 0 &&
            a.tile < tiles.size() && a.label.isEmpty())
            p.drawImage(QRectF(a.x * cell, a.y * cell, actorSize(a.tile).width() * cell,
                               actorSize(a.tile).height() * cell),
                        tiles[a.tile]);
    for (auto actor : contextActors)
        if (actor.tile >= 0 && actor.tile < tiles.size())
            p.drawImage(QRectF(actor.x * cell, actor.y * cell, actorSize(actor.tile).width() * cell,
                               actorSize(actor.tile).height() * cell),
                        tiles[actor.tile]);
    if (tool == InspectNpc) {
        for (auto marker : positionMarkers) {
            QRectF r(marker.x * cell, marker.y * cell, cell, cell);
            p.setPen(QPen(QColor("#a9df86"), 2));
            p.drawRect(r.adjusted(3, 3, -3, -3));
            p.drawText(r, Qt::AlignCenter, marker.label);
        }
        if (showContentsMarkers)
            for (auto marker : contentsMarkers) {
                p.setPen(QPen(QColor("#ffbd55"), 2));
                p.drawEllipse(QRectF(marker.x * cell + cell * .68, marker.y * cell + cell * .08,
                                     cell * .22, cell * .22));
            }

        for (auto actor : actors)
            if (!actor.label.isEmpty()) {
                QRectF r(actor.x * cell, actor.y * cell, actorSize(actor.tile).width() * cell,
                         actorSize(actor.tile).height() * cell);
                if (actor.x < 0 || actor.y < 0 || actor.x >= side || actor.y >= rowCount())
                    continue;
                if (actor.tile >= 512 && actor.tile < tiles.size())
                    p.drawImage(r, tiles[actor.tile]);
                else
                    p.fillRect(r.adjusted(2, 2, -2, -2), QColor(20, 30, 60, 180));
                p.setPen(actor.label.startsWith("T") ? QColor("#ffbd55") : QColor("#72caff"));
                p.drawRect(r.adjusted(2, 2, -2, -2));
                p.drawText(r, Qt::AlignCenter, actor.label);
            }
        for (auto actor : ghosts) {
            p.setOpacity(0.35);
            if (actor.tile >= 0 && actor.tile < tiles.size())
                p.drawImage(QRectF(actor.x * cell, actor.y * cell,
                                   actorSize(actor.tile).width() * cell,
                                   actorSize(actor.tile).height() * cell),
                            tiles[actor.tile]);
            p.setOpacity(1);
            p.setPen(QColor("#72caff"));
            p.drawText(QRectF(actor.x * cell, actor.y * cell, actorSize(actor.tile).width() * cell,
                              actorSize(actor.tile).height() * cell),
                       Qt::AlignTop | Qt::AlignLeft,
                       actor.label.isEmpty() ? QString::number(actor.npc + 1) : actor.label);
        }
        for (auto actor : actors)
            if (actor.npc == selectedNpc) {
                p.setPen(QPen(QColor("#72caff"), 2));
                p.drawRect(QRectF(actor.x * cell, actor.y * cell,
                                  actorSize(actor.tile).width() * cell,
                                  actorSize(actor.tile).height() * cell)
                               .adjusted(1, 1, -1, -1));
                if (npcDrag && npcDestination.x() >= 0) {
                    if (actor.tile >= 0 && actor.tile < tiles.size()) {
                        p.setOpacity(0.65);
                        p.drawImage(QRectF(npcDestination.x() * cell, npcDestination.y() * cell,
                                           actorSize(actor.tile).width() * cell,
                                           actorSize(actor.tile).height() * cell),
                                    tiles[actor.tile]);
                        p.setOpacity(1);
                    }
                    p.drawRect(QRectF(npcDestination.x() * cell, npcDestination.y() * cell,
                                      actorSize(actor.tile).width() * cell,
                                      actorSize(actor.tile).height() * cell)
                                   .adjusted(1, 1, -1, -1));
                    if (!actor.label.isEmpty()) {
                        p.setPen(QColor("#72caff"));
                        p.drawText(QRectF(npcDestination.x() * cell, npcDestination.y() * cell,
                                          cell, cell),
                                   Qt::AlignCenter, actor.label);
                    }
                }
            }
    }
    if (tool != InspectNpc && !selection.isEmpty()) {
        QRectF area(selection.x() * cell, selection.y() * cell, selection.width() * cell,
                    selection.height() * cell);
        p.fillRect(area, QColor(80, 170, 255, 35));
        p.setPen(QPen(QColor("#72caff"), 2, Qt::DashLine));
        p.drawRect(area.adjusted(1, 1, -1, -1));
    }
    if (pasting() && hover.x() >= 0) {
        p.setOpacity(0.6);
        for (int y = qMax(0, int(r.top() / cell) - hover.y());
             y <= qMin(stampSize.height() - 1, int(r.bottom() / cell) - hover.y()); ++y)
            for (int x = qMax(0, int(r.left() / cell) - hover.x());
                 x <= qMin(stampSize.width() - 1, int(r.right() / cell) - hover.x()); ++x) {
                int index = y * stampSize.width() + x;
                int id = stampExtended.isEmpty() ? U5::byte(stamp, index)
                                                 : TYPES_U32(stampExtended.constData() + index * 4);
                if (id < tiles.size())
                    p.drawImage(QRectF((hover.x() + x) * cell, (hover.y() + y) * cell, cell, cell),
                                tiles[id]);
            }
        p.setOpacity(1);
        p.setPen(QPen(pasteFits() ? QColor("#83e28b") : QColor("#ff6868"), 2));
        p.drawRect(QRectF(hover.x() * cell, hover.y() * cell, stampSize.width() * cell,
                          stampSize.height() * cell)
                       .adjusted(1, 1, -1, -1));
    }
    if (hover.x() >= 0) {
        QRectF target(hover.x() * cell, hover.y() * cell, cell, cell);
        if (!pasting() && !placing() && tool == 0 && brush >= 0 && brush < tiles.size()) {
            p.setOpacity(0.35);
            p.drawImage(target, tiles[brush]);
            p.setOpacity(1);
        }
        p.setPen(QPen(QColor("#f2ce65"), 2));
        p.drawRect(target.adjusted(1, 1, -1, -1));
    }
    if (placing() && hover.x() >= 0) {
        QRectF target(hover.x() * cell, hover.y() * cell, cell, cell);
        p.setOpacity(.65);
        if (placementTile >= 0 && placementTile < tiles.size()) {
            target.setWidth(cell * actorSize(placementTile).width());
            target.setHeight(cell * actorSize(placementTile).height());
            p.drawImage(target, tiles[placementTile]);
        }
        p.setOpacity(1);
        p.setPen(QPen(QColor("#72caff"), 2, Qt::DashLine));
        p.drawRect(target.adjusted(1, 1, -1, -1));
    }
}
void MapCanvas::line(QPoint a, QPoint b) {
    int x = a.x(), y = a.y(), dx = std::abs(b.x() - x), sx = x < b.x() ? 1 : -1;
    int dy = -std::abs(b.y() - y), sy = y < b.y() ? 1 : -1, error = dx + dy;
    for (;;) {
        if (editable({x, y}))
            setTileId(y * side + x, brush);
        if (x == b.x() && y == b.y())
            break;
        int e = 2 * error;
        if (e >= dy) {
            error += dy;
            x += sx;
        }
        if (e <= dx) {
            error += dx;
            y += sy;
        }
    }
    update(QRectF(qMin(a.x(), b.x()) * 16 * zoom, qMin(a.y(), b.y()) * 16 * zoom,
                  (std::abs(a.x() - b.x()) + 1) * 16 * zoom,
                  (std::abs(a.y() - b.y()) + 1) * 16 * zoom)
               .toAlignedRect());
}
void MapCanvas::point(QPointF position, bool draw) {
    auto cell = cellAt(position);
    auto previous = hover;
    hover = cell;
    if (pasting() || placing())
        update();
    if (previous != hover) {
        for (auto cell : {previous, hover})
            if (cell.x() >= 0)
                update(QRectF(cell.x() * 16 * zoom, cell.y() * 16 * zoom, 16 * zoom, 16 * zoom)
                           .toAlignedRect()
                           .adjusted(-2, -2, 2, 2));
    }
    if (cell.x() < 0) {
        last = {-1, -1};
        return;
    }
    int offset = cell.y() * side + cell.x();
    if (offset >= ids.size())
        return;
    if (inspect)
        inspect(cell.x(), cell.y(), tileId(offset));
    if (pasting())
        say(pasteFits() ? QString("Paste preview: %1 changed cells · click to place · Esc cancels")
                              .arg(pasteChangedCells())
                        : "Paste is outside map bounds; placement rejected");
    if (draw && stroke) {
        if (tool == Rectangle)
            previewRectangle(cell);
        else if (tool == Select) {
            selection = QRect(
                QPoint(qMin(start.x(), cell.x()), qMin(start.y(), cell.y())),
                QSize(std::abs(start.x() - cell.x()) + 1, std::abs(start.y() - cell.y()) + 1));
            update();
            if (selectionChanged)
                selectionChanged();
        } else {
            line(last.x() < 0 ? cell : last, cell);
            last = cell;
        }
    }
}
void MapCanvas::beginPlacement(int tile, bool repeat, std::function<bool(QPoint)> place) {
    cancelStroke();
    placementTile = tile;
    repeatPlacement = repeat;
    placeItem = std::move(place);
    setCursor(Qt::CrossCursor);
    setFocus(Qt::OtherFocusReason);
    update();
}
void MapCanvas::mousePressEvent(QMouseEvent *e) {
    setFocus(Qt::MouseFocusReason);
    auto cell = cellAt(e->position());
    if (cell.x() < 0)
        return;
    keyboardCell = cell;
    if (placing()) {
        if (e->button() == Qt::RightButton) {
            cancelStroke();
            return;
        }
        if (e->button() == Qt::LeftButton) {
            auto place = placeItem;
            const bool repeat = repeatPlacement;
            if (place(cell) && !repeat) {
                placeItem = {};
                placementTile = -1;
                unsetCursor();
            }
            update();
        }
        return;
    }
    if (e->button() == Qt::RightButton ||
        (e->button() == Qt::LeftButton && tool == 1 && !pasting())) {
        if (pick)
            pick(tileId(cell.y() * side + cell.x()));
        point(e->position(), false);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (pasting()) {
        pasteAt(cell);
        return;
    }
    if (tool == InspectNpc) {
        if (prepareSelection && !prepareSelection(cell))
            return;
        QList<int> candidates;
        for (auto a : actors)
            if (actorRect(a).contains(cell))
                candidates.append(a.npc);
        bool chooser = candidates.size() > 1 && !candidates.contains(selectedNpc);
        int npc = candidates.contains(selectedNpc)       ? selectedNpc
                  : candidates.size() == 1               ? candidates[0]
                  : candidates.isEmpty() || !chooseActor ? -1
                                                         : chooseActor(candidates);
        if (npc >= 0) {
            selectedNpc = npc;
            if (chooseNpc)
                chooseNpc(npc);
            auto selected = std::find_if(actors.begin(), actors.end(),
                                         [=](const Actor &a) { return a.npc == npc; });
            npcOrigin = npcDestination =
                selected == actors.end() ? cell : QPoint(selected->x, selected->y);
            npcGrabOffset = cell - npcOrigin;
            // The modal chooser consumes the release that initiated selection.
            // A subsequent drag moves the chosen actor, without reopening it.
            npcDrag = !chooser;
            update();
        }
        return;
    }

    if (tool != Pencil && tool != Select && tool != Rectangle && tool != Fill)
        return;
    if (!terrainEditingEnabled && tool != Select)
        return;
    if (tool != Select && !editable(cell))
        return;
    if ((tool == Pencil || actorBrushKinds.contains(brush) || specialBrushKinds.contains(brush)) &&
        pencilPlacement && pencilPlacement(cell, brush))
        return;
    before = ids;
    beforeExtended = extended;
    previousSelection = selection;
    start = cell;
    operation = tool == Rectangle ? "Rectangle" : tool == Fill ? "Fill" : "Paint";
    if (tool == Fill) {
        flood(cell);
        finishEdit();
        return;
    }
    stroke = true;
    last = {-1, -1};
    point(e->position(), true);
}
void MapCanvas::mouseMoveEvent(QMouseEvent *e) {
    if (npcDrag) {
        npcDestination = cellAt(e->position());
        if (npcDestination.x() >= 0)
            npcDestination -= npcGrabOffset;
        say(npcDestination.x() >= 0
                ? QString("Move to X %1 · Y %2").arg(npcDestination.x()).arg(npcDestination.y())
                : "Destination outside map");
        update();
        return;
    }
    point(e->position(), stroke && (e->buttons() & Qt::LeftButton));
}
void MapCanvas::mouseReleaseEvent(QMouseEvent *e) {
    if (npcDrag && e->button() == Qt::LeftButton) {
        npcDestination = cellAt(e->position());
        if (npcDestination.x() >= 0)
            npcDestination -= npcGrabOffset;
        npcDrag = false;
        if (npcDestination.x() >= 0 && npcDestination != npcOrigin && moveNpc)
            moveNpc(selectedNpc, npcDestination);
        update();
        return;
    }
    if (!stroke || e->button() != Qt::LeftButton)
        return;
    point(e->position(), true);
    stroke = false;
    last = {-1, -1};
    if (tool == Select) {
        before.clear();
        if (selectionChanged)
            selectionChanged();
    } else
        finishEdit();
}
void MapCanvas::cancelStroke() {
    placeItem = {};
    placementTile = -1;
    unsetCursor();
    npcDrag = false;
    update();
    if (stroke) {
        ids = before;
        extended = beforeExtended;
        if (tool == Select)
            selection = previousSelection;
        before.clear();
        stroke = false;
        last = {-1, -1};
        update();
        if (selectionChanged)
            selectionChanged();
    }
    if (pasting()) {
        stamp.clear();
        update();
        say("Paste cancelled");
    }
}
void MapCanvas::say(const QString &message) {
    if (feedback)
        feedback(message);
}
bool MapCanvas::editable(QPoint cell) const {
    return cell.x() >= 0 && cell.y() >= 0 && cell.x() < side && cell.y() < rowCount() &&
           (selection.isEmpty() || selection.contains(cell));
}
void MapCanvas::finishEdit() {
    auto rollback = before;
    before.clear();
    stroke = false;
    auto rollbackExtended = beforeExtended;
    if (ids == rollback && extended == rollbackExtended)
        return;
    if (!commit || !commit(ids)) {
        ids = rollback;
        extended = rollbackExtended;
    }
    update();
    if (changed)
        changed();
}
void MapCanvas::previewRectangle(QPoint cell) {
    ids = before;
    extended = beforeExtended;
    auto area =
        QRect(QPoint(qMin(start.x(), cell.x()), qMin(start.y(), cell.y())),
              QSize(std::abs(start.x() - cell.x()) + 1, std::abs(start.y() - cell.y()) + 1));
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (editable({x, y}) && (!outline || x == area.left() || x == area.right() ||
                                     y == area.top() || y == area.bottom()))
                setTileId(y * side + x, brush);
    update();
}
void MapCanvas::flood(QPoint cell) {
    unsigned source = tileId(cell.y() * side + cell.x());
    if (source == unsigned(brush))
        return;
    QVector<QPoint> queue{cell};
    setTileId(cell.y() * side + cell.x(), brush);
    for (int i = 0; i < queue.size(); ++i) {
        auto current = queue[i];
        for (auto delta : {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)}) {
            auto next = current + delta;
            if (editable(next) && tileId(next.y() * side + next.x()) == source) {
                setTileId(next.y() * side + next.x(), brush);
                queue.append(next);
            }
        }
    }
}
void MapCanvas::clearSelection() {
    selection = {};
    update();
    if (selectionChanged)
        selectionChanged();
}
bool MapCanvas::copySelection() {
    if (selection.isEmpty()) {
        say("Select terrain before copying");
        return false;
    }
    auto area = selection.intersected(QRect(0, 0, side, rowCount()));
    if (area != selection || ids.size() != side * rowCount())
        return false;
    QByteArray payload = clipboardMagic.toLatin1();
    for (int value : {area.width(), area.height()}) {
        payload.append(char(value & 255));
        payload.append(char(value >> 8));
    }
    for (int y = area.top(); y <= area.bottom(); ++y)
        payload += ids.mid(y * side + area.left(), area.width());
    bool custom = false;
    for (int y = area.top(); y <= area.bottom(); y++)
        for (int x = area.left(); x <= area.right(); x++)
            custom |= tileId(y * side + x) >= 512;
    if (custom) {
        payload = QByteArray("IMPTILE2", 8);
        for (int value : {area.width(), area.height()}) {
            payload.append(char(value));
            payload.append(char(value >> 8));
        }
        payload += libraryUuid;
        for (int y = area.top(); y <= area.bottom(); y++)
            for (int x = area.left(); x <= area.right(); x++) {
                char b[4];
                TYPES_Put32(b, tileId(y * side + x));
                payload.append(b, 4);
            }
    }
    auto mime = new QMimeData;
    mime->setData(clipboardMime, payload);
    QApplication::clipboard()->setMimeData(mime);
    say(QString("Copied %1 × %2 terrain tiles; NPCs and combat records excluded")
            .arg(area.width())
            .arg(area.height()));
    return true;
}
bool MapCanvas::beginPaste() {
    cancelStroke();
    if (!terrainEditingEnabled || tool == InspectNpc) {
        say("Switch to Terrain before pasting");
        return false;
    }
    auto mime = QApplication::clipboard()->mimeData();
    auto payload = mime ? mime->data(clipboardMime) : QByteArray();
    if (payload.size() < 12 ||
        (payload.left(8) != clipboardMagic.toLatin1() && payload.left(8) != "IMPTILE2")) {
        say("Clipboard does not contain Impera terrain tiles");
        return false;
    }
    int width = U5::byte(payload, 8) + 256 * U5::byte(payload, 9);
    int height = U5::byte(payload, 10) + 256 * U5::byte(payload, 11);
    bool custom = payload.left(8) == "IMPTILE2";
    if (width < 1 || height < 1 || width > 256 || height > 256 ||
        payload.size() != (custom ? 28 + width * height * 4 : 12 + width * height) ||
        (custom && payload.mid(12, 16) != libraryUuid)) {
        say("Clipboard terrain dimensions are invalid");
        return false;
    }
    stampSize = {width, height};
    stampExtended.clear();
    if (custom) {
        stampExtended = payload.mid(28);
        stamp = QByteArray(width * height, 0);
        for (int i = 0; i < width * height; i++) {
            unsigned id = TYPES_U32(stampExtended.constData() + i * 4);
            if (id >= 512 && !legacyTiles.contains(id)) {
                say("Clipboard tile definition is missing");
                stampExtended.clear();
                stamp.clear();
                return false;
            }
            stamp[i] = char(id >= 512 ? legacyTiles.value(id) : id);
        }
    } else
        stamp = payload.mid(12);
    update();
    say("Move pointer to preview terrain paste · click to place · Esc cancels");
    return true;
}
bool MapCanvas::pasteFits() const {
    return hover.x() >= 0 && hover.y() >= 0 && hover.x() + stampSize.width() <= side &&
           hover.y() + stampSize.height() <= rowCount();
}
int MapCanvas::pasteChangedCells() const {
    if (!pasting() || !pasteFits())
        return 0;
    int count = 0;
    for (int y = 0; y < stampSize.height(); ++y)
        for (int x = 0; x < stampSize.width(); ++x)
            count += tileId((hover.y() + y) * side + hover.x() + x) !=
                     (stampExtended.isEmpty()
                          ? U5::byte(stamp, y * stampSize.width() + x)
                          : TYPES_U32(stampExtended.constData() + (y * stampSize.width() + x) * 4));
    return count;
}
void MapCanvas::pasteAt(QPoint cell) {
    hover = cell;
    if (!pasteFits()) {
        say("Paste rejected: outside map bounds");
        return;
    }
    before = ids;
    beforeExtended = extended;
    operation = "Paste terrain";
    for (int y = 0; y < stampSize.height(); y++)
        for (int x = 0; x < stampSize.width(); x++) {
            int i = y * stampSize.width() + x;
            setTileId((cell.y() + y) * side + cell.x() + x,
                      stampExtended.isEmpty() ? U5::byte(stamp, i)
                                              : TYPES_U32(stampExtended.constData() + i * 4));
        }
    stamp.clear();
    finishEdit();
}

void MapCanvas::leaveEvent(QEvent *) {
    hover = {-1, -1};
    update();
}
void MapCanvas::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Delete && tool == InspectNpc && !placing() && deleteItem) {
        deleteItem();
        e->accept();
        return;
    }
    if (placing() && (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return)) {
        auto place = placeItem;
        bool repeat = repeatPlacement;
        if (place(keyboardCell) && !repeat) {
            placeItem = {};
            placementTile = -1;
            unsetCursor();
        }
        update();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        bool pending = stroke || npcDrag || pasting() || placing();
        cancelStroke();
        if (!pending)
            clearSelection();
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Copy)) {
        cancelStroke();
        copySelection();
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Paste)) {
        beginPaste();
        e->accept();
        return;
    }
    if (e->modifiers() == Qt::NoModifier || e->modifiers() == Qt::ShiftModifier) {
        QPoint direction;
        if (e->key() == Qt::Key_Left)
            direction = {-1, 0};
        if (e->key() == Qt::Key_Right)
            direction = {1, 0};
        if (e->key() == Qt::Key_Up)
            direction = {0, -1};
        if (e->key() == Qt::Key_Down)
            direction = {0, 1};
        if (!direction.isNull()) {
            QPoint previous = keyboardCell;
            keyboardCell += direction;
            keyboardCell.setX(qBound(0, keyboardCell.x(), side - 1));
            keyboardCell.setY(qBound(0, keyboardCell.y(), rowCount() - 1));
            if (e->modifiers() == Qt::ShiftModifier && tool != InspectNpc) {
                if (selection.isEmpty())
                    selection = QRect(previous, QSize(1, 1));
                selection = selection.united(QRect(keyboardCell, QSize(1, 1)));
                if (selectionChanged)
                    selectionChanged();
            }
            if (inspect)
                inspect(keyboardCell.x(), keyboardCell.y(),
                        tileId(keyboardCell.y() * side + keyboardCell.x()));
            if (auto scroll = parentWidget()
                                  ? dynamic_cast<QScrollArea *>(parentWidget()->parentWidget())
                                  : nullptr)
                scroll->ensureVisible((keyboardCell.x() + 0.5) * 16 * zoom,
                                      (keyboardCell.y() + 0.5) * 16 * zoom, 20, 20);
            update();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (painting())
                cancelStroke();
            if (tool == InspectNpc) {
                if (selectedNpc >= 0 && moveNpc)
                    moveNpc(selectedNpc, keyboardCell);
            } else {
                QPointF position((keyboardCell.x() + 0.5) * 16 * zoom,
                                 (keyboardCell.y() + 0.5) * 16 * zoom);
                QMouseEvent press(QEvent::MouseButtonPress, position, position, Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                mousePressEvent(&press);
                QMouseEvent release(QEvent::MouseButtonRelease, position, position, Qt::LeftButton,
                                    Qt::NoButton, Qt::NoModifier);
                mouseReleaseEvent(&release);
            }
            e->accept();
            return;
        }
    }
    const QMap<int, int> shortcuts{{Qt::Key_B, Pencil},
                                   {Qt::Key_I, Eyedropper},
                                   {Qt::Key_V, Select},
                                   {Qt::Key_R, Rectangle},
                                   {Qt::Key_F, Fill}};
    if (e->modifiers() == Qt::NoModifier && shortcuts.contains(e->key())) {
        cancelStroke();
        if (chooseTool)
            chooseTool(shortcuts[e->key()]);
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}
void MapCanvas::focusOutEvent(QFocusEvent *e) {
    cancelStroke();
    QWidget::focusOutEvent(e);
}
bool MapCanvas::event(QEvent *e) {
    if (e->type() == QEvent::UngrabMouse || e->type() == QEvent::WindowDeactivate)
        cancelStroke();
    return QWidget::event(e);
}

MapView::MapView(MapCanvas *c) : canvas(c) {
    setObjectName("canvasView");
    setWidget(canvas);
    setAlignment(Qt::AlignCenter);
    viewport()->installEventFilter(this);
    canvas->installEventFilter(this);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (changed)
            changed();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (changed)
            changed();
    });
}
QPointF MapView::mapCenter() const {
    auto p = canvas->mapFrom(viewport(), viewport()->rect().center());
    return QPointF(p) / (16 * canvas->zoom);
}
void MapView::centerMap(QPointF cell) {
    horizontalScrollBar()->setValue(
        qRound(cell.x() * 16 * canvas->zoom - viewport()->rect().center().x()));
    verticalScrollBar()->setValue(
        qRound(cell.y() * 16 * canvas->zoom - viewport()->rect().center().y()));
}
void MapView::setZoom(double scale, QPoint anchor, bool fit) {
    canvas->cancelStroke();
    if (anchor.x() < 0)
        anchor = viewport()->rect().center();
    QPointF at = QPointF(canvas->mapFrom(viewport(), anchor)) / (16 * canvas->zoom);
    fitting = fit;
    canvas->zoom = qBound(fit ? 0.001 : 0.02, scale, 8.0);
    canvas->resizeMap();
    horizontalScrollBar()->setValue(qRound(at.x() * 16 * canvas->zoom - anchor.x()));
    verticalScrollBar()->setValue(qRound(at.y() * 16 * canvas->zoom - anchor.y()));
    if (changed)
        changed();
}
void MapView::fitMap() {
    auto s = viewport()->size() - QSize(4, 4);
    setZoom(
        qMin(s.width() / double(canvas->side * 16), s.height() / double(canvas->rowCount() * 16)),
        {-1, -1}, true);
}
void MapView::resizeEvent(QResizeEvent *e) {
    // Qt has already resized the viewport when it forwards this event. Recover
    // its previous center so opening the library or rebuilding an editor does
    // not move the map when scrollbars/layout change the available space.
    auto center = mapCenter();
    if (e->oldSize().isValid())
        center -=
            QPointF(QRect(QPoint(), e->size()).center() - QRect(QPoint(), e->oldSize()).center()) /
            (16 * canvas->zoom);
    QScrollArea::resizeEvent(e);
    if (fitting)
        fitMap();
    else if (e->oldSize().isValid())
        centerMap(center);
}
bool MapView::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space) {
            if (!key->isAutoRepeat())
                space = event->type() == QEvent::KeyPress;
            canvas->setCursor(space ? Qt::OpenHandCursor : Qt::CrossCursor);
            return true;
        }
        if (event->type() == QEvent::KeyPress &&
            (key->key() == Qt::Key_Plus || key->key() == Qt::Key_Equal ||
             key->key() == Qt::Key_Minus)) {
            setZoom(canvas->zoom * (key->key() == Qt::Key_Minus ? 0.5 : 2));
            return true;
        }
        if (key->key() == Qt::Key_Escape && panning) {
            panning = false;
            canvas->setCursor(Qt::CrossCursor);
            return true;
        }
    }
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate) {
        space = false;
        panning = false;
        canvas->setCursor(Qt::CrossCursor);
    }
    if (event->type() == QEvent::Wheel) {
        auto wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() & Qt::ControlModifier) {
            QPoint anchor = watched == canvas
                                ? canvas->mapTo(viewport(), wheel->position().toPoint())
                                : wheel->position().toPoint();
            int delta = wheel->angleDelta().y();
            if (delta)
                setZoom(canvas->zoom * (delta > 0 ? 2 : 0.5), anchor);
            return true;
        }
        if (wheel->modifiers() & Qt::ShiftModifier) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() -
                                            wheel->angleDelta().y());
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto mouse = static_cast<QMouseEvent *>(event);
        // QScrollArea tries to reveal its focused child. For a map-sized child
        // that can recenter the entire map; focusing must preserve the viewport.
        int horizontal = horizontalScrollBar()->value(), vertical = verticalScrollBar()->value();
        canvas->setFocus(Qt::MouseFocusReason);
        horizontalScrollBar()->setValue(horizontal);
        verticalScrollBar()->setValue(vertical);
        if (mouse->button() == Qt::MiddleButton ||
            (mouse->button() == Qt::LeftButton && (space || canvas->tool == 2))) {
            canvas->cancelStroke();
            canvas->setFocus(Qt::MouseFocusReason);
            panning = true;
            panAt = mouse->globalPosition().toPoint();
            canvas->setCursor(Qt::ClosedHandCursor);
            return true;
        }
    }
    if (event->type() == QEvent::MouseMove && panning) {
        auto mouse = static_cast<QMouseEvent *>(event);
        auto now = mouse->globalPosition().toPoint();
        auto delta = now - panAt;
        panAt = now;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && panning) {
        panning = false;
        canvas->setCursor(space || canvas->tool == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
        return true;
    }
    return QScrollArea::eventFilter(watched, event);
}

MapMinimap::MapMinimap(MapCanvas *c, MapView *v) : canvas(c), view(v) {
    setObjectName("mapMinimap");
    setMinimumSize(100, 100);
    setMaximumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip("Click or drag to navigate; outline shows the visible map area");
}
QRectF MapMinimap::imageRect() const {
    QSizeF size(canvas->side, canvas->rowCount());
    size.scale(QSizeF(qMax(1, width() - 8), qMax(1, height() - 8)), Qt::KeepAspectRatio);
    return {(width() - size.width()) / 2, (height() - size.height()) / 2, size.width(),
            size.height()};
}
void MapMinimap::refresh() {
    cache = QImage(canvas->side * 4, canvas->rowCount() * 4, QImage::Format_RGB32);
    cache.fill(Qt::black);
    QPainter painter(&cache);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int y = 0; y < canvas->rowCount(); ++y)
        for (int x = 0; x < canvas->side; ++x) {
            int offset = y * canvas->side + x;
            if (offset >= canvas->ids.size())
                continue;
            int id = canvas->tileId(offset);
            if (id < canvas->tiles.size())
                painter.drawImage(QRect(x * 4, y * 4, 4, 4), canvas->tileImageAt(offset));
            if (canvas->comparison && canvas->original.size() == canvas->ids.size() &&
                canvas->original[offset] != canvas->ids[offset])
                painter.fillRect(QRect(x * 4, y * 4, 4, 4), QColor("#ffae42"));
        }
    update();
}
void MapMinimap::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    auto area = imageRect();
    painter.fillRect(rect(), QColor("#141b27"));
    painter.drawImage(area, cache);
    double scale = area.width() / canvas->side;
    QPointF top = QPointF(canvas->mapFrom(view->viewport(), QPoint(0, 0))) / (16 * canvas->zoom);
    QSizeF visible = QSizeF(view->viewport()->size()) / (16 * canvas->zoom);
    auto region = QRectF(top, visible).intersected(QRectF(0, 0, canvas->side, canvas->rowCount()));
    painter.setPen(QPen(QColor("#ffffff"), 1));
    painter.drawRect(QRectF(area.topLeft() + region.topLeft() * scale, region.size() * scale));
    if (!canvas->selection.isEmpty()) {
        painter.setPen(QPen(QColor("#72caff"), 1));
        painter.drawRect(QRectF(area.topLeft() + QPointF(canvas->selection.topLeft()) * scale,
                                QSizeF(canvas->selection.size()) * scale));
    }
}
void MapMinimap::navigate(QPointF position) {
    auto area = imageRect();
    if (area.contains(position))
        view->centerMap((position - area.topLeft()) * (canvas->side / area.width()));
}
void MapMinimap::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton)
        navigate(event->position());
}
void MapMinimap::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton)
        navigate(event->position());
}

MapWorkspace::MapWorkspace(
    Project *p, const QString &r, QMap<QString, MapViewState> *s, QMap<QString, int> *n,
    std::function<bool(const QMap<QString, QByteArray> &, const QString &)> commit,
    std::function<void(const QString &)> navigate, QWidget *parent)
    : QWidget(parent), project(p), resource(r), document(p, r), states(s), navigation(n),
      commitResources(commit), navigateResource(navigate) {
    setObjectName("mapWorkspace");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    auto split = new QSplitter;
    split->setObjectName("mapPanelSplitter");
    layout->addWidget(split, 1);
    auto left = new QWidget;
    auto leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto search = new QLineEdit;
    search->setToolTip("Filter the map list by location or resource name.");
    search->setObjectName("mapSearch");
    search->setPlaceholderText("Find a map…");
    leftLayout->addWidget(search);
    maps = new QTreeWidget;
    maps->setToolTip("Choose a world map, settlement floor, or combat map to edit.");
    maps->setObjectName("mapNavigation");
    maps->setHeaderLabel("Maps");
    leftLayout->addWidget(maps);
    auto world = new QTreeWidgetItem(maps, {"World"});
    auto towns = new QTreeWidgetItem(maps, {"Locations"});
    auto combat = new QTreeWidgetItem(maps, {"Combat maps"});
    for (auto name : project->resources.keys())
        if (MapDocument::supported(name)) {
            auto pages = project->mapPages(name);
            QMap<QString, QTreeWidgetItem *> locations;
            QTreeWidgetItem *combatGroup = nullptr;
            if (name.endsWith(".CBT"))
                combatGroup = new QTreeWidgetItem(combat, {name});
            for (int i = 0; i < pages.size(); ++i) {
                auto mp = pages[i];
                QTreeWidgetItem *parentItem = world;
                QString label = mp.name;
                if (mp.settlement >= 0) {
                    auto location = mp.name.section(" — ", 0, 0);
                    if (!locations.contains(location))
                        locations[location] = new QTreeWidgetItem(towns, {location});
                    parentItem = locations[location];
                    const auto previous = parentItem->data(0, Qt::UserRole + 1);
                    if (!previous.isValid() ||
                        std::abs(mp.floor) < std::abs(pages[previous.toInt()].floor)) {
                        parentItem->setData(0, Qt::UserRole, name);
                        parentItem->setData(0, Qt::UserRole + 1, i);
                        parentItem->setToolTip(0, "Open the main floor of " + location);
                    }
                    label = mp.floor < 0    ? "Basement"
                            : mp.floor == 0 ? "Ground floor"
                                            : QString("Upper floor %1").arg(mp.floor);
                } else if (combatGroup)
                    parentItem = combatGroup;
                auto item = new QTreeWidgetItem(parentItem, {label});
                item->setData(0, Qt::UserRole, name);
                item->setData(0, Qt::UserRole + 1, i);
                item->setToolTip(
                    0, QString("%1 · %2 · floor %3").arg(name).arg(mp.name).arg(mp.floor));
            }
        }
    towns->sortChildren(0, Qt::AscendingOrder);
    maps->expandToDepth(0);
    left->setMinimumWidth(170);
    split->addWidget(left);
    auto center = new QWidget;
    auto centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    auto row = new QHBoxLayout;
    row->setObjectName("mapLocationRow");
    auto locationLabel = new QLabel(this);
    locationLabel->setObjectName("mapLocationLabel");
    locationLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    row->addWidget(locationLabel);
    if (!resource.endsWith(".CBT")) {
        auto rename = new QToolButton;
        rename->setObjectName("renameLocation");
        rename->setIcon(MapCanvas::toolIcon(MapCanvas::Pencil));
        rename->setToolTip("Rename location");
        row->addWidget(rename);
        connect(rename, &QToolButton::clicked, this, [this] {
            if (!flushLocationMode())
                return;
            const int id = resource == "UNDER.DAT"
                               ? 41
                               : Workshop::positionMap(resource, document.pages[currentPage()]);
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, "Rename location", "Name", QLineEdit::Normal, project->locationName(id), &ok);
            if (!ok || name.trimmed() == project->locationName(id))
                return;
            try {
                QMap<QString, QByteArray> changes;
                if (MAPS_NewLocation(id)) {
                    auto maps = Workshop::extendedMaps(*project);
                    for (auto &m : maps)
                        if (m.id == unsigned(id) && m.kind == 1)
                            m.name = name.trimmed();
                    changes["MAPS.OVR"] = Workshop::writeExtendedMaps(maps);
                } else
                    changes["LOCATIONS.OVR"] = project->renameLocation(id, name);
                if (commitResources(changes, "Rename location")) {
                    document = MapDocument(project, resource);
                    populateFloors(currentPage());
                    if (locationChanged)
                        locationChanged();
                }
            } catch (const std::exception &error) {
                QMessageBox::warning(this, "Cannot rename location",
                                     QString::fromUtf8(error.what()));
            }
        });
    }

    page = new QComboBox;
    page->setToolTip("Switch floors within the current location. Choose another location in the "
                     "map list; edits and view state are preserved.");
    page->setObjectName("mapPage");
    populateFloors(qBound(0, navigation->value(resource + "/map"), int(document.pages.size() - 1)));
    page->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    row->addWidget(page);
    auto resize = new QToolButton;
    resize->setObjectName("resizeMap");
    resize->setText("Resize…");
    resize->setToolTip("Change map width and height; existing cells keep their coordinates.");
    row->addWidget(resize);
    connect(resize, &QToolButton::clicked, this, [this] {
        if (!flushLocationMode())
            return;
        const auto mp = document.pages[currentPage()];
        QDialog dialog(window());
        QPointer<MapWorkspace> owner(this);
        dialog.setWindowTitle("Resize map");
        QFormLayout form(&dialog);
        QSpinBox width, height;
        width.setObjectName("mapWidth");
        height.setObjectName("mapHeight");
        width.setRange(1, 256);
        height.setRange(1, 256);
        width.setValue(mp.side);
        height.setValue(mp.height());
        QSpinBox fill;
        fill.setObjectName("mapFill");
        fill.setRange(0, 255);
        fill.setValue(canvas->brush < 256 ? canvas->brush : 5);
        fill.setToolTip("Terrain tile used for newly added cells.");
        form.addRow("Width (tiles)", &width);
        form.addRow("Height (tiles)", &height);
        form.addRow("Fill tile", &fill);
        QLabel crop;
        form.addRow(&crop);
        auto preview = [&] {
            int n = mp.side * mp.height() -
                    qMin(mp.side, width.value()) * qMin(mp.height(), height.value());
            crop.setText(n ? QString("%1 terrain cells will be cropped.").arg(n) : QString());
        };
        connect(&width, &QSpinBox::valueChanged, &dialog, preview);
        connect(&height, &QSpinBox::valueChanged, &dialog, preview);
        preview();
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted || !owner)
            return;
        try {
            if (commitResources(Workshop::resizeMap(*project, resource, currentPage(),
                                                    width.value(), height.value(), fill.value()),
                                "Resize map")) {
                document = MapDocument(project, resource);
                loadPage();
                if (locationChanged)
                    locationChanged();
            }
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Cannot resize map", e.what());
        }
    });

    auto exportButton = new QPushButton("Export");
    exportButton->setToolTip("Export terrain, tile IDs, or the visible preview as a PNG.");
    auto exportMenu = new QMenu(exportButton);
    exportMenu->addAction("Terrain PNG", this, [this] { exportImage(false); })
        ->setToolTip("Export map terrain artwork without grid or character overlays.");
    exportMenu->addAction("Tile-ID PNG", this, [this] { exportImage(true); })
        ->setToolTip(
            "Export one grayscale pixel per tile containing its ID, for lossless terrain import.");
    exportMenu->setToolTipsVisible(true);
    auto previewExport = exportMenu->addAction("Visible preview PNG (includes overlays)", this,
                                               [this] { exportImage(false, true); });
    previewExport->setToolTip("Export the visible canvas, including grid, markers and trigger "
                              "preview. Not suitable for terrain import.");
    exportButton->setMenu(exportMenu);
    exportButton->setObjectName("mapExport");
    row->addWidget(exportButton);
    centerLayout->addLayout(row);
    auto tools = new QHBoxLayout;
    tools->setObjectName("mapToolRow");
    tool = new QButtonGroup(this);
    tool->setObjectName("mapTools");
    QString companion = resource.left(resource.size() - 4) + ".NPC";
    const QStringList names{"Pencil", "Eyedropper", "Pan", "Select / Move",
                            "Select", "Rectangle",  "Fill"};
    const QStringList descriptions{
        "Pencil (B): paint tiles with the current brush.",
        "Eyedropper (I): pick a tile from the map as your brush.",
        "Pan: drag to move the view without changing the map. Space or middle-drag also pans.",
        "NPCs: select and move NPC destinations or encounter markers.",
        "Select (V): drag a rectangle to copy or replace terrain.",
        "Rectangle (R): draw a filled rectangle with the current brush.",
        "Fill (F): replace connected tiles of the same kind with the current brush."};
    for (int i : {MapCanvas::InspectNpc, MapCanvas::Pencil, MapCanvas::Eyedropper,
                  MapCanvas::Select, MapCanvas::Rectangle, MapCanvas::Fill, MapCanvas::Pan}) {
        auto button = new QToolButton;
        button->setObjectName(QString("mapTool%1").arg(i));
        button->setAccessibleName(names[i]);
        button->setToolTip(descriptions[i]);
        button->setCheckable(true);
        button->setFixedSize(28, 28);
        button->setStyleSheet("QToolButton:checked { background:#365f8d; border:2px solid #86b9ed; "
                              "border-radius:3px; }");
        button->setIconSize({22, 22});
        button->setIcon(MapCanvas::toolIcon(i));
        tool->addButton(button, i);
        if (i == MapCanvas::InspectNpc && !project->resources.contains(companion) &&
            !resource.endsWith(".CBT")) {
            button->setToolTip("Select / Move: inspect and move map items.");
        }
        tools->addWidget(button);
    }
    schedule = new QComboBox;
    schedule->setObjectName("mapSchedule");
    schedule->setAccessibleName("NPC schedule change");
    schedule->addItems({"1", "2", "3", "4"});
    schedule->setToolTip("Choose which scheduled NPC destinations to show and edit. Hours appear "
                         "when NPCs on this map agree; otherwise changes are numbered 1–4. Changes "
                         "2 and 4 share destinations and behavior.");
    schedule->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    if (project->resources.contains(companion)) {
        auto scheduleLabel = new QLabel("NPC schedule:");
        scheduleLabel->setObjectName("mapScheduleLabel");
        scheduleLabel->setBuddy(schedule);
        scheduleLabel->setToolTip(schedule->toolTip());
        row->insertWidget(2, scheduleLabel);
        row->insertWidget(3, schedule);
    } else
        schedule->hide();
    tools->addStretch();
    centerLayout->addLayout(tools);
    zoom = new QComboBox;
    zoom->setObjectName("mapZoom");
    zoom->addItems({"Fit", "100%", "200%", "300%", "400%", "800%"});
    zoom->setToolTip(
        "Magnify tiles, or fit the whole map in the view. Ctrl+mouse wheel also zooms.");
    auto commandIcon = [](int command) {
        QPixmap image(24, 24);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setPen(QPen(QColor("#e0e8f0"), 2));
        if (command == 0) {
            painter.drawRect(3, 3, 12, 14);
            painter.drawRect(8, 7, 12, 14);
        } else if (command == 1) {
            painter.drawRect(5, 5, 14, 17);
            painter.drawRect(9, 2, 6, 5);
            painter.drawLine(8, 12, 16, 12);
            painter.drawLine(8, 17, 16, 17);
        } else {
            painter.setPen(QPen(QColor("#e0e8f0"), 2, Qt::DashLine));
            painter.drawRect(3, 3, 18, 18);
            painter.setPen(QPen(QColor("#e0e8f0"), 2));
            painter.drawLine(8, 8, 16, 16);
            painter.drawLine(8, 16, 16, 8);
        }
        return QIcon(image);
    };
    auto copy = new QToolButton;
    copy->setAccessibleName("Copy terrain");
    copy->setIcon(commandIcon(0));
    copy->setToolTip("Copy the selected rectangle of terrain (Ctrl+C).");
    copy->setObjectName("mapCopy");
    auto paste = new QToolButton;
    paste->setAccessibleName("Paste terrain");
    paste->setIcon(commandIcon(1));
    paste->setToolTip("Place copied terrain on the map; click to apply, Esc to cancel (Ctrl+V).");
    paste->setObjectName("mapPaste");
    auto clear = new QToolButton;
    clear->setAccessibleName("Clear selection");
    clear->setIcon(commandIcon(2));
    clear->setToolTip("Remove the selection boundary without deleting terrain.");
    clear->setObjectName("mapClearSelection");
    auto separator = new QFrame;
    separator->setFrameShape(QFrame::VLine);
    tools->insertWidget(tools->count() - 1, separator);
    for (auto command : {copy, paste, clear}) {
        command->setFixedSize(28, 28);
        command->setIconSize({22, 22});
    }
    tools->insertWidget(tools->count() - 1, copy);
    tools->insertWidget(tools->count() - 1, paste);
    tools->insertWidget(tools->count() - 1, clear);
    comparison = new QCheckBox("Highlight changes", this);
    comparison->setObjectName("mapComparison");
    comparison->setToolTip("Orange marks terrain that differs from original game files");
    comparison->hide();
    row->insertStretch(row->count() - 1);
    row->insertWidget(row->count() - 1, zoom);
    canvas = new MapCanvas;
    // Classify only decoded terrain cells; unused chunks and record metadata are excluded.
    for (const QString &name : project->resources.keys()) {
        if (!MapDocument::supported(name))
            continue;
        MapDocument map(project, name);
        for (int page = 0; page < map.pages.size(); ++page) {
            QSet<int> *membership = nullptr;
            if (name == "BRIT.DAT" || name == "UNDER.DAT")
                membership = &worldTiles;
            else if (map.pages[page].settlement >= 0)
                membership = &locationTiles;
            if (membership)
                for (unsigned char id : map.terrain(page))
                    membership->insert(id);
        }
    }
    canvas->tiles = U5::readGraphics("TILES.16", project->data("TILES.16")).images;
    view = new MapView(canvas);
    centerLayout->addWidget(view, 1);
    auto coordinates = new QHBoxLayout;
    coordinate = new QLabel;
    coordinate->setToolTip("Map coordinates and terrain tile under the pointer.");
    coordinate->setObjectName("mapCoordinates");
    coordinates->addWidget(coordinate, 1);
    auto x = new QSpinBox;
    x->setToolTip("Horizontal tile coordinate to center on; zero is the left edge.");
    auto y = new QSpinBox;
    y->setToolTip("Vertical tile coordinate to center on; zero is the top edge.");
    x->setObjectName("mapGoX");
    y->setObjectName("mapGoY");
    coordinates->addWidget(new QLabel("X"));
    coordinates->addWidget(x);
    coordinates->addWidget(new QLabel("Y"));
    coordinates->addWidget(y);
    auto go = new QPushButton("Go");
    go->setToolTip("Center the map on these coordinates without changing any tiles.");
    coordinates->addWidget(go);
    connect(go, &QPushButton::clicked, this, [=] {
        view->centerMap({double(x->value()) + 0.5, double(y->value()) + 0.5});
        canvas->setFocus();
    });
    centerLayout->addLayout(coordinates);
    split->addWidget(center);
    auto right = new QWidget;
    auto rightOuter = new QVBoxLayout(right);
    rightOuter->setContentsMargins(0, 0, 0, 0);
    inspector = new QStackedWidget;
    rightOuter->addWidget(inspector);
    auto terrainInspector = new QWidget;
    inspector->addWidget(terrainInspector);
    auto rightLayout = new QVBoxLayout(terrainInspector);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    brushImage = new QLabel;
    brushImage->setToolTip(
        "Current terrain brush artwork. Select a palette tile or use the Eyedropper to change it.");
    brushImage->setFixedHeight(70);
    brushImage->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(brushImage);
    auto editDefinition = new QPushButton("Edit tile…");
    editDefinition->setObjectName("mapEditTileDefinition");
    editDefinition->setToolTip("Edit this tile’s shared artwork and properties");
    rightLayout->addWidget(editDefinition);
    connect(editDefinition, &QPushButton::clicked, this, [this] {
        if (editTileType)
            editTileType(canvas->brush);
    });
    brushLabel = new QLabel;
    brushLabel->setToolTip("Name and ID of the tile that painting tools will place.");
    brushLabel->setObjectName("mapBrushLabel");
    brushLabel->setAlignment(Qt::AlignCenter);
    brushLabel->setWordWrap(true);
    rightLayout->addWidget(brushLabel);
    selectionLabel = new QLabel;
    selectionLabel->setToolTip(
        "Selected rectangle and its size; selection itself does not change terrain.");
    selectionLabel->setObjectName("mapSelectionInfo");
    selectionLabel->setWordWrap(true);
    rightLayout->addWidget(selectionLabel);
    minimap = new MapMinimap(canvas, view);
    minimap->setObjectName("mapMinimap");
    minimap->setToolTip("Click or drag to move the main map view.");
    rightLayout->addWidget(minimap);
    auto brushControls = new QHBoxLayout;
    paletteFilter = new QComboBox;
    paletteFilter->setToolTip("Filter tiles by category.");
    paletteFilter->setObjectName("mapPaletteFilter");
    paletteFilter->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    paletteFilter->setMinimumContentsLength(8);
    paletteFilter->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    paletteFilter->addItems({"All tiles", "World", "Locations", "NPCs", "Monsters", "Special"});
    const QStringList categoryHelp{"Every tile and placeable item.",
                                   "World terrain and location entrance tiles.",
                                   "Tiles found on location maps, including every floor.",
                                   "NPC types. Scheduled NPCs can be placed on location maps.",
                                   "Monster types.",
                                   "Signs, containers, transport, items and other map features."};
    for (int i = 0; i < categoryHelp.size(); ++i)
        paletteFilter->setItemData(i, categoryHelp[i], Qt::ToolTipRole);
    brushControls->addWidget(paletteFilter, 1);
    rightLayout->addLayout(brushControls);
    tileSearch = new QLineEdit;
    tileSearch->setToolTip("Find tiles by name, decimal number, or hexadecimal ID.");
    tileSearch->setPlaceholderText("Find tile by name or number");
    tileSearch->setObjectName("mapTileSearch");
    tileSearch->setAccessibleName("Find terrain tile");
    paletteFilter->setAccessibleName("Tile category");
    rightLayout->addWidget(tileSearch);
    palette = new QListWidget;
    palette->setToolTip("Select terrain or an item to place on the map.");
    palette->setObjectName("mapPalette");
    palette->setViewMode(QListView::IconMode);
    palette->setResizeMode(QListView::Adjust);
    palette->setIconSize({32, 32});
    palette->setGridSize({40, 40});
    palette->setSpacing(0);
    for (int i = 0; i < 256; ++i) {
        auto item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(canvas->tiles[i])
                      .scaled(32, 32, Qt::KeepAspectRatio, Qt::FastTransformation)),
            QString(), palette);
        item->setData(Qt::AccessibleTextRole,
                      QString("%1 · Tile %2").arg(MapDocument::tileName(i)).arg(i));
        item->setToolTip(QString("%1\n%2 · Tile %3 (0x%4)\nDescription from DOS artwork and engine "
                                 "definitions; appearance may differ with custom artwork.")
                             .arg(MapDocument::tileName(i))
                             .arg(tileCategories(i).join(" · "))
                             .arg(i)
                             .arg(i, 2, 16, QChar('0')));
    }
    for (int i = 0; i < 256; i++)
        palette->item(i)->setData(Qt::UserRole, i);

    auto customRecords = CustomTiles::records(*project);
    for (auto it = customRecords.begin(); it != customRecords.end(); ++it) {
        auto d = CustomTiles::definition(*project, it.key());
        if (canvas->tiles.size() <= int(d.id))
            canvas->tiles.resize(d.id + 1);
        const bool world = resource == "BRIT.DAT" || resource == "UNDER.DAT";
        canvas->tiles[d.id] = CustomTiles::image(*project, d.id, world ? 6 : 0);
        if (d.kind)
            canvas->actorFootprints[d.id] =
                QSize(world ? 1 : TYPES_Width(&d), world ? 1 : TYPES_Height(&d));
        if (d.kind)
            continue;
        canvas->legacyTiles[d.id] = d.base;
        if (d.render & TYPES_SPRITE)
            canvas->spriteGround[d.id] = d.ground;
        if (d.id < 256) {
            palette->item(d.id)->setIcon(
                QIcon(QPixmap::fromImage(canvas->tiles[d.id])
                          .scaled(32, 32, Qt::KeepAspectRatio, Qt::FastTransformation)));
            palette->item(d.id)->setToolTip(QString::fromUtf8(d.name));
        }
        if (d.id >= 512) {
            auto item = new QListWidgetItem(
                QIcon(QPixmap::fromImage(canvas->tiles[d.id])
                          .scaled(32, 32, Qt::KeepAspectRatio, Qt::FastTransformation)),
                QString(), palette);
            item->setData(Qt::UserRole, d.id);
            item->setToolTip(QString::fromUtf8(d.name));
        }
    }
    const auto supportedItems = Workshop::availableMapItems(*project, resource);
    auto actorEntry = [&](int id, int kind, const QString &name) {
        const bool supported = supportedItems.contains(kind == 1 ? Workshop::MapItemKind::Npc
                                                                 : Workshop::MapItemKind::Monster);
        canvas->actorBrushKinds[id] = kind;
        auto item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(canvas->tiles.value(id))
                      .scaled(32, 32, Qt::KeepAspectRatio, Qt::FastTransformation)),
            QString(), palette);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, name);
        item->setData(Qt::AccessibleTextRole, name);
        item->setToolTip(name + (kind == 1 ? " · NPC" : " · Monster") +
                         (supported   ? QString()
                          : kind == 1 ? "\nPlace scheduled NPCs on a location map."
                                      : "\nMonsters cannot be placed on this map."));
        if (!supported)
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    };
    for (int shape = 64; shape < 128; shape += 4)
        if (Workshop::actorName(shape) != "x")
            actorEntry(256 + shape, 1, Workshop::actorName(shape));
    if (resource == "BRIT.CBT")
        actorEntry(400, 2, "Encounter monster");
    else
        for (int shape : Workshop::monsterShapes())
            actorEntry(256 + shape, 2, Workshop::actorName(shape));
    for (auto record : customRecords) {
        TypeDef d;
        TYPES_Decode(reinterpret_cast<const unsigned char *>(record.constData()), &d);
        if (d.id >= 512 && d.kind)
            actorEntry(d.id, d.kind, QString::fromUtf8(d.name));
    }
    auto specialItems = supportedItems;
    std::sort(specialItems.begin(), specialItems.end(), [](auto a, auto b) {
        return Workshop::mapItemName(a).compare(Workshop::mapItemName(b), Qt::CaseInsensitive) < 0;
    });
    for (auto kind : specialItems) {
        if (kind == Workshop::MapItemKind::Npc || kind == Workshop::MapItemKind::Monster)
            continue;
        int id = 65536 + int(kind);
        int art = kind == Workshop::MapItemKind::Sign             ? 0xf8
                  : kind == Workshop::MapItemKind::Container      ? 257
                  : kind == Workshop::MapItemKind::HiddenContents ? 0x5d
                  : kind == Workshop::MapItemKind::Trigger        ? 0x45
                                                           : 256 + Workshop::mapItemShape(kind);
        auto image =
            canvas->tiles.value(art).scaled(32, 32, Qt::KeepAspectRatio, Qt::FastTransformation);
        if (kind == Workshop::MapItemKind::HiddenContents) {
            image = QImage(32, 32, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setPen(QPen(QColor("#72caff"), 2));
            painter.setBrush(QColor("#365f8d"));
            painter.drawRect(2, 4, 28, 24);
            QFont font = painter.font();
            font.setPixelSize(20);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(Qt::white);
            painter.drawText(image.rect(), Qt::AlignCenter, "C");
        }
        if (kind == Workshop::MapItemKind::Trigger) {
            image = image.convertToFormat(QImage::Format_ARGB32);
            QPainter painter(&image);
            painter.setPen(Qt::red);
            painter.drawRect(1, 1, 29, 29);
        }
        if (canvas->tiles.size() <= id)
            canvas->tiles.resize(id + 1);
        canvas->tiles[id] = image;
        canvas->specialBrushKinds[id] = int(kind);
        auto name = Workshop::mapItemName(kind);
        auto item = new QListWidgetItem(QIcon(QPixmap::fromImage(image)), QString(), palette);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, name);
        item->setData(Qt::AccessibleTextRole, name);
        item->setToolTip(name + " · Special");
    }
    rightLayout->addWidget(palette, 1);
    npcResource = companion;
    talkResource = resource.left(resource.size() - 4) + ".TLK";
    auto npcPanel = new QScrollArea;
    npcPanel->setObjectName("mapCharacterInspector");
    npcPanel->setWidgetResizable(true);
    auto npcContent = new QWidget;
    auto npcLayout = new QVBoxLayout(npcContent);
    if (resource.endsWith(".CBT"))
        createCombatInspector(npcLayout);
    else
        createNpcInspector(npcLayout);
    npcPanel->setWidget(npcContent);
    inspector->addWidget(npcPanel);
    const int inspectorWidth = npcContent->minimumSizeHint().width() + npcPanel->frameWidth() * 2 +
                               npcPanel->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    right->setMinimumWidth(qMax(220, inspectorWidth));
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setSizes({190, 780, 220});
    if (states->contains("map/editor")) {
        const auto shared = states->value("map/editor");
        paletteFilter->setCurrentIndex(qBound(0, shared.paletteFilter, paletteFilter->count() - 1));
        tileSearch->setText(shared.tileSearch);
    }
    connect(tileSearch, &QLineEdit::textChanged, this, [this] {
        (*states)["map/editor"].tileSearch = tileSearch->text();
        filterPalette();
    });
    connect(paletteFilter, &QComboBox::currentIndexChanged, this, [this] {
        (*states)["map/editor"].paletteFilter = paletteFilter->currentIndex();
        filterPalette();
    });
    connect(copy, &QToolButton::clicked, this, [this] { canvas->copySelection(); });
    connect(paste, &QToolButton::clicked, this, [this] {
        canvas->setFocus();
        canvas->beginPaste();
    });
    connect(clear, &QToolButton::clicked, this, [this] { canvas->clearSelection(); });
    connect(comparison, &QCheckBox::toggled, this, [this](bool enabled) {
        canvas->comparison = enabled;
        canvas->update();
        minimap->refresh();
        saveView();
    });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
            [this] { updateSelection(); });
    canvas->selectionChanged = [this] {
        updateSelection();
        minimap->update();
        saveView();
    };
    canvas->feedback = [this](const QString &message) { coordinate->setText(message); };
    canvas->changed = [this] {
        minimap->refresh();
        updateSelection();
    };
    connect(search, &QLineEdit::textChanged, this, [=](const QString &text) {
        std::function<bool(QTreeWidgetItem *, bool)> filter = [&](QTreeWidgetItem *item,
                                                                  bool parentMatch) {
            bool match = parentMatch || item->text(0).contains(text, Qt::CaseInsensitive) ||
                         item->data(0, Qt::UserRole).toString().contains(text, Qt::CaseInsensitive);
            bool child = false;
            for (int i = 0; i < item->childCount(); ++i)
                child = filter(item->child(i), match) || child;
            item->setHidden(!match && !child);
            if (!text.isEmpty() && child)
                item->setExpanded(true);
            return match || child;
        };
        for (int i = 0; i < maps->topLevelItemCount(); ++i)
            filter(maps->topLevelItem(i), false);
    });
    connect(maps, &QTreeWidget::currentItemChanged, this, [=](QTreeWidgetItem *item) {
        if (restoring || !item)
            return;
        auto name = item->data(0, Qt::UserRole).toString();
        if (name.isEmpty())
            return;
        int index = item->data(0, Qt::UserRole + 1).toInt();
        if (name == resource)
            selectPage(index);
        else {
            cancelGesture();
            saveView();
            (*navigation)[name + "/map"] = index;
            navigate(name);
        }
    });
    connect(page, &QComboBox::currentIndexChanged, this, [this] {
        if (page->currentIndex() >= 0)
            selectPage(currentPage());
    });
    connect(tool, &QButtonGroup::idClicked, this, [=](int i) {
        if (locationModes && i != MapCanvas::Pan) {
            int target = activeMode;
            if (i != MapCanvas::InspectNpc)
                target = 0;
            else if (activeMode == 0) {
                for (int index = 1; index < locationModes->count(); ++index)
                    if (locationModes->tabText(index) == "NPCs / Schedules" ||
                        locationModes->tabText(index) == "Encounter" ||
                        locationModes->tabText(index) == "Signs") {
                        target = index;
                        break;
                    }
            }
            if (target != activeMode) {
                locationModes->setCurrentIndex(target);
                if (activeMode != target)
                    return;
            }
            if (i != MapCanvas::InspectNpc)
                terrainTool = i;
        }
        cancelGesture();
        canvas->tool = i;
        if (!externalMode())
            inspector->setCurrentIndex(locationModes                ? activeMode
                                       : i == MapCanvas::InspectNpc ? 1
                                                                    : 0);
        if (resource.endsWith(".CBT"))
            refreshCombat();
        findChild<QToolButton *>("mapPaste")->setEnabled(i != MapCanvas::InspectNpc);
        canvas->update();
        canvas->setCursor(i == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
        saveView();
    });
    connect(schedule, &QComboBox::currentIndexChanged, this, [=] {
        saveView();
        loadPage();
    });
    connect(zoom, &QComboBox::currentIndexChanged, this, [=](int i) {
        if (restoring)
            return;
        if (i == 0)
            view->fitMap();
        else
            view->setZoom(QVector<double>{1, 2, 3, 4, 8}[i - 1]);
    });
    connect(palette, &QListWidget::currentRowChanged, this, [=](int i) {
        if (i >= 0) {
            setBrush(palette->item(i)->data(Qt::UserRole).toInt());
            if (!restoring && (canvas->actorBrushKinds.contains(canvas->brush) ||
                               canvas->specialBrushKinds.contains(canvas->brush)))
                tool->button(MapCanvas::Pencil)->click();
        }
    });
    connect(palette, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!(item->flags() & Qt::ItemIsEnabled))
            return;
        setBrush(item->data(Qt::UserRole).toInt());
        tool->button(MapCanvas::Pencil)->click();
    });
    canvas->pick = [=](int id) {
        tileSearch->clear();
        paletteFilter->setCurrentIndex(0);
        setBrush(id);
        if (palette->currentItem())
            palette->scrollToItem(palette->currentItem());
    };
    canvas->chooseTool = [=](int i) {
        if (auto button = tool->button(i); button && button->isEnabled())
            button->click();
    };
    canvas->inspect = [=](int cx, int cy, int id) {
        coordinate->setText(QString("X %1   Y %2   Tile %3   ·   %4")
                                .arg(cx)
                                .arg(cy)
                                .arg(id)
                                .arg(tool->checkedButton()->accessibleName()));
    };
    canvas->chooseNpc = [this](int npc) { selectNpc(npc); };
    canvas->chooseActor = [this](const QList<int> &candidates) {
        QStringList names;
        for (int npc : candidates)
            names.append(resource.endsWith(".CBT")
                             ? combatEntity->itemText(npc)
                             : QString("NPC %1 · %2").arg(npc + 1).arg(npcName(npc)));
        bool ok = false;
        QString selected =
            QInputDialog::getItem(this, "Choose NPC", "Actors on this tile", names, 0, false, &ok);
        canvas->setFocus();
        return ok ? candidates[names.indexOf(selected)] : -1;
    };
    canvas->moveNpc = [this](int npc, QPoint destination) {
        if (resource.endsWith(".CBT"))
            return moveCombat(npc, destination);
        return moveNpcTo(npc, destination.x(), destination.y(),
                         document.pages[currentPage()].floor);
    };
    canvas->commit = [=](const QByteArray &ids) {
        try {
            auto changes = document.changes(currentPage(), ids, canvas->extended);
            return commit(changes, canvas->operation + " " + document.pages[currentPage()].name);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Map edit rejected", QString::fromUtf8(e.what()));
            return false;
        }
    };
    view->changed = [this] {
        if (!restoring) {
            updateZoom();
            saveView();
            minimap->update();
        }
    };
    loadPage();
    x->setRange(0, canvas->side - 1);
    y->setRange(0, canvas->rowCount() - 1);
}
bool MapWorkspace::externalMode() const {
    return locationModes && locationModes->tabText(activeMode) != "Terrain" &&
           locationModes->tabText(activeMode) != "NPCs / Schedules" &&
           locationModes->tabText(activeMode) != "Encounter";
}
bool MapWorkspace::flushLocationMode() {
    cancelGesture();
    return !layerFlush || layerFlush();
}
void MapWorkspace::clearLocationLayer() {
    if (!locationModes)
        return;
    // Restore callbacks before deleting the controls they used to capture.
    canvas->chooseNpc = nativeChooseNpc;
    canvas->chooseActor = nativeChooseActor;
    canvas->moveNpc = nativeMoveNpc;
    canvas->commit = nativeCommit;
    layerFlush = {};
    if (locationLayer) {
        inspector->removeWidget(locationLayer);
        delete locationLayer;
        locationLayer = nullptr;
    }
}
void MapWorkspace::enableLocationModes(const QStringList &names, LocationLayerFactory factory) {
    layerFactory = std::move(factory);
    nativeChooseNpc = canvas->chooseNpc;
    nativeChooseActor = canvas->chooseActor;
    nativeMoveNpc = canvas->moveNpc;
    nativeCommit = canvas->commit;
    terrainTool = canvas->tool == MapCanvas::InspectNpc ? MapCanvas::Pencil : canvas->tool;
    locationModes = new QTabBar;
    locationModes->setObjectName("locationModes");
    locationModes->setExpanding(false);
    locationModes->setToolTip(
        "Switch what you edit at this location. Floor, zoom and map view stay shared.");
    for (const auto &name : names) {
        const int index = locationModes->addTab(name);
        const QString help =
            name == "Terrain" ? "Paint and select map tiles."
            : name == "NPCs / Schedules"
                ? "Inspect NPCs and edit their scheduled destinations on this floor."
            : name == "Signs"
                ? "Edit sign text and place text records on this floor; terrain stays unchanged."
            : name == "Starting Objects"
                ? "Place objects for new games in this world; existing saves stay unchanged."
            : name == "Encounter" ? "Place party starts, monsters and triggers in this combat map."
                                  : "Inspect advanced metadata for this combat map; unverified "
                                    "bytes are labeled explicitly.";
        locationModes->setTabToolTip(index, help);
    }
    activeMode = qBound(
        0, navigation->value(resource + "/mode", canvas->tool == MapCanvas::InspectNpc ? 1 : 0),
        int(names.size()) - 1);
    if (navigation->contains(resource + "/selectPosition")) {
        (*navigation)["positions/selected"] = navigation->take(resource + "/selectPosition");
        for (int i = 0; i < locationModes->count(); i++)
            if (locationModes->tabText(i) == "Positions")
                activeMode = i;
    }
    locationModes->setCurrentIndex(activeMode);
    locationModes->setParent(this);
    locationModes->hide();
    canvas->prepareSelection = [this](QPoint cell) { return selectMapItem(cell); };
    canvas->pencilPlacement = [this](QPoint cell, int tile) {
        if (canvas->specialBrushKinds.contains(tile)) {
            beginAdd(canvas->specialBrushKinds.value(tile), -1, cell);
            return true;
        }
        if (canvas->actorBrushKinds.contains(tile)) {
            int kind = canvas->actorBrushKinds.value(tile) == 1
                           ? int(Workshop::MapItemKind::Npc)
                           : int(Workshop::MapItemKind::Monster);
            if (!Workshop::availableMapItems(*project, resource)
                     .contains(Workshop::MapItemKind(kind))) {
                if (canvas->feedback)
                    canvas->feedback("This actor cannot be placed on this map.");
                return true;
            }
            beginAdd(kind,
                     resource == "BRIT.CBT" && tile < 512 ? -1
                     : tile >= 512                        ? tile
                                                          : tile - 256,
                     cell);
            return true;
        }
        if (resource != "BRIT.DAT" && resource != "UNDER.DAT")
            return false;
        const int profile = tile >= 512 ? CustomTiles::definition(*project, tile).base : tile;
        if (!((profile >= 0x10 && profile <= 0x1b) || profile == 0x39 || profile == 0x3e))
            return false;
        for (const auto &pos : Workshop::mapPositions(*project))
            if (pos.id >= 100 && pos.map == 0 && pos.floor == (resource == "UNDER.DAT" ? -1 : 0) &&
                pos.cell == cell)
                return false;
        beginAddLocation(cell, tile);
        return true;
    };
    canvas->deleteItem = [this] { deleteSelectedItem(); };
    auto remove = new QToolButton;
    remove->setObjectName("mapDeleteItem");
    remove->setAccessibleName("Delete map item");
    remove->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    remove->setToolTip(
        "Delete selected map item (Delete). Undo restores the item and its attached data.");
    connect(remove, &QToolButton::clicked, this, [this] {
        if (canvas->tool == MapCanvas::InspectNpc)
            deleteSelectedItem();
    });
    remove->setFixedSize(28, 28);
    remove->setIconSize({22, 22});
    if (auto row = findChild<QHBoxLayout *>("mapToolRow"))
        row->insertWidget(row->count() - 1, remove);
    auto markers = new QCheckBox("Contents markers");
    markers->setObjectName("contentsMarkersToggle");
    markers->setToolTip(
        "Show places with hidden items or custom container contents while selecting map items.");
    markers->setChecked(true);
    connect(markers, &QCheckBox::toggled, this, [this](bool enabled) {
        canvas->showContentsMarkers = enabled;
        canvas->update();
    });
    markers->setParent(this);
    markers->hide();
    auto viewOptions = new QToolButton;
    viewOptions->setObjectName("mapViewOptions");
    viewOptions->setText("View");
    viewOptions->setToolTip("Map display options");
    viewOptions->setAccessibleName("View options");
    viewOptions->setPopupMode(QToolButton::InstantPopup);
    auto viewMenu = new QMenu(viewOptions);
    auto markerAction = viewMenu->addAction("Contents markers");
    markerAction->setObjectName("viewContentsMarkers");
    markerAction->setToolTip(markers->toolTip());
    markerAction->setCheckable(true);
    markerAction->setChecked(true);
    connect(markerAction, &QAction::toggled, markers, &QCheckBox::setChecked);
    connect(markers, &QCheckBox::toggled, markerAction, &QAction::setChecked);
    auto highlightAction = viewMenu->addAction("Highlight changes");
    highlightAction->setObjectName("viewHighlightChanges");
    highlightAction->setCheckable(true);
    highlightAction->setChecked(comparison->isChecked());
    highlightAction->setToolTip(comparison->toolTip());
    connect(highlightAction, &QAction::toggled, comparison, &QCheckBox::setChecked);
    connect(comparison, &QCheckBox::toggled, highlightAction, &QAction::setChecked);
    for (int i = 0; i < locationModes->count(); ++i)
        if (locationModes->tabText(i) == "Positions") {
            auto positions = new QToolButton;
            positions->setText("Positions");
            positions->setObjectName("mapPositions");
            positions->setToolTip(
                "Place the party start, moonstones, and location entrances on the map.");
            connect(positions, &QToolButton::clicked, this,
                    [this, i] { locationModes->setCurrentIndex(i); });
            if (auto row = findChild<QHBoxLayout *>("mapLocationRow"))
                row->addWidget(positions);
        }
    viewOptions->setMenu(viewMenu);
    if (auto row = findChild<QHBoxLayout *>("mapLocationRow"))
        row->addWidget(viewOptions);
    tool->button(MapCanvas::InspectNpc)
        ->setToolTip("Select / Move — select an NPC, sign or map item and drag to move it.");
    tool->button(MapCanvas::InspectNpc)->setAccessibleName("Select / Move");
    connect(locationModes, &QTabBar::currentChanged, this, [this](int mode) {
        if (!flushLocationMode()) {
            QSignalBlocker block(locationModes);
            locationModes->setCurrentIndex(activeMode);
            return;
        }
        saveView();
        clearLocationLayer();
        activeMode = mode;
        (*navigation)[resource + "/mode"] = mode;
        applyLocationMode();
    });
    applyLocationMode();
}
void MapWorkspace::beginAddLocation(QPoint cell, int entranceTile) {
    if (!flushLocationMode())
        return;
    QDialog dialog(window());
    QPointer<MapWorkspace> owner(this);
    dialog.setWindowTitle("Add location");
    QFormLayout form(&dialog);
    QLineEdit name("New location");
    name.setObjectName("newLocationName");
    QSpinBox width, height, fill;
    width.setObjectName("newLocationWidth");
    height.setObjectName("newLocationHeight");
    width.setRange(1, 256);
    height.setRange(1, 256);
    width.setValue(32);
    height.setValue(32);
    fill.setRange(0, 255);
    fill.setValue(5);
    form.addRow("Name", &name);
    form.addRow("Width (tiles)", &width);
    form.addRow("Height (tiles)", &height);
    form.addRow("Fill tile", &fill);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !owner)
        return;
    const QString title = name.text();
    const int w = width.value(), h = height.value(), tile = fill.value();
    auto place = [this, title, w, h, tile, entranceTile](QPoint cell) {
        try {
            const auto changes =
                Workshop::addLocation(*project, resource, title, cell, w, h, tile, entranceTile);
            if (!commitResources(changes, "Add location"))
                return false;
            Workshop::hydrateExtendedMaps(*project);
            refreshTerrain();
            minimap->refresh();
            if (locationChanged)
                locationChanged();
            return true;
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Cannot add location", e.what());
            return false;
        }
    };
    if (cell.x() >= 0)
        place(cell);
    else
        canvas->beginPlacement(entranceTile, false, place);
}
void MapWorkspace::applyLocationMode() {
    clearLocationLayer();
    const QString mode = locationModes->tabText(activeMode);
    canvas->terrainEditingEnabled = activeMode == 0;
    int minimum = externalMode() ? 380 : 220;
    if (mode == "NPCs / Schedules" || mode == "Encounter") {
        auto panel = findChild<QScrollArea *>("mapCharacterInspector");
        panel->widget()->layout()->activate();
        minimum =
            qMax(minimum, panel->widget()->minimumSizeHint().width() + panel->frameWidth() * 2 +
                              panel->style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    }
    inspector->parentWidget()->setMinimumWidth(minimum);
    if (auto button = findChild<QToolButton *>("mapDeleteItem"))
        button->setEnabled(mode != "Positions");
    canvas->actors.clear();
    canvas->ghosts.clear();
    canvas->ids = document.terrain(currentPage());
    canvas->extended = CustomTiles::expanded(*project, resource, document.pages[currentPage()],
                                             currentPage(), canvas->ids);
    canvas->libraryUuid = project->resources.value("TYPES.OVR").edited.mid(8, 16);
    canvas->selectedNpc = states->value(key).npc;
    for (auto button : tool->buttons()) {
        const int id = tool->id(button);
        button->setEnabled(true);
        button->setVisible(true);
    }
    tool->button(MapCanvas::InspectNpc)->setEnabled(locationModes->count() > 1);
    schedule->setEnabled(project->resources.contains(npcResource) && !resource.endsWith(".CBT"));
    const bool showSchedule =
        !resource.endsWith(".CBT") && project->resources.contains(npcResource);
    schedule->setVisible(showSchedule);
    if (auto label = findChild<QLabel *>("mapScheduleLabel"))
        label->setVisible(showSchedule);
    comparison->hide();
    for (auto name : {"mapCopy", "mapPaste", "mapClearSelection"})
        findChild<QToolButton *>(name)->setEnabled(activeMode == 0);
    comparison->setEnabled(true);
    findChild<QToolButton *>("mapPaste")->setEnabled(activeMode == 0);
    if (!externalMode()) {
        canvas->tool = activeMode == 0 ? terrainTool : MapCanvas::InspectNpc;
        inspector->setCurrentIndex(activeMode);
        refreshNpcs();
    } else {
        canvas->tool = MapCanvas::InspectNpc;
        canvas->commit = {}; // A layer can never paint terrain through keyboard or mouse.
        canvas->moveNpc = {};
        canvas->chooseNpc = {};
        canvas->chooseActor = [this](const QList<int> &ids) {
            QStringList labels;
            for (int id : ids)
                for (const auto &actor : canvas->actors)
                    if (actor.npc == id) {
                        labels.append(QString("%1 · item %2").arg(actor.label).arg(id + 1));
                        break;
                    }
            bool ok = false;
            const auto label = QInputDialog::getItem(this, "Choose item", "Items on this tile",
                                                     labels, 0, false, &ok);
            return ok ? ids.value(labels.indexOf(label), -1) : -1;
        };
        QWidget *controls = nullptr;
        try {
            controls = layerFactory(mode, currentPage(), canvas, &layerFlush);
        } catch (const std::exception &error) {
            // A damaged companion must not escape a Qt signal handler or leave stale callbacks.
            layerFlush = {};
            canvas->chooseNpc = {};
            canvas->moveNpc = {};
            canvas->actors.clear();
            auto message =
                new QLabel("Cannot open this location layer: " + QString::fromUtf8(error.what()) +
                           "\nChoose another mode. Original data has not been changed.");
            message->setWordWrap(true);
            controls = message;
        }
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setWidget(controls);
        locationLayer = scroll;
        inspector->addWidget(locationLayer);
        inspector->setCurrentWidget(locationLayer);
    }
    canvas->contentsMarkers.clear();
    const auto contentTarget =
        Workshop::contentsTarget(resource, document.pages[currentPage()], currentPage());
    for (auto d : Workshop::mapContents(*project, contentTarget))
        canvas->contentsMarkers.append({d.x, d.y, -1, -1, {}});
    refreshContextActors();
    tool->button(canvas->tool)->setChecked(true);
    canvas->update();
}
bool MapWorkspace::selectMapItem(QPoint cell) {
    const auto &page = document.pages[currentPage()];
    unsigned selectedType = canvas->tileId(cell.y() * page.side + cell.x());
    if (selectedType >= 512)
        setBrush(selectedType);
    int npcMode = -1, signMode = -1, contentsMode = -1, objectMode = -1, encounterMode = -1,
        positionsMode = -1;
    for (int i = 0; i < locationModes->count(); ++i) {
        if (locationModes->tabText(i) == "Positions")
            positionsMode = i;
        if (locationModes->tabText(i) == "NPCs / Schedules")
            npcMode = i;
        if (locationModes->tabText(i) == "Contents")
            contentsMode = i;
        if (locationModes->tabText(i) == "Signs")
            signMode = i;
        if (locationModes->tabText(i) == "Starting Objects")
            objectMode = i;
        if (locationModes->tabText(i) == "Encounter")
            encounterMode = i;
    }
    bool npcHit = false, signHit = false, signRecordHit = false;
    bool objectHit = false, encounterHit = false;
    if (objectMode >= 0) {
        auto b = Workshop::WorldObjects::effective(*project, resource == "UNDER.DAT");
        for (int i = 1; i < 32; i++)
            objectHit |= U5::byte(b, i * 8) != 0 && U5::byte(b,i*8)!=1 && U5::byte(b, i * 8 + 2) == unsigned(cell.x()) &&
                         U5::byte(b, i * 8 + 3) == unsigned(cell.y());
    }
    if (encounterMode >= 0) {
        auto b = project->data(resource);
        int base = page.offset;
        for (int i = 0; i < 16; i++)
            encounterHit |= Workshop::mapMarkerActive(*project, resource, currentPage(), 24 + i,
                                                      cell.x(), cell.y()) &&
                            (resource == "BRIT.CBT" || U5::byte(b, base + 5 * 32 + 11 + i)) &&
                            U5::byte(b, base + 6 * 32 + 11 + i) == unsigned(cell.x()) &&
                            U5::byte(b, base + 7 * 32 + 11 + i) == unsigned(cell.y());
        for (int i = 0; i < 8; i++)
            encounterHit |= Workshop::mapMarkerActive(*project, resource, currentPage(), 40 + i,
                                                      cell.x(), cell.y()) &&
                            U5::byte(b, base + 8 * 32 + 11 + i) == unsigned(cell.x()) &&
                            U5::byte(b, base + 8 * 32 + 19 + i) == unsigned(cell.y());
        for (int i = 0; i < 6; i++) {
            const int rows[] = {4, 1, 3, 2};
            int offset = base + rows[combatEntry->currentIndex()] * 32;
            encounterHit |=
                Workshop::mapMarkerActive(*project, resource, currentPage(),
                                          (rows[combatEntry->currentIndex()] - 1) * 6 + i, cell.x(),
                                          cell.y()) &&
                U5::byte(b, offset + 11 + i) == unsigned(cell.x()) &&
                U5::byte(b, offset + 17 + i) == unsigned(cell.y());
        }
    }
    const int signMap = Workshop::positionMap(resource, page);
    const int signFloor = resource == "UNDER.DAT" ? -1 : page.floor;
    if (npcMode >= 0 && page.settlement >= 0 && project->data(npcResource).size() >= 4608) {
        MapNpcDocument npcs(project, npcResource);
        for (int id = 1; id < npcs.count(page.settlement); ++id) {
            auto position = npcs.location(page.settlement, id, schedule->currentIndex());
            npcHit |=
                U5::byte(project->data(npcResource), npcs.shapeOffset(page.settlement, id)) != 0 &&
                position.floor == page.floor && position.x == cell.x() && position.y == cell.y();
        }
    }
    if (signMode >= 0) {
        try {
            for (const auto &sign : Workshop::readSigns(project->data("SIGNS.DAT")))
                signRecordHit |= sign.map == signMap && sign.floor == signFloor &&
                                 sign.x == cell.x() && sign.y == cell.y();
        } catch (const std::exception &) {
        }
        const int tile = U5::byte(document.terrain(currentPage()), cell.y() * page.side + cell.x());
        signHit = signRecordHit || tile == 0x89 || tile == 0x8a || tile == 0xa0 || tile == 0xa4 ||
                  tile == 0xf8;
    }
    auto contentsTarget = Workshop::contentsTarget(resource, page, currentPage());
    const bool contentsHit =
        contentsMode >= 0 && Workshop::contentsHit(*project, contentsTarget, cell);
    const int selectedTile =
        U5::byte(document.terrain(currentPage()), cell.y() * page.side + cell.x());
    const bool containerTile = selectedTile == TILE_MAP_BARREL || selectedTile == TILE_MAP_TRUNK ||
                               selectedTile == TILE_MAP_DRESSER || selectedTile == TILE_MAP_DESK ||
                               selectedTile == TILE_MAP_VANITY;
    QStringList choices;
    QList<int> modes;
    auto offer = [&](bool hit, QString label, int mode) {
        if (hit) {
            choices.append(label);
            modes.append(mode);
        }
    };
    bool positionHit = false;
    int positionId = -1;
    if (positionsMode >= 0)
        for (auto pos : Workshop::mapPositions(*project))
            if (!pos.carried && pos.map == Workshop::positionMap(resource, page) &&
                pos.floor == (resource == "UNDER.DAT" ? -1 : page.floor) && pos.cell == cell) {
                positionHit = true;
                positionId = pos.id;
            }
    if (positionHit)
        (*navigation)["positions/selected"] = positionId;
    offer(positionHit, "Map position", positionsMode);
    offer(npcHit, "NPC", npcMode);
    offer(signHit, "Sign", signMode);
    offer(contentsHit || containerTile, "Contents", contentsMode);
    offer(objectHit, "World object", objectMode);
    offer(encounterHit, "Encounter", encounterMode);
    int target = modes.isEmpty() ? contentsMode : modes.first();
    if (target < 0)
        target = activeMode;
    if (choices.size() > 1) {
        bool ok = false;
        auto choice =
            QInputDialog::getItem(this, "Select map item", "Item", choices, 0, false, &ok);
        if (!ok)
            return false;
        target = modes[choices.indexOf(choice)];
    }
    if (target != activeMode) {
        locationModes->setCurrentIndex(target);
        if (activeMode != target)
            return false;
    }
    canvas->keyboardCell = cell;
    if (target == contentsMode && locationLayer) {
        auto scroll = qobject_cast<QScrollArea *>(locationLayer);
        if (auto panel =
                dynamic_cast<Workshop::ContentsEditor *>(scroll ? scroll->widget() : nullptr))
            panel->selectCell(cell);
    }
    if (target == signMode && signHit && !signRecordHit && locationLayer) {
        if (!flushLocationMode()) return false;
        if (auto entries = locationLayer->findChild<QListWidget *>("locationTextEntries")) {
            entries->setCurrentRow(-1);
            entries->clearSelection();
        }
        if (auto draft = locationLayer->findChild<QPlainTextEdit *>("locationTextDraft")) {
            draft->clear();
            draft->document()->setModified(false);
        }
        canvas->selectedNpc = -1;
    }
    return true;
}
void MapWorkspace::refreshContextActors() {
    canvas->contextActors.clear();
    canvas->positionMarkers.clear();
    const auto &page = document.pages[currentPage()];
    if (!resource.endsWith(".CBT") && locationModes->tabText(activeMode) != "Positions")
        for (auto pos : Workshop::mapPositions(*project))
            if (!pos.carried && pos.map == Workshop::positionMap(resource, page) &&
                pos.floor == (resource == "UNDER.DAT" ? -1 : page.floor))
                canvas->positionMarkers.append({pos.cell.x(), pos.cell.y(), -1, pos.id,
                                                pos.id == 0    ? "S"
                                                : pos.id < 100 ? QString("M%1").arg(pos.id)
                                                               : "E"});

    if ((resource == "BRIT.DAT" || resource == "UNDER.DAT") &&
        project->resources.contains("INIT.OOL") && project->resources.contains("BRIT.OOL") &&
        project->resources.contains("UNDER.OOL") &&
        locationModes->tabText(activeMode) != "Starting Objects") {
        auto b = Workshop::WorldObjects::effective(*project, resource == "UNDER.DAT");
        for (int i = 1; i < 32; i++)
            if (U5::byte(b, i * 8) &&
                !(U5::byte(b, i * 8) == 1 && locationModes->tabText(activeMode) == "Contents"))
                canvas->contextActors.append(
                    {int(U5::byte(b, i * 8 + 2)),
                     int(U5::byte(b, i * 8 + 3)),
                     int(CustomTiles::actorType(*project, resource, page, currentPage(), i,
                                                256 + U5::byte(b, i * 8))),
                     i,
                     {}});
    }
    if (locationModes->tabText(activeMode) != "Contents")
        for (auto d : Workshop::mapContents(
                 *project, Workshop::contentsTarget(resource, page, currentPage())))
            if (d.action && d.slot == 255)
                canvas->contextActors.append({d.x, d.y, 257, -1, {}});
    if (!externalMode() || page.settlement < 0 || !project->resources.contains(npcResource) ||
        project->data(npcResource).size() < 4608)
        return;
    MapNpcDocument npcs(project, npcResource);
    for (int id = 1; id < npcs.count(page.settlement); id++) {
        auto position = npcs.location(page.settlement, id, schedule->currentIndex());
        if (U5::byte(project->data(npcResource), npcs.shapeOffset(page.settlement, id)) &&
            position.floor == page.floor && position.x < 32 && position.y < 32)
            canvas->contextActors.append(
                {position.x, position.y, npcs.sprite(page.settlement, id), id, {}});
    }
}
int MapWorkspace::currentPage() const { return page->currentData().toInt(); }
void MapWorkspace::populateFloors(int index) {
    QSignalBlocker block(page);
    page->clear();
    const auto &current = document.pages[index];
    for (int i = 0; i < document.pages.size(); ++i) {
        const auto &candidate = document.pages[i];
        if (current.settlement >= 0 ? candidate.settlement == current.settlement : i == index) {
            QString label = candidate.settlement < 0 ? candidate.name
                            : candidate.floor < 0    ? "Basement"
                            : candidate.floor == 0   ? "Ground floor"
                                                   : QString("Upper floor %1").arg(candidate.floor);
            page->addItem(label, i);
        }
    }
    page->setCurrentIndex(page->findData(index));
    if (auto label = findChild<QLabel *>("mapLocationLabel")) {
        const QString location = current.name.section(" — ", 0, 0);
        label->setText(location + (page->count() > 1 ? ":" : ""));
        label->setToolTip(location);
    }
    page->setVisible(page->count() > 1);
}
void MapWorkspace::selectPage(int index) {
    if (locationModes && !flushLocationMode()) {
        populateFloors(navigation->value(resource + "/map"));
        return;
    }
    if (index < 0 || index >= document.pages.size())
        return;
    cancelGesture();
    saveView();
    clearLocationLayer();
    int oldPage = navigation->value(resource + "/map");
    if (!externalMode() && canvas->tool == MapCanvas::InspectNpc && canvas->selectedNpc >= 0 &&
        document.pages[oldPage].settlement == document.pages[index].settlement) {
        auto &target = (*states)[resource + "/" + QString::number(index)];
        target.tool = MapCanvas::InspectNpc;
        target.npc = canvas->selectedNpc;
        target.schedule = schedule->currentIndex();
        target.ghosts = npcGhosts->isChecked();
    }
    (*navigation)[resource + "/map"] = index;
    populateFloors(index);
    loadPage();
    findChild<QSpinBox *>("mapGoX")->setRange(0, canvas->side - 1);
    findChild<QSpinBox *>("mapGoY")->setRange(0, canvas->rowCount() - 1);
    if (locationChanged)
        locationChanged();
}
void MapWorkspace::cancelGesture() { canvas->cancelStroke(); }
void MapWorkspace::setBrush(int id) {
    canvas->brush = canvas->actorBrushKinds.contains(id) ||
                            canvas->specialBrushKinds.contains(id) ||
                            (id >= 512 && canvas->legacyTiles.contains(id))
                        ? id
                        : (id > 255 ? 5 : qBound(0, id, 255));
    {
        QSignalBlocker block(palette);
        for (int i = 0; i < palette->count(); i++)
            if (palette->item(i)->data(Qt::UserRole).toInt() == canvas->brush)
                palette->setCurrentRow(i);
    }
    if (auto edit = findChild<QPushButton *>("mapEditTileDefinition"))
        edit->setVisible(!canvas->specialBrushKinds.contains(canvas->brush));
    brushImage->setPixmap(QPixmap::fromImage(canvas->tiles[canvas->brush])
                              .scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation));
    QString name;
    for (int i = 0; i < palette->count(); ++i)
        if (palette->item(i)->data(Qt::UserRole).toInt() == canvas->brush) {
            name = palette->item(i)->data(Qt::UserRole + 1).toString();
            break;
        }
    if (name.isEmpty())
        name = canvas->brush >= 512
                   ? QString::fromUtf8(CustomTiles::definition(*project, canvas->brush).name)
               : canvas->brush >= 256 ? Workshop::actorName(canvas->brush - 256)
                                      : MapDocument::tileName(canvas->brush);
    brushLabel->setText(canvas->specialBrushKinds.contains(canvas->brush)
                            ? name
                            : QString("%1\nTile %2 · %3")
                                  .arg(name)
                                  .arg(canvas->brush)
                                  .arg(tileCategories(canvas->brush).join(" · ")));
    filterPalette();
    canvas->update();
    saveView();
}
QStringList MapWorkspace::tileCategories(int id) const {
    QStringList categories;
    if (canvas->specialBrushKinds.contains(id))
        return {"Special"};
    if (canvas->actorBrushKinds.contains(id))
        return {canvas->actorBrushKinds.value(id) == 1 ? "NPCs" : "Monsters"};
    const int profile = id >= 512 ? CustomTiles::definition(*project, id).base : id;
    if ((profile >= 0x10 && profile <= 0x1b) || profile == 0x39 || profile == 0x3e)
        categories << "World";
    if (id >= 512) {
        auto d = CustomTiles::definition(*project, id);
        if ((d.filters & 1) && !categories.contains("World"))
            categories << "World";
        if (d.filters & 2)
            categories << "Locations";
        return categories;
    }
    if (worldTiles.contains(id) && !categories.contains("World"))
        categories.append("World");
    if (locationTiles.contains(id))
        categories.append("Locations");
    return categories;
}
void MapWorkspace::filterPalette() {
    QString query = tileSearch->text().trimmed();
    for (int row = 0; row < palette->count(); ++row) {
        int i = palette->item(row)->data(Qt::UserRole).toInt();
        bool matches =
            query.isEmpty() ||
            ((canvas->actorBrushKinds.contains(i) || canvas->specialBrushKinds.contains(i))
                 ? palette->item(row)->data(Qt::UserRole + 1).toString()
             : i >= 512 ? QString::fromUtf8(CustomTiles::definition(*project, i).name)
                        : MapDocument::tileName(i))
                .contains(query, Qt::CaseInsensitive) ||
            tileCategories(i).join(" ").contains(query, Qt::CaseInsensitive) ||
            QString::number(i).contains(query) ||
            QString("0x%1").arg(i, 2, 16, QChar('0')).contains(query, Qt::CaseInsensitive);
        bool group = paletteFilter->currentIndex() == 0 ||
                     tileCategories(i).contains(paletteFilter->currentText());
        palette->item(row)->setHidden(!matches || !group);
    }
}
void MapWorkspace::updateSelection() {
    int changed = 0;
    if (canvas->ids.size() == canvas->original.size())
        for (int i = 0; i < canvas->ids.size(); ++i)
            changed += canvas->ids[i] != canvas->original[i];
    auto area = canvas->selection;
    findChild<QToolButton *>("mapCopy")->setEnabled(!area.isEmpty());
    findChild<QToolButton *>("mapClearSelection")->setEnabled(!area.isEmpty());
    findChild<QToolButton *>("mapPaste")->setEnabled(
        canvas->tool != MapCanvas::InspectNpc && QApplication::clipboard()->mimeData() &&
        QApplication::clipboard()->mimeData()->hasFormat(canvas->clipboardMime));
    selectionLabel->setText((area.isEmpty() ? QString("No selection")
                                            : QString("Selection: %1 × %2 at %3, %4")
                                                  .arg(area.width())
                                                  .arg(area.height())
                                                  .arg(area.x())
                                                  .arg(area.y())) +
                            QString("\n%1 changed terrain cells").arg(changed));
}
void MapWorkspace::updateZoom() {
    QSignalBlocker block(zoom);
    int index = view->fitting ? 0 : QVector<double>{1, 2, 3, 4, 8}.indexOf(canvas->zoom) + 1;
    if (index == 0 && !view->fitting) {
        zoom->setCurrentIndex(-1);
        zoom->setPlaceholderText(QString("%1%").arg(qRound(canvas->zoom * 100)));
    } else
        zoom->setCurrentIndex(index);
}
void MapWorkspace::saveView() {
    if (restoring || key.isEmpty())
        return;
    auto &s = (*states)[key];
    s.zoom = canvas->zoom;
    s.fit = view->fitting;
    s.grid = canvas->grid;
    s.selection = canvas->selection;
    s.comparison = canvas->comparison;
    s.outline = canvas->outline;
    s.keyboardCell = canvas->keyboardCell;
    s.brush = canvas->brush;
    if (!externalMode())
        s.tool = canvas->tool;
    s.schedule = schedule->currentIndex();
    if (!externalMode())
        s.npc = canvas->selectedNpc;
    s.ghosts = npcGhosts->isChecked();
    auto &shared = (*states)["map/editor"];
    shared.brush = canvas->brush;
    shared.tool = canvas->tool;
    shared.zoom = canvas->zoom;
    shared.fit = view->fitting;
    shared.comparison = canvas->comparison;
    shared.ghosts = npcGhosts->isChecked();
    shared.paletteFilter = paletteFilter->currentIndex();
    shared.tileSearch = tileSearch->text();
    if (isVisible())
        s.center = view->mapCenter();
}
void MapWorkspace::loadPage() {
    restoring = true;
    key = resource + "/" + QString::number(currentPage());
    auto state = states->value(key);
    if (states->contains("map/editor")) {
        const auto shared = states->value("map/editor");
        state.brush = shared.brush;
        state.tool = shared.tool;
        state.zoom = shared.zoom;
        state.fit = shared.fit;
        state.comparison = shared.comparison;
        state.ghosts = shared.ghosts;
    }
    const auto &mp = document.pages[currentPage()];
    canvas->side = mp.side;
    canvas->rows = mp.height();
    minimap->setVisible(resource == "BRIT.DAT" || resource == "UNDER.DAT");
    canvas->ids = document.terrain(currentPage());
    canvas->extended = CustomTiles::expanded(*project, resource, document.pages[currentPage()],
                                             currentPage(), canvas->ids);
    canvas->libraryUuid = project->resources.value("TYPES.OVR").edited.mid(8, 16);
    canvas->original = document.terrain(currentPage(), true);
    canvas->keyboardCell = state.keyboardCell;
    canvas->selection = state.selection.intersected(QRect(0, 0, mp.side, mp.height()));
    canvas->comparison = state.comparison;
    canvas->outline = false;
    {
        QSignalBlocker block(comparison);
        comparison->setChecked(state.comparison);
        if (auto action = findChild<QAction *>("viewHighlightChanges")) {
            QSignalBlocker actionBlock(action);
            action->setChecked(state.comparison);
        }
    }
    canvas->zoom = state.zoom;
    canvas->grid = false;
    canvas->tool = qBound(0, state.tool, tool->buttons().size() - 1);
    {
        QSignalBlocker block(tool);
        tool->button(canvas->tool)->setChecked(true);
    }
    {
        QSignalBlocker block(schedule);
        schedule->setCurrentIndex(state.schedule);
    }
    canvas->selectedNpc =
        state.npc >= 0
            ? state.npc
            : navigation->value(resource + "/actor/" + QString::number(mp.settlement), -1);
    {
        QSignalBlocker block(npcGhosts);
        npcGhosts->setChecked(state.ghosts);
    }
    inspector->setCurrentIndex(canvas->tool == MapCanvas::InspectNpc ? 1 : 0);
    findChild<QToolButton *>("mapPaste")->setEnabled(canvas->tool != MapCanvas::InspectNpc);
    refreshNpcs();
    setBrush(state.brush);
    canvas->setCursor(canvas->tool == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
    canvas->resizeMap();
    minimap->refresh();
    updateSelection();
    view->fitting = state.fit;
    updateZoom();
    coordinate->setText(QString("%1 · %2 × %3 tiles").arg(mp.name).arg(mp.side).arg(mp.height()));
    {
        QSignalBlocker block(maps);
        QTreeWidgetItemIterator it(maps);
        while (*it) {
            auto item = *it;
            if (item->childCount() == 0 && item->data(0, Qt::UserRole).toString() == resource &&
                item->data(0, Qt::UserRole + 1).toInt() == currentPage()) {
                maps->setCurrentItem(item);
                maps->scrollToItem(item);
                break;
            }
            ++it;
        }
    }
    if (locationModes)
        applyLocationMode();
    QString loadedKey = key;
    QTimer::singleShot(0, this, [this, state, loadedKey] {
        if (key != loadedKey)
            return;
        if (state.fit)
            view->fitMap();
        else if (state.center.x() >= 0)
            view->centerMap(state.center);
        else
            view->centerMap({canvas->side / 2.0, canvas->rowCount() / 2.0});
        restoring = false;
        updateZoom();
        saveView();
    });
}
QString MapWorkspace::npcName(int npc) const {
    MapNpcDocument npcs(project, npcResource);
    int id = npcs.dialogue(document.pages[currentPage()].settlement, npc);
    unsigned appearance = npcs.sprite(document.pages[currentPage()].settlement, npc);
    if (appearance >= 512 && !npcNames.contains(id))
        return QString::fromUtf8(CustomTiles::definition(*project, appearance).name);
    return npcNames.value(
        id,
        npcs.sprite(document.pages[currentPage()].settlement, npc) >= 384
            ? Workshop::actorName(npcs.sprite(document.pages[currentPage()].settlement, npc) - 256)
            : QString("Unnamed NPC %1").arg(npc + 1));
}
void MapWorkspace::createNpcInspector(QVBoxLayout *layout) {
    auto title = new QLabel("NPC schedule");
    layout->addWidget(title);
    npcList = new QComboBox;
    npcList->setToolTip(
        "Select an NPC, including NPCs whose current destination is on another floor.");
    npcList->setObjectName("mapNpcList");
    npcList->setAccessibleName("NPC");
    npcList->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    npcList->setMinimumContentsLength(12);
    npcList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(npcList);
    npcSprite = new QLabel;
    npcSprite->setToolTip("Artwork used for the selected NPC.");
    npcSprite->setAlignment(Qt::AlignCenter);
    layout->addWidget(npcSprite);
    npcInfo = new QLabel;
    npcInfo->setToolTip("Details of the selected NPC and schedule change; shared "
                        "destinations are identified here.");
    npcInfo->setObjectName("mapNpcInfo");
    npcInfo->setWordWrap(true);
    npcInfo->setTextFormat(Qt::PlainText);
    npcInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    npcInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(npcInfo);
    auto form = new QFormLayout;
    npcStartHour = new QSpinBox;
    npcStartHour->setObjectName("mapNpcStartHour");
    npcStartHour->setRange(0, 255);
    npcStartHour->setSuffix(":00");
    npcStartHour->setToolTip("Start hour for the selected schedule change. Normal hours are 0–23; "
                             "existing unusual values are preserved.");
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->addRow("Start hour", npcStartHour);
    npcNameField = new QLineEdit;
    npcNameField->setObjectName("mapNpcName");
    npcNameField->setToolTip("Name in the linked conversation. Changing it also affects NPCs "
                             "sharing that conversation.");
    form->addRow("Name", npcNameField);
    connect(npcNameField, &QLineEdit::editingFinished, this, [this] {
        if (canvas->selectedNpc < 1 || !project->resources.contains(talkResource))
            return;
        try {
            MapNpcDocument npcs(project, npcResource);
            int id = npcs.dialogue(document.pages[currentPage()].settlement, canvas->selectedNpc);
            auto entries = U5::readDialogue(project->data(talkResource));
            for (auto &e : entries)
                if (int(e.id) == id) {
                    auto doc = Dialogue::parse(e.bytes);
                    require(doc.structuralError.isEmpty(), "This conversation cannot be renamed");
                    auto text = Dialogue::text(npcNameField->text().trimmed());
                    require(!text.isEmpty(), "Enter an NPC name");
                    if (text == doc.entries[0].bytes)
                        return;
                    doc.entries[0].bytes = text;
                    e.bytes = doc.bytes();
                    if (commitResources({{talkResource, U5::writeDialogue(entries)}}, "Rename NPC"))
                        refreshNpcs();
                    break;
                }
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Cannot rename NPC", e.what());
            refreshNpcs();
        }
    });
    npcAppearance = new QComboBox;
    npcAppearance->setObjectName("mapNpcAppearance");
    for (int shape = 64; shape < 128; shape += 4)
        if (Workshop::actorName(shape) != "x")
            npcAppearance->addItem(Workshop::actorName(shape), shape);
    for (int shape : Workshop::monsterShapes())
        npcAppearance->addItem(Workshop::actorName(shape), shape);
    for (auto record : CustomTiles::records(*project)) {
        TypeDef d;
        TYPES_Decode(reinterpret_cast<const unsigned char *>(record.constData()), &d);
        if (d.kind && d.id >= 512)
            npcAppearance->addItem(QIcon(QPixmap::fromImage(CustomTiles::image(*project, d.id))),
                                   QString::fromUtf8(d.name), d.id);
    }
    npcAppearance->setToolTip("Appearance of this NPC on the map.");
    auto typeRow = new QHBoxLayout;
    typeRow->addWidget(npcAppearance, 1);
    auto editType = new QToolButton;
    editType->setText("Edit type…");
    editType->setToolTip("Edit the shared creature type used by this NPC.");
    typeRow->addWidget(editType);
    form->addRow("Type", typeRow);
    connect(editType, &QToolButton::clicked, this, [this] {
        unsigned type = npcAppearance->currentData().toUInt();
        if (editTileType && npcAppearance->currentIndex() >= 0)
            editTileType(type >= 512 ? type : 256 + type);
    });
    npcDialogue = new QComboBox;
    npcDialogue->setObjectName("mapNpcDialogue");
    npcDialogue->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    npcDialogue->setMinimumContentsLength(12);
    npcDialogue->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    npcDialogue->setToolTip("Link an existing conversation. Shared conversations stay shared.");
    form->addRow("Conversation", npcDialogue);
    npcBehavior = new QComboBox;
    npcBehavior->setObjectName("mapNpcBehavior");
    npcBehavior->addItem("Stay here", 0);
    npcBehavior->addItem("Wander nearby", 1);
    npcBehavior->addItem("Wander freely", 2);
    npcBehavior->addItem("Attack", 5);
    npcBehavior->setToolTip("Behavior during the selected schedule period.");
    form->addRow("Behavior", npcBehavior);
    for (auto field : {npcAppearance, npcDialogue, npcBehavior})
        connect(field, &QComboBox::activated, this, [this] { editNpcProperties(); });

    npcX = new QSpinBox;
    npcX->setToolTip("Destination column for the selected schedule change. Changes 2 and 4 share a "
                     "destination.");
    npcY = new QSpinBox;
    npcY->setToolTip(
        "Destination row for the selected schedule change. Changes 2 and 4 share a destination.");
    npcFloor = new QSpinBox;
    npcFloor->setToolTip(
        "Destination floor: -1 is basement, 0 is ground floor. Click Move here to apply.");
    npcX->setObjectName("mapNpcX");
    npcY->setObjectName("mapNpcY");
    npcFloor->setObjectName("mapNpcFloor");
    npcX->setRange(0, 255);
    npcY->setRange(0, 255);
    npcFloor->setRange(-128, 127);
    form->addRow("X", npcX);
    form->addRow("Y", npcY);
    form->addRow("Floor", npcFloor);
    layout->addLayout(form);
    auto apply = new QPushButton("Move here");
    apply->setToolTip("Apply the destination X/Y/floor entered above");
    apply->setObjectName("mapNpcMove");
    layout->addWidget(apply);
    auto locate = new QPushButton("Locate on map");
    locate->setToolTip("Center the view on the selected destination without moving the NPC.");
    locate->setObjectName("mapNpcLocate");
    layout->addWidget(locate);
    npcGhosts = new QCheckBox("Other destinations");
    npcGhosts->setObjectName("mapNpcGhosts");
    npcGhosts->setToolTip("Show the selected NPC’s other scheduled destinations. Numbers "
                          "correspond to schedule changes 1–4.");
    layout->addWidget(npcGhosts);
    npcConversation = new QPushButton("Edit conversation");
    npcConversation->setToolTip("Open this NPC’s dialogue for editing.");
    npcConversation->setObjectName("mapNpcConversation");
    layout->addWidget(npcConversation);
    auto advanced = new QPushButton("Advanced schedule details");
    advanced->setToolTip("Edit behavior and other schedule fields; unknown values are preserved.");
    advanced->setObjectName("mapNpcAdvanced");
    layout->addWidget(advanced);
    layout->addStretch();
    connect(npcStartHour, &QSpinBox::editingFinished, this, [this] {
        if (restoring || canvas->selectedNpc < 0)
            return;
        const int settlement = document.pages[currentPage()].settlement;
        const int offset =
            MapNpcDocument(project, npcResource).offset(settlement, canvas->selectedNpc) + 12 +
            schedule->currentIndex();
        auto bytes = project->data(npcResource);
        if (U5::byte(bytes, offset) == npcStartHour->value())
            return;
        if (npcStartHour->value() > 23) {
            npcStartHour->setValue(U5::byte(bytes, offset));
            coordinate->setText("Choose a start hour from 0 to 23. Unusual stored hours can be "
                                "edited in Advanced schedule details.");
            return;
        }
        bytes[offset] = char(npcStartHour->value());
        if (commitResources({{npcResource, bytes}}, "Change NPC schedule start hour")) {
            refreshNpcs();
            saveView();
        }
    });
    connect(npcList, &QComboBox::currentIndexChanged, this, [this](int npc) {
        if (!restoring && npc >= 0)
            selectNpc(npc);
    });
    connect(npcGhosts, &QCheckBox::toggled, this, [this] {
        refreshNpcs();
        saveView();
    });
    connect(locate, &QPushButton::clicked, this, [this] { locateNpc(); });
    connect(apply, &QPushButton::clicked, this, [this] {
        if (moveNpcTo(canvas->selectedNpc, npcX->value(), npcY->value(), npcFloor->value()))
            locateNpc();
    });
    connect(advanced, &QPushButton::clicked, this, [this] {
        if (canvas->selectedNpc < 0)
            return;
        saveView();
        (*navigation)[npcResource + "/settlement"] = document.pages[currentPage()].settlement;
        (*navigation)[npcResource + "/npc"] = canvas->selectedNpc;
        (*navigation)[npcResource + "/character"] =
            document.pages[currentPage()].settlement * 32 + canvas->selectedNpc;
        try {
            if (project->resources.contains(talkResource)) {
                const auto conversations = U5::readDialogue(project->data(talkResource));
                MapNpcDocument npcs(project, npcResource);
                const int town = document.pages[currentPage()].settlement;
                for (int i = 0; i < conversations.size(); ++i)
                    if (int(conversations[i].id) == npcs.dialogue(town, canvas->selectedNpc)) {
                        (*navigation)[talkResource + "/character"] =
                            Workshop::npcCharacter(town, canvas->selectedNpc);
                        (*navigation)[talkResource + "/conversation"] = i;
                        (*navigation)[talkResource + "/characterTab"] = 1;
                        navigateResource(talkResource);
                        return;
                    }
            }
        } catch (const std::exception &) { /* Schedule details remain available. */
        }
        navigateResource(npcResource);
    });
    connect(npcConversation, &QPushButton::clicked, this, [this] {
        if (canvas->selectedNpc <= 0 || !project->resources.contains(talkResource))
            return;
        try {
            const int town = document.pages[currentPage()].settlement;
            const int slot = canvas->selectedNpc;
            MapNpcDocument npcs(project, npcResource);
            int id = npcs.dialogue(town, slot);
            if (!conversationIndices.contains(id)) {
                const auto changes = Workshop::createNpcConversation(*project, npcResource, town,
                                                                     slot, npcName(slot));
                saveView();
                if (!commitResources(changes, "Create NPC conversation"))
                    return;
                id = npcs.dialogue(town, slot);
            }
            auto conversations = U5::readDialogue(project->data(talkResource));
            for (int i = 0; i < conversations.size(); ++i)
                if (int(conversations[i].id) == id) {
                    saveView();
                    (*navigation)[talkResource + "/character"] = Workshop::npcCharacter(town, slot);
                    (*navigation)[talkResource + "/conversation"] = i;
                    (*navigation)[talkResource + "/entry"] = 0;
                    (*navigation)[talkResource + "/characterTab"] = 0;
                    navigateResource(talkResource);
                    return;
                }
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Cannot create conversation", e.what());
        }
    });
}
void MapWorkspace::refreshNpcs() {
    if (resource.endsWith(".CBT")) {
        refreshCombat();
        return;
    }
    canvas->actors.clear();
    canvas->ghosts.clear();
    const auto &mp = document.pages[currentPage()];
    QSignalBlocker blocked(npcList);
    npcList->clear();
    npcConversation->setEnabled(false);
    for (auto name : {"mapNpcMove", "mapNpcLocate", "mapNpcAdvanced"})
        findChild<QPushButton *>(name)->setEnabled(canvas->selectedNpc >= 0 && mp.settlement >= 0 &&
                                                   project->resources.contains(npcResource));
    if (mp.settlement < 0 || !project->resources.contains(npcResource) ||
        project->data(npcResource).size() < 4608) {
        npcInfo->setText("No valid NPC schedule resource for this map");
        return;
    }
    npcNames.clear();
    conversationIndices.clear();
    try {
        if (project->resources.contains(talkResource)) {
            auto conversations = U5::readDialogue(project->data(talkResource));
            for (int i = 0; i < conversations.size(); ++i) {
                int id = conversations[i].id;
                conversationIndices[id] = i;
                auto doc = Dialogue::parse(conversations[i].bytes);
                if (!doc.entries.isEmpty()) {
                    auto name = Dialogue::displayName(doc.entries[0].bytes);
                    if (!name.isEmpty())
                        npcNames[id] = name.left(60);
                }
            }
        }
    } catch (const std::exception &) {
    }
    MapNpcDocument npcs(project, npcResource);
    int slot = schedule->currentIndex();
    for (int npc = 0; npc < npcs.count(mp.settlement); ++npc) {
        npcList->addItem(QString("%1 · %2").arg(npc + 1).arg(npcName(npc)));
        if(auto list=qobject_cast<QListView *>(npcList->view()))
            list->setRowHidden(npc, npc == 0 || !U5::byte(project->data(npcResource),
                                                          npcs.shapeOffset(mp.settlement, npc)));
        auto loc = npcs.location(mp.settlement, npc, slot);
        if (npc > 0 && U5::byte(project->data(npcResource), npcs.shapeOffset(mp.settlement, npc)) &&
            loc.floor == mp.floor && loc.x < 32 && loc.y < 32)
            canvas->actors.append({loc.x, loc.y, npcs.sprite(mp.settlement, npc), npc});
    }
    {
        int hours[4]{-1, -1, -1, -1};
        bool consistent = true;
        for (int actor = 0; actor < npcs.count(mp.settlement); ++actor) {
            bool onMap = false;
            for (int change = 0; change < 4; ++change) {
                auto loc = npcs.location(mp.settlement, actor, change);
                onMap |= loc.floor == mp.floor && loc.x < 32 && loc.y < 32;
            }
            if (!onMap)
                continue;
            for (int change = 0; change < 4; ++change) {
                int hour = npcs.hour(mp.settlement, actor, change);
                if (hour >= 24 || (hours[change] >= 0 && hours[change] != hour))
                    consistent = false;
                hours[change] = hour;
            }
        }
        QSignalBlocker block(schedule);
        for (int change = 0; change < 4; ++change)
            schedule->setItemText(change,
                                  consistent && hours[change] >= 0
                                      ? QString("%1:00").arg(hours[change], 2, 10, QChar('0'))
                                      : QString::number(change + 1));
    }
    int npc = canvas->selectedNpc;
    npcList->setCurrentIndex(npc);
    if (npc < 0 || npc >= npcs.count(mp.settlement)) {
        npcInfo->setText("No NPC selected");
        npcSprite->clear();
        npcStartHour->setEnabled(false);
        npcNameField->setEnabled(false);
        npcAppearance->setEnabled(false);
        npcDialogue->setEnabled(false);
        npcBehavior->setEnabled(false);
        canvas->update();
        return;
    }
    auto loc = npcs.location(mp.settlement, npc, slot);
    npcAppearance->setEnabled(true);
    npcDialogue->setEnabled(true);
    npcBehavior->setEnabled(true);
    npcNameField->setText(npcName(npc));
    npcNameField->setEnabled(conversationIndices.contains(npcs.dialogue(mp.settlement, npc)));
    npcAppearance->setCurrentIndex(npcAppearance->findData(
        npcs.sprite(mp.settlement, npc) >= 512 ? npcs.sprite(mp.settlement, npc)
                                               : npcs.sprite(mp.settlement, npc) - 256));
    npcDialogue->clear();
    for (auto it = conversationIndices.begin(); it != conversationIndices.end(); ++it)
        npcDialogue->addItem(npcNames.value(it.key(), QString("Conversation %1").arg(it.key())),
                             it.key());
    npcDialogue->setCurrentIndex(npcDialogue->findData(npcs.dialogue(mp.settlement, npc)));
    npcBehavior->setCurrentIndex(npcBehavior->findData(loc.ai));
    npcStartHour->setEnabled(true);
    npcStartHour->setValue(npcs.hour(mp.settlement, npc, slot));
    npcX->setValue(loc.x);
    npcY->setValue(loc.y);
    npcFloor->setValue(loc.floor);
    int sprite = npcs.sprite(mp.settlement, npc), id = npcs.dialogue(mp.settlement, npc);
    npcSprite->setPixmap(QPixmap::fromImage(canvas->tiles[sprite])
                             .scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation));
    QString info =
        QString("%1\nSchedule change %2 starts at %3\n")
            .arg(npcName(npc))
            .arg(slot + 1)
            .arg(npcs.hour(mp.settlement, npc, slot) < 24
                     ? QString("%1:00").arg(npcs.hour(mp.settlement, npc, slot), 2, 10, QChar('0'))
                     : "an unusual time (see advanced details)");
    if (loc.x >= mp.side || loc.y >= mp.height())
        info += "Position is outside this map.\n";
    if (loc.floor != mp.floor)
        info += QString("Floor %1.\n").arg(loc.floor);
    QStringList positions;
    for (int s = 0; s < 4; ++s) {
        auto other = npcs.location(mp.settlement, npc, s);
        positions.append(QString("Change %1 starts at %2:00: (%3,%4), floor %5")
                             .arg(s + 1)
                             .arg(npcs.hour(mp.settlement, npc, s))
                             .arg(other.x)
                             .arg(other.y)
                             .arg(other.floor));
        if (npcGhosts->isChecked() && s != slot && other.floor == mp.floor && other.x < mp.side &&
            other.y < mp.height())
            canvas->ghosts.append({other.x, other.y, sprite, s});
    }
    info += "Scheduled destinations:\n" + positions.join("\n");
    npcConversation->setEnabled(npc > 0 && project->resources.contains(talkResource));
    npcConversation->setText(conversationIndices.contains(id) ? "Edit conversation"
                                                              : "Create conversation");
    npcConversation->setToolTip(
        conversationIndices.contains(id)
            ? "Open this NPC’s dialogue for editing."
            : "Create a new conversation for this NPC and open it for editing.");
    npcInfo->setText(info);
    canvas->update();
}
void MapWorkspace::selectNpc(int npc) {
    if (resource.endsWith(".CBT")) {
        canvas->selectedNpc = npc;
        refreshCombat();
        saveView();
        return;
    }
    canvas->selectedNpc = npc;
    (*navigation)[resource + "/actor/" +
                  QString::number(document.pages[currentPage()].settlement)] = npc;
    refreshNpcs();
    saveView();
}
bool MapWorkspace::moveNpcTo(int npc, int x, int y, int floor) {
    try {
        require(npc > 0, "Select an NPC before moving");
        int settlement = document.pages[currentPage()].settlement;
        bool available = false;
        for (auto mp : document.pages)
            if (mp.settlement == settlement && mp.floor == floor)
                available = true;
        require(available, "That floor does not exist in this settlement");
        MapNpcDocument npcs(project, npcResource);
        auto destination = npcs.location(settlement, npc, schedule->currentIndex());
        destination.x = x;
        destination.y = y;
        destination.floor = floor;
        int destinationPage = currentPage();
        for (int i = 0; i < document.pages.size(); ++i)
            if (document.pages[i].settlement == settlement && document.pages[i].floor == floor)
                destinationPage = i;
        Workshop::validateActorPlacement(*project, resource, destinationPage, {x, y},
                                         npcs.sprite(settlement, npc), npc,
                                         schedule->currentIndex());
        auto bytes = npcs.move(settlement, npc, schedule->currentIndex(), destination);
        if (bytes != project->data(npcResource) &&
            !commitResources({{npcResource, bytes}}, "Move NPC " + npcName(npc)))
            return false;
        refreshNpcs();
        saveView();
        return true;
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "NPC move rejected", QString::fromUtf8(e.what()));
        return false;
    }
}
void MapWorkspace::locateNpc() {
    if (canvas->selectedNpc < 0 || !project->resources.contains(npcResource))
        return;
    int npc = canvas->selectedNpc;
    auto current = document.pages[currentPage()];
    MapNpcDocument npcs(project, npcResource);
    auto loc = npcs.location(current.settlement, npc, schedule->currentIndex());
    if (loc.x >= current.side || loc.y >= current.height()) {
        coordinate->setText("Stored NPC position is outside map bounds");
        return;
    }
    for (int i = 0; i < document.pages.size(); ++i)
        if (document.pages[i].settlement == current.settlement &&
            document.pages[i].floor == loc.floor) {
            if (currentPage() != i) {
                auto &target = (*states)[resource + "/" + QString::number(i)];
                target.schedule = schedule->currentIndex();
                target.npc = npc;
                target.ghosts = npcGhosts->isChecked();
                target.tool = MapCanvas::InspectNpc;
                selectPage(i);
            }
            selectNpc(npc);
            QTimer::singleShot(0, this,
                               [this, loc] { view->centerMap({loc.x + 0.5, loc.y + 0.5}); });
            return;
        }
    coordinate->setText("Stored NPC floor has no map page; record is preserved");
}

void MapWorkspace::exportImage(bool ids, bool preview) {
    try {
        auto path = QFileDialog::getSaveFileName(this,
                                                 preview ? "Export visible preview with overlays"
                                                 : ids   ? "Export tile IDs"
                                                         : "Export terrain",
                                                 {}, "PNG (*.png)");
        if (path.isEmpty())
            return;
        require(QFileInfo(path).absolutePath() != project->sourceDirectory,
                "Export images outside the original game folder");
        QImage image;
        if (preview) {
            const QRect visible = canvas->rect().intersected(
                QRect(canvas->mapFrom(view->viewport(), QPoint(0, 0)), view->viewport()->size()));
            image = canvas->grab(visible).toImage();
        } else {
            // Use the document, never a displayed trigger-preview buffer.
            image = document.terrainImage(currentPage(), canvas->tiles, ids);
        }
        require(image.save(path, "PNG"), "Cannot export PNG");
        coordinate->setText(QString("Saved %1: %2")
                                .arg(preview ? "visible preview with overlays"
                                     : ids   ? "tile IDs"
                                             : "stored terrain")
                                .arg(QFileInfo(path).fileName()));
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Map export", QString::fromUtf8(e.what()));
    }
}

// Metadata rows verified against src/macros.h and COMBAT_111a in src/combat.c.
namespace {
QPair<int, int> combatOffsets(int base, int entity, int entry) {
    if (entity < 6) {
        const int rows[] = {4, 1, 3, 2}; // North, East, South, West
        return {base + rows[entry] * 32 + 11 + entity, base + rows[entry] * 32 + 17 + entity};
    }
    if (entity < 22)
        return {base + 6 * 32 + 11 + entity - 6, base + 7 * 32 + 11 + entity - 6};
    return {base + 8 * 32 + 11 + entity - 22, base + 8 * 32 + 19 + entity - 22};
}
} // namespace
void MapWorkspace::createCombatInspector(QVBoxLayout *layout) {
    layout->addWidget(new QLabel("Encounter authoring"));
    npcGhosts = new QCheckBox(this);
    npcGhosts->hide();
    combatEntry = new QComboBox;
    combatEntry->setToolTip(
        "Choose the party entry direction whose starting positions are displayed.");
    combatEntry->setObjectName("combatEntry");
    combatEntry->setAccessibleName("Party entry direction");
    combatEntry->addItems({"North entry", "East entry", "South entry", "West entry"});
    combatEntry->setCurrentIndex(navigation->value(resource + "/entry", 0));
    layout->addWidget(combatEntry);
    combatEntity = new QComboBox;
    combatEntity->setToolTip(
        "Select a party start, monster start, or trigger to inspect and move.");
    combatEntity->setObjectName("combatEntity");
    combatEntity->setAccessibleName("Encounter character or trigger");
    combatEntity->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combatEntity->setMinimumContentsLength(12);
    combatEntity->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(combatEntity);
    combatInfo = new QLabel;
    combatInfo->setToolTip(
        "Details of the selected encounter marker, including disabled or out-of-bounds records.");
    combatInfo->setWordWrap(true);
    combatInfo->setTextFormat(Qt::PlainText);
    layout->addWidget(combatInfo);
    monsterAppearance = new QComboBox;
    monsterAppearance->setObjectName("combatMonsterAppearance");
    monsterAppearance->setToolTip("Monster species for this combat-room placement.");
    if (resource == "BRIT.CBT") monsterAppearance->addItem("From encounter", 0);
    else for (int shape : Workshop::monsterShapes())
        monsterAppearance->addItem(Workshop::actorName(shape), shape);
    for (auto record : CustomTiles::records(*project)) {
        TypeDef d;
        TYPES_Decode(reinterpret_cast<const unsigned char *>(record.constData()), &d);
        if (d.kind && d.id >= 512)
            monsterAppearance->addItem(
                QIcon(QPixmap::fromImage(CustomTiles::image(*project, d.id))),
                QString::fromUtf8(d.name), d.id);
    }
    auto monsterTypeRow = new QHBoxLayout;
    monsterTypeRow->addWidget(monsterAppearance, 1);
    auto editMonsterType = new QToolButton;
    editMonsterType->setText("Edit type…");
    editMonsterType->setToolTip("Edit this shared creature type’s artwork, stats and abilities.");
    monsterTypeRow->addWidget(editMonsterType);
    layout->addLayout(monsterTypeRow);
    connect(editMonsterType, &QToolButton::clicked, this, [this] {
        unsigned type = monsterAppearance->currentData().toUInt();
        if (editTileType && monsterAppearance->currentIndex() >= 0)
            editTileType(type >= 512 ? type : 256 + type);
    });
    connect(monsterAppearance, &QComboBox::activated, this, [this] {
        int entity = canvas->selectedNpc;
        if (entity < 6 || entity >= 22)
            return;
        auto bytes = project->data(resource);
        unsigned type = monsterAppearance->currentData().toUInt();
        if (resource == "DUNGEON.CBT")
            bytes[document.pages[currentPage()].offset + 5 * 32 + 11 + entity - 6] =
                char(type >= 512 ? CustomTiles::definition(*project, type).base : type);
        auto changes = CustomTiles::assignActor(*project, resource, document.pages[currentPage()],
                                                currentPage(), entity - 6, type);
        changes[resource] = bytes;
        if (commitResources(changes, "Change monster"))
            refreshCombat();
    });
    triggerProperties = new QWidget;
    auto triggerForm = new QFormLayout(triggerProperties);
    triggerForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    triggerTile = new QComboBox;
    triggerTile->setObjectName("combatTriggerTile");
    triggerTile->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    triggerTile->setMinimumContentsLength(12);
    triggerTile->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    triggerTile->setToolTip("Tile placed at both destinations when a party member steps here.");
    for (int id = 0; id < 256; id++)
        triggerTile->addItem(MapDocument::tileName(id), id);
    triggerForm->addRow("Replace with", triggerTile);
    auto editTrigger = [this] {
        int entity = canvas->selectedNpc;
        if (entity < 22 || entity >= 30)
            return;
        auto b = project->data(resource);
        int base = document.pages[currentPage()].offset, i = entity - 22;
        b[base + 11 + i] = char(triggerTile->currentData().toInt());
        for (int j = 0; j < 2; j++) {
            b[base + (9 + j) * 32 + 11 + i] = char(triggerX[j]->value());
            b[base + (9 + j) * 32 + 19 + i] = char(triggerY[j]->value());
        }
        Project view = *project;
        QMap<QString, QByteArray> changes;
        for (int j = 0; j < 2; j++) {
            auto flags = Workshop::setMapMarker(view, resource, currentPage(), 48 + j * 8 + i,
                                                triggerEnabled[j]->isChecked());
            for (auto it = flags.begin(); it != flags.end(); ++it) {
                changes[it.key()] = it.value();
                view.resources[it.key()].edited = it.value();
            }
            if (!triggerEnabled[j]->isChecked() && flags.isEmpty()) {
                b[base + (9 + j) * 32 + 11 + i] = char(255);
                b[base + (9 + j) * 32 + 19 + i] = char(255);
            }
        }
        changes[resource] = b;
        if (commitResources(changes, "Edit trigger destinations"))
            refreshCombat();
    };
    connect(triggerTile, &QComboBox::activated, this, [editTrigger] { editTrigger(); });
    for (int j = 0; j < 2; j++) {
        auto row = new QHBoxLayout;
        triggerEnabled[j] = new QCheckBox;
        triggerEnabled[j]->setToolTip("Enable this destination.");
        row->addWidget(triggerEnabled[j]);
        connect(triggerEnabled[j], &QCheckBox::clicked, this, [editTrigger] { editTrigger(); });
        triggerX[j] = new QSpinBox;
        triggerY[j] = new QSpinBox;
        for (auto spin : {triggerX[j], triggerY[j]}) {
            spin->setRange(0, 255);
            spin->setToolTip("Destination tile coordinate.");
            connect(spin, &QSpinBox::editingFinished, this, editTrigger);
        }
        triggerX[j]->setObjectName(QString("combatTriggerX%1").arg(j));
        triggerY[j]->setObjectName(QString("combatTriggerY%1").arg(j));
        row->addWidget(new QLabel("X"));
        row->addWidget(triggerX[j]);
        row->addWidget(new QLabel("Y"));
        row->addWidget(triggerY[j]);
        triggerForm->addRow(QString("Destination %1").arg(j + 1), row);
    }
    layout->addWidget(triggerProperties);
    combatPreview = new QCheckBox("Preview trigger result");
    combatPreview->setToolTip(
        "Preview terrain changes caused by the selected trigger without modifying the map.");
    combatPreview->setObjectName("combatPreview");
    combatPreview->setChecked(navigation->value(resource + "/preview", 0));
    layout->addWidget(combatPreview);
    layout->addStretch();
    connect(combatEntry, &QComboBox::currentIndexChanged, this, [this](int value) {
        canvas->cancelStroke();
        (*navigation)[resource + "/entry"] = value;
        refreshCombat();
    });
    connect(combatEntity, &QComboBox::currentIndexChanged, this, [this](int value) {
        canvas->cancelStroke();
        canvas->selectedNpc = value;
        refreshCombat();
        saveView();
    });
    connect(combatPreview, &QCheckBox::toggled, this, [this](bool value) {
        canvas->cancelStroke();
        (*navigation)[resource + "/preview"] = value;
        refreshCombat();
    });
}
void MapWorkspace::refreshCombat() {
    const auto geometry = document.pages[currentPage()];
    canvas->actors.clear();
    canvas->ghosts.clear();
    canvas->ids = document.terrain(currentPage());
    canvas->extended = CustomTiles::expanded(*project, resource, document.pages[currentPage()],
                                             currentPage(), canvas->ids);
    canvas->libraryUuid = project->resources.value("TYPES.OVR").edited.mid(8, 16);
    const auto bytes = project->data(resource);
    const int base = document.pages[currentPage()].offset;
    QSignalBlocker blocked(combatEntity);
    combatEntity->clear();
    QString info;
    for (int entity = 0; entity < 30; ++entity) {
        auto offsets = combatOffsets(base, entity, combatEntry->currentIndex());
        int x = U5::byte(bytes, offsets.first), y = U5::byte(bytes, offsets.second);
        QString label = entity < 6    ? QString("P%1").arg(entity + 1)
                        : entity < 22 ? QString("M%1").arg(entity - 5)
                                      : QString("T%1").arg(entity - 21);
        bool enabled = entity < 6 || entity >= 22 || resource == "BRIT.CBT" ||
                       U5::byte(bytes, base + 5 * 32 + 11 + entity - 6) != 0;
        int bit = entity < 6
                      ? (QList<int>{4, 1, 3, 2}[combatEntry->currentIndex()] - 1) * 6 + entity
                  : entity < 22 ? 24 + entity - 6
                                : 40 + entity - 22;
        enabled &= Workshop::mapMarkerActive(*project, resource, currentPage(), bit, x, y);
        combatEntity->addItem(QString("%1 · (%2, %3)%4")
                                  .arg(label)
                                  .arg(x)
                                  .arg(y)
                                  .arg(!enabled ? " · disabled"
                                       : x >= geometry.side || y >= geometry.height() ? " · outside"
                                                                                      : ""));
        if (enabled && x < geometry.side && y < geometry.height())
            canvas->actors.append({x, y,
                                   entity >= 6 && entity < 22 && resource == "DUNGEON.CBT"
                                       ? 256 + int(U5::byte(bytes, base + 5 * 32 + 11 + entity - 6))
                                       : 0,
                                   entity, label});
        if (entity != canvas->selectedNpc)
            continue;
        info = combatEntity->itemText(entity);
        if (entity >= 22) {
            int trigger = entity - 22, tile = U5::byte(bytes, base + 11 + trigger);
            info += QString("\nReplacement tile %1").arg(tile);
            for (int row : {9, 10}) {
                int cx = U5::byte(bytes, base + row * 32 + 11 + trigger);
                int cy = U5::byte(bytes, base + row * 32 + 19 + trigger);
                info +=
                    QString("\nChange %1: (%2, %3)%4")
                        .arg(row - 8)
                        .arg(cx)
                        .arg(cy)
                        .arg(cx >= geometry.side || cy >= geometry.height() ? " (ignored by engine)"
                                                                            : "");
                if (cx < geometry.side && cy < geometry.height() &&
                    Workshop::mapMarkerActive(*project, resource, currentPage(),
                                              48 + (row - 9) * 8 + trigger, cx, cy)) {
                    canvas->ghosts.append(
                        {cx, cy, tile, row - 8, QString("Change %1").arg(row - 8)});
                    if (combatPreview->isChecked() && canvas->tool == MapCanvas::InspectNpc)
                        canvas->ids[cy * geometry.side + cx] = char(tile);
                }
            }
        }
    }
    combatEntity->setCurrentIndex(canvas->selectedNpc);
    combatInfo->setText(info.isEmpty() ? "Select a marker or record." : info);
    monsterAppearance->setVisible(canvas->selectedNpc >= 6 && canvas->selectedNpc < 22);
    if (monsterAppearance->isVisible())
        monsterAppearance->setCurrentIndex(monsterAppearance->findData(
            CustomTiles::actorType(*project, resource, document.pages[currentPage()], currentPage(),
                                   canvas->selectedNpc - 6,
                                   resource == "BRIT.CBT" ? 0 : U5::byte(bytes, base + 5 * 32 + 11 + canvas->selectedNpc - 6))));
    triggerProperties->setVisible(canvas->selectedNpc >= 22);
    if (canvas->selectedNpc >= 22) {
        int i = canvas->selectedNpc - 22;
        triggerTile->setCurrentIndex(triggerTile->findData(U5::byte(bytes, base + 11 + i)));
        for (int j = 0; j < 2; j++) {
            bool enabled =
                Workshop::mapMarkerActive(*project, resource, currentPage(), 48 + j * 8 + i,
                                          U5::byte(bytes, base + (9 + j) * 32 + 11 + i),
                                          U5::byte(bytes, base + (9 + j) * 32 + 19 + i));
            triggerEnabled[j]->setChecked(enabled);
            triggerX[j]->setEnabled(enabled);
            triggerY[j]->setEnabled(enabled);
            triggerX[j]->setRange(0, geometry.side - 1);
            triggerY[j]->setRange(0, geometry.height() - 1);
            triggerX[j]->setValue(U5::byte(bytes, base + (9 + j) * 32 + 11 + i));
            triggerY[j]->setValue(U5::byte(bytes, base + (9 + j) * 32 + 19 + i));
        }
    }
    combatPreview->setEnabled(canvas->selectedNpc >= 22);
    for (auto &actor : canvas->actors)
        if (actor.npc >= 6 && actor.npc < 22)
            actor.tile = CustomTiles::actorType(*project, resource, document.pages[currentPage()],
                                                currentPage(), actor.npc - 6, actor.tile);
    canvas->update();
}
bool MapWorkspace::moveCombat(int entity, QPoint destination) {
    if (entity < 0 || entity >= 30 || destination.x() < 0 || destination.y() < 0 ||
        destination.x() >= document.pages[currentPage()].side ||
        destination.y() >= document.pages[currentPage()].height())
        return false;
    for (const auto &actor : canvas->actors)
        if (actor.npc == entity) {
            try {
                Workshop::validateActorPlacement(*project, resource, currentPage(), destination,
                                                 actor.tile, entity - 6);
            } catch (const std::exception &e) {
                QMessageBox::warning(this, "Move rejected", QString::fromUtf8(e.what()));
                return false;
            }
        }
    auto bytes = project->data(resource);
    auto offsets =
        combatOffsets(document.pages[currentPage()].offset, entity, combatEntry->currentIndex());
    bytes[offsets.first] = char(destination.x());
    bytes[offsets.second] = char(destination.y());
    int bit = entity < 6    ? (QList<int>{4, 1, 3, 2}[combatEntry->currentIndex()] - 1) * 6 + entity
              : entity < 22 ? 24 + entity - 6
                            : 40 + entity - 22;
    auto edits = Workshop::setMapMarker(*project, resource, currentPage(), bit, true);
    edits[resource] = bytes;
    if (!commitResources(edits, "Move encounter marker"))
        return false;
    refreshCombat();
    saveView();
    return true;
}

namespace {
const QMap<int, QPair<QString, QString>> &tileCatalog() {
    // Descriptions are backed by the engine's named symbols in src/tiles.h.
    // Numeric symbols are classified from the original DOS artwork, including
    // animation frames and rendering masks (which are not ordinary terrain).
    static const QMap<int, QPair<QString, QString>> catalog{
        {TILE_MAP_WATER_1, {"Deep water", "Ground"}},
        {TILE_MAP_WATER_2, {"Water", "Ground"}},
        {TILE_MAP_WATER_3, {"Shoals", "Ground"}},
        {TILE_MAP_POISON, {"Swamp", "Ground"}},
        {TILE_MAP_GRASS, {"Grass", "Ground"}},
        {TILE_MAP_HUT, {"Hut location", "Other"}},
        {TILE_MAP_CODEX, {"Codex shrine location", "Other"}},
        {TILE_MAP_KEEP, {"Keep location", "Other"}},
        {TILE_MAP_VILLAGE, {"Village location", "Other"}},
        {TILE_MAP_TOWNE, {"Town location", "Other"}},
        {TILE_MAP_CASTLE, {"Castle location", "Other"}},
        {TILE_MAP_CAVE, {"Cave entrance", "Other"}},
        {TILE_MAP_MINE, {"Mine entrance", "Other"}},
        {TILE_MAP_DUNGEON, {"Dungeon entrance", "Other"}},
        {TILE_MAP_SHRINE, {"Shrine location", "Other"}},
        {TILE_MAP_RUINS, {"Ruined shrine location", "Other"}},
        {TILE_MAP_LIGHTHOUSE, {"Lighthouse location", "Other"}},
        {TILE_MAP_STUMP, {"Hollow stump", "Objects"}},
        {TILE_MAP_CROPS_PICKED, {"Crops picked", "Ground"}},
        {TILE_MAP_CROPS, {"Crops", "Ground"}},
        {TILE_MAP_TREE, {"Fruit tree", "Objects"}},
        {TILE_MAP_PALACEBT, {"Blackthorn’s castle location", "Other"}},
        {TILE_MAP_CASTLELB, {"Lord British’s castle location", "Other"}},
        {TILE_MAP_HIDDEN_DOOR, {"Hidden door", "Buildings"}},
        {TILE_MAP_WALL, {"Wall", "Buildings"}},
        {TILE_MAP_SHELF, {"Window shelf", "Buildings"}},
        {TILE_MAP_BOOKSHELF, {"Bookshelf", "Objects"}},
        {TILE_MAP_TRAPDOOR, {"Loose brick / trapdoor", "Buildings"}},
        {TILE_MAP_LAVA, {"Lava", "Ground"}},
        {TILE_MAP_CHAIR_90, {"Chair", "Objects"}},
        {TILE_MAP_CHAIR_91, {"Chair", "Objects"}},
        {TILE_MAP_CHAIR_92, {"Chair", "Objects"}},
        {TILE_MAP_CHAIR_93, {"Chair", "Objects"}},
        {TILE_MAP_TABLE_94, {"Table", "Objects"}},
        {TILE_MAP_TABLE_95, {"Table", "Objects"}},
        {TILE_MAP_TABLE_96, {"Table", "Objects"}},
        {TILE_MAP_TABLE_9A, {"Table", "Objects"}},
        {TILE_MAP_TABLE_9B, {"Table", "Objects"}},
        {TILE_MAP_TABLE_9C, {"Table", "Objects"}},
        {TILE_MAP_MIRROR, {"Mirror", "Objects"}},
        {TILE_MAP_MIRROR_9E, {"Mirror", "Objects"}},
        {TILE_MAP_MIRROR_BROKEN, {"Mirror broken", "Objects"}},
        {TILE_MAP_WELL, {"Well", "Objects"}},
        {TILE_MAP_DESK, {"Desk", "Objects"}},
        {TILE_MAP_BARREL, {"Barrel", "Objects"}},
        {TILE_MAP_VANITY, {"Vanity", "Objects"}},
        {TILE_MAP_DRESSER, {"Dresser", "Objects"}},
        {TILE_MAP_TRUNK, {"Trunk", "Objects"}},
        {TILE_MAP_BRAZIER, {"Brazier", "Objects"}},
        {TILE_MAP_FLAME, {"Roasting spit", "Objects"}},
        {TILE_MAP_CANNON_B4, {"Cannon", "Objects"}},
        {TILE_MAP_CANNON_B5, {"Cannon", "Objects"}},
        {TILE_MAP_CANNON_B6, {"Cannon", "Objects"}},
        {TILE_MAP_CANNON_B7, {"Cannon", "Objects"}},
        {TILE_MAP_DOOR_B8, {"Door", "Buildings"}},
        {TILE_MAP_DOOR_B9, {"Door", "Buildings"}},
        {TILE_MAP_DOOR_BA, {"Door", "Buildings"}},
        {TILE_MAP_DOOR_BB, {"Door", "Buildings"}},
        {TILE_MAP_FIREPLACE, {"Fireplace", "Buildings"}},
        {TILE_MAP_TABLE_BE, {"Candelabrum", "Objects"}},
        {TILE_MAP_STAIR, {"Stair", "Buildings"}},
        {TILE_MAP_LADDER_UP, {"Ladder up", "Buildings"}},
        {TILE_MAP_LADDER_DOWN, {"Ladder down", "Buildings"}},
        {TILE_MAP_WATERFALL, {"Waterfall", "Ground"}},
        {TILE_MAP_FOUNTAIN, {"Fountain", "Objects"}},
        {TILE_MAP_MOONGATE, {"Moongate", "Objects"}},
        {TILE_MAP_WATER_E4, {"Water", "Ground"}},
        {TILE_MAP_WATER_E7, {"Water", "Ground"}},
        {TILE_MAP_SIGN_F0, {"Provisioner sign", "Objects"}},
        {TILE_MAP_SIGN_F8, {"Sign", "Objects"}},
        {TILE_MAP_CLOCK, {"Clock", "Objects"}},
        {TILE_MAP_BELLOWS_FC, {"Bellows", "Objects"}},
        {TILE_MAP_BELLOWS_FD, {"Bellows", "Objects"}},
        {0x00, {"Magic flash", "Other"}},
        {0x06, {"Brush", "Ground"}},
        {0x07, {"Desert", "Ground"}},
        {0x08, {"Brush", "Ground"}},
        {0x09, {"Forest", "Ground"}},
        {0x0a, {"Tropical forest", "Ground"}},
        {0x0b, {"Hills", "Ground"}},
        {0x0c, {"Mountains", "Ground"}},
        {0x0d, {"High mountains", "Ground"}},
        {0x0e, {"Foothills and grass", "Ground"}},
        {0x0f, {"Foothills and forest", "Ground"}},
        {0x1c, {"Banner", "Objects"}},
        {0x1d, {"Bridge", "Buildings"}},
        {0x1e, {"Desert vegetation", "Ground"}},
        {0x1f, {"Desert vegetation", "Ground"}},
        {0x20, {"Road", "Ground"}},
        {0x21, {"Road edge", "Ground"}},
        {0x22, {"Road corner", "Ground"}},
        {0x23, {"Road corner", "Ground"}},
        {0x24, {"Road corner", "Ground"}},
        {0x25, {"Road corner", "Ground"}},
        {0x26, {"Road junction", "Ground"}},
        {0x27, {"Roof", "Buildings"}},
        {0x28, {"Roof", "Buildings"}},
        {0x29, {"Crystal ball", "Objects"}},
        {0x2a, {"Emblem", "Objects"}},
        {0x2f, {"Cactus", "Ground"}},
        {0x30, {"Grass edge", "Ground"}},
        {0x31, {"Grass edge", "Ground"}},
        {0x32, {"Road and grass", "Ground"}},
        {0x33, {"Road and grass", "Ground"}},
        {0x34, {"Riverbank", "Ground"}},
        {0x35, {"Riverbank", "Ground"}},
        {0x36, {"Riverbank", "Ground"}},
        {0x37, {"Riverbank", "Ground"}},
        {0x38, {"Gargoyle statue", "Objects"}},
        {0x3a, {"Castle location tower", "Other"}},
        {0x3b, {"Castle location battlements", "Other"}},
        {0x3c, {"Castle location tower", "Other"}},
        {0x3d, {"Castle location tower", "Other"}},
        {0x3f, {"Castle location tower", "Other"}},
        {0x40, {"Wood floor", "Ground"}},
        {0x41, {"Codex", "Objects"}},
        {0x42, {"Mast", "Objects"}},
        {0x43, {"Railing", "Buildings"}},
        {0x44, {"Brick floor", "Ground"}},
        {0x45, {"Stone floor", "Ground"}},
        {0x46, {"Pillar", "Objects"}},
        {0x47, {"Bridge", "Buildings"}},
        {0x48, {"Bridge", "Buildings"}},
        {0x49, {"Bridge", "Buildings"}},
        {0x4a, {"Arrow slit", "Buildings"}},
        {0x4b, {"Window", "Buildings"}},
        {0x4c, {"Pile of rocks", "Ground"}},
        {0x4d, {"Stone wall", "Buildings"}},
        {0x50, {"Crenellation corner", "Buildings"}},
        {0x51, {"Crenellation corner", "Buildings"}},
        {0x52, {"Crenellation corner", "Buildings"}},
        {0x53, {"Crenellation corner", "Buildings"}},
        {0x54, {"Crenellation corner", "Buildings"}},
        {0x55, {"Crenellation corner", "Buildings"}},
        {0x56, {"Crenellation wall", "Buildings"}},
        {0x57, {"Crenellation wall", "Buildings"}},
        {0x58, {"Anvil", "Objects"}},
        {0x59, {"Telescope", "Objects"}},
        {0x5b, {"Potted plant", "Objects"}},
        {0x5d, {"Bookshelf", "Objects"}},
        {0x5e, {"Codex guardian", "Objects"}},
        {0x5f, {"Codex guardian", "Objects"}},
        {0x60, {"River", "Ground"}},
        {0x61, {"River", "Ground"}},
        {0x62, {"River", "Ground"}},
        {0x63, {"River", "Ground"}},
        {0x64, {"River", "Ground"}},
        {0x65, {"River", "Ground"}},
        {0x66, {"River", "Ground"}},
        {0x67, {"River", "Ground"}},
        {0x68, {"River", "Ground"}},
        {0x69, {"River", "Ground"}},
        {0x6a, {"Wooden bridge", "Buildings"}},
        {0x6b, {"Wooden bridge", "Buildings"}},
        {0x6c, {"River", "Ground"}},
        {0x6d, {"River", "Ground"}},
        {0x6e, {"River", "Ground"}},
        {0x6f, {"River", "Ground"}},
        {0x70, {"Dungeon corridor mask", "Other"}},
        {0x71, {"Dungeon corridor mask", "Other"}},
        {0x72, {"Dungeon corridor mask", "Other"}},
        {0x73, {"Dungeon corridor mask", "Other"}},
        {0x74, {"Dungeon corridor mask", "Other"}},
        {0x75, {"Dungeon corridor mask", "Other"}},
        {0x76, {"Dungeon corridor mask", "Other"}},
        {0x77, {"Dungeon corridor mask", "Other"}},
        {0x78, {"Dungeon corridor mask", "Other"}},
        {0x79, {"Dungeon corridor mask", "Other"}},
        {0x7a, {"Dungeon corridor mask", "Other"}},
        {0x7b, {"Dungeon corridor mask", "Other"}},
        {0x7c, {"Dungeon corridor mask", "Other"}},
        {0x7d, {"Dungeon corridor mask", "Other"}},
        {0x7e, {"Dungeon corridor mask", "Other"}},
        {0x7f, {"Dungeon corridor mask", "Other"}},
        {0x80, {"Pendulum", "Objects"}},
        {0x81, {"Pendulum", "Objects"}},
        {0x82, {"Pendulum", "Objects"}},
        {0x83, {"Pendulum", "Objects"}},
        {0x84, {"Stocks", "Objects"}},
        {0x85, {"Manacles", "Objects"}},
        {0x86, {"Metal grate", "Buildings"}},
        {0x87, {"Archway", "Buildings"}},
        {0x88, {"Cannonballs", "Objects"}},
        {0x89, {"Cross", "Objects"}},
        {0x8a, {"Gravestone", "Objects"}},
        {0x8b, {"Torture rack", "Objects"}},
        {0x8d, {"Harpsichord", "Objects"}},
        {0x8e, {"Guillotine", "Objects"}},
        {0x97, {"Odd door", "Buildings"}},
        {0x98, {"Odd door", "Buildings"}},
        {0x99, {"Bars", "Buildings"}},
        {0xa0, {"Signpost", "Objects"}},
        {0xa2, {"Hitching post", "Objects"}},
        {0xa3, {"Stack of logs", "Objects"}},
        {0xa4, {"Stone cross", "Objects"}},
        {0xa7, {"Wine cask", "Objects"}},
        {0xa9, {"Pitcher", "Objects"}},
        {0xaa, {"Carpet", "Objects"}},
        {0xab, {"Bed", "Objects"}},
        {0xac, {"Bed", "Objects"}},
        {0xae, {"End table", "Objects"}},
        {0xb0, {"Wall torch", "Objects"}},
        {0xb1, {"Wall torch", "Objects"}},
        {0xbd, {"Street lamp", "Objects"}},
        {0xbf, {"Stove", "Objects"}},
        {0xc0, {"Stone rendering fragment", "Other"}},
        {0xc1, {"Stone rendering fragment", "Other"}},
        {0xc2, {"Stone rendering fragment", "Other"}},
        {0xc3, {"Rendering fragment", "Other"}},
        {0xc5, {"Stairs", "Buildings"}},
        {0xc6, {"Stairs", "Buildings"}},
        {0xc7, {"Stairs", "Buildings"}},
        {0xca, {"Wooden fence", "Buildings"}},
        {0xcb, {"Wooden fence", "Buildings"}},
        {0xcc, {"Brick rendering fragment", "Other"}},
        {0xcd, {"Brick rendering fragment", "Other"}},
        {0xce, {"Vegetation rendering fragment", "Other"}},
        {0xcf, {"Vegetation rendering fragment", "Other"}},
        {0xd0, {"Diagonal wall mask", "Other"}},
        {0xd1, {"Diagonal wall mask", "Other"}},
        {0xd2, {"Diagonal wall mask", "Other"}},
        {0xd3, {"Diagonal wall mask", "Other"}},
        {0xd5, {"Waterfall animation", "Ground"}},
        {0xd6, {"Waterfall animation", "Ground"}},
        {0xd7, {"Waterfall animation", "Ground"}},
        {0xd9, {"Fountain animation", "Objects"}},
        {0xda, {"Fountain animation", "Objects"}},
        {0xdb, {"Fountain animation", "Objects"}},
        {0xdd, {"Desert animation", "Ground"}},
        {0xde, {"Sacred flame", "Objects"}},
        {0xdf, {"Collapsed dungeon entrance", "Other"}},
        {0xe0, {"Multi-tile object base", "Other"}},
        {0xe1, {"Multi-tile object left edge", "Other"}},
        {0xe2, {"Multi-tile object right edge", "Other"}},
        {0xe3, {"Flagpole", "Objects"}},
        {0xe5, {"Water edge", "Ground"}},
        {0xe6, {"Water edge", "Ground"}},
        {0xe8, {"Hourglass", "Objects"}},
        {0xe9, {"Hourglass", "Objects"}},
        {0xea, {"Hourglass", "Objects"}},
        {0xeb, {"Hourglass", "Objects"}},
        {0xec, {"Britannia banner", "Objects"}},
        {0xed, {"Britannia banner", "Objects"}},
        {0xee, {"Britannia banner", "Objects"}},
        {0xef, {"Britannia banner", "Objects"}},
        {0xf1, {"Government sign", "Objects"}},
        {0xf2, {"Armoury sign", "Objects"}},
        {0xf3, {"Healer sign", "Objects"}},
        {0xf4, {"Stable sign", "Objects"}},
        {0xf5, {"Guild sign", "Objects"}},
        {0xf6, {"Inn sign", "Objects"}},
        {0xf7, {"Apothecary sign", "Objects"}},
        {0xf9, {"Shipwright sign", "Objects"}},
        {0xfb, {"Clock animation", "Objects"}},
        {0xfe, {"Bellows", "Objects"}},
        {0xff, {"Bellows", "Objects"}},
    };
    return catalog;
}
} // namespace
QString MapDocument::tileName(int id) {
    return tileCatalog().value(id, {"Unidentified tile", "Uncategorized"}).first;
}
QString MapDocument::tileCategory(int id) {
    return tileCatalog().value(id, {"Unidentified tile", "Uncategorized"}).second;
}
QImage MapDocument::terrainImage(int page, const QVector<QImage> &tiles, bool ids) const {
    auto cells = terrain(page);
    int side = pages[page].side;
    int rows = pages[page].height();
    auto ext = CustomTiles::expanded(*project, resource, pages[page], page, cells);
    bool custom = false;
    for (int i = 0; i < cells.size(); i++)
        custom |= TYPES_U32(ext.constData() + i * 4) >= 512;
    QImage image(side * (ids ? 1 : 16), rows * (ids ? 1 : 16),
                 ids ? (custom ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8)
                     : QImage::Format_RGB32);
    require(!image.isNull(), "Cannot allocate map export");
    image.fill(Qt::black);
    if (ids) {
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < side; x++) {
                unsigned id = TYPES_U32(ext.constData() + (y * side + x) * 4);
                if (!id)
                    id = U5::byte(cells, y * side + x);
                if (custom)
                    reinterpret_cast<quint16 *>(image.scanLine(y))[x] = id;
                else
                    image.scanLine(y)[x] = id;
            }
    } else {
        MapCanvas canvas;
        canvas.ids = cells;
        canvas.extended = ext;
        canvas.tiles = tiles;
        canvas.side = side;
        canvas.rows = rows;
        auto definitions = CustomTiles::records(*project);
        for (auto it = definitions.begin(); it != definitions.end(); it++) {
            auto d = CustomTiles::definition(*project, it.key());
            if (d.kind)
                continue;
            if (canvas.tiles.size() <= int(d.id))
                canvas.tiles.resize(d.id + 1);
            canvas.tiles[d.id] = CustomTiles::image(*project, d.id);
            if (d.render & TYPES_SPRITE)
                canvas.spriteGround[d.id] = d.ground;
        }
        QPainter painter(&image);
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < side; x++)
                painter.drawImage(QRect(x * 16, y * 16, 16, 16), canvas.tileImageAt(y * side + x));
    }
    return image;
}

void MapWorkspace::setItemMode(const QString &mode) {
    for (int i = 0; i < locationModes->count(); ++i)
        if (locationModes->tabText(i) == mode) {
            if (activeMode == i)
                applyLocationMode();
            else
                locationModes->setCurrentIndex(i);
            return;
        }
}
void MapWorkspace::beginAdd(int value, int selectedAppearance, QPoint cell) {
    try {
        if (!flushLocationMode())
            return;
        auto kind = Workshop::MapItemKind(value);
        int appearance = selectedAppearance, conversation = -1;
        QString name = "New NPC";
        if (!Workshop::mapItemVariants(kind).isEmpty() || kind == Workshop::MapItemKind::Npc ||
            (kind == Workshop::MapItemKind::Monster && selectedAppearance < 0 && cell.x() < 0)) {
            QDialog dialog(this);
            dialog.setWindowTitle("Add " + Workshop::mapItemName(kind));
            auto layout = new QFormLayout(&dialog);
            auto names = new QLineEdit(name, &dialog);
            names->setObjectName("newNpcName");
            auto shapes = new QComboBox(&dialog);
            auto conversations = new QComboBox(&dialog);
            names->setVisible(kind == Workshop::MapItemKind::Npc);
            conversations->setVisible(kind == Workshop::MapItemKind::Npc);
            if (kind == Workshop::MapItemKind::Npc) {
                layout->addRow("Name", names);
                for (int shape = 64; shape < 128; shape += 4)
                    if (Workshop::actorName(shape) != "x")
                        shapes->addItem(QIcon(QPixmap::fromImage(canvas->tiles.value(256 + shape))),
                                        Workshop::actorName(shape), shape);
                shapes->setCurrentIndex(shapes->findData(80));
                conversations->addItem("New conversation", -1);
                if (project->resources.contains(talkResource))
                    for (auto e : U5::readDialogue(project->data(talkResource))) {
                        auto doc = Dialogue::parse(e.bytes);
                        if (e.id > 0 && e.id < 128 && doc.structuralError.isEmpty())
                            conversations->addItem(Dialogue::displayName(doc.entries[0].bytes),
                                                   int(e.id));
                    }
                layout->addRow("Conversation", conversations);
                conversations->setToolTip("Create a conversation or share an existing one.");
                connect(conversations, &QComboBox::currentIndexChanged, &dialog,
                        [=] { names->setEnabled(conversations->currentData().toInt() < 0); });
            } else if (!Workshop::mapItemVariants(kind).isEmpty()) {
                for (auto variant : Workshop::mapItemVariants(kind))
                    shapes->addItem(QIcon(QPixmap::fromImage(canvas->tiles.value(
                                        256 + Workshop::mapItemShape(kind, variant.second)))),
                                    variant.first, variant.second);
            } else if (resource == "BRIT.CBT")
                shapes->addItem("From encounter", -1);
            else
                for (int shape : Workshop::monsterShapes())
                    shapes->addItem(QIcon(QPixmap::fromImage(canvas->tiles.value(256 + shape))),
                                    Workshop::actorName(shape), shape);
            if (kind == Workshop::MapItemKind::Npc || kind == Workshop::MapItemKind::Monster)
                for (auto record : CustomTiles::records(*project)) {
                    TypeDef d;
                    TYPES_Decode(reinterpret_cast<const unsigned char *>(record.constData()), &d);
                    if (d.id >= 512 && d.kind == (kind == Workshop::MapItemKind::Npc ? 1 : 2))
                        shapes->addItem(QIcon(QPixmap::fromImage(canvas->tiles.value(d.id))),
                                        QString::fromUtf8(d.name), d.id);
                }
            shapes->setToolTip(Workshop::mapItemVariants(kind).isEmpty()
                                   ? "Choose the artwork and native creature type."
                                   : "Choose the item to place.");
            if (selectedAppearance >= 0)
                shapes->setCurrentIndex(shapes->findData(selectedAppearance));
            if (selectedAppearance < 0)
                layout->addRow(Workshop::mapItemVariants(kind).isEmpty() ? "Appearance" : "Type",
                               shapes);
            else
                shapes->hide();
            auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            layout->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            appearance = shapes->currentData().toInt();
            name = names->text().trimmed();
            conversation =
                kind == Workshop::MapItemKind::Npc ? conversations->currentData().toInt() : -1;
            if (name.isEmpty())
                name = "New NPC";
        }
        int tile = kind == Workshop::MapItemKind::Gold        ? 258
                   : kind == Workshop::MapItemKind::Food      ? 271
                   : kind == Workshop::MapItemKind::Moonstone ? 281
                   : kind == Workshop::MapItemKind::Corpse    ? 286
                   : kind == Workshop::MapItemKind::Sign      ? 0xf8
                   : kind == Workshop::MapItemKind::Container ? 257
                   : kind == Workshop::MapItemKind::Horse     ? 272
                   : kind == Workshop::MapItemKind::Skiff     ? 296
                   : kind == Workshop::MapItemKind::Frigate   ? 288
                   : kind == Workshop::MapItemKind::Carpet    ? 283
                   : appearance >= 0                          ? 256 + appearance
                                                              : -1;
        if (kind != Workshop::MapItemKind::Sign && kind != Workshop::MapItemKind::Container &&
            kind != Workshop::MapItemKind::HiddenContents && kind != Workshop::MapItemKind::Trigger)
            tile = 256 + Workshop::mapItemShape(kind, appearance);
        if (appearance >= 512 &&
            (kind == Workshop::MapItemKind::Npc || kind == Workshop::MapItemKind::Monster))
            tile = appearance;
        if (cell.x() >= 0) {
            placeAddedItem(value, appearance, conversation, name, cell);
            return;
        }
        canvas->beginPlacement(
            tile,
            kind != Workshop::MapItemKind::Npc && kind != Workshop::MapItemKind::Sign &&
                kind != Workshop::MapItemKind::Moonstone,
            [this, value, appearance, conversation, name](QPoint cell) {
                return placeAddedItem(value, appearance, conversation, name, cell);
            });
    } catch (const std::exception &error) {
        QMessageBox::warning(this, "Cannot add map item", error.what());
    }
}
bool MapWorkspace::placeAddedItem(int value, int appearance, int conversation, QString name,
                                  QPoint cell) {
    try {
        auto kind = Workshop::MapItemKind(value);
        // Mode changes cancel gestures; retain the placement callback for repeat stamping.
        auto place = canvas->placeItem;
        int tile = canvas->placementTile;
        bool repeat = canvas->repeatPlacement;
        auto edit = Workshop::addMapItem(*project, resource, currentPage(), cell, kind, appearance,
                                         conversation, name);
        if (!commitResources(edit.changes, "Add " + Workshop::mapItemName(kind)))
            return false;
        canvas->keyboardCell = cell;
        if (edit.mode == "Positions")
            (*navigation)["positions/selected"] = edit.slot;
        if (edit.mode == "Starting Objects")
            (*navigation)["objects/slot"] = edit.slot;
        setItemMode(edit.mode);
        canvas->keyboardCell = cell;
        if (edit.mode == "NPCs / Schedules" || edit.mode == "Encounter")
            selectNpc(edit.slot);
        else if (edit.mode == "Contents" && locationLayer) {
            auto scroll = qobject_cast<QScrollArea *>(locationLayer);
            if (auto editor = dynamic_cast<Workshop::ContentsEditor *>(scroll->widget()))
                editor->selectCell(cell);
        } else if (edit.mode == "Signs") {
            for (auto actor : canvas->actors)
                if (canvas->actorRect(actor).contains(cell) && canvas->chooseNpc)
                    canvas->chooseNpc(actor.npc);
        }
        if (repeat)
            canvas->beginPlacement(tile, true, place);
        else {
            canvas->tool = MapCanvas::InspectNpc;
            tool->button(canvas->tool)->setChecked(true);
        }
        saveView();
        return true;
    } catch (const std::exception &error) {
        QMessageBox::warning(this, "Cannot add map item", error.what());
        return false;
    }
}
void MapWorkspace::deleteSelectedItem() {
    if (!locationModes || !flushLocationMode())
        return;
    try {
        QPoint cell = canvas->keyboardCell;
        for (auto actor : canvas->actors)
            if (actor.npc == canvas->selectedNpc)
                cell = {actor.x, actor.y};
        const auto mode = locationModes->tabText(activeMode);
        if (mode == "Positions")
            return;
        auto changes = Workshop::deleteMapItem(*project, resource, currentPage(), mode,
                                               canvas->selectedNpc, cell);
        if (commitResources(changes, "Delete map item")) {
            canvas->selectedNpc = -1;
            (*states)[key].npc = -1;
            (*navigation)["objects/slot"] = -1;
            applyLocationMode();
            saveView();
        }
    } catch (const std::exception &error) {
        QMessageBox::warning(this, "Cannot delete map item", error.what());
    }
}
void MapWorkspace::editNpcProperties() {
    int slot = canvas->selectedNpc;
    if (slot <= 0)
        return;
    int town = document.pages[currentPage()].settlement;
    MapNpcDocument npcs(project, npcResource);
    auto bytes = project->data(npcResource);
    if (npcAppearance->currentIndex() >= 0)
        bytes[npcs.shapeOffset(town, slot)] =
            char(npcAppearance->currentData().toUInt() >= 512
                     ? CustomTiles::definition(*project, npcAppearance->currentData().toUInt()).base
                     : npcAppearance->currentData().toUInt());
    if (npcDialogue->currentIndex() >= 0)
        bytes[npcs.dialogueOffset(town, slot)] = char(npcDialogue->currentData().toInt());
    if (npcBehavior->currentIndex() >= 0)
        bytes[npcs.offset(town, slot) + MapNpcDocument::locationIndex(schedule->currentIndex())] =
            char(npcBehavior->currentData().toInt());
    auto changes =
        CustomTiles::assignActor(*project, resource, document.pages[currentPage()], currentPage(),
                                 slot, npcAppearance->currentData().toUInt());
    changes[npcResource] = bytes;
    if (commitResources(changes, "Edit NPC properties")) {
        refreshNpcs();
        saveView();
    }
}
