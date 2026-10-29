// Exercise the real Workshop importer in a private in-memory project.
#include "project.h"
#include "map_json.h"
#include "map_editor.h"
#include "map_positions.h"
#include "extended_maps.h"
#include "mod/types.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPainter>
#include <cstdio>

static void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    U5::require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Output exists or is unwritable");
    U5::require(file.write(data) == data.size(), "Short output write");
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "--survey-game") {
            const QString output = QString::fromLocal8Bit(argv[3]);
            U5::require(!QFile::exists(output) && QDir().mkpath(output), "Choose a new survey directory");
            Project original;
            original.openGame(QString::fromLocal8Bit(argv[2]));
            unsigned count = 0;
            for (const auto &resource : {"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"}) {
                const auto pages = original.mapPages(resource);
                for (int page = 0; page < pages.size(); ++page)
                    if (pages[page].floor == 0) {
                        const int id = Workshop::positionMap(resource, pages[page]);
                        write(output + QString("/location-%1.json").arg(id, 2, 10, QChar('0')),
                              Workshop::exportMapJson(original, resource, page));
                        ++count;
                    }
            }
            U5::require(count == 32, "Expected all 32 original locations");
            puts("PASS read-only survey export of all 32 original Ultima V locations.");
            return 0;
        }
        U5::require(argc == 4 || (argc == 7 && QString::fromLocal8Bit(argv[4]) == "--arrival"),
                    "Usage: city_workshop_probe GAME_DIR INPUT_JSON NEW_OUTPUT_DIR [--arrival X Y]");
        const QString output = QString::fromLocal8Bit(argv[3]);
        U5::require(!QFile::exists(output) && QDir().mkpath(output), "Choose a new output directory");
        QFile input(QString::fromLocal8Bit(argv[2]));
        U5::require(input.open(QIODevice::ReadOnly), "Cannot read input JSON");
        const auto bytes = input.readAll();
        const auto node = QJsonDocument::fromJson(bytes).object().value("map").toObject();
        U5::require(node.value("kind").toString() == "location", "Probe currently supports locations");
        const auto name = node.value("name").toString();
        Project p;
        p.openGame(QString::fromLocal8Bit(argv[1]));
        QString resource;
        int page = -1;
        for (const auto &r : {"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"}) {
            const auto pages = p.mapPages(r);
            for (int i = 0; i < pages.size(); ++i)
                if (pages[i].floor == 0 && pages[i].name.section(" — ", 0, 0) == name) {
                    U5::require(page < 0, "Ambiguous location name");
                    resource = r;
                    page = i;
                }
        }
        U5::require(page >= 0, "Location is not present in the supplied game project");
        const auto changes = Workshop::importMapJson(p, resource, page, bytes);
        for (auto it = changes.begin(); it != changes.end(); ++it)
            p.resources[it.key()].edited = it.value();
        if (argc == 7) {
            bool xOk = false, yOk = false;
            const QPoint arrival(QString::fromLocal8Bit(argv[5]).toInt(&xOk),
                                 QString::fromLocal8Bit(argv[6]).toInt(&yOk));
            const auto mp = p.mapPages(resource)[page];
            U5::require(xOk && yOk && arrival.x() >= 0 && arrival.y() >= 0 &&
                        arrival.x() < mp.side && arrival.y() < mp.height(), "Invalid arrival");
            const int mapId = Workshop::positionMap(resource, mp);
            auto catalog = Workshop::extendedMaps(p);
            bool linked = false;
            for (auto &m : catalog)
                if (m.resource == resource && m.floor == 0) {
                    if (m.world == -2) {
                        bool found = false;
                        for (const auto &position : Workshop::mapPositions(p))
                            if (position.id == 99 + mapId) {
                                m.world = position.floor;
                                m.entrance = position.cell;
                                found = true;
                                break;
                            }
                        U5::require(found, "Cannot find original world entrance");
                    }
                    m.arrival = arrival;
                    linked = true;
                }
            U5::require(linked, "No extended location to configure");
            p.resources["MAPS.OVR"].edited = Workshop::writeExtendedMaps(catalog);
        }
        p.validate();
        if (argc == 7) {
            p.title = "Ultima IX: " + name + " city-design experiment";
            U5::require(p.data("INIT.GAM") == p.resources["INIT.GAM"].original,
                        "Portal arrival must not change the global new-game state");
            const auto package = p.package();
            Project restored;
            restored.openGame(QString::fromLocal8Bit(argv[1]));
            restored.importPackage(package);
            restored.validate();
            U5::require(restored.data("MAPS.OVR") == p.data("MAPS.OVR"),
                        "Package lost portal arrival or map catalog");
            for (const auto &resourceName : p.changed())
                U5::require(restored.data(resourceName) == p.data(resourceName),
                            "Package changed an edited resource: " + resourceName);
            write(output + "/Minoc-experiment.imperamod", package);
            p.save(output + "/Minoc-experiment.imperaproj");
            QJsonArray arrivals;
            for (const auto &m : Workshop::extendedMaps(restored))
                if (m.resource == resource && m.floor == 0)
                    arrivals.append(QJsonObject{{"map", int(m.id)}, {"world", m.world},
                        {"entranceX", m.entrance.x()}, {"entranceY", m.entrance.y()},
                        {"arrivalX", m.arrival.x()}, {"arrivalY", m.arrival.y()}});
            write(output + "/arrival.json", QJsonDocument(arrivals).toJson());
        }
        write(output + "/roundtrip.json", Workshop::exportMapJson(p, resource, page));
        // Independent C++ DOS decode and native terrain grid for pixel parity.
        const auto images = U5::readGraphics("TILES.16", p.data("TILES.16")).images;
        MapDocument map(&p, resource);
        const auto selected = map.pages[page];
        for (int i = 0; i < map.pages.size(); ++i) {
            const auto &f = map.pages[i];
            if (f.settlement != selected.settlement)
                continue;
            QImage preview(f.side * 16, f.height() * 16, QImage::Format_RGB32);
            QPainter painter(&preview);
            const auto terrain = map.rawTerrain(i);
            for (int y = 0; y < f.height(); ++y)
                for (int x = 0; x < f.side; ++x)
                    painter.drawImage(x * 16, y * 16, images[U5::byte(terrain, y * f.side + x)]);
            painter.end();
            U5::require(preview.save(output + QString("/floor%1.png").arg(f.floor)), "Cannot save preview");
        }
        QJsonArray walking;
        for (unsigned tile = 0; tile < 256; ++tile)
            walking.append(bool(TYPES_Walkable(0x1c, tile)));
        write(output + "/native-walking.json", QJsonDocument(walking).toJson());
        puts("PASS Workshop import, Project::validate, JSON re-export and DOS previews. Not a game play-test.");
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "FAIL Workshop probe: %s\n", e.what());
        return 1;
    }
}
