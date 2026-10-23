#include "map_positions.h"
#include "mod/maps.h"
#include "extended_maps.h"
#include "engine_table_names.h"
#include <QtWidgets>
#include <memory>
using U5::require;
namespace Workshop {
int positionMap(const QString &resource, const U5::MapPage &page) {
    if (resource == "DUNGEON.DAT")
        return 33 + page.settlement;
    if (resource == "BRIT.DAT" || resource == "UNDER.DAT")
        return 0;
    if (auto id = MAPS_LocationResource(resource.toLatin1().constData()))
        return id;
    int bank = QStringList{"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"}.indexOf(resource);
    return bank < 0 ? -1 : bank * 8 + page.settlement + 1;
}
QVector<MapPosition> mapPositions(const Project &p) {
    QVector<MapPosition> out;
    auto b = p.data("INIT.GAM");
    if (b.size() == 4192) {
        out.append({0,
                    "Party start",
                    int(U5::byte(b, 0x2ed)),
                    int(int8_t(b[0x2ef])),
                    {int(U5::byte(b, 0x2f0)), int(U5::byte(b, 0x2f1))}});
        for (int i = 0; i < 8; ++i)
            out.append({i + 1,
                        QString("Moonstone %1").arg(i + 1),
                        0,
                        int(int8_t(b[0x2a2 + i])),
                        {int(U5::byte(b, 0x28a + i)), int(U5::byte(b, 0x292 + i))},
                        U5::byte(b, 0x29a + i) == 255});
    }
    for (int i = 0; i < 40; ++i) {
        QString name = p.locationName(i + 1);
        out.append({100 + i,
                    name + " entrance",
                    0,
                    0,
                    {TableDocument::value(p.data("RULES.TBL"), 7, i),
                     TableDocument::value(p.data("RULES.TBL"), 8, i)}});
    }
    for (const auto &m : extendedMaps(p))
        if (m.kind == 1 && MAPS_NewLocation(m.id)) {
            out.append({1000 + int(m.id), m.name + " entrance", 0, m.world, m.entrance});
            out.append({2000 + int(m.id), m.name + " arrival", int(m.id), 0, m.arrival});
        }
    return out;
}
QMap<QString, QByteArray> moveMapPosition(const Project &p, int id, int map, int floor, QPoint cell,
                                          bool moveLandmark) {
    const auto size = mapSize(p, map, floor);
    require((map == 0 || MAPS_Town(map) || (map >= 33 && map <= 40)) && cell.x() >= 0 &&
                cell.y() >= 0 && cell.x() < size.width() && cell.y() < size.height(),
            "Position is outside this map");
    require(map == 0                ? (floor == 0 || floor == -1)
            : MAPS_NewLocation(map) ? floor == 0
            : map >= 33             ? floor >= 0 && floor <= 7
                                    : floor >= -1 && floor <= 3,
            "Unsupported map floor");
    if (id >= 1000) {
        auto maps = extendedMaps(p);
        bool found = false;
        ExtendedMap previous;
        for (auto &m : maps)
            if (m.kind == 1 && MAPS_NewLocation(m.id) &&
                (id == 1000 + int(m.id) || id == 2000 + int(m.id))) {
                previous = m;
                if (id < 2000) {
                    require(map == 0, "Entrances belong on a world map");
                    for (auto pos : mapPositions(p))
                        require(pos.id == id || pos.id < 100 || pos.map != map ||
                                    pos.floor != floor || pos.cell != cell,
                                "Another entrance already occupies this tile");
                    if (m.world != floor || m.entrance != cell) {
                        MapDocument destination(&p, floor == -1 ? "UNDER.DAT" : "BRIT.DAT");
                        m.underlay = U5::byte(destination.terrain(0),
                                              cell.y() * destination.pages[0].side + cell.x());
                    }
                    m.world = floor;
                    m.entrance = cell;
                } else {
                    require(map == int(m.id) && floor == 0, "Arrival belongs inside its location");
                    m.arrival = cell;
                }
                found = true;
            }
        require(found, "Unknown location position");
        QMap<QString, QByteArray> result{{"MAPS.OVR", writeExtendedMaps(maps)}};
        if (moveLandmark && id < 2000 && (previous.entrance != cell || previous.world != floor)) {
            Project candidate = p;
            candidate.resources["MAPS.OVR"].edited = result["MAPS.OVR"];
            MapDocument source(&p, previous.world == -1 ? "UNDER.DAT" : "BRIT.DAT");
            const int entranceTile =
                U5::byte(source.terrain(0),
                         previous.entrance.y() * source.pages[0].side + previous.entrance.x());
            for (int pass = 0; pass < 2; pass++) {
                QString world = (pass ? floor : previous.world) == -1 ? "UNDER.DAT" : "BRIT.DAT";
                MapDocument doc(&candidate, world);
                auto terrain = doc.terrain(0);
                QPoint at = pass ? cell : previous.entrance;
                terrain[at.y() * doc.pages[0].side + at.x()] =
                    pass ? char(entranceTile) : char(previous.underlay);
                auto edits = doc.changes(0, terrain);
                for (auto it = edits.begin(); it != edits.end(); ++it) {
                    result[it.key()] = it.value();
                    candidate.resources[it.key()].edited = it.value();
                }
            }
        }
        return result;
    }
    if (id >= 100 && id < 140) {
        require(map == 0 && floor == 0, "Location entrances belong on Britannia");
        auto b = TableDocument::change(p.data("RULES.TBL"), 7, id - 100, cell.x());
        b = TableDocument::change(b, 8, id - 100, cell.y());
        QMap<QString, QByteArray> changes{{"RULES.TBL", b}};
        if (moveLandmark && p.resources.contains("BRIT.DAT") && p.resources.contains("DATA.OVL")) {
            MapDocument document(&p, "BRIT.DAT");
            auto terrain = document.terrain(0);
            QPoint old{TableDocument::value(p.data("RULES.TBL"), 7, id - 100),
                       TableDocument::value(p.data("RULES.TBL"), 8, id - 100)};
            auto landmark = [](int tile) {
                return (tile >= 0x10 && tile <= 0x1b) || tile == 0x39 || tile == 0x3e;
            };
            int tile = U5::byte(terrain, old.y() * document.pages[0].side + old.x());
            require(landmark(tile), "This entrance has no landmark tile. Turn off Move landmark "
                                    "tile to move only its coordinate.");
            if (old != cell) {
                require(!landmark(U5::byte(terrain, cell.y() * document.pages[0].side + cell.x())),
                        "Another landmark already occupies this tile");
                for (auto pos : mapPositions(p))
                    require(pos.id < 100 || pos.id == id || pos.cell != cell,
                            "Another entrance already occupies this tile");
                int ground = 5;
                for (auto delta : {QPoint(0, 1), QPoint(1, 0), QPoint(-1, 0), QPoint(0, -1)}) {
                    QPoint neighbor = old + delta;
                    if (neighbor.x() < 0 || neighbor.y() < 0 ||
                        neighbor.x() >= document.pages[0].side ||
                        neighbor.y() >= document.pages[0].height())
                        continue;
                    int nearby =
                        U5::byte(terrain, neighbor.y() * document.pages[0].side + neighbor.x());
                    if (nearby < 0x10) {
                        ground = nearby;
                        break;
                    }
                }
                terrain[old.y() * document.pages[0].side + old.x()] = char(ground);
                terrain[cell.y() * document.pages[0].side + cell.x()] = char(tile);
                auto mapChanges = document.changes(0, terrain);
                for (auto it = mapChanges.begin(); it != mapChanges.end(); ++it)
                    changes[it.key()] = it.value();
            }
        }
        return changes;
    }
    auto b = p.data("INIT.GAM");
    require(b.size() == 4192, "Starting state is missing or incomplete");
    if (id == 0) {
        b[0x2ed] = char(map);
        b[0x2ef] = char(floor);
        b[0x2f0] = char(cell.x());
        b[0x2f1] = char(cell.y());
        // Keep the active-party actor's position in sync with the saved map position.
        b[0x6b6] = char(cell.x());
        b[0x6b7] = char(cell.y());
        b[0x6b8] = char(floor);
    } else {
        require(id >= 1 && id <= 8 && map == 0, "Moonstones belong on a world map");
        int phase = id - 1;
        b[0x28a + phase] = char(cell.x());
        b[0x292 + phase] = char(cell.y());
        b[0x29a + phase] = 0;
        b[0x2a2 + phase] = char(floor);
    }
    return {{"INIT.GAM", b}};
}
QWidget *
mapPositionEditor(Project *p, const QString &resource, const U5::MapPage &page, MapCanvas *canvas,
                  std::function<bool(const QMap<QString, QByteArray> &, const QString &)> commit,
                  QMap<QString, int> *state, std::function<void(QPoint)> locate,
                  std::function<void(const QString &)> navigate) {
    auto root = new QWidget;
    root->setObjectName("mapPositionEditor");
    auto layout = new QVBoxLayout(root);
    auto list = new QListWidget;
    list->setObjectName("mapPositionList");
    list->setToolTip("Select a position to move on this map.");
    layout->addWidget(list);
    auto place = new QPushButton("Place on this map");
    place->setObjectName("placeMapPosition");
    place->setToolTip("Click a tile to set the selected position without changing terrain.");
    layout->addWidget(place);
    auto carried = new QCheckBox("Carried by the party");
    carried->setObjectName("moonstoneCarried");
    carried->setToolTip("Start with this moonstone in the party's inventory.");
    layout->addWidget(carried);
    auto landmark = new QCheckBox("Move landmark tile");
    landmark->setObjectName("moveEntranceLandmark");
    landmark->setChecked(true);
    landmark->setToolTip("Move the map landmark with its entrance coordinate.");
    layout->addWidget(landmark);
    auto link = new QPushButton("Open location");
    link->setObjectName("openLocationPosition");
    link->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    layout->addWidget(link, 0, Qt::AlignLeft);
    auto worldRow = new QWidget;
    auto worldForm = new QFormLayout(worldRow);
    worldForm->setContentsMargins(0, 0, 0, 0);
    auto worlds = new QComboBox;
    worlds->setObjectName("locationEntranceWorld");
    worlds->addItem("Britannia", 0);
    worlds->addItem("Underworld", -1);
    worldForm->addRow("Entrance world", worlds);
    layout->addWidget(worldRow);
    auto loading = std::make_shared<bool>(false);
    const int map = positionMap(resource, page);
    const int floor = resource == "UNDER.DAT" ? -1 : page.floor;
    auto refresh = [=] {
        *loading = true;
        list->clear();
        canvas->actors.clear();
        for (auto pos : mapPositions(*p)) {
            if ((pos.id >= 1000 && (pos.map != map || pos.floor != floor)) ||
                (pos.id >= 100 && pos.id < 1000 && (map || floor)) ||
                (pos.id > 0 && pos.id < 100 && map))
                continue;
            auto item = new QListWidgetItem(pos.name + (pos.carried ? " (carried)" : ""), list);
            item->setData(Qt::UserRole, pos.id);
            item->setToolTip(pos.map == map && pos.floor == floor && !pos.carried
                                 ? "Drag the marker to move it."
                                 : "Place this position on the current map.");
            if (pos.map == map && pos.floor == floor && !pos.carried)
                canvas->actors.append({pos.cell.x(), pos.cell.y(), -1, pos.id,
                                       pos.id == 0    ? "S"
                                       : pos.id < 100 ? QString("M%1").arg(pos.id)
                                                      : "E"});
            if (pos.id == state->value("positions/selected", 0))
                list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count())
            list->setCurrentRow(0);
        canvas->selectedNpc =
            list->currentItem() ? list->currentItem()->data(Qt::UserRole).toInt() : -1;
        *loading = false;
        canvas->update();
    };
    auto select = [=](bool center) {
        if (*loading || !list->currentItem())
            return;
        int id = list->currentItem()->data(Qt::UserRole).toInt();
        (*state)["positions/selected"] = id;
        canvas->selectedNpc = id;
        *loading = true;
        carried->setVisible(id > 0 && id < 100);
        landmark->setVisible(id >= 100 && id < 2000);
        link->setVisible(id >= 1000 && bool(navigate));
        worldRow->setVisible(id >= 1000);
        link->setText(id >= 2000 ? "Show entrance" : "Open location");
        for (const auto &m : extendedMaps(*p))
            if (m.kind == 1 && MAPS_NewLocation(m.id) &&
                (id == 1000 + int(m.id) || id == 2000 + int(m.id)))
                worlds->setCurrentIndex(worlds->findData(m.world));

        for (auto pos : mapPositions(*p))
            if (pos.id == id) {
                carried->setChecked(pos.carried);
                if (center && pos.map == map && pos.floor == floor && !pos.carried)
                    locate(pos.cell);
            }
        *loading = false;
        canvas->update();
    };
    auto move = [=](int id, QPoint cell) {
        try {
            if (!commit(moveMapPosition(*p, id, map, floor, cell, landmark->isChecked()),
                        "Move map position"))
                return false;
            (*state)["positions/selected"] = id;
            refresh();
            select(false);
            return true;
        } catch (const std::exception &e) {
            QMessageBox::warning(root, "Cannot move position", e.what());
            return false;
        }
    };
    canvas->chooseNpc = [=](int id) {
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toInt() == id) {
                *loading = true;
                list->setCurrentRow(i);
                *loading = false;
                select(false);
            }
    };
    canvas->moveNpc = move;
    QObject::connect(list, &QListWidget::currentRowChanged, root, [=] { select(true); });
    QObject::connect(place, &QPushButton::clicked, root, [=] {
        if (!list->currentItem())
            return;
        int id = list->currentItem()->data(Qt::UserRole).toInt();
        canvas->beginPlacement(-1, false, [=](QPoint cell) { return move(id, cell); });
    });
    QObject::connect(carried, &QCheckBox::toggled, root, [=](bool on) {
        if (*loading || !list->currentItem())
            return;
        int id = list->currentItem()->data(Qt::UserRole).toInt();
        if (id < 1 || id > 8)
            return;
        auto b = p->data("INIT.GAM");
        b[0x29a + id - 1] = on ? char(255) : 0;
        if (commit({{"INIT.GAM", b}}, "Change moonstone inventory")) {
            refresh();
            select(false);
        }
    });
    QObject::connect(link, &QPushButton::clicked, root, [=] {
        if (!navigate || !list->currentItem())
            return;
        int id = list->currentItem()->data(Qt::UserRole).toInt();
        for (const auto &m : extendedMaps(*p))
            if (m.kind == 1 && MAPS_NewLocation(m.id) &&
                (id == 1000 + int(m.id) || id == 2000 + int(m.id))) {
                const QString target =
                    id >= 2000 ? (m.world == -1 ? "UNDER.DAT" : "BRIT.DAT") : m.resource;
                (*state)[target + "/selectPosition"] =
                    id >= 2000 ? 1000 + int(m.id) : 2000 + int(m.id);
                (*state)["positions/locate"] = 1;
                QTimer::singleShot(0, root, [=] { navigate(target); });
                break;
            }
    });
    QObject::connect(worlds, &QComboBox::currentIndexChanged, root, [=] {
        if (*loading || !list->currentItem())
            return;
        int id = list->currentItem()->data(Qt::UserRole).toInt();
        for (const auto &m : extendedMaps(*p))
            if (m.kind == 1 && MAPS_NewLocation(m.id) &&
                (id == 1000 + int(m.id) || id == 2000 + int(m.id))) {
                try {
                    if (commit(moveMapPosition(*p, 1000 + m.id, 0, worlds->currentData().toInt(),
                                               m.entrance, true),
                               "Change entrance world")) {
                        refresh();
                        select(false);
                    }
                } catch (const std::exception &e) {
                    QMessageBox::warning(root, "Cannot move entrance", e.what());
                    select(false);
                }
                break;
            }
    });
    refresh();
    select(state->take("positions/locate") != 0);
    return root;
}
} // namespace Workshop
