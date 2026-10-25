#include "custom_tiles.h"
#include "map_positions.h"
#include "extended_maps.h"
#include "mod/npcs.h"
#include "content_navigation.h"
#include "contents_editor.h"
#include "map_entities.h"
#include "dialogue.h"
#include "dialogue_editor.h"
#include "dialogue_preview.h"
#include "native_dialogue_session.h"
#include "native_dialogue_frame.h"
#include "dialogue_surface.h"
#include "dungeon_editor.h"
#include "engine_tables.h"
#include "font_editor.h"
#include "location_text.h"
#include "mod/creation_scene.h"
#include "mod/engine_tables.h"
#include "mod/package.h"
#include "mod/world_resources.h"
#include "narrative_editor.h"
#include "resource_document.h"
#include "scene_editor.h"
#include "cutscene_editor.h"
#include "cutscene_composition.h"
#include "mod/cutscene_layout.h"
#include "text_preview.h"
#include "ui_theme.h"
#include "window.h"
#include "shop_editor.h"
#include "mod/tileset_resources.h"
#include "world_objects.h"
#include <QTemporaryDir>
#include <QtWidgets>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
extern "C" int WorkshopEngineFindSign(const unsigned char *, unsigned, unsigned, unsigned, unsigned, unsigned);
extern "C" int WorkshopEngineDungeonRoom(int, unsigned char);
extern "C" int WorkshopEngineTalkWords(const unsigned char *, int, int *);
extern "C" int WorkshopEngineMatch(const unsigned char *, char *);
extern "C" int WorkshopEngineDecode(const void *, size_t, void **, unsigned *);
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> static void rejects(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Expected invalid resource to be rejected");
}
// End iteration before selection/undo can replace the tree's owning workspace.
static QTreeWidgetItem *treeItem(QTreeWidget *tree, const QString &text, bool heading = false) {
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        if ((*it)->text(0) == text && (!heading || (*it)->childCount()))
            return *it;
        ++it;
    }
    throw std::runtime_error("Expected tree entry is missing");
}
static void file(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly), "Cannot make fixture");
    check(f.write(bytes) == bytes.size(), "Cannot write fixture");
}
static Project fixture(const QString &directory) {
    file(directory + "/tiles.16", QByteArray(65536, 0));
    QByteArray init(4192, 0);
    init[0x2b5] = 1;
    file(directory + "/init.gam", init);
    Project p;
    p.openGame(directory);
    return p;
}
static void waitFor(const std::function<bool()> &ready, int timeout = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < timeout) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    check(ready(), "Timed out waiting for native Playtest");
}
static void nativeKeys(Dialogue::NativeTextViewport *viewport, const QString &keys) {
    check(viewport, "Native keyboard viewport missing");
    for (QChar ch : keys) {
        QKeyEvent event(QEvent::KeyPress, ch == '\r' ? Qt::Key_Return : ch.unicode(),
                        Qt::NoModifier, ch == '\r' ? QString() : QString(ch));
        QApplication::sendEvent(viewport, &event);
    }
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("IMPERA_WORKSHOP_SCREENSHOT_DIR"))
        Workshop::applyTheme(app);
    QTemporaryDir settingsDirectory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    app.setOrganizationName("ImperaTests");
    app.setApplicationName("WorkshopTests");
    int count = 0;
    auto test = [&](const char *name, auto fn) {
        fn();
        ++count;
        std::cout << "PASS " << name << "\n";
    };
    try {
        test("Extended map dimensions preserve native layouts, rectangular edits and crop safety",
             [&] {
                 QTemporaryDir dir;
                 auto p = fixture(dir.path());
                 QByteArray town(16384, char(5));
                 town[10 * 32 + 20] = 68;
                 p.resources["TOWNE.DAT"] = {town, town};
                 auto apply = [&](const QMap<QString, QByteArray> &edits) {
                     for (auto it = edits.begin(); it != edits.end(); ++it)
                         p.resources[it.key()].edited = it.value();
                 };
                 apply(Workshop::resizeMap(p, "TOWNE.DAT", 0, 80, 48, 1));
                 MapDocument map(&p, "TOWNE.DAT");
                 check(map.pages[0].side == 80 && map.pages[0].height() == 48 &&
                           map.pages[1].side == 32 && map.pages[1].height() == 32,
                       "Resizing one floor changed another floor");
                 auto cells = map.terrain(0);
                 check(cells.size() == 80 * 48 && cells[10 * 80 + 20] == 68 && cells[79] == 1 &&
                           p.data("TOWNE.DAT") == town,
                       "Resize lost terrain or changed native bank data");
                 cells[47 * 80 + 79] = 68;
                 apply(map.changes(0, cells));
                 check(MapDocument(&p, "TOWNE.DAT").terrain(0) == cells,
                       "Rectangular map paint did not roundtrip");
                 auto image = map.terrainImage(0, {}, true);
                 check(image.size() == QSize(80, 48) && image.constScanLine(47)[79] == 68,
                       "Rectangular map export used square dimensions");
                 check(p.package().startsWith("IMOD0018"), "Extended maps need package version 18");
                 auto encoded = p.data("MAPS.OVR");
                 for (int n = 0; n < encoded.size(); ++n)
                     check(!MAPS_Validate(encoded.constData(), n),
                           "Truncated map catalog was accepted");
                 auto corrupt = encoded;
                 corrupt[MAPS_HEADER + 10] = 0;
                 corrupt[MAPS_HEADER + 11] = 0;
                 check(!MAPS_Validate(corrupt.constData(), corrupt.size()),
                       "Zero-width map was accepted");
                 rejects([&] { Workshop::resizeMap(p, "TOWNE.DAT", 0, 257, 32, 5); });
                 rejects([&] { Workshop::resizeMap(p, "TOWNE.DAT", 0, 32, 0, 5); });
                 rejects([&] { Workshop::resizeMap(p, "TOWNE.DAT", 0, 256, 1, 5); });
                 apply(Workshop::resizeMap(p, "TOWNE.DAT", 0, 256, 48, 5));
                 check(MapDocument(&p, "TOWNE.DAT").terrain(0).size() == 256 * 48,
                       "Maximum map width did not roundtrip");
                 p.resources["TOWNE.NPC"] = {QByteArray(4608, 0), QByteArray(4608, 0)};
                 auto &npc = p.resources["TOWNE.NPC"].edited;
                 npc[512 + 1] = 1; // Active native shape, slot 1.
                 npc[16 + 3] = 100;
                 npc[16 + 6] = 0;
                 rejects([&] { Workshop::resizeMap(p, "TOWNE.DAT", 0, 64, 32, 5); });
             });
        test("Extended combat markers preserve disabled slots and maximum coordinates", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray combat(352, char(255));
            for (int y = 0; y < 11; y++)
                for (int x = 0; x < 11; x++)
                    combat[y * 32 + x] = 5;
            p.resources["BRIT.CBT"] = {combat, combat};
            auto apply = [&](const QMap<QString, QByteArray> &changes) {
                for (auto it = changes.begin(); it != changes.end(); ++it)
                    p.resources[it.key()].edited = it.value();
            };
            apply(Workshop::resizeMap(p, "BRIT.CBT", 0, 256, 256, 5));
            check(!Workshop::mapMarkerActive(p, "BRIT.CBT", 0, 24, 255, 255),
                  "Resize activated an unused monster marker");
            auto added = Workshop::addMapItem(p, "BRIT.CBT", 0, {255, 255},
                                              Workshop::MapItemKind::Monster, 144);
            apply(added.changes);
            check(added.slot == 6 && Workshop::mapMarkerActive(p, "BRIT.CBT", 0, 24, 255, 255),
                  "Corner monster marker was not enabled");
            rejects([&] { Workshop::resizeMap(p, "BRIT.CBT", 0, 128, 128, 5); });
            apply(Workshop::deleteMapItem(p, "BRIT.CBT", 0, "Encounter", 6, {255, 255}));
            check(!Workshop::mapMarkerActive(p, "BRIT.CBT", 0, 24, 255, 255),
                  "Deleted corner marker remained enabled");
            apply(Workshop::resizeMap(p, "BRIT.CBT", 0, 128, 128, 5));
            check(MapDocument(&p, "BRIT.CBT").pages[0].side == 128,
                  "Inactive markers prevented resize");
        });
        test("New location catalog allocates independent identities and validates entrances", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            p.resources["BRIT.DAT"] = {QByteArray(256, 1), QByteArray(256, 1)};
            p.resources["DATA.OVL"] = {QByteArray(0x3986, 0), QByteArray(0x3986, 0)};
            p.resources["UNDER.DAT"] = {QByteArray(65536, 1), QByteArray(65536, 1)};
            auto apply = [&](const QMap<QString, QByteArray> &changes) {
                for (auto it = changes.begin(); it != changes.end(); ++it)
                    p.resources[it.key()].edited = it.value();
                Workshop::hydrateExtendedMaps(p);
            };
            apply(Workshop::addLocation(p, "BRIT.DAT", "New island", {200, 100}, 48, 80, 5));
            apply(Workshop::addLocation(p, "UNDER.DAT", "Deep cavern", {200, 100}, 256, 256, 68));
            auto maps = Workshop::extendedMaps(p);
            check(maps.size() == 2 && maps[0].id == 42 && maps[1].id == 43 && maps[0].world == 0 &&
                      maps[1].world == -1 && maps[0].arrival == QPoint(24, 79),
                  "New location identities or entrances were not independent");
            check(p.mapPages("LOC042.DAT")[0].height() == 80 &&
                      MapDocument(&p, "LOC042.DAT").terrain(0).size() == 48 * 80 &&
                      U5::readDialogue(p.data("LOC042.TLK")).isEmpty() &&
                      NPCS_Validate(p.data("LOC042.NPC").constData(), p.data("LOC042.NPC").size()),
                  "New location did not create independent terrain and actor resources");
            rejects(
                [&] { Workshop::addLocation(p, "BRIT.DAT", "Duplicate", {200, 100}, 32, 32, 5); });
            rejects(
                [&] { Workshop::addLocation(p, "BRIT.DAT", "Outside", {256, 100}, 32, 32, 5); });
            rejects([&] { Workshop::addLocation(p, "BRIT.DAT", "Invalid", {2, 2}, -1, 32, 5); });
            rejects([&] { Workshop::resizeMap(p, "LOC042.DAT", 0, 48, 64, 5); });
            auto duplicate = maps;
            duplicate[1].id = 42;
            rejects([&] { Workshop::writeExtendedMaps(duplicate); });
            check(!MAPS_NewLocation(41) && !MAPS_NewLocation(64) && !MAPS_NewLocation(66) &&
                      !MAPS_NewLocation(128) && MAPS_NewLocation(127),
                  "New locations overlap reserved engine identities");
        });
        test("Extended locations roundtrip entities and expose resize with undo", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            file(dir.path() + "/BRIT.DAT", QByteArray(256, 1));
            file(dir.path() + "/UNDER.DAT", QByteArray(65536, 1));
            file(dir.path() + "/DATA.OVL", QByteArray(0x3986, 0));
            QByteArray signs(67, 0);
            signs[66] = char(255);
            file(dir.path() + "/SIGNS.DAT", signs);
            p.openGame(dir.path());
            auto apply = [&](const QMap<QString, QByteArray> &edits) {
                for (auto it = edits.begin(); it != edits.end(); ++it)
                    p.resources[it.key()].edited = it.value();
                Workshop::hydrateExtendedMaps(p);
                p.validate();
            };
            apply(Workshop::addLocation(p, "BRIT.DAT", "New island", {200, 100}, 48, 80, 5));
            apply(Workshop::addLocation(p, "UNDER.DAT", "Deep cavern", {210, 110}, 40, 80, 68));
            auto npc = Workshop::addMapItem(p, "LOC042.DAT", 0, {40, 70},
                                            Workshop::MapItemKind::Npc, 80, -1, "Ada");
            apply(npc.changes);
            check(npc.slot == 1, "New location did not allocate its own NPC slots");
            MapNpcDocument actors(&p, "LOC042.NPC");
            apply({{"LOC042.NPC", actors.move(0, 1, 0, {45, 75, 0, 0})}});
            check(actors.location(0, 1, 0).x == 45, "Large-coordinate NPC move was lost");
            apply(Workshop::addMapItem(p, "LOC042.DAT", 0, {43, 70}, Workshop::MapItemKind::Sign)
                      .changes);
            apply(
                Workshop::addMapItem(p, "LOC042.DAT", 0, {44, 70}, Workshop::MapItemKind::Container)
                    .changes);
            check(p.data("SIGNS.DAT").startsWith("ISIG0001") &&
                      Workshop::readSigns(p.data("SIGNS.DAT")).last().map == 42,
                  "New location sign was assigned to a vanilla map");
            check(!Workshop::mapContents(p, {0, 42, 0}).isEmpty(),
                  "New location contents are missing");
            apply(Workshop::moveMapPosition(p, 1042, 0, -1, {220, 120}, true));
            check(Workshop::extendedMaps(p)[0].world == -1 &&
                      MapDocument(&p, "UNDER.DAT").terrain(0)[120 * 256 + 220] == 0x12,
                  "Moving an entrance failed to move its landmark/world");
            apply(Workshop::moveMapPosition(p, 2042, 42, 0, {46, 78}));
            auto package = p.package();
            check(package.startsWith("IMOD0018"), "Extended map package capability missing");
            Project restored;
            restored.openGame(dir.path());
            restored.importPackage(package);
            check(restored.locationName(42) == "New island" &&
                      restored.data("LOC042.NPC") == p.data("LOC042.NPC") &&
                      restored.data("LOC042.TLK") == p.data("LOC042.TLK") &&
                      restored.data("SIGNS.DAT") == p.data("SIGNS.DAT"),
                  "Mod package lost location entities");
            auto characters = Workshop::characterDestinations(restored);
            check(std::any_of(
                      characters.begin(), characters.end(),
                      [](const auto &d) { return d.resource == "LOC042.TLK" && d.title == "Ada"; }),
                  "New NPC is absent from Dialogue");
            auto setup =
                Dialogue::nativeSetup(restored, "LOC042.TLK", 0, Workshop::npcCharacter(0, 1));
            check(setup.hasNpc && uchar(setup.gameState[0x2ed]) == 42,
                  "New NPC has an incorrect Playtest map identity");
            p.save(dir.path() + "/maps.imperaproject");
            Project loaded;
            loaded.load(p.projectPath);
            check(loaded.data("MAPS.OVR") == p.data("MAPS.OVR") &&
                      loaded.data("LOC042.TLK") == p.data("LOC042.TLK"),
                  "Project save/load lost new location data");
            WorkshopWindow window;
            check(window.openProject(p.projectPath), "Cannot open extended map project");
            window.selectResource("LOC042.DAT");
            QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            app.processEvents();
            auto active = [&] {
                return window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto resize = active()->findChild<QToolButton *>("resizeMap");
            check(resize, "Resize control missing");
            bool handled = false;
            QTimer::singleShot(0, [&] {
                auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                    return;
                auto w = dialog->findChild<QSpinBox *>("mapWidth"),
                     h = dialog->findChild<QSpinBox *>("mapHeight");
                if (!w || !h) {
                    dialog->reject();
                    return;
                }
                w->setValue(70);
                h->setValue(100);
                handled = true;
                dialog->accept();
            });
            resize->click();
            check(handled && window.projectForTests().mapPages("LOC042.DAT")[0].side == 70 &&
                      window.projectForTests().mapPages("LOC042.DAT")[0].height() == 100,
                  "Resize dialog did not update map geometry");
            QAction *undo = nullptr, *redo = nullptr;
            for (auto action : window.findChildren<QAction *>()) {
                if (action->shortcut() == QKeySequence::Undo)
                    undo = action;
                if (action->shortcut() == QKeySequence::Redo)
                    redo = action;
            }
            check(undo && redo, "History actions missing");
            undo->trigger();
            check(window.projectForTests().data("MAPS.OVR") == p.data("MAPS.OVR"),
                  "Resize undo did not restore map data");
            redo->trigger();
            check(window.projectForTests().mapPages("LOC042.DAT")[0].height() == 100,
                  "Resize redo did not restore dimensions");
            window.selectResource("BRIT.DAT");
            QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            app.processEvents();
            auto filter = active()->findChild<QComboBox *>("mapPaletteFilter");
            filter->setCurrentText("World");
            active()->findChild<QListWidget *>("mapPalette")->setCurrentRow(0x12);
            QTimer::singleShot(0, [&] {
                auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                    return;
                dialog->findChild<QLineEdit *>("newLocationName")->setText("Third location");
                dialog->findChild<QSpinBox *>("newLocationWidth")->setValue(64);
                dialog->findChild<QSpinBox *>("newLocationHeight")->setValue(48);
                dialog->accept();
            });
            auto canvas = static_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            check(canvas && canvas->brush == 0x12, "Location palette did not select an entrance");
            auto terrainBefore = MapDocument(&window.projectForTests(), "BRIT.DAT").terrain(0);
            canvas->keyboardCell = {230, 130};
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(canvas, &enter);
            app.processEvents();
            check(Workshop::extendedMaps(window.projectForTests()).size() == 3 &&
                      window.projectForTests().locationName(44) == "Third location",
                  "UI placement did not create a new location");
            check(active()->findChild<QLabel *>("mapLocationLabel")->text().contains("Britannia"),
                  "Creating a location unexpectedly left the world editor");
            undo->trigger();
            app.processEvents();
            check(Workshop::extendedMaps(window.projectForTests()).size() == 2 &&
                      MapDocument(&window.projectForTests(), "BRIT.DAT").terrain(0) ==
                          terrainBefore &&
                      window.projectForTests().data("LOC044.NPC").isEmpty(),
                  "Creation undo left terrain or NPC data behind");
            redo->trigger();
            app.processEvents();
            check(Workshop::extendedMaps(window.projectForTests()).size() == 3 &&
                      window.projectForTests().mapPages("LOC044.DAT")[0].side == 64,
                  "Creation redo lost the new map");
            window.selectResource("LOC042.DAT");
            app.processEvents();
            auto positions = active()->findChild<QAbstractButton *>("mapPositions");
            check(positions, "Positions control missing");
            positions->click();
            auto list = active()->findChild<QListWidget *>("mapPositionList");
            check(list, "Position inspector missing");
            for (int i = 0; i < list->count(); i++)
                if (list->item(i)->data(Qt::UserRole).toInt() == 2042)
                    list->setCurrentRow(i);
            auto worlds = active()->findChild<QComboBox *>("locationEntranceWorld");
            check(worlds && worlds->currentData().toInt() == -1,
                  "Entrance world property is incorrect");
            worlds->setCurrentIndex(worlds->findData(0));
            check(Workshop::extendedMaps(window.projectForTests())[0].world == 0,
                  "Right-pane world change failed");
            auto entrance = active()->findChild<QPushButton *>("openLocationPosition");
            check(entrance && entrance->isEnabled(), "Entrance navigation missing");
            entrance->click();
            app.processEvents();
            list = active()->findChild<QListWidget *>("mapPositionList");
            check(list && list->currentItem() &&
                      list->currentItem()->data(Qt::UserRole).toInt() == 1042,
                  "Show entrance did not select its marker on the world map");
        });
        test("Pencil location tiles create entrances atomically and keep the world open", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/BRIT.DAT", QByteArray(256, 1));
            file(dir.path() + "/UNDER.DAT", QByteArray(65536, 1));
            file(dir.path() + "/DATA.OVL", QByteArray(0x3986, 0));
            QByteArray signs(67, 0);
            signs[66] = char(255);
            file(dir.path() + "/SIGNS.DAT", signs);
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Pencil location fixture");
            window.show();
            auto active = [&] {
                return window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            int dialogCount = 0;
            for (const QString world : {QString("BRIT.DAT"), QString("UNDER.DAT")}) {
                window.selectResource(world);
                app.processEvents();
                auto canvas = static_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
                auto pencil = active()->findChild<QToolButton *>("mapTool0");
                check(canvas && pencil, "Pencil world workspace is missing");
                pencil->click();
                canvas->brush = world == "BRIT.DAT" ? 0x14 : 0x16;
                auto press = [&] {
                    QPointF at((220 + .5) * 16 * canvas->zoom, (120 + .5) * 16 * canvas->zoom);
                    QMouseEvent down(QEvent::MouseButtonPress, at, at, Qt::LeftButton,
                                     Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(canvas, &down);
                    QMouseEvent up(QEvent::MouseButtonRelease, at, at, Qt::LeftButton, Qt::NoButton,
                                   Qt::NoModifier);
                    QApplication::sendEvent(canvas, &up);
                };
                const auto before = MapDocument(&window.projectForTests(), world).terrain(0);
                const auto catalog = window.projectForTests().data("MAPS.OVR");
                QTimer::singleShot(0, &window, [&] {
                    auto d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    check(d, "Pencil did not open Add location");
                    ++dialogCount;
                    d->reject();
                });
                press();
                check(MapDocument(&window.projectForTests(), world).terrain(0) == before &&
                          window.projectForTests().data("MAPS.OVR") == catalog,
                      "Cancelled pencil location changed terrain or catalog");
                QTimer::singleShot(0, &window, [&] {
                    auto d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    check(d, "Pencil did not reopen Add location");
                    ++dialogCount;
                    d->findChild<QLineEdit *>("newLocationName")->setText("Painted location");
                    d->accept();
                });
                press();
                app.processEvents();
                check(canvas->side == 256 &&
                          active()
                              ->findChild<QLabel *>("mapLocationLabel")
                              ->text()
                              .contains(world == "BRIT.DAT" ? "Britannia" : "Underworld"),
                      "Pencil creation switched away from world map");
                auto maps = Workshop::extendedMaps(window.projectForTests());
                const auto m = maps.last();
                check(m.world == (world == "BRIT.DAT" ? 0 : -1) && m.entrance == QPoint(220, 120) &&
                          U5::byte(MapDocument(&window.projectForTests(), world).terrain(0),
                                   120 * 256 + 220) == unsigned(canvas->brush),
                      "Pencil lost selected entrance tile or world");
                check(!canvas->painting() && !canvas->placing(),
                      "Pencil entrance left a painting gesture active");
                auto changes = Workshop::moveMapPosition(window.projectForTests(), 1000 + m.id, 0,
                                                         m.world, {221, 120}, true);
                Project moved = window.projectForTests();
                for (auto it = changes.begin(); it != changes.end(); ++it)
                    moved.resources[it.key()].edited = it.value();
                auto terrain = MapDocument(&moved, world).terrain(0);
                check(U5::byte(terrain, 120 * 256 + 221) == unsigned(canvas->brush) &&
                          U5::byte(terrain, 120 * 256 + 220) == U5::byte(before, 120 * 256 + 220),
                      "Moving entrance changed its artwork or lost underlying terrain");
                canvas->brush = 0x10;
                press();
                check(Workshop::extendedMaps(window.projectForTests()).size() == maps.size(),
                      "Repainting an existing entrance created a second location");
            }
            check(dialogCount == 4, "Incorrect number of location dialogs");
        });
        test("Actor palette filters place NPCs and monsters without painting terrain", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 5));
            file(dir.path() + "/TOWNE.NPC", QByteArray(4608, 0));
            file(dir.path() + "/TOWNE.TLK", QByteArray::fromHex("0000"));
            p.openGame(dir.path());
            QMap<unsigned, QByteArray> actorRecords;
            const QList<QSize> sizes{{1, 1}, {1, 2}, {2, 1}, {2, 2}};
            for (int size = 0; size < sizes.size(); ++size)
                for (unsigned kind : {1u, 2u}) {
                    TypeDef d;
                    TYPES_Vanilla(1, kind == 1 ? 80 : 144, &d);
                    d.kind = kind;
                    d.id = 59999 + kind + size * 2;
                    d.width = sizes[size].width();
                    d.height = sizes[size].height();
                    d.render = TYPES_SPRITE;
                    strcpy(d.name, kind == 1 ? "Custom scholar" : "Custom beast");
                    QImage art(16 * d.width, 16 * d.height, QImage::Format_RGBA8888);
                    art.fill(Qt::white);
                    QImage worldArt(16, 16, QImage::Format_RGBA8888);
                    worldArt.fill(Qt::white);
                    actorRecords[d.id] =
                        CustomTiles::record(d, {{art, 150, 0}, {worldArt, 150, 6}});
                }
            p.resources["TYPES.OVR"].edited = CustomTiles::library(p, actorRecords);
            p.save(dir.path() + "/actors.imperaproject");
            WorkshopWindow window;
            check(window.openProject(p.projectPath), "Actor palette fixture");
            window.show();
            window.selectResource("TOWNE.DAT");
            app.processEvents();
            auto active = [&] {
                return window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            check(!active()->findChild<QToolButton *>("mapAddMenu"),
                  "Add menu should be replaced by Special");
            auto palette = active()->findChild<QListWidget *>("mapPalette");
            auto filter = active()->findChild<QComboBox *>("mapPaletteFilter");
            auto canvas = static_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            auto choose = [&](const QString &category, int id) {
                active()->findChild<QToolButton *>("mapTool0")->click();
                filter->setCurrentText(category);
                for (int i = 0; i < palette->count(); ++i)
                    if (palette->item(i)->data(Qt::UserRole).toInt() == id) {
                        check(!palette->item(i)->isHidden(), "Actor missing from category");
                        palette->setCurrentRow(i);
                        return;
                    }
                check(false, "Actor absent from palette");
            };
            const auto terrain = window.projectForTests().data("TOWNE.DAT");
            choose("NPCs", 336);
            check(canvas->brush == 336, "NPC palette selection lost its sprite identity");
            QTimer::singleShot(0, &window, [&] {
                auto d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                check(d, "NPC placement dialog missing");
                d->findChild<QLineEdit *>("newNpcName")->setText("Palette friend");
                d->accept();
            });
            canvas->keyboardCell = {6, 7};
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(canvas, &enter);
            check(U5::byte(window.projectForTests().data("TOWNE.NPC"), 513) == 80 &&
                      !U5::readDialogue(window.projectForTests().data("TOWNE.TLK")).isEmpty(),
                  "NPC brush failed to create a scheduled NPC and conversation");
            choose("Monsters", 400);
            check(canvas->brush == 400, "Monster palette selection lost sprite identity");
            canvas->keyboardCell = {9, 8};
            QApplication::sendEvent(canvas, &enter);
            check(U5::byte(window.projectForTests().data("TOWNE.NPC"), 514) == 144,
                  "Monster brush failed to create a real actor");
            for (int size = 0; size < sizes.size(); ++size)
                for (unsigned kind : {1u, 2u}) {
                    const unsigned id = 59999 + kind + size * 2;
                    choose(kind == 1 ? "NPCs" : "Monsters", id);
                    check(canvas->actorSize(id) == sizes[size] &&
                              canvas->tiles[id].size() == sizes[size] * 16,
                          "Actor picker lost footprint or artwork dimensions");
                    if (kind == 1)
                        QTimer::singleShot(0, &window, [&] {
                            auto d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                            check(d, "Custom NPC dialog missing");
                            d->accept();
                        });
                    canvas->keyboardCell = {int(10 + kind * 4), 10 + size * 4};
                    QApplication::sendEvent(canvas, &enter);
                    const auto &project = window.projectForTests();
                    check(CustomTiles::actorType(project, "TOWNE.DAT",
                                                 project.mapPages("TOWNE.DAT")[0], 0,
                                                 kind + 2 + size * 2, 0) == id,
                          "Custom actor palette lost its type assignment");
                    check(CustomTiles::image(project, id, 6).size() == QSize(16, 16),
                          "Multi-tile actor lost its single-cell world artwork");
                }
            check(window.projectForTests().data("TOWNE.DAT") == terrain,
                  "Actor palette painted terrain");
        });
        test("Custom location tiles appear in the picker and keep their identity when moved", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            file(dir.path() + "/BRIT.DAT", QByteArray(256, 1));
            file(dir.path() + "/UNDER.DAT", QByteArray(65536, 1));
            file(dir.path() + "/DATA.OVL", QByteArray(0x3986, 0));
            QByteArray signs(67, 0);
            signs[66] = char(255);
            file(dir.path() + "/SIGNS.DAT", signs);
            p.openGame(dir.path());
            TypeDef d;
            TYPES_Vanilla(0, 0x14, &d);
            d.id = 61000;
            d.filters = 1;
            strcpy(d.name, "Crystal citadel");
            QImage art(16, 16, QImage::Format_RGBA8888);
            art.fill(Qt::white);
            p.resources["TYPES.OVR"].edited =
                CustomTiles::library(p, {{d.id, CustomTiles::record(d, {{art, 150, 0}})}});
            p.save(dir.path() + "/entrance.imperaproject");
            WorkshopWindow window;
            check(window.openProject(p.projectPath), "Custom entrance project");
            window.show();
            window.selectResource("BRIT.DAT");
            app.processEvents();
            auto active = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            auto filter = active->findChild<QComboBox *>("mapPaletteFilter");
            filter->setCurrentText("World");
            auto palette = active->findChild<QListWidget *>("mapPalette");
            bool found = false;
            for (int i = 0; i < palette->count(); ++i)
                if (palette->item(i)->data(Qt::UserRole).toUInt() == d.id) {
                    check(!palette->item(i)->isHidden(),
                          "Custom location tile absent from World filter");
                    check(palette->item(i)->icon().actualSize(QSize(32, 32)) == QSize(32, 32),
                          "Custom tile thumbnail is smaller than vanilla thumbnails");
                    palette->setCurrentRow(i);
                    found = true;
                    break;
                }
            check(found, "Custom entrance missing from tile picker");
            auto canvas = static_cast<MapCanvas *>(active->findChild<QWidget *>("mapCanvas"));
            QTimer::singleShot(0, &window, [&] {
                auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                check(dialog, "Custom entrance did not open creation dialog");
                dialog->findChild<QLineEdit *>("newLocationName")->setText("Crystal citadel");
                dialog->accept();
            });
            canvas->keyboardCell = {220, 120};
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(canvas, &enter);
            app.processEvents();
            Project moved = window.projectForTests();
            auto at = [&](const Project &project, const QString &world, QPoint cell) {
                MapDocument map(&project, world);
                auto bytes = CustomTiles::expanded(project, world, map.pages[0], 0, map.terrain(0));
                return TYPES_U32(bytes.constData() + (cell.y() * map.pages[0].side + cell.x()) * 4);
            };
            check(at(moved, "BRIT.DAT", {220, 120}) == 61000 &&
                      moved.locationName(42) == "Crystal citadel",
                  "Custom entrance was reduced to native artwork");
            auto changes = Workshop::moveMapPosition(moved, 1042, 0, -1, {221, 121}, true);
            for (auto it = changes.begin(); it != changes.end(); ++it)
                moved.resources[it.key()].edited = it.value();
            moved.validate();
            check(at(moved, "BRIT.DAT", {220, 120}) == 0 &&
                      at(moved, "UNDER.DAT", {221, 121}) == 61000,
                  "Moving entrance lost its custom tile identity");
            auto restored = p;
            restored.importPackage(moved.package());
            check(at(restored, "UNDER.DAT", {221, 121}) == 61000,
                  "Package roundtrip lost custom entrance artwork");
        });
        test("World NPC entries explain availability and Special covers all remaining placements",
             [&] {
                 QTemporaryDir dir;
                 fixture(dir.path());
                 file(dir.path() + "/BRIT.DAT", QByteArray(256, 1));
                 file(dir.path() + "/UNDER.DAT", QByteArray(65536, 1));
                 file(dir.path() + "/DATA.OVL", QByteArray(0x3986, 0));
                 for (auto name : {"INIT.OOL", "BRIT.OOL", "UNDER.OOL"})
                     file(dir.path() + "/" + name, QByteArray(256, 0));
                 QByteArray signs(67, 0);
                 signs[66] = char(255);
                 file(dir.path() + "/SIGNS.DAT", signs);
                 WorkshopWindow window;
                 check(window.openGame(dir.path()), "Special fixture load");
                 window.show();
                 window.selectResource("BRIT.DAT");
                 app.processEvents();
                 auto active = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
                 auto canvas = static_cast<MapCanvas *>(active->findChild<QWidget *>("mapCanvas"));
                 auto palette = active->findChild<QListWidget *>("mapPalette");
                 auto filter = active->findChild<QComboBox *>("mapPaletteFilter");
                 filter->setCurrentText("NPCs");
                 int npcCount = 0;
                 for (int i = 0; i < palette->count(); ++i)
                     if (!palette->item(i)->isHidden()) {
                         ++npcCount;
                         check(!(palette->item(i)->flags() & Qt::ItemIsEnabled) &&
                                   palette->item(i)->toolTip().contains("location map"),
                               "World NPC availability is not explained");
                     }
                 check(npcCount > 0, "World NPC filter is empty");
                 filter->setCurrentText("Special");
                 QListWidgetItem *shadowLord = nullptr;
                 for (int i = 0; i < palette->count(); ++i)
                     if (canvas->specialBrushKinds.value(palette->item(i)->data(Qt::UserRole).toInt(), -1) ==
                         int(Workshop::MapItemKind::ShadowLord))
                         shadowLord = palette->item(i);
                 check(shadowLord && shadowLord->isHidden(), "Shadow Lord remains in Special");
                 filter->setCurrentText("Monsters");
                 check(!shadowLord->isHidden(), "Shadow Lord missing from Monsters");
                 bool pirateFound = false;
                 for (int i = 0; i < palette->count(); ++i)
                     if (canvas->specialBrushKinds.value(palette->item(i)->data(Qt::UserRole).toInt(), -1) ==
                         int(Workshop::MapItemKind::PirateShip)) {
                         pirateFound = !palette->item(i)->isHidden();
                         filter->setCurrentText("Special");
                         check(palette->item(i)->isHidden(), "Pirate ship remains in Special");
                         break;
                     }
                 check(pirateFound, "Pirate ship missing from Monsters");
                 filter->setCurrentText("Special");
                 QSet<int> seen;
                 QString previous;
                 int goldRow = -1;
                 for (int i = 0; i < palette->count(); ++i)
                     if (!palette->item(i)->isHidden()) {
                         const auto item = palette->item(i);
                         int id = item->data(Qt::UserRole).toInt();
                         check(canvas->specialBrushKinds.contains(id),
                               "Special includes ordinary terrain");
                         auto name = item->data(Qt::UserRole + 1).toString();
                         check(previous.compare(name, Qt::CaseInsensitive) <= 0,
                               "Special entries are not alphabetized");
                         previous = name;
                         int kind = canvas->specialBrushKinds.value(id);
                         seen.insert(kind);
                         if (kind == int(Workshop::MapItemKind::Gold))
                             goldRow = i;
                     }
                 auto expected = Workshop::availableMapItems(window.projectForTests(), "BRIT.DAT");
                 for (auto kind : expected)
                     if (kind != Workshop::MapItemKind::Npc &&
                         kind != Workshop::MapItemKind::Monster &&
                         kind != Workshop::MapItemKind::ShadowLord &&
                         kind != Workshop::MapItemKind::PirateShip)
                         check(seen.contains(int(kind)), "Special omitted a former Add entry");
                 check(Workshop::mapItemName(Workshop::MapItemKind::Corpse) == "Corpse" &&
                           Workshop::mapItemShape(Workshop::MapItemKind::Corpse) == 30 &&
                           Workshop::mapItemName(Workshop::MapItemKind::MoldyCorpse) == "Gore" &&
                           !expected.contains(Workshop::MapItemKind::BloodSplatter),
                       "Corpse labels or removed Blood splatter placement are incorrect");
                 check(goldRow >= 0, "Special Gold missing");
                 palette->setCurrentRow(goldRow);
                 canvas->keyboardCell = {220, 120};
                 auto terrain = window.projectForTests().data("BRIT.DAT");
                 QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                 QApplication::sendEvent(canvas, &enter);
                 check(U5::byte(Workshop::WorldObjects::effective(window.projectForTests(), 0),
                                8) == 2 &&
                           window.projectForTests().data("BRIT.DAT") == terrain,
                       "Special Gold was not placed as a world object");
                 for (auto action : window.findChildren<QAction *>())
                     if (action->shortcut() == QKeySequence::Undo) {
                         action->trigger();
                         break;
                     }
                 app.processEvents();
                 check(!U5::byte(Workshop::WorldObjects::effective(window.projectForTests(), 0), 8),
                       "Special item undo failed");
             });
        test("Every Special placement uses location records, preserves properties and avoids "
             "dialogue aliases",
             [&] {
                 QTemporaryDir dir;
                 auto p = fixture(dir.path());
                 file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 5));
                 file(dir.path() + "/TOWNE.NPC", QByteArray(4608, 0));
                 file(dir.path() + "/TOWNE.TLK", QByteArray::fromHex("0000"));
                 QByteArray signs(67, 0);
                 signs[66] = char(255);
                 file(dir.path() + "/SIGNS.DAT", signs);
                 p.openGame(dir.path());
                 const auto original = p.data("TOWNE.NPC");
                 int n = 0, gold = -1, trigger = -1, shadow = -1, shard = -1;
                 for (auto kind : Workshop::availableMapItems(p, "TOWNE.DAT")) {
                     if (kind == Workshop::MapItemKind::Npc ||
                         kind == Workshop::MapItemKind::Monster)
                         continue;
                     int variant = kind == Workshop::MapItemKind::Equipment   ? 32
                                   : kind == Workshop::MapItemKind::Potion    ? 7
                                   : kind == Workshop::MapItemKind::Scroll    ? 255
                                   : kind == Workshop::MapItemKind::Moonstone ? 4
                                   : kind == Workshop::MapItemKind::Shard ||
                                           kind == Workshop::MapItemKind::ShadowLord
                                       ? 2
                                       : -1;
                     auto placed = Workshop::addMapItem(
                         p, "TOWNE.DAT", 0, {2 + (n % 8) * 3, 2 + (n / 8) * 3}, kind, variant);
                     ++n;
                     for (auto it = placed.changes.begin(); it != placed.changes.end(); ++it)
                         p.resources[it.key()].edited = it.value();
                     p.validate();
                     if (placed.mode == "NPCs / Schedules") {
                         check(placed.slot >= 32, "Special placement overwrote a vanilla NPC slot");
                         auto bytes = p.data("TOWNE.NPC");
                         auto base = NPCS_Offset(bytes.constData(), bytes.size(), 0, placed.slot);
                         int shape =
                             U5::byte(bytes, NPCS_ShapeOffset(bytes.constData(), bytes.size(), 0,
                                                              placed.slot));
                         check(NPCS_ObjectKind(bytes.constData() + base, shape) == int(kind),
                               "Location object lost its behavior marker");
                         if (kind == Workshop::MapItemKind::ShadowLord)
                             shadow = placed.slot;
                         if (kind == Workshop::MapItemKind::Shard)
                             shard = placed.slot;
                         if (kind == Workshop::MapItemKind::Gold)
                             gold = placed.slot;
                         if (kind == Workshop::MapItemKind::Trigger)
                             trigger = placed.slot;
                         if (kind == Workshop::MapItemKind::Frigate)
                             check(shape == 36 && U5::byte(bytes, base + 15) == 1,
                                   "New ship is not boardable or has invalid skiff cargo");
                         if (kind == Workshop::MapItemKind::BloodSplatter)
                             check(NPCS_Blood(shape,
                                              U5::byte(bytes, NPCS_DialogueOffset(bytes.constData(),
                                                                                  bytes.size(), 0,
                                                                                  placed.slot)),
                                              U5::byte(bytes, base + 15)),
                                   "Decorative blood is still a searchable corpse");
                     }
                 }
                 check(n == 28 && gold >= 32 && trigger >= 32,
                       "Special types are still unavailable on locations");
                 check(p.data("TOWNE.NPC").left(4608) == original &&
                           p.data("TOWNE.TLK") == QByteArray::fromHex("0000"),
                       "Special placement changed vanilla NPCs or conversation data");
                 rejects([&] {
                     Workshop::addMapItem(p, "TOWNE.DAT", 0, {28, 28},
                                          Workshop::MapItemKind::ShadowLord, 0);
                 });
                 rejects([&] {
                     Workshop::addMapItem(p, "TOWNE.DAT", 0, {28, 28}, Workshop::MapItemKind::Shard,
                                          3);
                 });
                 auto blob = p.package();
                 check(blob.startsWith("IMOD0019"),
                       "Location objects need an updated engine package");
                 Project imported = fixture(dir.path());
                 imported.importPackage(blob);
                 check(imported.data("TOWNE.NPC") == p.data("TOWNE.NPC"),
                       "Location objects failed package roundtrip");
                 auto old = blob;
                 old[7] = '8';
                 rejects([&] {
                     Project copy = p;
                     copy.importPackage(old);
                 });
                 auto destinations = Workshop::contentDestinations(p);
                 for (auto d : destinations)
                     check(d.workspace != "Dialogue" || d.scheduleResource != "TOWNE.NPC",
                           "Object quantities became dialogue entries");
                 auto npc = Workshop::addMapItem(p, "TOWNE.DAT", 0, {30, 30},
                                                 Workshop::MapItemKind::Npc, 80, -1, "Neighbor");
                 for (auto it = npc.changes.begin(); it != npc.changes.end(); ++it)
                     p.resources[it.key()].edited = it.value();
                 MapNpcDocument npcs(&p, "TOWNE.NPC");
                 check(npcs.dialogue(0, npc.slot) == 1,
                       "Objects reserved ordinary conversation numbers");
                 // Talking animals remain NPCs even though their sprite is in the object range.
                 auto smith = p.data("TOWNE.NPC");
                 smith[NPCS_ShapeOffset(smith.constData(), smith.size(), 0, npc.slot)] = 17;
                 p.resources["TOWNE.NPC"].edited = smith;
                 check(!Workshop::locationObject(p, "TOWNE.NPC", 0, npc.slot),
                       "Talking horse became an inert map object");
                 bool talkingHorse = false;
                 for (auto d : Workshop::contentDestinations(p))
                     if (d.scheduleResource == "TOWNE.NPC" &&
                         d.character == Workshop::npcCharacter(0, npc.slot) && d.conversation >= 0)
                         talkingHorse = true;
                 check(talkingHorse, "Talking horse disappeared from Dialogue");
                 p.save(dir.path() + "/objects.imperaproject");
                 WorkshopWindow window;
                 check(window.openProject(p.projectPath), "Cannot open location objects");
                 window.show();
                 window.selectResource("TOWNE.DAT");
                 app.processEvents();
                 auto active = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
                 active->findChild<QToolButton *>("mapTool3")->click();
                 auto list = active->findChild<QComboBox *>("mapNpcList");
                 list->setCurrentIndex(gold);
                 auto value = active->findChild<QSpinBox *>("locationObjectValue");
                 check(value && value->isVisible(), "Location object quantity editor missing");
                 value->setValue(42);
                 QMetaObject::invokeMethod(value, "editingFinished", Qt::DirectConnection);
                 const auto bytes = window.projectForTests().data("TOWNE.NPC");
                 check(U5::byte(bytes, NPCS_DialogueOffset(bytes.constData(), bytes.size(), 0,
                                                           gold)) == 42,
                       "Object quantity editor did not persist");
                 list->setCurrentIndex(shadow);
                 auto identity = active->findChild<QComboBox *>("locationObjectVariant");
                 check(identity->isVisible() && identity->currentData().toInt() == 2,
                       "Shadow Lord identity inspector missing");
                 identity->setCurrentIndex(identity->findData(1));
                 QMetaObject::invokeMethod(identity, "activated", Qt::DirectConnection,
                                           Q_ARG(int, identity->currentIndex()));
                 auto changed = window.projectForTests().data("TOWNE.NPC");
                 check(U5::byte(changed,
                                NPCS_Offset(changed.constData(), changed.size(), 0, shadow) + 15) ==
                           1,
                       "Shadow identity was not persisted");
                 list->setCurrentIndex(shard);
                 check(identity->isVisible() && identity->currentData().toInt() == 2,
                       "Shard identity inspector missing");
                 list->setCurrentIndex(trigger);
                 check(active->findChild<QComboBox *>("locationObjectVariant")->isVisible(),
                       "Location trigger replacement editor missing");
             });
        test("Cutscene pages share an editor, preserve originals and roundtrip variable sequences", [&] {
            QTemporaryDir dir;
            auto p=fixture(dir.path());
            QByteArray intro(12000,0), ending(3698,0);
            for(int offset:U5::storyOffsets()) intro.replace(offset,6,"Hello",6);
            for(auto entry:Workshop::narrativeEntries("END.DAT")) ending.replace(entry->offset,6,"Hello",6);
            file(dir.path()+"/STORY.DAT",intro); file(dir.path()+"/END.DAT",ending);
            p.openGame(dir.path());
            auto pages=Workshop::cutscenePages(p,0);
            check(pages.size()==21 && pages[6].text.contains("blue door"),"Portal scene is missing from introduction");
            pages.insert(1,{0,"A new page"}); pages.removeLast();
            p.resources["CUTSCENE.OVR"].edited=Workshop::changeCutscene(p,0,pages);
            check(Workshop::cutscenePages(p,0)[1].text=="A new page","Added page missing");
            auto ends=Workshop::cutscenePages(p,1); ends.append({2,"A new ending"});
            p.resources["CUTSCENE.OVR"].edited=Workshop::changeCutscene(p,1,ends);
            check(Workshop::cutscenePages(p,0)[1].text=="A new page","Editing ending overwrote introduction");
            check(p.data("STORY.DAT")==intro && p.data("END.DAT")==ending,"Cutscene edits mutated original files");
            auto package=p.package(); check(package.startsWith("IMOD0006"),"Missing cutscene package capability");
            Project restored; restored.openGame(dir.path()); restored.importPackage(package);
            check(Workshop::cutscenePages(restored,1).size()==7,"Package lost added ending");
            p.save(dir.path()+"/pages.imperaproject"); Project loaded; loaded.load(p.projectPath);
            check(loaded.data("CUTSCENE.OVR")==p.data("CUTSCENE.OVR"),"Project lost cutscene pages");
            auto placed=Workshop::cutscenePages(p,0);
            placed[0].placement=U5_DefaultCutscenePlacement(0,placed[0].layout);
            placed[0].placement.custom=1;placed[0].placement.imageX=30;placed[0].placement.x=190;
            p.resources["CUTSCENE.OVR"].edited=Workshop::changeCutscene(p,0,placed);
            check(p.package().startsWith("IMOD0007"),"Custom layout lacks its package capability");
            restored.importPackage(p.package());
            check(Workshop::cutscenePages(restored,0)[0]==placed[0],"Custom placement lost in package");
            p.save(p.projectPath);loaded.load(p.projectPath);
            check(Workshop::cutscenePages(loaded,0)[0]==placed[0],"Custom placement lost in project");
            auto invalid=placed;invalid[0].placement.right[0]=invalid[0].placement.left[0];
            rejects([&]{Workshop::changeCutscene(p,0,invalid);});
            invalid=placed;invalid[0].placement.imageY=200;
            rejects([&]{Workshop::changeCutscene(p,0,invalid);});
            rejects([&]{Workshop::changeCutscene(p,0,{});});
            rejects([&]{Workshop::changeCutscene(p,1,{{6,"Invalid layout"}});});
            rejects([&]{Workshop::changeCutscene(p,1,{{0,QString(2000,'a')}});});
            rejects([&]{Workshop::changeCutscene(p,0,{{0,QString(QChar(0x100))}});});
            WorkshopWindow window;
            check(window.openGame(dir.path()),"Cannot open cutscene fixture");
            for(auto resource:{"STORY.DAT","END.DAT"}) {
                window.selectResource(resource);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
                check(window.findChild<QWidget *>("compositionCanvas"),"Missing composition canvas");
                int scene=QString(resource)=="END.DAT";
                int before=Workshop::compositionPages(window.projectForTests(),scene).size();
                window.findChild<QPushButton *>("addCutscenePage")->click();
                check(Workshop::compositionPages(window.projectForTests(),scene).size()==before+1,"Adding a composition page failed");
                for(auto action:window.findChildren<QAction *>()) if(action->text().startsWith("Undo ")) {action->trigger();break;}
                app.processEvents();app.processEvents();
                check(Workshop::compositionPages(window.projectForTests(),scene).size()==before,"Composition page undo failed");
                QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
                int row=window.findChild<QListWidget *>("cutscenePages")->currentRow();
                int textCount=Workshop::compositionPages(window.projectForTests(),scene)[row].texts.size();
                window.findChild<QPushButton *>("addCutsceneText")->click();
                window.findChild<QPushButton *>("deleteCutsceneElement")->click();
                check(Workshop::compositionPages(window.projectForTests(),scene)[row].texts.size()==textCount,"Text box deletion failed");
                for(auto action:window.findChildren<QAction *>())if(action->text().startsWith("Undo ")){action->trigger();break;}
                app.processEvents();app.processEvents();
                check(Workshop::compositionPages(window.projectForTests(),scene)[row].texts.size()==textCount+1,"Undo did not restore a deleted text box");
            }
        });
        test("Original illustrated cutscene previews use every vanilla layout (optional)", [&] {
            if(!qEnvironmentVariableIsSet("U5_GAME_DIR")) return;
            Project p;p.openGame(qEnvironmentVariable("U5_GAME_DIR"));
            for(int scene=0;scene<2;scene++) {
                auto pages=Workshop::cutscenePages(p,scene);
                auto compositions=Workshop::compositionPages(p,scene);int number=0;
                for(const auto &page:pages) {
                    auto image=Workshop::previewCutscene(p,scene,page);
                    check(image.size()==QSize(320,200),"Cutscene preview has incorrect game dimensions");
                    bool visible=false;
                    for(int y=0;y<200 && !visible;y++) for(int x=0;x<320;x++)
                        if(image.pixel(x,y)!=qRgb(0,0,0)) {visible=true;break;}
                    check(visible,"Illustrated cutscene preview is blank");
                    check(Workshop::previewComposition(p,compositions[number++])==image,"Converting an original page changed its composition");
                    auto placed=page;
                    placed.placement=U5_DefaultCutscenePlacement(scene,page.layout);placed.placement.custom=1;
                    check(Workshop::previewCutscene(p,scene,placed)==image,"Enabling custom layout changed a preset's composition");
                    placed.placement.imageX=(placed.placement.imageX+20)%320;
                    check(Workshop::previewCutscene(p,scene,placed)!=image,"Moving artwork did not affect its preview");
                }
            }
        });
        test("Editing original cutscene text preserves indentation and shaped wrapping", [&] {
            QTemporaryDir dir;auto p=fixture(dir.path());
            Workshop::CutsceneTextBox box;
            box.bounds=QRect(10,20,200,100);box.text="Original paragraph";box.flow=true;
            box.layout.left[0]=0;box.layout.right[0]=200;
            box.layout.left[1]=30;box.layout.right[1]=180;
            box.layout.top=10;box.layout.bottom=70;box.layout.x=12;box.layout.y=0;
            Workshop::CutsceneComposition page;page.texts.append(box);
            p.resources["CUTSCENE.OVR"].edited=Workshop::changeComposition(p,0,{page});
            QMap<QString,int> state;std::function<bool()> flush;
            std::unique_ptr<QWidget> editor(Workshop::compositionEditor(&p,"STORY.DAT",[&](const QMap<QString,QByteArray> &edits,const QString &){for(auto it=edits.begin();it!=edits.end();++it)p.resources[it.key()].edited=it.value();return true;},&state,&flush));
            editor->findChild<QComboBox *>("cutsceneElement")->setCurrentIndex(0);
            editor->findChild<QPlainTextEdit *>("compositionText")->setPlainText("Edited paragraph with the original indentation");
            check(flush(),"Original text edit did not save");
            auto edited=Workshop::compositionPages(p,0)[0];
            box.text=edited.texts[0].text;page.texts[0]=box;
            check(edited.texts[0].flow && Workshop::previewComposition(p,edited)==Workshop::previewComposition(p,page),"Text edit discarded original indentation or shaped flow");
            editor->findChild<QSpinBox *>("compositionWidth")->setValue(220);
            editor->findChild<QSpinBox *>("compositionHeight")->setValue(110);
            check(flush(),"Original box resize did not save");
            auto resized=Workshop::compositionPages(p,0)[0].texts[0];
            check(resized.flow && resized.layout.x==12 && resized.layout.left[1]==30 && resized.layout.right[0]==220 && resized.layout.right[1]==200 && resized.layout.top==10 && resized.layout.bottom==70,"Resizing lost indentation or wrap-band geometry");
        });
        test("Cutscene layout controls, guides, four corners and reset persist", [&] {
            QTemporaryDir dir;auto p=fixture(dir.path());
            Workshop::CutsceneComposition page;
            page.texts.append({QRect(30,30,180,120),"A paragraph that wraps around artwork.",false,{}});
            p.resources["CUTSCENE.OVR"].edited=Workshop::changeComposition(p,0,{page});
            QMap<QString,int> state;std::function<bool()> flush;
            int commits=0;
            std::unique_ptr<QWidget> editor(Workshop::compositionEditor(&p,"STORY.DAT",[&](const QMap<QString,QByteArray> &edits,const QString &){++commits;for(auto it=edits.begin();it!=edits.end();++it)p.resources[it.key()].edited=it.value();return true;},&state,&flush));
            editor->resize(1500,950);editor->show();app.processEvents();
            editor->findChild<QComboBox *>("cutsceneElement")->setCurrentIndex(0);
            auto field=[&](const QString &name){return editor->findChild<QSpinBox *>("composition"+name);};
            field("Indent")->setValue(12);field("LeftMargin")->setValue(5);field("RightMargin")->setValue(7);
            field("TextTop")->setValue(9);
            editor->findChild<QCheckBox *>("compositionWrap")->setChecked(true);
            field("BandTop")->setValue(20);field("BandBottom")->setValue(90);
            field("BandLeft")->setValue(50);field("BandRight")->setValue(10);
            check(flush(),"Layout controls did not save");
            auto box=Workshop::compositionPages(p,0)[0].texts[0];
            check(box.flow&&box.layout.x==17&&box.layout.y==9&&box.layout.left[0]==5&&box.layout.right[0]==173&&box.layout.top==20&&box.layout.bottom==90&&box.layout.left[1]==50&&box.layout.right[1]==170,"Layout controls changed the wrong coordinates");
            auto package=p.package();Project restored;restored.openGame(dir.path());restored.importPackage(package);
            check(Workshop::previewComposition(restored,Workshop::compositionPages(restored,0)[0])==Workshop::previewComposition(p,Workshop::compositionPages(p,0)[0]),"Custom layout package changed rendering");
            auto canvas=editor->findChild<QWidget *>("compositionCanvas");
            auto drag=[&](QPoint a,QPoint b,bool cancel=false) {
                app.processEvents();QPointF origin((canvas->width()-640)/2.0,(canvas->height()-400)/2.0);
                QPointF from=origin+QPointF(a*2),to=origin+QPointF(b*2);
                QMouseEvent down(QEvent::MouseButtonPress,from,from,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&down);
                QMouseEvent move(QEvent::MouseMove,to,to,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&move);
                if(cancel){QKeyEvent key(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(canvas,&key);}
                QMouseEvent up(QEvent::MouseButtonRelease,to,to,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas,&up);
            };
            auto saved=p.data("CUTSCENE.OVR");int before=commits;
            drag({47,35},{53,35},true);
            check(flush()&&p.data("CUTSCENE.OVR")==saved&&commits==before,"Escape committed a layout guide drag");
            drag({47,35},{53,35});
            check(Workshop::compositionPages(p,0)[0].texts[0].layout.x==23&&commits==before+1,"Indent guide did not commit exactly once");
            // Normal left/right, text top, band top/bottom and band margins.
            drag({35,38},{38,38});drag({203,38},{199,38});drag({120,39},{120,41});
            drag({120,50},{120,53});drag({120,120},{120,116});
            drag({80,84},{85,84});drag({200,84},{194,84});
            box=Workshop::compositionPages(p,0)[0].texts[0];
            check(box.layout.left[0]==8&&box.layout.right[0]==169&&box.layout.y==11&&box.layout.top==23&&box.layout.bottom==86&&box.layout.left[1]==55&&box.layout.right[1]==164,"Dragging layout guides changed the wrong boundaries");
            for(int corner=0;corner<4;++corner){
                auto old=Workshop::compositionPages(p,0)[0].texts[0];auto r=old.bounds;
                QPoint at=corner==0?r.topLeft():corner==1?r.topRight():corner==2?r.bottomLeft():r.bottomRight();
                QPoint delta(corner%2?-3:3,corner<2?2:-2);
                drag(at,at+delta);
                auto next=Workshop::compositionPages(p,0)[0].texts[0];
                check(next.bounds.width()==r.width()-3&&next.bounds.height()==r.height()-2,"One of the four corner grips failed to resize");
                check((corner%2?next.bounds.left()==r.left():next.bounds.right()==r.right())&&(corner<2?next.bounds.bottom()==r.bottom():next.bounds.top()==r.top()),"Corner resize moved the opposite corner");
                check(next.flow&&next.layout.left[1]==55,"Corner resize discarded text layout");
            }
            field("BandTop")->setValue(640);
            field("BandBottom")->setValue(-640);
            check(flush(),"Clamped wrap-band edit did not save");
            auto limited=Workshop::compositionPages(p,0)[0].texts[0];
            check(limited.layout.top<limited.layout.bottom&&editor->findChild<QCheckBox *>("compositionWrap")->isChecked(),"Crossing band edges disabled wrapping instead of clamping");
            auto beforeReset=Workshop::compositionPages(p,0)[0].texts[0];
            editor->findChild<QPushButton *>("resetCompositionLayout")->click();
            box=Workshop::compositionPages(p,0)[0].texts[0];
            check(!box.flow&&box.bounds==beforeReset.bounds&&box.text==beforeReset.text&&!editor->findChild<QCheckBox *>("compositionWrap")->isChecked(),"Reset layout changed text or geometry");
        });
        test("Cutscene compositions embed PNG alpha, clip text, deduplicate assets and roundtrip", [&] {
            QTemporaryDir dir;auto p=fixture(dir.path());
            QImage png(2,2,QImage::Format_ARGB32);png.fill(qRgba(255,0,0,128));png.setPixel(1,0,qRgba(0,0,0,255));png.setPixel(0,1,qRgba(0,255,0,0));
            const QString path=dir.path()+"/custom.png";check(png.save(path),"Cannot save PNG fixture");
            auto imported=Workshop::importCutscenePng(path);check(imported.image.pixel(1,0)==png.pixel(1,0),"PNG alpha import changed pixels");
            imported.position={0,0};QImage blue(2,2,QImage::Format_ARGB32);blue.fill(qRgba(0,0,255,255));
            Workshop::CutsceneComposition page;page.images={{"Blue",blue,{0,0}},imported,imported};
            auto once=page;once.images.removeLast();auto preview=Workshop::previewComposition(p,once);
            check(preview.pixel(0,0)==qRgb(128,0,127)&&preview.pixel(1,0)==qRgb(0,0,0)&&preview.pixel(0,1)==qRgb(0,0,255),"PNG transparency or opaque black composition failed");
            page.texts.append({QRect(0,0,5,8),"A",false,{}});
            QStringList issues;check(Workshop::previewComposition(p,page,&issues).pixel(0,0)==qRgb(255,255,255),"Text did not render above images");
            page.texts[0].text="A long paragraph that cannot fit";
            Workshop::previewComposition(p,page,&issues);check(!issues.isEmpty(),"Text overflow was not reported");
            auto bytes=Workshop::changeComposition(p,0,{page});check(U5_CompositionWord(reinterpret_cast<const unsigned char *>(bytes.constData())+16)==2,"Repeated images were not deduplicated");
            p.resources["CUTSCENE.OVR"].edited=bytes;auto package=p.package();check(package.startsWith("IMOD0008"),"Composition package capability missing");
            QFile::remove(path);Project restored;restored.openGame(dir.path());restored.importPackage(package);
            auto pages=Workshop::compositionPages(restored,0);
            check(pages.size()==1&&pages[0].images.size()==3&&pages[0].images[1].image.pixel(1,0)==png.pixel(1,0),"Embedded image package depends on source PNG");
            p.save(dir.path()+"/composition.imperaproject");restored.load(p.projectPath);
            check(Workshop::compositionPages(restored,0)[0].texts[0].text==page.texts[0].text,"Text box project roundtrip failed");
            for(int i=0;i<bytes.size();i++) check(!U5_CompositionRead(bytes.constData(),i,-1,0,nullptr,nullptr),"Truncated composition was accepted");
            auto invalid=page;invalid.texts[0].bounds.setWidth(321);rejects([&]{Workshop::changeComposition(p,0,{invalid});});
            page.images.clear();bytes=Workshop::changeComposition(p,0,{page});check(U5_CompositionWord(reinterpret_cast<const unsigned char *>(bytes.constData())+16)==0,"Deleted images remained in package assets");
            QImage oversized(1025,1,QImage::Format_ARGB32);oversized.fill(Qt::red);oversized.save(path);
            rejects([&]{Workshop::importCutscenePng(path);});
        });
        test("Composition canvas imports original and modded artwork, deletes objects and edits boxes", [&] {
            QTemporaryDir dir;auto p=fixture(dir.path());
            QByteArray raw=QByteArray::fromHex("010006000000020002002200000022000000");
            auto original=U5::compress(raw);raw[10]=raw[14]=char(0x33);auto modded=U5::compress(raw);
            file(dir.path()+"/STORY1.16",original);p.resources["STORY1.16"]={original,modded};
            p.resources["CUTSCENE.OVR"].edited=Workshop::changeComposition(p,0,{Workshop::CutsceneComposition{}});
            QMap<QString,int> state;std::function<bool()> flush;
            std::unique_ptr<QWidget> editor(Workshop::compositionEditor(&p,"STORY.DAT",[&](const QMap<QString,QByteArray> &edits,const QString &){for(auto it=edits.begin();it!=edits.end();++it)p.resources[it.key()].edited=it.value();p.validate();return true;},&state,&flush));
            editor->resize(1500,850);editor->show();app.processEvents();
            auto chooseArt=[&](int version) {
                bool handled=false;
                QTimer::singleShot(0,[&] {
                    auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog)return;
                    auto bank=dialog->findChild<QComboBox *>("gameArtworkBank");auto source=dialog->findChild<QComboBox *>("gameArtworkVersion");auto list=dialog->findChild<QListWidget *>("gameArtworkImages");
                    if(!bank||!source||!list){dialog->reject();return;}
                    bank->setCurrentIndex(bank->findData("STORY1.16"));source->setCurrentIndex(version);list->setCurrentRow(0);handled=list->count()==1;dialog->accept();
                });
                editor->findChild<QAction *>("importGameCutsceneArt")->trigger();check(handled,"Game artwork chooser did not expose source variants");
            };
            chooseArt(0);chooseArt(1);
            auto page=Workshop::compositionPages(p,0)[0];check(page.images.size()==2&&page.images[0].image.pixel(0,0)==U5::palette()[3]&&page.images[1].image.pixel(0,0)==U5::palette()[2],"Game artwork import ignored original/modded choice");
            check(!editor->findChild<QPushButton *>("duplicateCutsceneElement"),"Element duplication should not clutter the editor");
            auto object=editor->findChild<QComboBox *>("cutsceneElement");object->setCurrentIndex(0);
            editor->findChild<QPushButton *>("bringCutsceneImageForward")->click();
            check(Workshop::compositionPages(p,0)[0].images[1].image.pixel(0,0)==U5::palette()[3],"Image ordering failed");
            editor->findChild<QPushButton *>("sendCutsceneImageBackward")->click();object->setCurrentIndex(1);
            auto canvas=editor->findChild<QWidget *>("compositionCanvas");
            QPointF origin((canvas->width()-640)/2.0,(canvas->height()-400)/2.0);
            auto drag=[&](QPointF a,QPointF b,bool cancel=false) {
                QMouseEvent down(QEvent::MouseButtonPress,a,a,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&down);
                QMouseEvent move(QEvent::MouseMove,b,b,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&move);
                if(cancel){QKeyEvent key(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(canvas,&key);}
                QMouseEvent up(QEvent::MouseButtonRelease,b,b,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas,&up);
            };
            drag(origin+QPointF(17,17),origin+QPointF(57,37));page=Workshop::compositionPages(p,0)[0];check(page.images[1].position==QPoint(28,18),"Image drag used incorrect coordinates");
            auto before=p.data("CUTSCENE.OVR");drag(origin+QPointF(57,37),origin+QPointF(77,57),true);check(flush()&&p.data("CUTSCENE.OVR")==before,"Escape did not cancel image drag");
            editor->findChild<QPushButton *>("deleteCutsceneElement")->click();check(Workshop::compositionPages(p,0)[0].images.size()==1,"Delete did not remove an image");
            editor->findChild<QPushButton *>("addCutsceneText")->click();
            auto text=editor->findChild<QPlainTextEdit *>("compositionText");text->setPlainText("Editable text");
            editor->findChild<QSpinBox *>("compositionWidth")->setValue(100);editor->findChild<QSpinBox *>("compositionHeight")->setValue(40);
            check(flush(),"Text draft could not be applied");page=Workshop::compositionPages(p,0)[0];check(page.texts[0].bounds==QRect(16,16,100,40)&&page.texts[0].text=="Editable text","Text box properties failed");
            app.processEvents();origin=QPointF((canvas->width()-640)/2.0,(canvas->height()-400)/2.0);
            drag(origin+QPointF(230,110),origin+QPointF(270,130));page=Workshop::compositionPages(p,0)[0];check(page.texts[0].bounds.size()==QSize(120,50),"Text-box resize handle failed");
            drag(origin+QPointF(40,40),origin+QPointF(60,60));page=Workshop::compositionPages(p,0)[0];check(page.texts[0].bounds.topLeft()==QPoint(26,26),"Text-box drag failed");
            editor->findChild<QPushButton *>("deleteCutsceneElement")->click();check(Workshop::compositionPages(p,0)[0].texts.isEmpty(),"Delete did not remove a text box");
        });
        test("Map contents inherit Minoc, edit sparsely, roundtrip packages and move without "
             "painting",
             [&] {
                 QTemporaryDir dir;
                 auto p = fixture(dir.path());
                 file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
                 p.openGame(dir.path());
                 auto inherited = Workshop::inheritedContents({0, 5, 0});
                 auto minoc = std::find_if(inherited.begin(), inherited.end(),
                                           [](const ContentsDef &d) { return d.original == 14; });
                 check(minoc != inherited.end() && minoc->items[0].type == 6 &&
                           minoc->items[0].quantity == 5 && minoc->refill == 2,
                       "Minoc discovery semantics lost");
                 MapCanvas canvas;
                 canvas.side = 32;
                 canvas.ids = QByteArray(1024, 0);
                 canvas.keyboardCell = {2, 2};
                 Workshop::ContentsEditor editor(
                     &p, {0, 5, 0}, &canvas,
                     [&](const QMap<QString, QByteArray> &changes, const QString &) {
                         for (auto it = changes.begin(); it != changes.end(); ++it)
                             p.resources[it.key()].edited = it.value();
                         p.validate();
                         return true;
                     });
                 editor.selectCell({2, 2});
                 check(p.changed().isEmpty(), "Selecting inherited loot changed project");
                 auto mode = editor.findChild<QComboBox *>("contentsMode");
                 auto table = editor.findChild<QTableWidget *>("contentsItems");
                 check(table->rowCount() == 1 && table->item(0, 0)->text() == "Skull keys",
                       "Inherited contents are not readable");
                 mode->setCurrentIndex(1);
                 auto definitions = Workshop::readContents(p.data("CONTENTS.OVR"));
                 check(definitions.size() == 1 && definitions[0].original == 14,
                       "Contents override not sparse");
                 auto quantity = qobject_cast<QSpinBox *>(table->cellWidget(0, 1));
                 quantity->setValue(8);
                 QMetaObject::invokeMethod(quantity, "editingFinished");
                 check(Workshop::readContents(p.data("CONTENTS.OVR"))[0].items[0].quantity == 8,
                       "Quantity edit lost");
                 auto bytes = p.package();
                 if (qEnvironmentVariableIsSet("IMPERA_CONTENTS_TEST_PACKAGE"))
                     file(qEnvironmentVariable("IMPERA_CONTENTS_TEST_PACKAGE"), bytes);
                 check(bytes.left(8) == "IMOD0004", "Contents lacks capability package version");
                 auto imported = fixture(dir.path());
                 imported.importPackage(bytes);
                 check(imported.data("CONTENTS.OVR") == p.data("CONTENTS.OVR"),
                       "Contents package failed roundtrip");
                 p.save(dir.path() + "/contents.imperaproject");
                 Project restored;
                 restored.load(dir.path() + "/contents.imperaproject");
                 check(restored.data("CONTENTS.OVR") == p.data("CONTENTS.OVR"),
                       "Contents project identity changed");
                 int selected = -1;
                 for (auto actor : canvas.actors)
                     if (actor.x == 2 && actor.y == 2)
                         selected = actor.npc;
                 check(selected >= 0 && canvas.moveNpc(selected, {3, 4}), "Contents drag failed");
                 definitions = Workshop::readContents(p.data("CONTENTS.OVR"));
                 check(definitions[0].x == 3 && definitions[0].y == 4 &&
                           definitions[0].original == 14,
                       "Moved contents lost source suppression");
                 check(canvas.ids == QByteArray(1024, 0) &&
                           p.data("TOWNE.DAT") == QByteArray(16384, 0),
                       "Contents movement painted terrain");
                 mode->setCurrentIndex(0);
                 check(p.changed().isEmpty(), "Restore original retained a contents patch");
                 auto invalid = *minoc;
                 invalid.items[0].quantity = 100;
                 rejects([&] { Workshop::writeContents({invalid}, {}); });
             });
        test("Location names follow maps, conversations, packages and undo", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            file(dir.path() + "/CASTLE.DAT", QByteArray(16384, 0));
            file(dir.path() + "/TOWNE.TLK",
                 U5::writeDialogue(
                     {{1, U5::encodeText(
                              "Mage<Entry>A "
                              "mage.<Entry>Hello<Entry>Work<Entry>Bye<Entry><Any><Label 15>@")}}));
            QByteArray npcs(4608, 0);
            npcs[513] = 64;
            npcs[545] = 1;
            file(dir.path() + "/TOWNE.NPC", npcs);
            p.openGame(dir.path());
            check(p.locationName(1) == "Moonglow" && p.locationName(17) == "Lord British's Castle",
                  "Original location names changed");
            p.resources["LOCATIONS.OVR"].edited = p.renameLocation(1, " New Moonglow ");
            p.resources["LOCATIONS.OVR"].edited = p.renameLocation(17, "Avatar's Castle");
            p.resources["LOCATIONS.OVR"].edited = p.renameLocation(33, "Abyss");
            check(p.mapPages("TOWNE.DAT")[0].name == "New Moonglow — Floor 0" &&
                      p.mapPages("TOWNE.DAT")[1].name == "New Moonglow — Floor 1",
                  "Rename did not apply to all floors");
            bool foundMap = false, foundNpc = false;
            const auto destinations = Workshop::contentDestinations(p);
            for (auto d : destinations) {
                if (d.id == "world/TOWNE.DAT/0")
                    foundMap = d.title == "New Moonglow";
                if (d.character == Workshop::npcCharacter(0, 1))
                    foundNpc = d.group == "New Moonglow";
            }
            check(foundMap && foundNpc, "Renamed location missing from map or dialogue navigation");
            auto changes = Workshop::contentChanges(p, destinations);
            check(changes["world/TOWNE.DAT/0"].contains("Location name") &&
                      !changes["world/TOWNE.DAT/1"].contains("Location name"),
                  "Rename change markers leaked to unrelated locations");
            const auto positions = Workshop::mapPositions(p);
            check(std::any_of(positions.begin(), positions.end(),
                              [](auto pos) {
                                  return pos.id == 100 && pos.name == "New Moonglow entrance";
                              }),
                  "Entrance retained its old name");
            auto package = p.package();
            check(package.startsWith("IMOD0017"), "Renames lack package version protection");
            Project imported;
            imported.openGame(dir.path());
            imported.importPackage(package);
            check(imported.locationName(1) == "New Moonglow" &&
                      imported.locationName(33) == "Abyss",
                  "Package lost location names");
            package.replace(0, 8, "IMOD0016");
            rejects([&] { imported.importPackage(package); });
            p.save(dir.path() + "/locations.imperaproject");
            Project reopened;
            reopened.load(p.projectPath);
            check(reopened.locationName(17) == "Avatar's Castle", "Project lost location names");
            rejects([&] { p.renameLocation(1, " "); });
            rejects([&] { p.renameLocation(1, QString(64, 'x')); });
            rejects([&] { p.renameLocation(42, "Out of bounds"); });
            rejects([&] { p.renameLocation(1, "Bad\nname"); });
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Cannot open renaming fixture");
            window.selectResource("TOWNE.DAT");
            auto active = [&] {
                return window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto rename = active()->findChild<QToolButton *>("renameLocation");
            check(rename, "Map header lacks Rename location control");
            QTimer::singleShot(0, [] {
                auto dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                if (dialog) {
                    dialog->setTextValue("New Moonglow");
                    dialog->accept();
                }
            });
            rename->click();
            check(window.projectForTests().locationName(1) == "New Moonglow" &&
                      active()->findChild<QLabel *>("mapLocationLabel")->text() == "New Moonglow:",
                  "Rename UI did not refresh current map");
            QAction *undo = nullptr, *redo = nullptr;
            for (auto action : window.findChildren<QAction *>()) {
                if (action->shortcut() == QKeySequence::Undo)
                    undo = action;
                if (action->shortcut() == QKeySequence::Redo)
                    redo = action;
            }
            check(undo && redo && undo->isEnabled(), "Rename missing undo");
            undo->trigger();
            QApplication::processEvents();
            check(window.projectForTests().locationName(1) == "Moonglow" &&
                      active()->findChild<QLabel *>("mapLocationLabel")->text() == "Moonglow:",
                  "Undo did not restore map name");
            redo->trigger();
            QApplication::processEvents();
            check(window.projectForTests().locationName(1) == "New Moonglow", "Redo lost map name");
        });
        test("Select Move opens contextual contents on settlement and encounter maps", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            file(dir.path() + "/BRIT.CBT", QByteArray(352, 0));
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Contents fixture failed");
            window.selectResource("TOWNE.DAT");
            auto active = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            auto pages = U5::mapPages("TOWNE.DAT", window.projectForTests().data("TOWNE.DAT"));
            auto workspace =
                dynamic_cast<MapWorkspace *>(active->findChild<QWidget *>("mapWorkspace"));
            if (!workspace)
                for (auto w : active->findChildren<QWidget *>())
                    if ((workspace = dynamic_cast<MapWorkspace *>(w)))
                        break;
            check(workspace, "Map workspace missing");
            int minocPage = -1;
            for (int i = 0; i < pages.size(); i++)
                if (pages[i].settlement == 4 && pages[i].floor == 0)
                    minocPage = i;
            check(minocPage >= 0, "Minoc page missing");
            workspace->openPage(minocPage);
            auto canvas = dynamic_cast<MapCanvas *>(workspace->findChild<QWidget *>("mapCanvas"));
            check(canvas && canvas->prepareSelection({2, 2}), "Select Move did not find discovery");
            check(workspace->findChild<QComboBox *>("contentsMode") &&
                      workspace->findChild<QTableWidget *>("contentsItems")->rowCount() == 1,
                  "Context contents pane absent");
            window.selectResource("BRIT.CBT");
            active = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            canvas = dynamic_cast<MapCanvas *>(active->findChild<QWidget *>("mapCanvas"));
            check(canvas && canvas->prepareSelection({1, 1}), "Encounter cell selection failed");
            auto mode = active->findChild<QComboBox *>("contentsMode");
            check(mode, "Encounter contents pane absent");
            mode->setCurrentIndex(2);
            auto defs = Workshop::readContents(window.projectForTests().data("CONTENTS.OVR"));
            check(defs.size() == 1 && defs[0].kind == 1 && defs[0].map == 0,
                  "Encounter target identity wrong");
        });
        test("Placed container move and removal are atomic and undo restores contents", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            QByteArray overlay(0x3986, 0);
            overlay.replace(0x3886, 256, QByteArray(256, char(255)));
            file(dir.path() + "/DATA.OVL", overlay);
            file(dir.path() + "/BRIT.DAT", QByteArray(256, 0));
            file(dir.path() + "/INIT.OOL", QByteArray(256, 0));
            file(dir.path() + "/UNDER.OOL", QByteArray(256, 0));
            QByteArray objects(256, 0);
            objects[8] = objects[9] = 1;
            objects[10] = 4;
            objects[11] = 5;
            file(dir.path() + "/BRIT.OOL", objects);
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Chest fixture failed");
            window.projectForTests().resources["BRIT.OOL"].included = true;
            window.selectResource("BRIT.DAT");
            auto active = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            auto canvas = dynamic_cast<MapCanvas *>(active->findChild<QWidget *>("mapCanvas"));
            check(canvas && canvas->prepareSelection({4, 5}), "Chest selection failed");
            auto mode = active->findChild<QComboBox *>("contentsMode");
            mode->setCurrentIndex(2);
            auto before = window.projectForTests().data("CONTENTS.OVR");
            check(!before.isEmpty(), "Empty chest override lost");
            int entry = canvas->selectedNpc;
            check(entry >= 0 && canvas->moveNpc(entry, {6, 7}), "Empty chest move failed");
            check(U5::byte(window.projectForTests().data("BRIT.OOL"), 10) == 6 &&
                      Workshop::readContents(window.projectForTests().data("CONTENTS.OVR"))[0].y ==
                          7,
                  "Chest and contents separated");
            auto moved = window.projectForTests().data("CONTENTS.OVR");
            auto remove = active->findChild<QPushButton *>("contentsDeleteContainer");
            check(remove, "Container deletion absent");
            remove->click();
            check(!U5::byte(window.projectForTests().data("BRIT.OOL"), 8) &&
                      window.projectForTests().data("CONTENTS.OVR").isEmpty(),
                  "Delete left orphan contents");
            QAction *undo = nullptr;
            for (auto action : window.findChildren<QAction *>())
                if (action->shortcut() == QKeySequence::Undo)
                    undo = action;
            check(undo, "Undo missing");
            undo->trigger();
            app.processEvents();
            check(U5::byte(window.projectForTests().data("BRIT.OOL"), 8) == 1 &&
                      window.projectForTests().data("CONTENTS.OVR") == moved,
                  "Undo did not restore both records");
        });
        test("Dungeon contents use contextual selection and validate target floors", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/DUNGEON.DAT", QByteArray(4096, 0));
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Dungeon contents fixture failed");
            window.selectResource("DUNGEON.DAT");
            auto active = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            MapCanvas *canvas = nullptr;
            for (auto widget : active->findChildren<QWidget *>())
                if (auto c = dynamic_cast<MapCanvas *>(widget))
                    canvas = c;
            check(canvas && canvas->prepareSelection({1, 2}), "Dungeon Select Move failed");
            auto mode = active->findChild<QComboBox *>("contentsMode");
            check(mode, "Dungeon inspector missing");
            mode->setCurrentIndex(2);
            auto definitions =
                Workshop::readContents(window.projectForTests().data("CONTENTS.OVR"));
            check(definitions.size() == 1 && definitions[0].map == 33 && definitions[0].x == 1,
                  "Dungeon contents target wrong");
            auto invalid = window.projectForTests();
            invalid.resources.remove("DUNGEON.DAT");
            rejects([&] { invalid.validate(); });
        });
        test("Startup opens Britannia at the castle, world-only thumbnails and quiet canvas", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray overlay(0x3986, 0);
            overlay.replace(0x3886, 256, QByteArray(256, char(255)));
            file(dir.path() + "/DATA.OVL", overlay);
            file(dir.path() + "/BRIT.DAT", QByteArray(256, 0));
            file(dir.path() + "/UNDER.DAT", QByteArray(65536, 0));
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Startup fixture failed");
            w.resize(1280, 800);
            w.show();
            app.processEvents();
            auto active = [&] { return w.findChild<QStackedWidget *>("workspaces")->currentWidget(); };
            auto view = [&] { return dynamic_cast<MapView *>(active()->findChild<QScrollArea *>("canvasView")); };
            QPointF castle(Workshop::TableDocument::value({}, 7, 16) + 0.5, Workshop::TableDocument::value({}, 8, 16) + 0.5);
            check(view() && QLineF(view()->mapCenter(), castle).length() < 0.1, "Startup castle is not centered");
            check(active()->findChild<QComboBox *>("mapPage")->currentText() == "Britannia", "Startup did not open Britannia");
            check(active()->findChild<QWidget *>("mapMinimap")->isVisible(), "Britannia thumbnail missing");
            check(!active()->findChild<QLabel *>("mapInteractionHint") && active()->findChild<QWidget *>("mapCanvas")->toolTip().isEmpty() && view()->toolTip().isEmpty(), "Map instructions or hover tooltip remain");
            check(!w.findChild<QToolBar *>(), "Command toolbar still occupies a row");
            auto rules = Workshop::TableDocument::change({}, 7, 16, 94);
            rules = Workshop::TableDocument::change(rules, 8, 16, 110);
            w.projectForTests().resources["RULES.TBL"].edited = rules;
            w.projectForTests().save(dir.path() + "/castle.imperaproject");
            check(w.openProject(dir.path() + "/castle.imperaproject"), "Project startup failed");
            app.processEvents();
            check(QLineF(view()->mapCenter(), QPointF(94.5, 110.5)).length() < 0.1, "Project startup ignored castle entrance override");
            for (const QString name : {"UNDER.DAT", "TOWNE.DAT"}) {
                w.selectResource(name);
                app.processEvents();
                check(active()->findChild<QWidget *>("mapMinimap")->isVisible() == (name == "UNDER.DAT"), "Thumbnail shown on a settlement or absent on Underworld");
            }
        });
        test("Character inspector fits native control metrics with larger fonts", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            QByteArray schedules(4608, 0); schedules[512] = 1; schedules[544] = 1;
            file(dir.path() + "/TOWNE.NPC", schedules);
            file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, U5::encodeText("A character with an unusually long name that should wrap rather than widen the inspector<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry><Any><Label 15>@")}}));
            WorkshopWindow w;
            auto font = w.font(); font.setPointSize(qMax(14, font.pointSize() + 3)); w.setFont(font);
            check(w.openGame(dir.path()), "Inspector fixture failed");
            w.resize(1400, 900); w.show();
            w.selectResource("TOWNE.DAT"); app.processEvents();
            auto root = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            root->findChild<QButtonGroup *>("mapTools")->button(MapCanvas::InspectNpc)->click();
            root->findChild<QComboBox *>("mapNpcList")->setCurrentIndex(0);
            app.processEvents();
            auto inspector = root->findChild<QScrollArea *>("mapCharacterInspector");
            check(inspector && inspector->horizontalScrollBar()->maximum() == 0, "Native control metrics overflow the inspector");
            check(inspector->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff, "Overflow was hidden instead of fixed");
            check(inspector->widget()->width() >= inspector->widget()->minimumSizeHint().width(), "Inspector controls do not fit their measured minimum");
        });
        test("Aggregate changes isolate locations, shared conversations and encounters", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            auto put = [&](const QString &name, const QByteArray &bytes) { p.resources[name] = {bytes, bytes, false}; };
            put("TOWNE.DAT", QByteArray(16384, 0));
            QByteArray schedules(4608, 0);
            for (int slot = 0; slot < 3; ++slot) {
                schedules[512 + slot] = 1;
                schedules[544 + slot] = slot < 2 ? 1 : 2;
            }
            put("TOWNE.NPC", schedules);
            auto talk = [](QString name) { return U5::encodeText(name + "<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry><Any><Label 15>@"); };
            put("TOWNE.TLK", U5::writeDialogue({{1, talk("Shared")}, {2, talk("Other")}}));
            put("BRIT.CBT", QByteArray(704, 0));
            QByteArray seed(66, 0); seed += QByteArray::fromHex("ffff");
            put("SIGNS.DAT", Workshop::writeSigns(seed, {{1, 0, 1, 1, Workshop::encodeSign("Moonglow")}}));
            auto routes = Workshop::contentDestinations(p);
            auto changed = Workshop::contentChanges(p, routes);
            for (auto parts : changed) check(parts.isEmpty(), "Pristine content marked changed");
            p.resources["TOWNE.DAT"].edited[1024] = 4; // Upper floor of Moonglow.
            changed = Workshop::contentChanges(p, routes);
            check(changed["world/TOWNE.DAT/0"].contains("Terrain") && changed["world/TOWNE.DAT/1"].isEmpty(), "Terrain changes leak into unrelated settlements");
            p.resources["TOWNE.DAT"].edited = p.resources["TOWNE.DAT"].original;
            p.resources["TOWNE.TLK"].edited = U5::writeDialogue({{1, talk("Updated")}, {2, talk("Other")}});
            changed = Workshop::contentChanges(p, routes);
            check(changed["character/TOWNE/0/0"].contains("Conversation") && changed["character/TOWNE/0/1"].contains("Conversation") && changed["character/TOWNE/0/2"].isEmpty(), "Shared dialogue markers are not identity-specific");
            check(changed["world/TOWNE.DAT/0"].contains("Conversations") && changed["world/TOWNE.DAT/1"].isEmpty(), "Location does not aggregate its conversations");
            p.resources["TOWNE.TLK"].edited = p.resources["TOWNE.TLK"].original;
            p.resources["TOWNE.NPC"].edited[16] = 7;
            changed = Workshop::contentChanges(p, routes);
            check(changed["character/TOWNE/0/0"].isEmpty() && changed["character/TOWNE/0/1"].contains("Schedule / appearance"), "Schedule changes mark unrelated characters");
            p.resources["SIGNS.DAT"].edited = Workshop::writeSigns(seed, {{1, 0, 1, 1, Workshop::encodeSign("Changed")}});
            changed = Workshop::contentChanges(p, routes);
            check(changed["world/TOWNE.DAT/0"].contains("Signs") && changed["world/TOWNE.DAT/1"].isEmpty(), "Sign changes mapped to wrong town");
            p.resources["BRIT.CBT"].edited[363] = 5;
            changed = Workshop::contentChanges(p, routes);
            check(changed["world/BRIT.CBT/0"].isEmpty() && changed["world/BRIT.CBT/1"].contains("Encounter details"), "Combat metadata marks every encounter");
            p.resources["TOWNE.NPC"].edited = p.resources["TOWNE.NPC"].original;
            changed = Workshop::contentChanges(p, routes);
            check(changed["character/TOWNE/0/1"].isEmpty(), "Reverted content remains changed");
        });
        test("Overview dashboard validation, navigation and compact toolbar", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Overview fixture failed");
            auto &p = w.projectForTests();
            p.resources["INIT.GAM"].edited[0x202] = 1;
            auto workspace = w.findChild<QComboBox *>("contentWorkspace");
            workspace->setCurrentText("Mod Overview");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto overview = w.findChild<QWidget *>("modOverview");
            check(overview, "Overview did not open from workspace selector");
            auto overviewTabs = overview->findChild<QTabWidget *>("overviewTabs");
            auto advanced = overview->findChild<QToolButton *>("overviewAdvanced");
            check(overviewTabs && overviewTabs->count() == 2 &&
                      overviewTabs->tabText(0) == "Changed content" &&
                      overviewTabs->tabText(1) == "Validation" && advanced && !advanced->isChecked(),
                  "Overview should show only changes and validation by default");
            for (auto button : overview->findChildren<QPushButton *>())
                check(!QStringList{"Save Project", "Name Mod", "Export Package", "Test Mod"}
                           .contains(button->text()), "Duplicate overview command remains");
            advanced->click();
            check(overviewTabs->count() == 3 &&
                      overviewTabs->currentWidget()->objectName() == "overviewAdvancedPanel",
                  "Advanced diagnostics are inaccessible");
            advanced->click();
            check(overviewTabs->count() == 2,
                  "Advanced diagnostics did not collapse");
            auto changed = overview->findChild<QTreeWidget *>("overviewChanges");
            auto resources = overview->findChild<QTreeWidget *>("overviewPackageResources");
            check(changed && changed->topLevelItemCount() == 1 && resources && resources->topLevelItemCount() == 1, "Overview does not list changed content and actual package resources");
            overview->findChild<QPushButton *>("overviewValidate")->click();
            check(overview->findChild<QTreeWidget *>("overviewDiagnostics")->topLevelItemCount() > 0 && !overview->findChild<QLabel *>("overviewValidationStatus")->text().startsWith("Not checked"), "Overview validation not populated");
            check(p.changed() == QStringList{"INIT.GAM"}, "Overview validation mutated resources");
            changed->itemDoubleClicked(changed->topLevelItem(0), 0);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            check(workspace->currentText() == "Game Rules", "Changed-content link did not open its editor");
            check(!w.findChild<QToolBar *>(), "Command toolbar still occupies a row");
            QStringList menus;
            for (auto action : w.menuBar()->actions())
                menus.append(action->text());
            check(menus == QStringList{"Project", "Edit", "Tools"},
                  "Command menus are out of order");
            auto editMenu = w.menuBar()->actions()[1]->menu();
            QStringList editCommands;
            for (auto action : editMenu->actions())
                if (!action->isSeparator())
                    editCommands.append(action->text());
            check(editCommands.size() == 4 && editCommands[0].startsWith("Undo") &&
                      editCommands[1].startsWith("Redo") && editCommands[2] == "Back" &&
                      editCommands[3] == "Forward",
                  "Edit commands lost their menu access");
            auto projectMenu = w.menuBar()->actions().first()->menu();
            check(projectMenu && projectMenu->actions().size() >= 6, "Project commands lost their menu access");
            bool shortcut = false;
            for (auto action : projectMenu->actions()) if (action->text() == "Open Project") shortcut = action->shortcut() == QKeySequence::Open;
            check(shortcut, "Moved project command lost its shortcut");
            p.resources["INIT.GAM"].edited = p.resources["INIT.GAM"].original;
            w.selectResource("INIT.GAM");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto field = w.findChild<QStackedWidget *>("workspaces")->currentWidget()->findChild<QSpinBox *>();
            check(field && field->value() < field->maximum(), "Missing editable starting-state field");
            field->setValue(field->value() + 1);
            workspace->setCurrentText("Mod Overview");
            auto undo = [&] {
                for (auto action : w.findChildren<QAction *>()) if (action->text().startsWith("Undo ")) { action->trigger(); break; }
                app.processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            };
            undo();
            auto active = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            check(active->objectName() == "modOverview" && active->findChild<QTreeWidget *>("overviewChanges")->topLevelItemCount() == 0 && p.changed().isEmpty(), "Overview failed to refresh after Undo");
            for (auto action : w.findChildren<QAction *>()) if (action->text().startsWith("Redo ")) { action->trigger(); break; }
            app.processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            active = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            check(active->findChild<QTreeWidget *>("overviewChanges")->topLevelItemCount() == 1 && active->findChild<QLabel *>("overviewValidationStatus")->text().startsWith("Not checked"), "Overview failed to refresh and invalidate validation after Redo");

            w.findChild<QAction *>("showResourceLibrary")->trigger();
        });
        test("Alternate tilesets preserve native pixels, package loading and editor undo", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            for (const auto &sheet : modTilesetSheets) {
                const QString name = QString::fromLatin1(sheet.resource);
                auto g = U5::readGraphics(name, p.data(name));
                check(g.images.size() == sheet.columns * 16 &&
                          g.images[0].size() == QSize(sheet.width, sheet.height),
                      "Native tileset layout was normalized or lost");
                check(U5::writeGraphics(g) == p.data(name), "Unedited PNG changed bytes");
                g.images[0].setPixel(0, 0, qRgb(12, 34, 56));
                auto edited = U5::writeGraphics(g);
                check(U5::readGraphics(name, edited).images[0].pixel(0, 0) == qRgb(12, 34, 56),
                      "RGB artwork edit lost its native color");
                p.resources[name].edited = edited;
            }
            p.validate();
            auto package = p.package();
            auto base = fixture(dir.path());
            base.importPackage(package);
            for (const auto &sheet : modTilesetSheets)
                check(base.data(sheet.resource) == p.data(sheet.resource),
                      "Alternate tileset package did not roundtrip");
            p.save(dir.path() + "/tilesets.imperaproject");
            Project reopened;
            reopened.load(p.projectPath);
            check(reopened.changed() == p.changed(), "Tileset project edits disappeared");
            const auto diagnostics = validateMod(p);
            check(std::none_of(diagnostics.cbegin(), diagnostics.cend(),
                               [](const auto &d) { return d.severity == ModDiagnostic::Error; }),
                  "Tileset package validation failed");
            auto g = U5::readGraphics("TILE_APP.PNG", p.data("TILE_APP.PNG"));
            g.images[0].setPixel(0, 0, qRgba(0, 0, 0, 0));
            rejects([&] { U5::writeGraphics(g); });
            g = U5::readGraphics("TILE_APP.PNG", p.data("TILE_APP.PNG"));
            g.images[0] = QImage(16, 16, QImage::Format_ARGB32);
            rejects([&] { U5::writeGraphics(g); });
            g = U5::readGraphics("TILE_APP.PNG", p.data("TILE_APP.PNG"));
            for (int i = 0; i < 241; i++)
                g.images[i / 224].setPixel(i % 14, (i % 224) / 14, qRgb(i, 17, 23));
            rejects([&] { U5::writeGraphics(g); });
            if (qEnvironmentVariableIsSet("IMPERA_TILESET_TEST_PACKAGE"))
                file(qEnvironmentVariable("IMPERA_TILESET_TEST_PACKAGE"), package);
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Tileset editor fixture open");
            for (const auto &sheet : modTilesetSheets) {
                w.selectResource(sheet.resource);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                auto gallery = w.findChild<QListWidget *>("artworkGallery");
                check(gallery && gallery->count() == sheet.columns * 16,
                      "Alternate artwork slots missing");
            }
            w.selectResource("TILE_APP.PNG");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto canvas = w.findChild<QWidget *>("pixelCanvas");
            auto original = w.projectForTests().data("TILE_APP.PNG");
            QMouseEvent press(QEvent::MouseButtonPress, {1, 1}, {1, 1}, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, {1, 1}, {1, 1}, Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &press);
            QApplication::sendEvent(canvas, &release);
            check(w.projectForTests().data("TILE_APP.PNG") != original,
                  "Painting alternate artwork did not commit");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            check(w.projectForTests().data("TILE_APP.PNG") == original,
                  "Alternate artwork undo lost original pixels");
        });
        test("Content routes consolidate companions and artwork preserves slot identity", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            for (const QString name : {"MISCMAPS.DAT", "QUESTION.DAT", "MISCMSG.DAT", "ENDMSG.DAT", "INIT.OOL", "UNDER.OOL", "SHOPPE.DAT", "DATA.OVL"})
                p.resources[name] = {QByteArray(), QByteArray(), false};
            QByteArray sceneBank(1216, 0);
            sceneBank += QByteArray::fromHex("06000c030109");
            p.resources["MISCMAPS.DAT"] = {sceneBank, sceneBank, false};
            auto routes = Workshop::contentDestinations(p);
            QSet<QString> ids;
            int scenes = 0, underworld = 0;
            for (const auto &d : routes) {
                check(!ids.contains(d.id), "Duplicate semantic destination");
                ids.insert(d.id);
                check(d.resource != "QUESTION.DAT" && d.resource != "MISCMSG.DAT" && d.resource != "ENDMSG.DAT", "Scene message companion duplicated");
                if (d.resource == "MISCMAPS.DAT") {
                    ++scenes;
                    int scene = d.selection.value("scene/current", -1);
                    check(scene >= 0 && scene <= 4 && Workshop::matchesSelection(d, d.selection), "Scene selection missing");
                    check((d.workspace == "Game Rules") == (scene == 4), "Creation route in wrong workspace");
                    check(d.sources.size() == 2, "Scene message provenance missing");
                }
                if (d.resource == "UNDER.OOL" || d.resource == "INIT.OOL") ++underworld;
                if (d.resource == "SHOPPE.DAT")
                    check(d.workspace == "Dialogue", "Shop dialogue misplaced");
                if (d.resource == "DATA.OVL") check(d.workspace == "Mod Overview", "Overlay coverage is not a rule editor");
            }
            check(scenes == 5 && underworld == 1, "Scene or effective-world routes missing");
            Project consolidated = p;
            consolidated.resources["BRIT.DAT"] = {QByteArray(256, 0), QByteArray(256, 0)};
            consolidated.resources["UNDER.DAT"] = {QByteArray(65536, 0), QByteArray(65536, 0)};
            consolidated.resources["BRIT.OOL"] = {QByteArray(256, 0), QByteArray(256, 0)};
            consolidated.resources["SIGNS.DAT"] = {QByteArray(), QByteArray()};
            consolidated.resources["LOOK2.DAT"] = {QByteArray(), QByteArray()};
            const auto consolidatedRoutes = Workshop::contentDestinations(consolidated);
            for (const auto &route : consolidatedRoutes) {
                check(route.resource != "SIGNS.DAT" && route.resource != "BRIT.OOL" &&
                          route.resource != "INIT.OOL" && route.resource != "UNDER.OOL",
                      "Map-owned signs or objects retain duplicate navigation entries");
                if (route.resource == "LOOK2.DAT")
                    check(route.workspace == "Game Rules",
                          "Look descriptions are not in Game Rules");
            }
            p.resources["MISCMAPS.DAT"].edited = QByteArray(1, 0);
            routes = Workshop::contentDestinations(p);
            check(std::any_of(routes.begin(), routes.end(), [](const auto &d) { return d.resource == "MISCMSG.DAT"; }), "Damaged scenes hide independent messages");
            p.resources.remove("MISCMAPS.DAT");
            routes = Workshop::contentDestinations(p);
            check(std::any_of(routes.begin(), routes.end(), [](const auto &d) { return d.resource == "QUESTION.DAT" && d.workspace == "Game Rules"; }), "Missing scene file hides creation text");
            check(Workshop::artworkTitle("DNG1.16").contains("Dungeon scenery") && Workshop::artworkTitle("STORY2.16").contains("Introduction"), "Verified artwork names missing");
            check(Workshop::artworkTitle("UNKNOWN.16").contains("UNKNOWN"), "Unknown artwork purpose invented");
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Artwork fixture failed");
            w.selectResource("TILES.16");
            auto imageSlots = w.findChild<QComboBox *>("imageSlot");
            auto gallery = w.findChild<QListWidget *>("artworkGallery");
            auto zoom = w.findChild<QComboBox *>("artworkZoom");
            check(imageSlots && imageSlots->count() == 512 && gallery && gallery->count() == 512, "Artwork slots lost");
            imageSlots->setCurrentIndex(127);
            zoom->setCurrentIndex(4);
            w.selectResource("INIT.GAM");
            w.selectResource("TILES.16");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            check(w.findChild<QComboBox *>("imageSlot")->currentIndex() == 127 && w.findChild<QComboBox *>("artworkZoom")->currentIndex() == 4, "Artwork selection or zoom not restored");
            check(w.projectForTests().changed().isEmpty(), "Artwork navigation edited resources");
            auto graphicsWorkspace = w.findChild<QComboBox *>("contentWorkspace");
            auto graphicsTree = w.findChild<QWidget *>("resourceLibrary")->findChild<QTreeWidget *>();
            check(graphicsWorkspace->currentText() == "Graphics" &&
                      graphicsWorkspace->count() == 6 &&
                      graphicsWorkspace->findText("New Game") < 0 &&
                      graphicsTree->topLevelItem(0)->text(0) == "Tilesets" &&
                      graphicsTree->topLevelItem(0)->child(0)->text(0) == "DOS",
                  "Graphics should put Tiles first and New Game should be consolidated");
            QByteArray scenesData(1216, 0);
            scenesData += QByteArray::fromHex("06000c030109");
            file(dir.path() + "/MISCMAPS.DAT", scenesData);
            check(w.openGame(dir.path()), "Scene navigation fixture failed");
            auto workspace = w.findChild<QComboBox *>("contentWorkspace");
            workspace->setCurrentText("Game Rules");
            auto tree = w.findChild<QWidget *>("resourceLibrary")->findChild<QTreeWidget *>();
            tree->setCurrentItem(treeItem(tree, "Character creation"));
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto sceneSelector = w.findChild<QComboBox *>("sceneSelector");
            check(sceneSelector && sceneSelector->currentIndex() == 4, "Creation destination opened wrong scene");
            sceneSelector->setCurrentIndex(0);
            check(workspace->currentText() == "Cutscenes", "Scene selector lost semantic workspace");

        });
        test(
            "Cross-bank character identity, shared dialogue and unused "
            "conversations",
            [&] {
                QTemporaryDir dir;
                auto p = fixture(dir.path());
                auto conversation = [](const QString &name) {
                    return U5::encodeText(name + "<Entry>Description<Entry>Greeting<Entry>"
                                                 "Job<Entry>Bye<Entry><Any><Label 15>@");
                };
                for (const QString stem : {"TOWNE", "KEEP"}) {
                    file(dir.path() + "/" + stem + ".DAT", QByteArray(16384, 0));
                    file(dir.path() + "/" + stem + ".TLK",
                         U5::writeDialogue({{1, conversation(stem == "TOWNE" ? "Alice<Set Flag><Byte 255>" : "Keeper")},
                                            {2, conversation("Unused")}}));
                    QByteArray schedules(4608, 0);
                    schedules[512] = 64;
                    schedules[544] = 1;
                    schedules[576 + 512 + 1] = 64;
                    schedules[576 + 544 + 1] = 1;
                    schedules[576 + 512 + 2] = 64;
                    schedules[576 + 544 + 2] = 99;
                    file(dir.path() + "/" + stem + ".NPC", schedules);
                }
                p.openGame(dir.path());
                auto index = Workshop::characterDestinations(p);
                check(index.size() == 8, "Character index merged slots, banks or "
                                         "unassigned conversations");
                QSet<QString> ids;
                for (const auto &d : index) {
                    if (d.resource == "TOWNE.TLK" && d.character >= 0)
                        check(d.title == "Alice", "Runtime dialogue actions leaked into character names");
                    check(!ids.contains(d.id), "Cross-bank character identity collision");
                    ids.insert(d.id);
                    if (d.character >= 0 && d.conversation == 0)
                        check(d.provenance.contains("Shared dialogue: 2"),
                              "Shared dialogue provenance missing");
                    if (d.character == 34)
                        check(d.conversation == -1 && d.resource.endsWith(".NPC"),
                              "Special character has invented dialogue");
                }
                const auto navigation = Workshop::contentDestinations(p);
                int dialogueEntries = 0;
                for (const auto &d : navigation)
                    if (d.workspace == "Dialogue") {
                        check(d.conversation >= 0, "Schedule-only NPC cluttered dialogue navigation");
                        ++dialogueEntries;
                    }
                int typeLibraries = 0;
                for (const auto &d : navigation)
                    if (d.workspace == "Graphics" && d.group == "Custom tiles" &&
                        d.selection.value("customTiles/kind") > 0) {
                        check(d.resource == "TYPES.OVR" &&
                                  d.selection.value("customTiles/kind") > 0,
                              "Type library lost its semantic navigation route");
                        ++typeLibraries;
                    }
                check(typeLibraries == 2,
                      "NPC and monster type libraries must be under Graphics / Custom tiles");
                check(dialogueEntries == 6,
                      "Dialogue navigation lost shared NPCs or unassigned conversations");
                check(p.data("TOWNE.NPC") == p.resources["TOWNE.NPC"].original,
                      "Filtering dialogue navigation modified NPC schedules");
                WorkshopWindow w;
                check(w.openGame(dir.path()), "Character UI fixture failed");
                w.show();
                auto selector = w.findChild<QComboBox *>("contentWorkspace");
                selector->setCurrentText("Dialogue");
                auto filter = w.findChild<QComboBox *>("characterLocationFilter");
                check(filter && filter->findText("Moonglow") >= 0 &&
                          filter->findText("Unassigned conversations") >= 0,
                      "Cross-bank location filters missing");
                auto tree = w.findChild<QWidget *>("resourceLibrary")->findChild<QTreeWidget *>();
                auto choose = [&](const QString &id) {
                    selector->setCurrentText("Dialogue");
                    filter->setCurrentIndex(0);
                    QTreeWidgetItemIterator it(tree);
                    while (*it) {
                        if ((*it)->data(0, Qt::UserRole + 2).toString() == id) {
                            tree->setCurrentItem(*it);
                            app.processEvents();
                            return;
                        }
                        ++it;
                    }
                    throw std::runtime_error("Character navigation entry missing");
                };
                filter->setCurrentIndex(filter->findText("Moonglow"));
                check(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 1,
                      "Location filter retained other locations");
                choose("character/TOWNE/0/0");
                auto active = [&] {
                    return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
                };
                auto modes = active()->findChild<QTabWidget *>("characterModes");
                check(modes && modes->count() == 1 &&
                          active()->findChild<QWidget *>("conversationBrowser")->isHidden(),
                      "Character workspace duplicates collection navigation");
                check(active()
                          ->findChild<QLabel *>("characterProvenance")
                          ->text()
                          .contains("Shared dialogue"),
                      "Shared dialogue warning absent from editor");
                auto editor = dynamic_cast<ConversationEditor *>(active()->findChild<QWidget *>("conversationEditor"));
                auto source = editor->findChild<QPlainTextEdit *>("dialogueText");
                const QString baseline = source->toPlainText();
                source->setPlainText("<Unknown>");
                QTimer::singleShot(0, [&] {
                    auto dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                    check(dialog, "Invalid character draft did not prompt");
                    dialog->done(QMessageBox::Cancel);
                });
                choose("character/KEEP/0/0");
                check(modes->currentIndex() == 0 && source->toPlainText() == "<Unknown>",
                      "Invalid draft was lost while switching character mode");
                source->setPlainText(baseline);
                check(editor->flush(), "Cannot restore character draft");
                app.processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                const auto originalName = Dialogue::parse(U5::readDialogue(w.projectForTests().data("TOWNE.TLK"))[0].bytes).entries[0].bytes;
                editor->findChild<QPlainTextEdit *>("conversationBlockText")->setPlainText("Renamed Alice");
                check(editor->flush(), "Name edit did not commit");
                const auto renamedName = Dialogue::parse(U5::readDialogue(w.projectForTests().data("TOWNE.TLK"))[0].bytes).entries[0].bytes;
                check(renamedName == Dialogue::text("Renamed Alice") + originalName.mid(U5::encodeText("Alice").size()),
                      "Mode switch did not commit shared dialogue or preserved commands incorrectly");
                check(active()->findChild<QLabel *>("characterTitle")->text().startsWith("Renamed Alice"),
                      "Character heading did not follow dialogue name edit");
                check(!active()->findChild<QTableWidget *>("characterSchedule"),
                      "Schedule table still occupies the conversation page");
                check(modes->tabBar()->isHidden(), "Redundant conversation tab remains");
                tree->setMaximumHeight(85);
                app.processEvents();
                tree->verticalScrollBar()->setValue(tree->verticalScrollBar()->maximum());
                const int scroll = tree->verticalScrollBar()->value();
                check(scroll > 0, "Navigation regression did not exercise a scrolled list");
                editor->findChild<QPlainTextEdit *>("conversationBlockText")->setPlainText("Alice again");
                check(editor->flush(), "Second name edit failed");
                app.processEvents();
                check(tree->verticalScrollBar()->value() == scroll,
                      "Committing a draft scrolled the navigation tree under the pointer");
                tree->setMaximumHeight(QWIDGETSIZE_MAX);
                choose("character/KEEP/0/0");
                check(
                    active()->findChild<QLabel *>("characterProvenance")->text().contains("Ararat"),
                    "Cross-bank character navigation retained wrong location");
                for (auto action : w.findChildren<QAction *>())
                    if (action->text() == "Back") {
                        action->trigger();
                        break;
                    }
                app.processEvents();
                check(active()->findChild<QTabWidget *>("characterModes")->currentIndex() == 0,
                      "Back lost selected character mode");
                active()->findChild<QPushButton *>("showCharacterMap")->click();
                app.processEvents();
                auto canvas =
                    dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
                check(canvas && canvas->selectedNpc == 0 &&
                          active()->findChild<QTabBar *>("locationModes")->currentIndex() == 1,
                      "Character map link lost character selection");
                choose("conversation/TOWNE/2");
                check(active()->findChild<QTabWidget *>("characterModes")->count() == 1 &&
                          active()
                              ->findChild<QLabel *>("characterProvenance")
                              ->text()
                              .contains("special game logic"),
                      "Unassigned conversation inaccessible or mislabeled");
                QTreeWidgetItemIterator remaining(tree);
                while (*remaining) {
                    check((*remaining)->data(0, Qt::UserRole + 2).toString() !=
                              "character/TOWNE/1/2",
                          "NPC without dialogue remained in the dialogue tree");
                    ++remaining;
                }
                const QString screenshots = qEnvironmentVariable("IMPERA_WORKSHOP_SCREENSHOT_DIR");
                if (!screenshots.isEmpty()) {
                    choose("character/TOWNE/0/0");
                    QDir().mkpath(screenshots);
                    w.resize(1320, 860);
                    app.processEvents();
                    w.grab().save(screenshots + "/workshop-character-schedule.png");
                }
            });
        test("Engine table schema bounds, inheritance and package v3", [] {
            QByteArray bytes;
            bytes = Workshop::TableDocument::change(bytes, 1, 0, 321);
            bytes = Workshop::TableDocument::change(bytes, 4, 0, 17);
            bytes = Workshop::TableDocument::change(bytes, 7, 0, 111);
            bytes = Workshop::TableDocument::change(bytes, 8, 0, 222);
            check(Workshop::TableDocument::value(bytes, 1, 0) == 321 &&
                      Workshop::TableDocument::value(bytes, 1, 1) == U5_TableDefault1[1],
                  "Table override/default wrong");
            rejects([&] { Workshop::TableDocument::change(bytes, 4, 0, 48); });
            rejects([&] { Workshop::TableDocument::change(bytes, 4, 7, 2); });
            rejects([&] { Workshop::TableDocument::change(bytes, 5, 3, 25); });
            rejects([&] { Workshop::TableDocument::change(bytes, 5, 12, 25); });
            rejects([&] { Workshop::TableDocument::change(bytes, 6, 3, 25); });
            rejects([&] { Workshop::TableDocument::change(bytes, 7, 40, 25); });
            rejects([&] { Workshop::TableDocument::change(bytes, 2, 0, 256); });
            rejects([&] { Workshop::TableDocument::validate(bytes + "x"); });
            auto bad = bytes;
            bad[8] = char(255);
            rejects([&] { Workshop::TableDocument::validate(bad); });
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray ending(3698, 0);
            for (auto e : Workshop::narrativeEntries("END.DAT"))
                ending.replace(e->offset, 6, "Hello", 6);
            file(dir.path() + "/END.DAT", ending);
            p.openGame(dir.path());
            p.resources["RULES.TBL"].edited = bytes;
            QByteArray narrative("ITXT0001", 8);
            auto append32 = [&](unsigned n) {
                for (int i = 0; i < 4; ++i) narrative.append(char(n >> (8 * i)));
            };
            QByteArray id("ending/page/1"), text("Test ending");
            append32(1);
            append32(id.size()); narrative += id;
            append32(text.size() + 1); narrative += text; narrative.append(char(0));
            p.resources["TEXT.OVR"].edited = narrative;
            auto package = p.package();
            check(package.startsWith("IMOD0003"), "Table package is not v3");
            Project copy;
            copy.openGame(dir.path());
            copy.importPackage(package);
            check(copy.data("RULES.TBL") == bytes, "Table package failed roundtrip");
            check(copy.data("TEXT.OVR") == narrative, "Version 3 lost narrative overrides");
            copy.save(dir.path() + "/tables.imperaproject");
            Project saved;
            saved.load(dir.path() + "/tables.imperaproject");
            check(saved.data("RULES.TBL") == bytes, "Table project failed roundtrip");
            bytes = Workshop::TableDocument::change({}, 1, 0, 321);
            check(Workshop::TableDocument::change(bytes, 1, 0, 321, true).isEmpty(),
                  "Last override was not removed");
            package.replace(0, 8, "IMOD0002");
            rejects([&] { copy.importPackage(package); });
        });
        test("Table workspace, inventory filtering and undo", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Table fixture open");
            w.selectResource("RULES.TBL");
            auto table = w.findChild<QTableWidget *>("engineTableEntries");
            check(table && table->rowCount() == 48, "Equipment table workspace missing");
            check(!w.findChild<QTreeWidget *>("overlayCoverage") &&
                      !w.findChild<QLineEdit *>("overlayInventorySearch"),
                  "Developer overlay inventory is still exposed");
            w.findChild<QSpinBox *>("engineTableValue")->setValue(345);
            w.findChild<QPushButton *>("applyEngineTable")->click();
            check(Workshop::TableDocument::value(w.projectForTests().data("RULES.TBL"), 1, 0) ==
                      345,
                  "Table UI edit failed");
            for (auto a : w.findChildren<QAction *>())
                if (a->text().startsWith("Undo ")) {
                    a->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            check(w.projectForTests().data("RULES.TBL").isEmpty(),
                  "Table undo did not restore inheritance");
            auto selector = w.findChild<QComboBox *>("engineTableSelector");
            selector->setCurrentIndex(3);
            check(w.findChild<QComboBox *>("engineStockItem")->isEnabled(),
                  "Stock references unavailable");
        });
        test("Verified table spans match original DOS overlay (optional)", [] {
            if (!qEnvironmentVariableIsSet("U5_GAME_DIR"))
                return;
            Project p;
            p.openGame(qEnvironmentVariable("U5_GAME_DIR"));
            auto overlay = p.data("DATA.OVL");
            for (auto &spec : U5_EngineTables) {
                bool words = spec.id == 1 || spec.id == 5 || spec.id == 6;
                for (unsigned i = 0; i < spec.rows * spec.columns; ++i) {
                    unsigned value = words ? U5::word(overlay, spec.address + 16 + i * 2)
                                           : U5::byte(overlay, spec.address + 16 + i);
                    check(value == spec.defaults[i],
                          "Audited table span does not match DOS edition");
                }
            }
        });
        test("World objects use fresh-game precedence and preserve properties", [] {
            QTemporaryDir dir;auto p=fixture(dir.path());
            QByteArray seed(256,0);seed[8]=seed[9]=41;seed[10]=14;seed[11]=char(242);seed[12]=char(255);
            p.resources["INIT.OOL"]={seed,seed};
            QByteArray used(256,char(77));
            p.resources["BRIT.OOL"]={used,used};p.resources["UNDER.OOL"]={used,used};
            check(Workshop::WorldObjects::effective(p,0)==QByteArray(256,0),"Used Britannia seeded new game");
            check(Workshop::WorldObjects::effective(p,1)==seed,"Underworld did not inherit INIT.OOL");
            auto r=seed.mid(8,8);r[2]=55;r[6]=char(203);
            auto b=Workshop::WorldObjects::change(p,1,1,r);
            check(b.mid(8,8)==r && b.mid(16)==seed.mid(16),"Object move lost properties");
            rejects([&]{Workshop::WorldObjects::change(p,1,0,r);});
            r[4]=0;rejects([&]{Workshop::WorldObjects::change(p,1,1,r);});
        });
        test("World object inclusion survives project/package persistence and undo", [&] {
            QTemporaryDir dir;auto p=fixture(dir.path());QByteArray empty(256,0);
            for(QString name:{"INIT.OOL","BRIT.OOL","UNDER.OOL"})file(dir.path()+"/"+name,empty);
            WorkshopWindow w;check(w.openGame(dir.path()),"Object fixture open");w.selectResource("BRIT.OOL");
            w.findChild<QPushButton *>("addWorldObject")->click();
            check(w.findChild<QListWidget *>("worldObjectList")->count()==1,"Create object failed");
            check(uchar(w.projectForTests().data("BRIT.OOL")[8])==36 &&
                  uchar(w.projectForTests().data("BRIT.OOL")[13])==99,"Ship defaults wrong");
            w.findChild<QSpinBox *>("worldObjectX")->setValue(101);
            check(uchar(w.projectForTests().data("BRIT.OOL")[10])==101,"Object placement failed");
            auto package=w.projectForTests().package();Project imported;imported.openGame(dir.path());imported.importPackage(package);
            check(Workshop::WorldObjects::effective(imported,0)==w.projectForTests().data("BRIT.OOL"),"Exported world objects lost");
            w.findChild<QPushButton *>("removeWorldObject")->click();
            // Clearing the shape preserves other fields. Explicit inclusion also supports a byte-identical empty override.
            auto &model=w.projectForTests();model.resources["BRIT.OOL"].edited=empty;
            check(model.changed().contains("BRIT.OOL"),"Empty explicit override disappeared");
            model.save(dir.path()+"/objects.imperaproject");Project saved;saved.load(dir.path()+"/objects.imperaproject");
            check(saved.resources["BRIT.OOL"].included && saved.changed().contains("BRIT.OOL"),"Saved inclusion disappeared");
            Project roundtrip;roundtrip.openGame(dir.path());roundtrip.importPackage(saved.package());
            check(roundtrip.resources["BRIT.OOL"].included,"Zero-span package lost explicit inclusion");
            w.findChild<QPushButton *>("revertWorldObjects")->click();
            check(!model.changed().contains("BRIT.OOL"),"Inherited revert failed");
            for(auto a:w.findChildren<QAction *>())if(a->text().startsWith("Undo ")){a->trigger();break;}
            app.processEvents();app.processEvents();
            check(model.resources["BRIT.OOL"].included,"Undo did not restore inclusion");
        });
        test("Bitmap fonts preserve every slot and verified row bit order", [] {
            for (QString name : {"IBM.CH", "RUNES.CH", "IBM.HCS", "RUNES.HCS"}) {
                int stride = Workshop::FontDocument::stride(name);
                QByteArray b(128 * stride, 0);
                b[65 * stride] = char(0x80);
                b[65 * stride + stride - 1] = 1;
                auto image = Workshop::FontDocument::glyph(name, b, 65);
                check(qGray(image.pixel(0,0)) == 255 && qGray(image.pixel(1,0)) == 0 &&
                      qGray(image.pixel(image.width()-1,image.height()-1)) == 255,
                      "Font bit order or row stride wrong");
                check(Workshop::FontDocument::replace(name,b,65,image) == b,"Font roundtrip");
                image.setPixel(1,0,qRgb(255,255,255));
                auto edited = Workshop::FontDocument::replace(name,b,65,image);
                b[65 * stride] = char(0xc0);
                check(edited == b,"Font edit altered unrelated slots");
                rejects([&]{Workshop::FontDocument::validate(name,b.left(b.size()-1));});
                rejects([&]{Workshop::FontDocument::glyph(name,b,128);});
            }
        });
        test("Font workspace copy, paste, preview and undo", [&] {
            QTemporaryDir dir;
            auto p=fixture(dir.path());
            QByteArray font(1024,0);font[65*8]=char(0x80);
            file(dir.path()+"/IBM.CH",font);file(dir.path()+"/RUNES.CH",font);
            WorkshopWindow w;check(w.openGame(dir.path()),"Font fixture open");w.selectResource("IBM.CH");
            auto gallery=w.findChild<QListWidget *>("glyphGallery");
            check(gallery && gallery->count()==128,"Font gallery slots missing");
            gallery->setCurrentRow(65);w.findChild<QPushButton *>("copyGlyph")->click();
            gallery->setCurrentRow(66);w.findChild<QPushButton *>("pasteGlyph")->click();
            check(uchar(w.projectForTests().data("IBM.CH")[66*8])==0x80,"Glyph paste failed");
            for(auto a:w.findChildren<QAction *>())if(a->text().startsWith("Undo ")){a->trigger();break;}
            app.processEvents();app.processEvents();
            check(w.projectForTests().data("IBM.CH")==font,"Glyph undo failed");
            check(w.findChild<QListWidget *>("glyphGallery")->currentRow()==66,"Glyph selection lost");
        });
        test("Original fonts roundtrip (optional)", [] {
            if(!qEnvironmentVariableIsSet("U5_GAME_DIR"))return;
            Project p;p.openGame(qEnvironmentVariable("U5_GAME_DIR"));
            for(QString name:{"IBM.CH","RUNES.CH","IBM.HCS","RUNES.HCS"}) {
                auto b=p.data(name);
                for(int i=0;i<128;++i)check(Workshop::FontDocument::replace(name,b,i,
                    Workshop::FontDocument::glyph(name,b,i))==b,"Original font roundtrip failed");
            }
        });
        test("Scene maps preserve padding, unrelated scenes and creation trailer", [] {
            QByteArray b(1216, char(213));
            b += QByteArray::fromHex("06000c00002005010e020d00010f030109");
            b += "preserved trailer";
            Workshop::SceneDocument::validate(b);
            for (int scene = 0; scene < 5; ++scene)
                for (int stage = 0; stage < (scene == 4 ? 4 : 1); ++stage) {
                    auto cells = Workshop::SceneDocument::map(b, scene, stage);
                    cells[0] = 9;
                    cells[cells.size() - 1] = 7;
                    auto changed = Workshop::SceneDocument::changeMap(b, scene, stage, cells);
                    check(Workshop::SceneDocument::map(changed, scene, stage) == cells,
                          "Scene map edit failed");
                    int delta = 0;
                    for (int i = 0; i < b.size(); ++i)
                        delta += b[i] != changed[i];
                    check(delta == 2, "Scene edit changed padding or unrelated bytes");
                }
            auto steps = Workshop::SceneDocument::steps(b);
            QVector<QByteArray> bytes;
            for (auto step : steps)
                bytes.append(step.bytes);
            check(Workshop::SceneDocument::changeSteps(b, bytes) == b,
                  "Sequence roundtrip changed bytes");
            QVector<MapCanvas::Actor> actors;
            int stage;
            Workshop::SceneDocument::preview(b, 6, &actors, &stage);
            check(stage == 0 && actors.size() == 1 && actors[0].x == 7 && actors[0].y == 1,
                  "Repeat preview differs from engine movement");
            bytes.insert(bytes.size() - 1, QByteArray::fromHex("0302"));
            auto longer = Workshop::SceneDocument::changeSteps(b, bytes);
            check(longer.endsWith("preserved trailer") && longer.left(1216) == b.left(1216),
                  "Structural edit lost original bytes");
            bytes[2] = QByteArray::fromHex("0000201201");
            rejects([&] { Workshop::SceneDocument::changeSteps(b, bytes); });
            auto unknown = b;
            unknown.insert(1216, char(33));
            Workshop::SceneDocument::validate(unknown);
            rejects([&] { Workshop::SceneDocument::changeSteps(unknown, bytes); });
            for (auto bad : {QByteArray::fromHex("030009"), QByteArray::fromHex("0f09"),
                             QByteArray::fromHex("0e0209"), QByteArray::fromHex("000020130009"),
                             QByteArray::fromHex("02000109"), QByteArray::fromHex("060409")})
                rejects([&] {
                    Workshop::SceneDocument::validate(b.left(1216) + QByteArray::fromHex("0600") +
                                                      bad);
                });
            auto effects =
                b.left(1216) + QByteArray::fromHex("06000c0000200501040702050a0c0803010009");
            Workshop::SceneDocument::validate(effects);
            check(uchar(Workshop::SceneDocument::preview(effects, 3)[2 * 19 + 7]) ==
                      TILE_MAP_MOONGATE,
                  "Moongate opening preview failed");
            check(uchar(Workshop::SceneDocument::preview(effects, 4)[2 * 19 + 7]) == TILE_MAP_GRASS,
                  "Moongate closing preview failed");
            check(uchar(Workshop::SceneDocument::preview(effects, 5)[3 * 19 + 8]) == 12,
                  "Terrain operation preview failed");
            check(Workshop::SceneDocument::affectedCell(effects, 6) == QPoint(5, 1),
                  "Removed actor navigation lost its coordinates");
            MapCanvas canvas;
            canvas.side = 19;
            canvas.rows = 4;
            canvas.zoom = 1;
            canvas.resizeMap();
            check(canvas.height() == 64 && canvas.cellAt({303, 63}) == QPoint(18, 3) &&
                      canvas.cellAt({10, 64}) == QPoint(-1, -1),
                  "Rectangular canvas clipping failed");
        });
        test("Original special scenes and message entry points (optional)", [] {
            if (!qEnvironmentVariableIsSet("U5_GAME_DIR"))
                return;
            Project p;
            p.openGame(qEnvironmentVariable("U5_GAME_DIR"));
            auto b = p.data("MISCMAPS.DAT");
            Workshop::SceneDocument::validate(b);
            auto steps = Workshop::SceneDocument::steps(b);
            for (int i = 0; i < steps.size(); ++i)
                Workshop::SceneDocument::preview(b, i);
            check(Workshop::narrativeEntries("MISCMSG.DAT").size() == 47 &&
                      Workshop::narrativeEntries("ENDMSG.DAT").size() == 11,
                  "Missing scene message entry points");
            for (auto name : {"MISCMSG.DAT", "ENDMSG.DAT"}) {
                Workshop::validateNarrative(p, name);
                for (auto e : Workshop::narrativeEntries(name))
                    check(Workshop::changeNarrative(
                              p, *e,
                              Workshop::narrativeSource(Workshop::narrativeText(p, *e), false),
                              false)[name] == p.data(name),
                          "Scene text no-op changed original bytes");
            }
        });
        test("Native scene workspace, sequence editing and undo", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray b(1216, 0);
            b += QByteArray::fromHex("06000c0000200501030109");
            file(dir.path() + "/MISCMAPS.DAT", b);
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Open scene fixture failed");
            w.selectResource("MISCMAPS.DAT");
            auto scenes = w.findChild<QComboBox *>("sceneSelector");
            check(scenes, "Missing scene selector");
            scenes->setCurrentIndex(4);
            w.findChild<QTabWidget *>("sceneTabs")->setCurrentIndex(2);
            auto list = w.findChild<QListWidget *>("creationSteps");
            list->setCurrentRow(3);
            auto apply = w.findChild<QPushButton *>("applyCreationStep");
            auto fields = apply->parentWidget()->findChildren<QSpinBox *>();
            check(fields.size() == 4, "Missing sequence operands");
            fields[0]->setValue(2);
            apply->click();
            check(w.projectForTests().data("MISCMAPS.DAT").endsWith(QByteArray::fromHex("030209")),
                  "Native sequence edit failed");
            for (auto a : w.findChildren<QAction *>())
                if (a->text().startsWith("Undo ")) {
                    a->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            check(w.projectForTests().data("MISCMAPS.DAT") == b, "Scene undo failed");
            check(w.findChild<QTabWidget *>("sceneTabs")->currentIndex()==2,"Undo lost the active scene tab");
            list = w.findChild<QListWidget *>("creationSteps");
            list->setCurrentRow(3);
            apply = w.findChild<QPushButton *>("applyCreationStep");
            fields = apply->parentWidget()->findChildren<QSpinBox *>();
            fields[0]->setValue(3);
            list->setCurrentRow(4);
            check(w.projectForTests().data("MISCMAPS.DAT").endsWith(QByteArray::fromHex("030309")),
                  "Step navigation lost a parameter draft");
            auto canvas = dynamic_cast<MapCanvas *>(w.findChild<QWidget *>("sceneMap"));
            check(canvas, "Missing scene map canvas");
            auto cells = canvas->ids;
            cells[0] = 5;
            check(canvas->commit(cells), "Native scene map commit failed");
            auto package = w.projectForTests().package();
            Project restored;
            restored.openGame(dir.path());
            restored.importPackage(package);
            check(restored.data("MISCMAPS.DAT") == w.projectForTests().data("MISCMAPS.DAT"),
                  "Scene package roundtrip failed");
        });
        test("Scene messages, scoped search and navigation drafts", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            QByteArray b(1216, 0);
            b += QByteArray::fromHex("06000c030109");
            file(dir.path() + "/MISCMAPS.DAT", b);
            file(dir.path() + "/MISCMSG.DAT", QByteArray(2745, 0));
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Open scene message fixture failed");
            w.selectResource("MISCMAPS.DAT");
            auto scenes = w.findChild<QComboBox *>("sceneSelector");
            auto entries = w.findChild<QListWidget *>("narrativeEntries");
            auto text = w.findChild<QPlainTextEdit *>("narrativeText");
            text->setPlainText("New interrogation");
            text->document()->setModified(true);
            scenes->setCurrentIndex(2);
            check(w.projectForTests().data("MISCMSG.DAT").startsWith("New interrogation"),
                  "Scene navigation lost message draft");
            entries = w.findChild<QListWidget *>("narrativeEntries");
            for (int i = 0; i < entries->count(); ++i)
                if (!entries->item(i)->isHidden())
                    check(entries->item(i)->text().startsWith("Codex"),
                          "Scene exposes unrelated messages");
            auto filter = entries->parentWidget()->parentWidget()->findChild<QLineEdit *>();
            check(filter, "Missing scene message search");
            filter->setText("Blackthorn");
            filter->clear();
            for (int i = 0; i < entries->count(); ++i)
                if (!entries->item(i)->isHidden())
                    check(entries->item(i)->text().startsWith("Codex"),
                          "Search bypassed scene scope");
        });
        test("Narrative fixed entries, budgets and original byte preservation", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray bytes(7746, 0);
            for (auto e : Workshop::narrativeEntries("QUESTION.DAT"))
                bytes.replace(e->offset, 6, "Hello", 6);
            p.resources["QUESTION.DAT"] = {bytes, bytes};
            auto entries = Workshop::narrativeEntries("QUESTION.DAT");
            check(entries.size() == 30, "Expected intro, completion and 28 virtue pairs");
            auto changes = Workshop::changeNarrative(p, *entries[2], "A new question", false);
            auto changed = changes["QUESTION.DAT"];
            check(changed.size() == bytes.size(), "Fixed text changed resource length");
            check(changed.left(entries[2]->offset) == bytes.left(entries[2]->offset),
                  "Unrelated prefix changed");
            check(changed.mid(entries[2]->offset + 15) == bytes.mid(entries[2]->offset + 15),
                  "Unrelated suffix changed");
            rejects([&] { Workshop::changeNarrative(p, *entries[2], QString(2100, 'a'), false); });
            check(Workshop::narrativeEntries("END.DAT").size() == 6, "Six ending pages missing");
            check(Workshop::narrativeEntries("KARMA.DAT").size() == 6,
                  "Distinct karma speeches missing");
            check(Workshop::narrativeEntries("SHOPPE.DAT").size() == 194,
                  "Shop subtables incomplete");
        });
        test("Shop workspace navigation, insertions, extended text and undo", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/SHOPPE.DAT", QByteArray(12000, 0));
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Shop fixture open");
            window.selectResource("SHOPPE.DAT");
            auto editor = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            auto text = editor->findChild<QTextEdit *>("shopDialogueText");
            auto responses = editor->findChild<QTreeWidget *>("shopResponses");
            check(text && responses && !editor->findChild<QWidget *>("textPreviewPanel") &&
                      !editor->findChild<QCheckBox *>("narrativeExtended") &&
                      !editor->findChild<QPushButton *>("narrativeApply"),
                  "Shop editor still exposes preview or format controls");
            check(window.findChild<QComboBox *>("contentWorkspace")->currentText() == "Dialogue",
                  "Shop dialogue is not in NPCs");
            auto serviceFilter = window.findChild<QComboBox *>("characterLocationFilter");
            check(serviceFilter && !serviceFilter->isHidden() &&
                      serviceFilter->findData("Shops & Services") >= 0,
                  "Shop dialogue filter choice is missing");
            serviceFilter->setCurrentIndex(serviceFilter->findData("Shops & Services"));
            auto library =
                window.findChild<QWidget *>("resourceLibrary")->findChild<QTreeWidget *>();
            int visibleGroups = 0;
            for (int i = 0; i < library->topLevelItemCount(); ++i)
                if (!library->topLevelItem(i)->isHidden()) {
                    ++visibleGroups;
                    check(library->topLevelItem(i)->text(0).startsWith("Shops & Services"),
                          "Services filter includes location NPCs");
                }
            check(visibleGroups == 1, "Services filter hid its service entries");
            auto insert = editor->findChild<QPushButton *>("shopInsertValue0");
            insert->click();
            text->insertPlainText(QString(600, 'a'));
            text->selectAll();
            text->copy();
            text->moveCursor(QTextCursor::End);
            text->paste();
            check(Workshop::shopTextBytes(*text->document()).count('#') == 2,
                  "Copy/paste lost inline substitution identity");
            QEventLoop loop;
            QTimer::singleShot(650, &loop, &QEventLoop::quit);
            loop.exec();
            check(!window.projectForTests().data("TEXT.OVR").isEmpty(),
                  "Long shop response did not use automatic extended storage");
            auto encoded = Workshop::shopTextBytes(*text->document());
            check(encoded.startsWith('#'), "Inline shop-name value lost runtime identity");
            const auto routes = Workshop::contentDestinations(window.projectForTests());
            const auto markers = Workshop::contentChanges(window.projectForTests(), routes);
            int changedServices = 0;
            for (const auto &route : routes)
                if (route.resource == "SHOPPE.DAT" && !markers.value(route.id).isEmpty())
                    ++changedServices;
            check(changedServices == 1, "Shop edit marked unrelated services as modified");
            auto package = window.projectForTests().package();
            Project restored = window.projectForTests();
            for (auto it = restored.resources.begin(); it != restored.resources.end(); ++it)
                it->edited = it->original;
            restored.importPackage(package);
            check(restored.data("TEXT.OVR") == window.projectForTests().data("TEXT.OVR"),
                  "Shop extended response package did not roundtrip");
            for (auto action : window.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            check(window.projectForTests().data("TEXT.OVR").isEmpty(), "Shop undo failed");
        });
        test("Shop tokens roundtrip and bounded runtime substitution", [] {
            QByteArray raw("# $ @ % ^ & * ");
            raw += char(128);
            raw += " sword";
            QTextEdit human;
            Workshop::loadShopText(&human, raw);
            check(Workshop::shopTextBytes(*human.document()) == raw,
                  "Readable shop editor changed encoded words or substitution identity");
            auto cursor = human.textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertText("!");
            check(Workshop::shopTextBytes(*human.document()) == raw + "!",
                  "Shop text edit lost unrelated compressed words");
            auto source = Workshop::narrativeSource(raw, true);
            check(Workshop::encodeNarrative(source, true) == raw, "Shop token roundtrip failed");
            const char *values[7] = {"shop", "keeper", "morning", "25", "2", "sword", "Avatar"};
            char out[256];
            check(U5_ExpandShopText(reinterpret_cast<const unsigned char *>(raw.constData()), out,
                                    sizeof(out), values),
                  "Substitution failed");
            check(QByteArray(out) == "shop keeper morning 25 2 sword Avatar  the  sword",
                  "Substitution spacing differs from original engine");
            char small[5];
            check(!U5_ExpandShopText(reinterpret_cast<const unsigned char *>(raw.constData()),
                                     small, sizeof(small), values),
                  "Overflow was not rejected");
            rejects([] { Workshop::encodeNarrative("[unknown]", true); });
            rejects([] { Workshop::encodeNarrative("[word:87]", true); });
            rejects([] { Workshop::encodeNarrative(QString(QChar(0x100)), false); });
        });
        test("Extended narrative packages, project persistence and schema validation", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray bytes(3698, 0);
            for (auto e : Workshop::narrativeEntries("END.DAT"))
                bytes.replace(e->offset, 6, "Hello", 6);
            file(dir.path() + "/END.DAT", bytes);
            p.openGame(dir.path());
            auto entry = Workshop::narrativeEntries("END.DAT")[0];
            auto changes = Workshop::changeNarrative(p, *entry, QString(600, 'a'), true);
            for (auto it = changes.begin(); it != changes.end(); ++it)
                p.resources[it.key()].edited = it.value();
            p.validate();
            check(p.data("END.DAT") == bytes, "Extended text moved fixed offsets");
            check(Workshop::narrativeText(p, *entry) == QByteArray(600, 'a'),
                  "Extended lookup failed");
            auto package = p.package();
            check(package.startsWith("IMOD0002"), "Missing extended package compatibility version");
            auto q = fixture(dir.path());
            q.openGame(dir.path());
            q.importPackage(package);
            check(Workshop::narrativeText(q, *entry) == QByteArray(600, 'a'),
                  "Imported extended text lost");
            p.save(dir.path() + "/narrative.imperaproject");
            Project loaded;
            loaded.load(dir.path() + "/narrative.imperaproject");
            check(loaded.data("TEXT.OVR") == p.data("TEXT.OVR"), "Saved companion lost");
            auto session = prepareModTest(p, dir.path() + "/sessions");
            check(QFile::exists(session + "/Mods/workshop.imperamod"),
                  "Isolated narrative test package missing");
            auto diagnostics = validateMod(p);
            for (auto d : diagnostics)
                check(d.severity != ModDiagnostic::Error, "Extended package validation failed");
            auto corrupt = p;
            corrupt.resources["TEXT.OVR"].edited[0] = 'X';
            rejects([&] { corrupt.validate(); });
            auto fixed = Workshop::changeNarrative(p, *entry, "Short", false);
            for (auto it = fixed.begin(); it != fixed.end(); ++it)
                p.resources[it.key()].edited = it.value();
            check(Workshop::narrativeText(p, *entry) == "Short",
                  "Removing override did not restore DAT precedence");
        });
        test("Narrative native UI, selection and undo", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray bytes(7746, 0);
            for (auto e : Workshop::narrativeEntries("QUESTION.DAT"))
                bytes.replace(e->offset, 6, "Hello", 6);
            file(dir.path() + "/QUESTION.DAT", bytes);
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Open narrative game failed");
            w.selectResource("QUESTION.DAT");
            auto list = w.findChild<QListWidget *>("narrativeEntries");
            auto text = w.findChild<QPlainTextEdit *>("narrativeText");
            auto apply = w.findChild<QPushButton *>("applyNarrative");
            check(list && text && apply, "Narrative controls missing");
            list->setCurrentRow(2);
            text->selectAll();
            text->insertPlainText("Changed question");
            apply->click();
            check(w.projectForTests().data("QUESTION.DAT") != bytes,
                  "Native narrative edit failed");
            for (auto a : w.findChildren<QAction *>())
                if (a->text().startsWith("Undo ")) {
                    a->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            check(w.projectForTests().data("QUESTION.DAT") == bytes, "Narrative undo failed");
        });
        test("Original narrative entries and token schemas (optional)", [] {
            if (!qEnvironmentVariableIsSet("U5_GAME_DIR"))
                return;
            Project p;
            p.openGame(qEnvironmentVariable("U5_GAME_DIR"));
            for (auto name : QStringList{"QUESTION.DAT", "END.DAT", "KARMA.DAT", "SHOPPE.DAT"}) {
                Workshop::validateNarrative(p, name);
                for (auto entry : Workshop::narrativeEntries(name)) {
                    auto raw = Workshop::narrativeText(p, *entry);
                    bool shop = name == "SHOPPE.DAT";
                    if (shop) {
                        QTextEdit human;
                        Workshop::loadShopText(&human, raw);
                        check(Workshop::shopTextBytes(*human.document()) == raw,
                              "Original shop response failed readable-document byte roundtrip");
                    }
                    check(Workshop::encodeNarrative(Workshop::narrativeSource(raw, shop), shop) ==
                              raw,
                          "Original narrative token no-op failed");
                    check(Workshop::changeNarrative(p, *entry, Workshop::narrativeSource(raw, shop),
                                                    false)[name] == p.data(name),
                          "Original fixed text no-op altered resource bytes");
                }
            }
        });
        test("Sign codec preserves glyphs, locations and unknown trailer", [] {
            QByteArray seed(66, 0);
            seed += char(255);
            seed += char(255);
            seed += "opaque trailer";
            QVector<Workshop::SignRecord> records = {
                {4, 0, 3, 4, Workshop::encodeSign("Normal [runes]LOVE[/runes]\n[byte:29]")},
                {17, -1, 6, 13, QByteArray("Runes")}};
            auto bytes = Workshop::writeSigns(seed, records);
            check(Workshop::readSigns(bytes) == records, "Sign records changed");
            check(Workshop::writeSigns(bytes, Workshop::readSigns(bytes)) == bytes,
                  "No-op signs changed");
            check(bytes.endsWith("opaque trailer"), "Unknown signs trailer lost");
            const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData() + 66);
            unsigned length = bytes.size() - 66;
            check(WorkshopEngineFindSign(data, length, 4, 0, 3, 4) == 0,
                  "Engine did not find authored sign");
            check(WorkshopEngineFindSign(data, length, 4, 255, 6, 13) == -1,
                  "Engine crossed into another map");
            check(WorkshopEngineFindSign(data, 4, 4, 0, 3, 4) == -1,
                  "Engine accepted truncated header");
            unsigned char broken[] = {4, 0, 3, 4, 'x'};
            check(WorkshopEngineFindSign(broken, 5, 4, 0, 3, 4) == -1,
                  "Engine accepted unterminated text");
            for (const auto &r : records)
                check(Workshop::encodeSign(Workshop::signSource(r.text)) == r.text,
                      "Sign charset roundtrip");
            auto bad = bytes;
            bad[8] = char(1);
            rejects([&] { Workshop::readSigns(bad); });
            rejects([&] { Workshop::readSigns(QByteArray(65, 0)); });
            rejects([&] { Workshop::encodeSign("[runes]unterminated"); });
            rejects([&] { Workshop::writeSigns(seed, {{4, 0, 17, 21, "ignored"}}); });
            rejects([&] { Workshop::writeSigns(seed, {{33, 0, 1, 1, "invalid map"}}); });
            rejects([&] { Workshop::writeSigns(seed, {{1, 0, -1, 1, "invalid cell"}}); });
            rejects([&] { Workshop::writeSigns(seed, {{1, 0, 1, 1, QByteArray(3000, 'A')}}); });
        });
        test("Description pointer aliases detach unless explicitly linked", [] {
            QByteArray bytes(1024, 0);
            for (int i = 0; i < 512; i++)
                bytes[i * 2 + 1] = 4;
            bytes += "shared";
            bytes += '\0';
            auto changed = Workshop::changeDescription(bytes, 7, "Independent", false);
            check(Workshop::description(changed, 7) == "Independent", "Description not changed");
            check(Workshop::description(changed, 8) == "shared",
                  "Shared description unexpectedly changed");
            check(changed.left(14) == bytes.left(14), "Unrelated description pointers changed");
            check(Workshop::changeDescription(bytes, 7, "shared", false) == bytes,
                  "No-op descriptions changed");
            auto linked = Workshop::changeDescription(bytes, 7, "All linked", true);
            for (int i = 0; i < 512; i++)
                check(Workshop::description(linked, i) == "All linked", "Linked text not updated");
            rejects([&] { Workshop::changeDescription(bytes, 7, QString(128, 'a'), false); });
            rejects([&] { Workshop::description(QByteArray(1024, 0), 0); });
            rejects([&] { Workshop::description(bytes.left(bytes.size() - 1), 0); });
        });
        test("Location text project export, undo and floor navigation", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, char(0xf8)));
            file(dir.path() + "/IBM.CH", QByteArray(1024, char(255)));
            file(dir.path() + "/RUNES.CH", QByteArray(1024, char(255)));
            QByteArray seed(66, 0);
            seed += char(255);
            seed += char(255);
            auto signs =
                Workshop::writeSigns(seed, {{1, 0, 2, 3, Workshop::encodeSign("Original")}});
            file(dir.path() + "/SIGNS.DAT", signs);
            QByteArray looks(1024, 0);
            for (int i = 0; i < 512; i++)
                looks[i * 2 + 1] = 4;
            looks += "a sign";
            looks += '\0';
            file(dir.path() + "/LOOK2.DAT", looks);
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Text fixture load failed");
            w.selectResource("SIGNS.DAT");
            app.processEvents();
            auto floor = w.findChild<QComboBox *>("signFloor");
            check(floor && floor->count() == 2, "Sign floors include other locations");
            auto draft = w.findChild<QPlainTextEdit *>("locationTextDraft");
            draft->setPlainText("Changed [runes]LOVE[/runes]");
            for (auto button : w.findChildren<QPushButton *>())
                if (button->text() == "Apply text")
                    button->click();
            auto changed = w.projectForTests().data("SIGNS.DAT");
            check(changed != signs, "Synthetic sign edit failed");
            w.projectForTests().save(dir.path() + "/text.imperaproject");
            Project reopened;
            reopened.load(dir.path() + "/text.imperaproject");
            check(reopened.data("SIGNS.DAT") == changed, "Text project did not reopen");
            auto package = reopened.package();
            Project imported;
            imported.openGame(dir.path());
            imported.importPackage(package);
            check(imported.data("SIGNS.DAT") == changed, "Text package did not import");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Undo"))
                    action->trigger();
            app.processEvents();
            check(w.projectForTests().data("SIGNS.DAT") == signs, "Sign undo failed");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            w.selectResource("LOOK2.DAT");
            app.processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            draft = w.findChild<QPlainTextEdit *>("locationTextDraft");
            check(draft->toPlainText()=="a sign","Initial description was not displayed");
            draft->selectAll();
            draft->insertPlainText("Unsaved draft");
            w.selectResource("SIGNS.DAT");
            check(Workshop::description(w.projectForTests().data("LOOK2.DAT"), 0) ==
                      "Unsaved draft",
                  "Resource navigation lost description draft");
            auto bad =
                Workshop::writeSigns(seed, {{1, 3, 2, 3, Workshop::encodeSign("Missing floor")}});
            imported.resources["SIGNS.DAT"].edited = bad;
            rejects([&] { imported.validate(); });
        });
        test("Map selection opens NPC and sign inspectors without location tabs", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            const QByteArray terrain(16384, char(8));
            file(dir.path() + "/TOWNE.DAT", terrain);
            QByteArray npcs(4608, 0);
            npcs[19] = 2;
            npcs[22] = 3;
            npcs[513] = 25;
            file(dir.path() + "/TOWNE.NPC", npcs);
            QByteArray seed(66, 0); seed += QByteArray::fromHex("ffff");
            file(dir.path() + "/SIGNS.DAT", Workshop::writeSigns(seed,
                 {{1, 0, 5, 6, Workshop::encodeSign("A sign")}}));
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Selection fixture failed");
            w.show(); w.selectResource("TOWNE.DAT"); app.processEvents();
            auto root = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            auto canvas = dynamic_cast<MapCanvas *>(root->findChild<QWidget *>("mapCanvas"));
            auto modes = root->findChild<QTabBar *>("locationModes");
            check(modes->isHidden() && !w.statusBar()->isVisible(), "Unused tab or status rows remain visible");
            root->findChild<QButtonGroup *>("mapTools")->button(MapCanvas::InspectNpc)->click();
            auto press = [&](QPoint cell) {
                QPointF pixel((cell.x() + .5) * 16 * canvas->zoom, (cell.y() + .5) * 16 * canvas->zoom);
                QMouseEvent event(QEvent::MouseButtonPress, pixel, pixel, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &event);
            };
            auto release = [&](QPoint cell) {
                QPointF pixel((cell.x() + .5) * 16 * canvas->zoom, (cell.y() + .5) * 16 * canvas->zoom);
                QMouseEvent move(QEvent::MouseMove, pixel, pixel, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &move);
                QMouseEvent event(QEvent::MouseButtonRelease, pixel, pixel, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &event);
                app.processEvents();
            };
            press({2, 3}); release({3, 4});
            check(canvas->selectedNpc == 1 &&
                      U5::byte(w.projectForTests().data("TOWNE.NPC"), 19) == 3,
                  "Unified selector did not select and move an NPC");
            press({5, 6}); release({7, 8});
            auto signs = Workshop::readSigns(w.projectForTests().data("SIGNS.DAT"));
            check(modes->tabText(modes->currentIndex()) == "Signs" && signs[0].x == 7 && signs[0].y == 8,
                  "Clicking and dragging a sign did not open its inspector and move its record");
            check(!canvas->contextActors.isEmpty(), "NPCs disappeared while inspecting a sign");
            auto draft = root->findChild<QPlainTextEdit *>("locationTextDraft");
            draft->selectAll(); draft->insertPlainText("Edited sign");
            press({3, 4}); release({3, 4});
            check(modes->tabText(modes->currentIndex()) == "NPCs / Schedules" &&
                      canvas->selectedNpc == 1,
                  "Selecting an NPC from the sign inspector did not switch inspectors");
            check(Workshop::signSource(Workshop::readSigns(w.projectForTests().data("SIGNS.DAT"))[0].text) == "Edited sign",
                  "Selecting an NPC discarded a pending sign draft");
            root->findChild<QButtonGroup *>("mapTools")->button(MapCanvas::Pencil)->click();
            check(canvas->terrainEditingEnabled && modes->currentIndex() == 0 &&
                      w.projectForTests().data("TOWNE.DAT") == terrain,
                  "Selection changed terrain or painting did not restore its palette");
        });
        test("Location modes share canvas, floors, drafts and history", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            const QByteArray terrain(16384, char(0xf8));
            file(dir.path() + "/TOWNE.DAT", terrain);
            QByteArray npcs(4608, 0);
            npcs[19] = 2;
            npcs[22] = 3;
            npcs[513] = 25;
            file(dir.path() + "/TOWNE.NPC", npcs);
            file(dir.path() + "/IBM.CH", QByteArray(1024, char(255)));
            file(dir.path() + "/RUNES.CH", QByteArray(1024, char(255)));
            QByteArray seed(66, 0);
            seed += char(255);
            seed += char(255);
            const auto signs =
                Workshop::writeSigns(seed, {{1, 0, 2, 3, Workshop::encodeSign("Ground")},
                                            {1, 1, 4, 5, Workshop::encodeSign("Upstairs")},
                                            {2, 0, 6, 7, Workshop::encodeSign("Britain")}});
            file(dir.path() + "/SIGNS.DAT", signs);
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Location fixture failed");
            w.show();
            w.selectResource("TOWNE.DAT");
            app.processEvents();
            auto active = [&] {
                return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto modes = active()->findChild<QTabBar *>("locationModes");
            auto setMode = [&](QString title) {
                for (int i = 0; i < modes->count(); ++i)
                    if (modes->tabText(i) == title) {
                        modes->setCurrentIndex(i);
                        app.processEvents();
                        return;
                    }
                throw std::runtime_error("Location mode missing");
            };
            auto canvas = dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            auto view = dynamic_cast<MapView *>(active()->findChild<QScrollArea *>("canvasView"));
            auto floor = active()->findChild<QComboBox *>("mapPage");
            check(modes->count() == 5 && modes->tabText(4) == "Contents", "Wrong settlement modes");
            view->setZoom(2);
            view->centerMap({16, 16});
            const auto center = view->mapCenter();
            setMode("Signs");
            check(active()->findChild<QWidget *>("mapCanvas") == canvas &&
                      active()->findChild<QScrollArea *>("canvasView") == view,
                  "Mode replaced the map canvas or camera");
            check(canvas->zoom == 2 && QLineF(center, view->mapCenter()).length() < 0.1,
                  "Mode changed zoom or camera");
            check(active()->findChild<QComboBox *>("signLocation")->isHidden() &&
                      active()->findChild<QComboBox *>("signFloor")->isHidden(),
                  "Signs can escape the shared location context");
            auto list = active()->findChild<QListWidget *>("locationTextEntries");
            check(list->count() == 1 && list->item(0)->text().contains("Ground"),
                  "Signs leaked another floor or settlement");
            auto draft = active()->findChild<QPlainTextEdit *>("locationTextDraft");
            draft->selectAll();
            draft->insertPlainText("Changed ground");
            setMode("NPCs / Schedules");
            check(Workshop::signSource(
                      Workshop::readSigns(w.projectForTests().data("SIGNS.DAT"))[0].text) ==
                      "Changed ground",
                  "Mode switch lost sign draft");
            check(canvas->actors.size() == 1 && canvas->actors[0].tile == 281,
                  "Sign markers leaked into character mode");
            setMode("Signs");
            floor->setCurrentIndex(floor->findData(1));
            app.processEvents();
            auto navigator = w.findChild<QWidget *>("resourceLibrary")->findChild<QTreeWidget *>();
            check(navigator->currentItem() && navigator->currentItem()->data(0, Qt::UserRole + 2).toString() == "world/TOWNE.DAT/0",
                  "Changing floors lost the shared location highlight");
            check(active()
                      ->findChild<QListWidget *>("locationTextEntries")
                      ->item(0)
                      ->text()
                      .contains("Upstairs"),
                  "Shared floor selector did not update signs");
            check(canvas->actors.size() == 1 && canvas->actors[0].x == 4,
                  "Sign marker floor context is stale");
            canvas->keyboardCell = {8, 9};
            for (auto b : active()->findChildren<QPushButton *>())
                if (b->text() == "Add sign at selected cell")
                    b->click();
            auto records = Workshop::readSigns(w.projectForTests().data("SIGNS.DAT"));
            check(std::any_of(records.begin(), records.end(),
                              [](const Workshop::SignRecord &r) {
                                  return r.floor == 1 && r.map == 1 && r.x == 8 && r.y == 9;
                              }),
                  "Adding a sign used the wrong shared cell or floor");
            check(w.projectForTests().data("TOWNE.DAT") == terrain, "Signs painted terrain");
            // Invalid drafts block both mode changes and floor changes, rather than dropping edits.
            draft = active()->findChild<QPlainTextEdit *>("locationTextDraft");
            draft->selectAll();
            draft->insertPlainText("[byte:ZZ]");
            auto closeWarning = [] {
                QTimer::singleShot(0, [] {
                    for (auto widget : QApplication::topLevelWidgets())
                        if (auto box = qobject_cast<QMessageBox *>(widget))
                            box->accept();
                });
            };
            closeWarning();
            setMode("Terrain");
            check(modes->tabText(modes->currentIndex()) == "Signs" &&
                      draft->toPlainText() == "[byte:ZZ]",
                  "Invalid sign draft was dropped by mode change");
            closeWarning();
            floor->setCurrentIndex(floor->findData(0));
            check(floor->currentData().toInt() == 1 && draft->toPlainText() == "[byte:ZZ]",
                  "Invalid sign draft was dropped by floor change");
            draft->selectAll();
            draft->insertPlainText("New sign");
            setMode("Terrain");
            check(canvas->chooseNpc && canvas->moveNpc && canvas->commit,
                  "Layer callbacks did not restore native editing");
            check(active()->findChild<QComboBox *>("mapPage")->currentData().toInt() == 1,
                  "Mode switch lost current floor");
            setMode("Signs");
            for (auto a : w.findChildren<QAction *>())
                if (a->text().startsWith("Undo ")) {
                    a->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            check(
                active()->findChild<QTabBar *>("locationModes")
                            ->tabText(
                                active()->findChild<QTabBar *>("locationModes")->currentIndex()) ==
                        "Signs" &&
                    active()->findChild<QComboBox *>("mapPage")->currentData().toInt() == 1,
                "Undo lost location mode or floor");
            check(Workshop::readSigns(w.projectForTests().data("SIGNS.DAT")).size() == 3,
                  "Undo did not remove the new sign");
            modes = active()->findChild<QTabBar *>("locationModes");
            setMode("Terrain");
            w.projectForTests().resources["SIGNS.DAT"].edited = "broken";
            setMode("Signs");
            bool explained = false;
            for (auto label : active()->findChildren<QLabel *>())
                explained |= label->text().contains("Cannot open this location layer");
            check(explained, "Broken sign companion did not produce a recoverable layer error");
            setMode("Terrain");
            check(
                bool(
                    dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"))->commit),
                "Cannot leave a damaged location layer");
        });
        test("Starting object mode uses the shared world and preserves terrain", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            QByteArray overlay(0x3986, char(255));
            file(dir.path() + "/DATA.OVL", overlay);
            const QByteArray world(256, 1);
            file(dir.path() + "/BRIT.DAT", world);
            file(dir.path() + "/UNDER.DAT", QByteArray(65536, 1));
            for (QString name : {"INIT.OOL", "BRIT.OOL", "UNDER.OOL"})
                file(dir.path() + "/" + name, QByteArray(256, 0));
            WorkshopWindow w;
            check(w.openGame(dir.path()), "World layer fixture failed");
            w.show();
            w.selectResource("BRIT.DAT");
            app.processEvents();
            auto active = [&] {
                return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto modes = active()->findChild<QTabBar *>("locationModes");
            auto canvas = dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            check(modes->count() == 4 && modes->tabText(1) == "Starting Objects" &&
                      modes->tabText(3) == "Contents",
                  "World mode availability wrong");
            canvas->keyboardCell = {50, 60};
            modes->setCurrentIndex(1);
            app.processEvents();
            check(active()->findChild<QComboBox *>("objectWorld")->isHidden(),
                  "Objects can switch away from shared world");
            check(!active()->findChild<QWidget *>("worldObjectMap"),
                  "Objects created a second canvas");
            MapCanvas clipboard;
            clipboard.side = 1;
            clipboard.ids = QByteArray(1, 5);
            clipboard.selection = QRect(0, 0, 1, 1);
            check(clipboard.copySelection(), "Could not create valid terrain clipboard");
            active()->findChild<QButtonGroup *>("mapTools")->button(MapCanvas::Pan)->click();
            check(!canvas->beginPaste(), "Panning in object mode enabled terrain paste");
            active()->findChild<QButtonGroup *>("mapTools")->button(MapCanvas::InspectNpc)->click();

            active()->findChild<QPushButton *>("addWorldObject")->click();
            auto objects = Workshop::WorldObjects::effective(w.projectForTests(), 0);
            check(uchar(objects[10]) == 50 && uchar(objects[11]) == 60,
                  "Object added at wrong shared cell");
            check(canvas->actors.size() == 1 && canvas->moveNpc(1, {70, 80}),
                  "Shared object drag failed");
            objects = Workshop::WorldObjects::effective(w.projectForTests(), 0);
            check(uchar(objects[10]) == 70 && uchar(objects[11]) == 80,
                  "Object drag did not edit world override");
            check(w.projectForTests().data("BRIT.DAT") == world &&
                      w.projectForTests().data("DATA.OVL") == overlay,
                  "Object mode modified map terrain");
            modes->setCurrentIndex(0);
            app.processEvents();
            check(canvas->actors.isEmpty() && canvas->commit,
                  "Object layer leaked into terrain mode");
            w.selectResource("UNDER.DAT");
            app.processEvents();
            modes = active()->findChild<QTabBar *>("locationModes");
            modes->setCurrentIndex(1);
            canvas = dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            canvas->keyboardCell = {20, 30};
            active()->findChild<QPushButton *>("addWorldObject")->click();
            check(uchar(Workshop::WorldObjects::effective(w.projectForTests(), 1)[12]) == 255,
                  "Underworld object got the wrong world level");
            check(Workshop::WorldObjects::effective(w.projectForTests(), 0) == objects,
                  "Underworld edit changed Britannia objects");
        });
        test("Encounter modes keep advanced metadata tied to the selected combat map", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            QByteArray combat(704, 0);
            file(dir.path() + "/BRIT.CBT", combat);
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Encounter mode fixture failed");
            w.selectResource("BRIT.CBT");
            app.processEvents();
            auto root = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            auto map = dynamic_cast<MapWorkspace *>(root->findChild<QWidget *>("mapWorkspace"));
            map->openPage(1);
            app.processEvents();
            auto modes = root->findChild<QTabBar *>("locationModes");
            check(modes->count() == 4 && modes->tabText(3) == "Contents" &&
                      modes->tabText(1) == "Encounter" && modes->tabText(2) == "Encounter Details",
                  "Combat modes incorrect");
            modes->setCurrentIndex(2);
            auto table = root->findChild<QTableWidget *>();
            check(table, "Advanced encounter editor missing");
            table->item(5, 0)->setText("25");
            auto expected = combat;
            expected[352 + 5 * 32 + 11] = 25;
            check(w.projectForTests().data("BRIT.CBT") == expected,
                  "Advanced metadata edited a different encounter or touched terrain");
            modes->setCurrentIndex(1);
            check(root->findChild<QComboBox *>("mapPage")->currentData().toInt() == 1 &&
                      root->findChild<QComboBox *>("combatEntity")->count() == 30,
                  "Encounter mode lost its selected map or markers");
        });
        test("Location text original resources and native editors (optional)", [&] {
            auto directory = qEnvironmentVariable("U5_GAME_DIR");
            if (directory.isEmpty())
                return;
            Project p;
            p.openGame(directory);
            auto signs = p.data("SIGNS.DAT");
            auto records = Workshop::readSigns(signs);
            check(Workshop::writeSigns(signs, records) == signs, "Original signs changed");
            for (const auto &r : records)
                check(Workshop::encodeSign(Workshop::signSource(r.text)) == r.text,
                      "Original sign encoding changed");
            for (int i = 0; i < 512; i++)
                Workshop::description(p.data("LOOK2.DAT"), i);
            WorkshopWindow w;
            check(w.openGame(directory), "Location text window load failed");
            w.selectResource("SIGNS.DAT");
            app.processEvents();
            auto entries = w.findChild<QListWidget *>("locationTextEntries");
            check(entries && entries->count() > 0, "Sign list missing");
            auto draft = w.findChild<QPlainTextEdit *>("locationTextDraft");
            check(draft, "Sign editor missing");
            draft->setPlainText("Edited sign");
            for (auto button : w.findChildren<QPushButton *>())
                if (button->text() == "Apply text")
                    button->click();
            check(w.projectForTests().changed().contains("SIGNS.DAT"), "Sign UI did not commit");
            w.selectResource("LOOK2.DAT");
            app.processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            entries = w.findChild<QListWidget *>("locationTextEntries");
            check(entries && entries->count() == 512, "Descriptions list missing entries");
            draft = w.findChild<QPlainTextEdit *>("locationTextDraft");
            draft->setPlainText("a test description");
            for (auto button : w.findChildren<QPushButton *>())
                if (button->text() == "Apply text")
                    button->click();
            check(Workshop::description(w.projectForTests().data("LOOK2.DAT"), 0) ==
                      "a test description",
                  "Description UI did not commit");
        });
        test("Dungeon floors resize independently, roundtrip and keep contents, links and undo",
             [&] {
                 QTemporaryDir dir;
                 auto p = fixture(dir.path());
                 QByteArray native(4096, 0);
                 native[63] = char(0xa1);
                 file(dir.path() + "/DUNGEON.DAT", native);
                 file(dir.path() + "/DUNGEON.CBT", QByteArray(112 * 352, 0));
                 p.openGame(dir.path());
                 int dungeonCount = 0;
                 for (const auto &route : Workshop::contentDestinations(p))
                     if (route.resource == "DUNGEON.DAT") {
                         ++dungeonCount;
                         check(route.title == p.locationName(33 + route.selection.value("DUNGEON.DAT/dungeon")),
                               "Dungeon navigation name mismatch");
                     }
                 check(dungeonCount == 8, "Dungeon navigation does not list each dungeon");
                 auto apply = [&](const QMap<QString, QByteArray> &edits) {
                     for (auto it = edits.begin(); it != edits.end(); ++it)
                         p.resources[it.key()].edited = it.value();
                     p.validate();
                 };
                 apply(Workshop::resizeMap(p, "DUNGEON.DAT", 0, 13, 9, 0xb0));
                 Workshop::DungeonDocument doc(&p);
                 check(doc.size(0, 0) == QSize(13, 9) && doc.size(0, 1) == QSize(8, 8),
                       "Dungeon dimensions leaked to another floor");
                 check(U5::byte(doc.level(0, 0), 7 * 13 + 7) == 0xa1 &&
                           U5::byte(doc.level(0, 0), 8 * 13 + 12) == 0xb0 &&
                           p.data("DUNGEON.DAT") == native,
                       "Resize lost feature bytes or changed native dungeon banks");
                 auto cells = doc.level(0, 0);
                 cells[8 * 13 + 12] = 0x20;
                 apply(doc.changes(0, 0, cells));
                 check(doc.warnings(0, 0, 12, 8).join(" ").contains("matching"),
                       "Cross-floor ladder warning failed outside adjacent dimensions");
                 apply({{"CONTENTS.OVR", Workshop::addContentsAt(p, {0, 33, 0}, {12, 8}, true)}});
                 rejects([&] { Workshop::resizeMap(p, "DUNGEON.DAT", 0, 12, 8, 0); });
                 apply(Workshop::resizeMap(p, "DUNGEON.DAT", 1, 5, 3, 0));
                 apply(Workshop::resizeMap(p, "DUNGEON.DAT", 2, 1, 256, 0));
                 apply(Workshop::resizeMap(p, "DUNGEON.DAT", 3, 256, 256, 0));
                 check(doc.level(0, 3).size() == 65536 && doc.size(0, 2) == QSize(1, 256),
                       "Dungeon size boundary was truncated");
                 Project positioned = p;
                 auto starts = Workshop::moveMapPosition(positioned, 0, 33, 3, {255, 255});
                 for (auto it = starts.begin(); it != starts.end(); ++it)
                     positioned.resources[it.key()].edited = it.value();
                 rejects([&] { Workshop::resizeMap(positioned, "DUNGEON.DAT", 3, 255, 256, 0); });
                 rejects([&] { Workshop::resizeMap(p, "DUNGEON.DAT", 0, 257, 9, 0); });
                 rejects([&] { Workshop::resizeMap(p, "DUNGEON.DAT", 0, 13, 0, 0); });
                 rejects([&] { Workshop::resizeMap(p, "DUNGEON.DAT", 0, 13, 9, 0x90); });
                 auto package = p.package();
                 check(package.startsWith("IMOD0020"),
                       "Dungeon dimensions lack package capability protection");
                 Project imported;
                 imported.openGame(dir.path());
                 imported.importPackage(package);
                 check(Workshop::DungeonDocument(&imported).level(0, 0) == doc.level(0, 0) &&
                           Workshop::DungeonDocument(&imported).size(0, 3) == QSize(256, 256),
                       "Dungeon package lost resized terrain");
                 auto old = package;
                 old.replace(0, 8, "IMOD0019");
                 rejects([&] { imported.importPackage(old); });
                 p.save(dir.path() + "/dungeons.imperaproject");
                 WorkshopWindow window;
                 check(window.openProject(p.projectPath), "Cannot reopen resized dungeons");
                 window.show();
                 window.selectResource("DUNGEON.DAT");
                 auto chooseDungeon = [&](int dungeon) {
                     auto tree = window.findChild<QWidget *>("resourceLibrary")->findChild<QTreeWidget *>();
                     QTreeWidgetItemIterator it(tree);
                     while (*it) {
                         if ((*it)->data(0, Qt::UserRole + 2).toString() ==
                             QString("world/dungeon/%1").arg(dungeon)) {
                             tree->setCurrentItem(*it);
                             app.processEvents();
                             return;
                         }
                         ++it;
                     }
                     throw std::runtime_error("Dungeon navigation entry missing");
                 };
                 chooseDungeon(3);
                 app.processEvents();
                 check(window.findChild<QStackedWidget *>("workspaces")->currentWidget()->findChild<QLabel *>("dungeonName")->text() ==
                           window.projectForTests().locationName(36),
                       "Dungeon navigation did not open the selected dungeon");
                 chooseDungeon(0);
                 app.processEvents();
                 auto active = [&] {
                     return window.findChild<QStackedWidget *>("workspaces")->currentWidget();
                 };
                 auto canvas =
                     dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("dungeonCanvas"));
                 check(canvas && canvas->side == 13 && canvas->rowCount() == 9,
                       "Dungeon editor still assumes eight cells");
                 canvas->inspect(12, 8, 0);
                 QTimer::singleShot(0, [] {
                     auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                     check(dialog, "Resize dialog missing");
                     dialog->findChild<QSpinBox *>("mapWidth")->setValue(16);
                     dialog->findChild<QSpinBox *>("mapHeight")->setValue(10);
                     dialog->accept();
                 });
                 check(!active()->findChild<QComboBox *>("dungeonSelector") &&
                           active()->findChild<QLabel *>("dungeonName"),
                       "Dungeon picker remains in the editor");
                 active()->findChild<QToolButton *>("resizeDungeonFloor")->click();
                 app.processEvents();
                 check(Workshop::DungeonDocument(&window.projectForTests()).size(0, 0) ==
                           QSize(16, 10),
                       "Dungeon Resize control failed");
                 for (auto action : window.findChildren<QAction *>())
                     if (action->shortcut() == QKeySequence::Undo) {
                         action->trigger();
                         break;
                     }
                 app.processEvents();
                 canvas =
                     dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("dungeonCanvas"));
                 check(canvas && canvas->side == 13 && canvas->rowCount() == 9 &&
                           window.projectForTests().data("DUNGEON.DAT") == native,
                       "Dungeon resize undo lost dimensions or original terrain");
             });
        test("Dungeon codec, feature validation and engine room mapping", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray base(4096, char(0xb0));
            base[511] = char(0x9a); // Unusual original encoding is preserved.
            file(dir.path() + "/DUNGEON.DAT", base);
            file(dir.path() + "/DUNGEON.CBT", QByteArray(112 * 352, 0));
            p.openGame(dir.path());
            Workshop::DungeonDocument document(&p);
            check(document.changes(0, 0, document.level(0, 0))["DUNGEON.DAT"] == base,
                  "No-op dungeon changed bytes");
            for (int d = 0; d < 8; ++d)
                for (int room = 0; room < 16; ++room)
                    check(Workshop::DungeonDocument::roomIndex(d, 0xa0 | room) * 352 ==
                              WorkshopEngineDungeonRoom(0x21 + d, 0xa0 | room),
                          "Engine room mapping mismatch");
            check(Workshop::DungeonDocument::roomIndex(1, 0xf3) == 3, "Despise room bank mismatch");
            rejects([&] { document.level(8, 0); });
            rejects([&] { document.level(0, 8); });
            rejects([&] { document.changes(0, 0, QByteArray(63, 0)); });
            auto level = document.level(2, 3);
            level[9] = char(0x23);
            level[10] = char(0xa5);
            p.resources["DUNGEON.DAT"].edited = document.changes(2, 3, level)["DUNGEON.DAT"];
            p.validate();
            check(p.data("DUNGEON.DAT").left(1216) == base.left(1216) &&
                      p.data("DUNGEON.DAT").mid(1280) == base.mid(1280),
                  "Other dungeon levels changed");
            check(!document.warnings(2, 3, 1, 1).isEmpty(), "Unpaired ladder not diagnosed");
            check(document.level(2, 3)[10] == char(0xa5), "Room reference changed");
            p.save(dir.path() + "/dungeon.imperaproject");
            Project loaded;
            loaded.load(dir.path() + "/dungeon.imperaproject");
            check(loaded.data("DUNGEON.DAT") == p.data("DUNGEON.DAT"),
                  "Dungeon project round trip failed");
            auto package = p.package();
            const auto exported = qEnvironmentVariable("IMPERA_DUNGEON_TEST_PACKAGE");
            if (!exported.isEmpty())
                file(exported, package);
            p.resources["DUNGEON.DAT"].edited = base;
            p.importPackage(package);
            check(document.level(2, 3) == level, "Dungeon package round trip failed");
            auto diagnostics = validateMod(p);
            bool found = false;
            for (auto diagnostic : diagnostics)
                if (diagnostic.entryId == "dungeon/2/level/3/cell/9")
                    found = true;
            check(found, "Dungeon diagnostic lost stable cell ID");
            p.resources["DUNGEON.DAT"].edited[0] = char(0x90);
            rejects([&] { p.validate(); });
            p.resources["DUNGEON.DAT"].edited = base;
            p.resources["DUNGEON.DAT"].edited[0] = char(0xaf);
            p.resources["DUNGEON.CBT"].edited.resize(352);
            rejects([&] { p.validate(); });
            p.resources["DUNGEON.DAT"].edited = base;
            p.resources["DUNGEON.DAT"].edited[0] = p.resources["DUNGEON.DAT"].original[0] =
                char(0xaf);
            rejects([&] {
                p.validate();
            }); // Shrinking combat data also checks unchanged dungeon references.
            p.resources["DUNGEON.DAT"].edited.resize(4095);
            rejects([&] { Workshop::DungeonDocument invalid(&p); });
        });
        test("Dungeon artwork covers every feature and uses project tiles", [] {
            QVector<QImage> tiles;
            for (int i = 0; i < 512; ++i) {
                QImage tile(16, 16, QImage::Format_ARGB32);
                tile.fill(QColor(i % 256, i / 256, 64));
                tiles.append(tile);
            }
            for (int id : {0xc8, 0xc9, 257, 0xd8, 0x00})
                tiles[id].setPixelColor(0, 0, Qt::black);
            const auto images = Workshop::dungeonFeatureImages(tiles);
            check(images.size() == 256, "Missing dungeon feature artwork");
            for (const auto &image : images)
                check(!image.isNull() && image.size() == QSize(32, 32), "Invalid dungeon artwork");
            check(images[0].pixelColor(0, 0) == tiles[68].pixelColor(0, 0) &&
                      images[0x40].pixelColor(0, 0) == tiles[257].pixelColor(0, 0),
                  "Dungeon artwork does not use project floor and actor chest tiles");
            check(images[0xa0] != images[0xa1], "Room number badges are indistinguishable");
            check(images[0x10] != images[0x18], "Ceiling opening marker is missing");
            check(images[0x40] != images[0x70], "Open chest marker is missing");
            check(images[0xb0] != images[0xb1], "Wall message marker is missing");
            for (int feature : {0x10, 0x20, 0x40, 0x50, 0x80})
                check(images[feature].pixelColor(0, 0) == QColor(Qt::black),
                      "Dungeon sprite black background was replaced or recolored");
            const auto originalFont = QApplication::font();
            auto differentFont = originalFont;
            differentFont.setLetterSpacing(QFont::AbsoluteSpacing, 8);
            differentFont.setStretch(200);
            QApplication::setFont(differentFont);
            const auto repeat = Workshop::dungeonFeatureImages(tiles);
            QApplication::setFont(originalFont);
            check(images == repeat, "Dungeon badge rendering depends on native font metrics");
        });
        test("Dungeon UI edits, undo and room return", [] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/DUNGEON.DAT", QByteArray(4096, 0));
            file(dir.path() + "/DUNGEON.CBT", QByteArray(112 * 352, 0));
            WorkshopWindow window;
            window.openGame(dir.path());
            window.selectResource("DUNGEON.DAT");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto canvas = dynamic_cast<MapCanvas *>(window.findChild<QWidget *>("dungeonCanvas"));
            check(canvas && canvas->tiles.size() == 256, "Dungeon artwork canvas missing");
            check(!window.findChild<QListWidget *>("dungeonPalette")->item(0)->icon().isNull() &&
                      window.findChild<QLabel *>("dungeonBrushPreview") &&
                      window.findChild<QComboBox *>("dungeonZoom") &&
                      !window.findChild<QLabel *>("dungeonCellDetails")->text().contains("Feature byte"),
                  "Dungeon visual palette, preview or zoom is missing");
            window.resize(1400, 900);
            window.show();
            QApplication::processEvents();
            auto inspector = window.findChild<QStackedWidget *>("dungeonInspector");
            const int inspectorWidth = inspector->width();
            auto cells = canvas->ids;
            for (int feature = 0; feature < 256; ++feature) {
                canvas->ids[0] = char(feature);
                canvas->inspect(0, 0, feature);
                QApplication::processEvents();
                check(inspector->width() == inspectorWidth,
                      "Dungeon inspector width changes with inspected feature");
            }
            canvas->ids = cells;
            canvas->selection = QRect(0, 0, 1, 1);
            check(canvas->copySelection(), "Dungeon copy failed");
            MapCanvas terrain;
            terrain.side = 8;
            terrain.ids = QByteArray(64, 0);
            check(!terrain.beginPaste(), "Dungeon features accepted as terrain");
            terrain.selection = QRect(0, 0, 1, 1);
            check(terrain.copySelection(), "Terrain copy failed");
            check(!canvas->beginPaste(), "Terrain accepted as dungeon features");
            canvas->inspect(2, 3, 0);
            auto palette = window.findChild<QListWidget *>("dungeonPalette");
            for (int i = 0; i < palette->count(); ++i)
                if (palette->item(i)->data(Qt::UserRole).toInt() == 0xa0)
                    palette->setCurrentRow(i);
            window.findChild<QComboBox *>("dungeonSubtype")->setCurrentIndex(3);
            for (auto button : window.findChildren<QPushButton *>())
                if (button->text() == "Apply brush to selected cell")
                    button->click();
            check(window.projectForTests().data("DUNGEON.DAT")[26] == char(0xa3),
                  "Configured room not applied");
            window.findChild<QPushButton *>("openDungeonRoom")->click();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto back = window.findChild<QPushButton *>("backToDungeon");
            check(back, "Room return missing");
            back->click();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            canvas = dynamic_cast<MapCanvas *>(window.findChild<QWidget *>("dungeonCanvas"));
            check(canvas && canvas->keyboardCell == QPoint(2, 3), "Room return lost dungeon cell");
            for (auto action : window.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            QApplication::processEvents();
            check(window.projectForTests().data("DUNGEON.DAT")[26] == 0, "Dungeon undo failed");
        });
        test("Shared resource documents preserve bytes and stable identities", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            file(dir.path() + "/INIT.OOL", QByteArray(256, 3));
            p.openGame(dir.path());
            Workshop::ResourceDocument document(p, "init.ool");
            auto entry = document.entries()[2];
            check(entry.id == "object/2" && document.id() == "INIT.OOL",
                  "Unstable document identity");
            check(document.replace(entry, document.read(entry))["INIT.OOL"] == p.data("INIT.OOL"),
                  "No-op altered bytes");
            auto changes = document.replace(entry, QByteArray(8, 9));
            check(changes["INIT.OOL"].left(16) == QByteArray(16, 3) &&
                      changes["INIT.OOL"].mid(24) == QByteArray(232, 3),
                  "Unknown bytes lost");
            rejects(
                [&] { document.read({"bad", "Bad", std::numeric_limits<qsizetype>::max(), 8}); });
            rejects([&] { document.replace(entry, QByteArray(9, 1)); });
            p.resources["INIT.OOL"].edited = changes["INIT.OOL"];
            check(document.read(entry) == QByteArray(8, 9), "Document kept stale state");
            p.save(dir.path() + "/document.imperaproj");
            Project loaded;
            loaded.load(dir.path() + "/document.imperaproj");
            check(loaded.data("INIT.OOL") == p.data("INIT.OOL"), "Project round trip failed");
            auto package = p.package();
            p.resources["INIT.OOL"].edited = p.resources["INIT.OOL"].original;
            p.importPackage(package);
            check(document.read(entry) == QByteArray(8, 9), "Package round trip failed");
            p.resources["INIT.OOL"].edited.resize(255);
            rejects([&] { document.entries(); });
            check(Workshop::capability("UNKNOWN.DAT").editor == Workshop::EditorKind::Inspector,
                  "Unknown file lost inspection");
        });
        test("Direct workspaces, resource markers and universal resource navigation", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            QByteArray initialObjects(256, 0);
            initialObjects[8] = initialObjects[9] = char(25);
            initialObjects[10] = initialObjects[11] = char(3);
            initialObjects[12] = char(255);
            file(dir.path() + "/INIT.OOL", initialObjects);
            file(dir.path() + "/BRIT.OOL", QByteArray(256, 0));
            file(dir.path() + "/UNDER.OOL", QByteArray(256, 0));
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            file(
                dir.path() + "/TOWNE.TLK",
                U5::writeDialogue({{1, U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>"
                                                      "Job<Entry>Bye<Entry><Any><Label 15>@")}}));
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Open navigation fixture");
            window.setProperty("overviewAdvanced", true);
            window.selectResource("INIT.OOL");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            check(window.findChild<QListWidget *>("worldObjectList"), "Starting objects workspace missing");
            check(!window.findChild<QSpinBox *>("resourceByteOffset") &&
                      !window.findChild<QTableWidget *>("byteInspector"),
                  "Hex editor is still exposed");
            window.findChild<QSpinBox *>("worldObjectX")->setValue(4);
            auto marker = [&] {
                auto library = window.findChild<QWidget *>("resourceLibrary");
                QTreeWidgetItemIterator it(library->findChild<QTreeWidget *>());
                while (*it) {
                    if ((*it)->data(0, Qt::UserRole).toString() == "UNDER.OOL")
                        return (*it)->text(0).endsWith(" *");
                    ++it;
                }
                return false;
            };
            check(marker(), "Modified resource has no asterisk");
            for (auto action : window.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            check(!marker(), "Undo left a modified resource marker");
            auto resources = window.findChild<QAction *>("showResourceLibrary");
            auto library = window.findChild<QWidget *>("resourceLibrary");
            check(resources && resources->shortcut() == QKeySequence("Ctrl+L"),
                  "Universal resource navigation missing");
            for (auto resource : {"TOWNE.DAT", "TOWNE.TLK"}) {
                window.selectResource(resource);
                check(!library->isHidden(), "Content navigator disappeared in editor");
                auto workspace = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
                auto draft = resource == QString("TOWNE.TLK")
                                 ? workspace->findChild<QPlainTextEdit *>("dialogueText")
                                 : nullptr;
                QString text;
                if (draft) {
                    text = draft->toPlainText() + "draft";
                    draft->setPlainText(text);
                }
                resources->trigger();
                check(!library->isHidden(), "Cannot return to resources from focused editor");
                check(window.findChild<QStackedWidget *>("workspaces")->currentWidget() ==
                              workspace &&
                          (!draft || draft->toPlainText() == text),
                      "Resource navigation replaced workspace or discarded draft");
            }
            auto selector = window.findChild<QComboBox *>("contentWorkspace");
            check(selector && selector->count() == 6, "Content workspace selector missing");
            window.selectResource("TOWNE.DAT");
            auto activeMap = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            check(activeMap->findChild<QTreeWidget *>("mapNavigation")->parentWidget()->isHidden(),
                  "Duplicate map sidebar is visible");
            auto destinations = Workshop::contentDestinations(window.projectForTests());
            QSet<QString> ids;
            int moonglow = 0;
            for (const auto &d : destinations) {
                check(!ids.contains(d.id), "Duplicate semantic destination ID");
                ids.insert(d.id);
                if (d.title == "Moonglow") {
                    ++moonglow;
                    check(d.page >= 0, "Location has no main floor");
                }
            }
            check(moonglow == 1, "Settlement floors leaked into shared navigation");
            auto floors = activeMap->findChild<QComboBox *>("mapPage");
            floors->setCurrentIndex(floors->count() - 1);
            const int rememberedFloor = floors->currentData().toInt();
            window.selectResource("INIT.GAM");
            for (auto action : window.findChildren<QAction *>())
                if (action->text() == "Back") {
                    action->trigger();
                    break;
                }
            check(selector->currentText() == "World", "Back did not restore workspace");
            check(window.findChild<QStackedWidget *>("workspaces")->currentWidget()
                          ->findChild<QComboBox *>("mapPage")->currentData().toInt() == rememberedFloor,
                  "Back lost selected floor");
            for (auto action : window.findChildren<QAction *>())
                if (action->text() == "Forward") {
                    action->trigger();
                    break;
                }
            check(selector->currentText() == "Game Rules", "Forward did not restore workspace");
            auto workspace = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            for (auto tabs : workspace->findChildren<QTabWidget *>())
                for (int i = 0; i < tabs->count(); ++i)
                    check(tabs->tabText(i) != "Visual editor" &&
                              tabs->tabText(i) != "Resource inspector",
                          "Redundant workspace tabs remain");
        });
        test("Conversation filters use top-level locations and combine with search", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            auto conversation = [](QString name) {
                return U5::encodeText(
                    name +
                    "<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry><Any><Label 15>@");
            };
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, conversation("Alice")},
                                                               {2, conversation("Bob")},
                                                               {3, conversation("Unused")}}));
            QByteArray schedules(4608, 0);
            auto assign = [&](int town, int slot, int id) {
                schedules[town * 576 + 512 + slot] = char(0x40);
                schedules[town * 576 + 544 + slot] = char(id);
            };
            assign(0, 0, 1);
            assign(3, 1, 1);
            assign(3, 2, 2);
            file(dir.path() + "/TOWNE.NPC", schedules);
            p.openGame(dir.path());
            ConversationEditor editor(
                &p, "TOWNE.TLK", [](auto &, auto &) {}, [](auto &, auto &) {}, 0, 0,
                [](int, int) {});
            auto filter = editor.findChild<QComboBox *>("conversationLocationFilter");
            auto npcs = editor.findChild<QTreeWidget *>("conversationNpcs");
            auto search = editor.findChild<QLineEdit *>("conversationSearch");
            check(filter->count() == 10 && filter->findText("Yew") >= 0,
                  "Filter does not use eight top-level locations");
            for (int i = 0; i < filter->count(); ++i)
                check(!filter->itemText(i).contains("Floor") &&
                          !filter->itemText(i).contains("Basement"),
                      "Filter exposes floors");
            auto visible = [&] {
                QStringList names;
                for (int i = 0; i < npcs->topLevelItemCount(); ++i)
                    if (!npcs->topLevelItem(i)->isHidden())
                        names.append(npcs->topLevelItem(i)->text(0));
                return names.join(',');
            };
            filter->setCurrentIndex(filter->findText("Moonglow"));
            check(visible().contains("Alice") && !visible().contains("Bob"),
                  "Location filter uses wrong assignments");
            filter->setCurrentIndex(filter->findText("Yew"));
            check(visible().contains("Alice") && visible().contains("Bob"),
                  "Shared dialogue was omitted from a location");
            search->setText("Bob");
            check(!visible().contains("Alice") && visible().contains("Bob"),
                  "Search bypassed location filter");
            filter->setCurrentIndex(filter->findText("Moonglow"));
            check(visible().isEmpty(), "Combined filters did not produce empty result");
            search->clear();
            filter->setCurrentIndex(filter->findData(-2));
            check(visible().contains("Unused") && !visible().contains("Alice"),
                  "Unassigned filter is incorrect");
        });
        test("Original proportional font compatibility (optional)", [] {
            auto directory = qEnvironmentVariable("U5_GAME_DIR");
            if (directory.isEmpty())
                return;
            QString path;
            for (auto name : QDir(directory).entryList(QDir::Files))
                if (name.compare("PROPORT.PCS", Qt::CaseInsensitive) == 0)
                    path = QDir(directory).filePath(name);
            if (path.isEmpty())
                return;
            QFile font(path);
            check(font.open(QIODevice::ReadOnly), "Cannot read optional font");
            auto preview = Workshop::previewText(U5::decompress(font.readAll()), "The Avatar",
                                                 Workshop::FontMode::Proportional);
            check(preview.issues.isEmpty(), "Original proportional font incompatible");
        });
        test("Shared font previews reject corruption and unsupported glyphs", [] {
            QByteArray normal(1024, char(0xff));
            auto preview =
                Workshop::previewText(normal, QString::fromUtf8("Aé"), Workshop::FontMode::Normal);
            check(!preview.issues.isEmpty(), "Unsupported Unicode silently aliased");
            check(Workshop::previewText(normal, "A", Workshop::FontMode::Runic).issues.isEmpty(),
                  "Runic font rejected");
            rejects(
                [&] { Workshop::previewText(normal.left(1023), "A", Workshop::FontMode::Normal); });
            QByteArray proportional(8, 0);
            U5::setWord(proportional, 0, 1);
            U5::setWord(proportional, 2, 4);
            U5::setWord(proportional, 4, 3);
            U5::setWord(proportional, 6, 1);
            rejects([&] {
                Workshop::previewText(proportional, " ", Workshop::FontMode::Proportional);
            });
            proportional.append(char(0xe0));
            check(Workshop::previewText(proportional, " ", Workshop::FontMode::Proportional)
                      .issues.isEmpty(),
                  "Valid proportional glyph rejected");
            U5::setWord(proportional, 4, 0);
            check(Workshop::previewText(proportional, " ", Workshop::FontMode::Proportional)
                      .issues.isEmpty(),
                  "Blank proportional space rejected");
        });
        test("Export Britannia package for engine integration", [] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray overlay(U5_WORLD_INDEX_OFFSET + 256, 0);
            std::memcpy(overlay.data() + U5_WORLD_INDEX_OFFSET, U5_BritanniaDefaultIndex, 256);
            QByteArray chunks(65536, 5);
            chunks.replace(256, 256, QByteArray(256, 7));
            file(dir.path() + "/DATA.OVL", overlay);
            file(dir.path() + "/BRIT.DAT", chunks);
            project.openGame(dir.path());
            auto &changed = project.resources["DATA.OVL"].edited;
            changed.replace(U5_WORLD_INDEX_OFFSET, 256, QByteArray(256, char(255)));
            changed[U5_WORLD_INDEX_OFFSET] = 1;
            changed[U5_WORLD_INDEX_OFFSET + 255] = 0;
            auto package = project.package();
            Project loaded;
            loaded.openGame(dir.path());
            loaded.importPackage(package);
            check(loaded.data("DATA.OVL") == changed, "World package round trip");
            // Optional bridge to save_slots_test's production world-loader test.
            if (qEnvironmentVariableIsSet("IMPERA_WORLD_TEST_PACKAGE"))
                file(qEnvironmentVariable("IMPERA_WORLD_TEST_PACKAGE"), package);
        });
        test("World resource validation and capability guidance", [] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray overlay(U5_WORLD_INDEX_OFFSET + 256, 0);
            std::memcpy(overlay.data() + U5_WORLD_INDEX_OFFSET, U5_BritanniaDefaultIndex, 256);
            file(dir.path() + "/DATA.OVL", overlay);
            file(dir.path() + "/BRIT.DAT", QByteArray(65536, 5));
            file(dir.path() + "/INIT.OOL", QByteArray(256, 0));
            file(dir.path() + "/UNDER.OOL", QByteArray(256, 0));
            project.openGame(dir.path());
            auto &changed = project.resources["DATA.OVL"].edited;
            changed.replace(U5_WORLD_INDEX_OFFSET, 256, QByteArray(256, char(255)));
            changed[U5_WORLD_INDEX_OFFSET] = 0;
            project.resources["BRIT.DAT"].edited = QByteArray(256, 7);
            project.validate();
            auto report = validateMod(project);
            check(std::none_of(report.begin(), report.end(), [](const auto &d) {
                return d.severity == ModDiagnostic::Error;
            }), "Paired Britannia map/index edit reported invalid");
            changed[U5_WORLD_INDEX_OFFSET] = 1;
            rejects([&] { project.validate(); });
            changed[U5_WORLD_INDEX_OFFSET] = 0;
            changed[0] = 1;
            project.resources["INIT.OOL"].edited[0] = 1;
            project.resources["UNDER.OOL"].edited[0] = 2;
            report = validateMod(project);
            check(std::any_of(report.begin(), report.end(), [](const auto &d) {
                return d.message.contains("outside the Britannia map index");
            }), "Unsupported overlay edits were not explained");
            check(std::any_of(report.begin(), report.end(), [](const auto &d) {
                return d.message.contains("UNDER.OOL overrides this");
            }), "Initial object precedence was not explained");
            project.resources["INIT.OOL"].edited.resize(255);
            rejects([&] { project.validate(); });
        });
        test("Add map items respects native identities, capacities and package persistence", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            p.resources["TOWNE.DAT"] = {QByteArray(16384, 5), QByteArray(16384, 5)};
            p.resources["TOWNE.NPC"] = {QByteArray(4608, 0), QByteArray(4608, 0)};
            p.resources["TOWNE.TLK"] = {QByteArray::fromHex("0000"), QByteArray::fromHex("0000")};
            auto seed = QByteArray(66, 0) + QByteArray::fromHex("ffff");
            p.resources["SIGNS.DAT"] = {seed, seed};
            p.resources["INIT.GAM"].edited[0x5b4] = char(255);
            auto install = [&](const Workshop::MapItemEdit &edit) {
                for (auto it = edit.changes.begin(); it != edit.changes.end(); ++it) {
                    p.resources[it.key()].edited = it.value();
                    if (it.key().endsWith(".OOL"))
                        p.resources[it.key()].included = true;
                }
            };
            auto npc = Workshop::addMapItem(p, "TOWNE.DAT", 0, {6, 7}, Workshop::MapItemKind::Npc,
                                            80, -1, "New friend");
            check(npc.slot == 1 && npc.changes.contains("TOWNE.TLK") &&
                      !U5::byte(npc.changes["TOWNE.NPC"], 512),
                  "NPC creation reused party slot or omitted conversation");
            install(npc);
            MapNpcDocument records(&p, "TOWNE.NPC");
            for (int slot = 0; slot < 4; slot++) {
                auto loc = records.location(0, 1, slot);
                check(loc.x == 6 && loc.y == 7 && loc.floor == 0 &&
                          records.hour(0, 1, slot) == slot * 6,
                      "New NPC destinations are uninitialized");
            }
            check(!(U5::byte(p.data("INIT.GAM"), 0x5b4) & 2), "New NPC retained a killed flag");
            int conversation = records.dialogue(0, 1);
            auto tlk = p.data("TOWNE.TLK");
            auto shared = Workshop::addMapItem(p, "TOWNE.DAT", 0, {6, 7},
                                               Workshop::MapItemKind::Npc, 84, conversation);
            check(shared.slot == 2 && !shared.changes.contains("TOWNE.TLK"),
                  "Sharing a conversation duplicated it");
            install(shared);
            auto monster = Workshop::addMapItem(p, "TOWNE.DAT", 0, {9, 8},
                                                Workshop::MapItemKind::Monster, 144);
            install(monster);
            check(records.location(0, monster.slot, 0).ai == 5 &&
                      records.dialogue(0, monster.slot) == 0,
                  "Settlement monster did not receive attack behavior");
            auto linked = Workshop::createNpcConversation(p, "TOWNE.NPC", 0, monster.slot,
                                                          "Friendly monster");
            const auto oldNpc = p.data("TOWNE.NPC");
            for (auto it = linked.begin(); it != linked.end(); ++it)
                p.resources[it.key()].edited = it.value();
            const int monsterConversation = records.dialogue(0, monster.slot);
            check(monsterConversation > 0 && monsterConversation != conversation &&
                      U5::readDialogue(p.data("TOWNE.TLK")).size() == 2,
                  "Monster conversation was not created with a free ID");
            auto expectedNpc = oldNpc;
            expectedNpc[records.dialogueOffset(0, monster.slot)] = char(monsterConversation);
            check(p.data("TOWNE.NPC") == expectedNpc,
                  "Creating dialogue changed monster behavior or appearance");
            p.resources["TOWNE.NPC"].edited = oldNpc;
            p.resources["TOWNE.TLK"].edited = tlk;
            auto sign =
                Workshop::addMapItem(p, "TOWNE.DAT", 0, {10, 11}, Workshop::MapItemKind::Sign);
            install(sign);
            check(MapDocument(&p, "TOWNE.DAT").terrain(0)[11 * 32 + 10] == char(0xf8),
                  "New sign has no visible sign tile");
            auto terrain = Workshop::moveSignTerrain(p, "TOWNE.DAT", 0, {10, 11}, {12, 13});
            check(terrain["TOWNE.DAT"][11 * 32 + 10] == 5 &&
                      terrain["TOWNE.DAT"][13 * 32 + 12] == char(0xf8),
                  "Moving sign failed to move its visible tile");
            rejects([&] {
                Workshop::addMapItem(p, "TOWNE.DAT", 0, {10, 11}, Workshop::MapItemKind::Sign);
            });
            auto container =
                Workshop::addMapItem(p, "TOWNE.DAT", 0, {14, 15}, Workshop::MapItemKind::Container);
            install(container);
            auto defs = Workshop::readContents(p.data("CONTENTS.OVR"));
            check(defs.size() == 1 && defs[0].action == 1 && defs[0].slot == 255 &&
                      p.data("TOWNE.DAT") == sign.changes["TOWNE.DAT"],
                  "Container changed terrain or lacks stable identity");
            auto hidden = Workshop::addMapItem(p, "TOWNE.DAT", 0, {16, 17},
                                               Workshop::MapItemKind::HiddenContents);
            install(hidden);
            rejects([&] {
                Workshop::addMapItem(p, "TOWNE.DAT", 0, {14, 15},
                                     Workshop::MapItemKind::HiddenContents);
            });
            p.validate();
            Project imported = p;
            for (auto it = imported.resources.begin(); it != imported.resources.end(); ++it) {
                it->edited = it->original;
                it->included = false;
            }
            imported.importPackage(p.package());
            check(imported.data("CONTENTS.OVR") == p.data("CONTENTS.OVR") &&
                      imported.data("TOWNE.NPC") == p.data("TOWNE.NPC") &&
                      imported.data("TOWNE.TLK") == tlk,
                  "Entity package roundtrip failed");
            auto deleted =
                Workshop::deleteMapItem(p, "TOWNE.DAT", 0, "NPCs / Schedules", 1, {6, 7});
            check(!U5::byte(deleted["TOWNE.NPC"], 513) && !deleted.contains("TOWNE.TLK"),
                  "Deleting shared NPC deleted its conversation");
            rejects(
                [&] { Workshop::deleteMapItem(p, "TOWNE.DAT", 0, "NPCs / Schedules", 0, {0, 0}); });
            for (int i = 1; i < 32; i++)
                p.resources["TOWNE.NPC"].edited[512 + i] = 80;
            auto before = p.data("TOWNE.NPC");
            auto additional =
                Workshop::addMapItem(p, "TOWNE.DAT", 0, {1, 1}, Workshop::MapItemKind::Npc);
            check(additional.slot == 32 && additional.changes["TOWNE.NPC"].left(4608) == before,
                  "Extended placement changed original NPC records");
            check(p.data("TOWNE.NPC") == before, "Placement preview committed its changes");
            install(additional);
            check(records.count(0) == 33 && records.location(0, 32, 0).x == 1 &&
                      records.location(0, 32, 0).y == 1,
                  "Extended NPC was not initialized");
            auto extraMonster = Workshop::addMapItem(p, "TOWNE.DAT", 0, {2, 1},
                                                     Workshop::MapItemKind::Monster, 216);
            install(extraMonster);
            check(extraMonster.slot == 33, "Expected extended monster slot");
            auto extraDialogue =
                Workshop::createNpcConversation(p, "TOWNE.NPC", 0, extraMonster.slot, "Daemon");
            for (auto it = extraDialogue.begin(); it != extraDialogue.end(); ++it)
                p.resources[it.key()].edited = it.value();
            check(records.dialogue(0, 33) > 0 &&
                      records.dialogue(0, 33) != records.dialogue(0, 32) &&
                      records.location(0, 33, 0).ai == 5,
                  "Extended monster dialogue changed another NPC or its behavior");
            rejects([&] { Workshop::createNpcConversation(p, "TOWNE.NPC", 0, 33, "Duplicate"); });
            p.validate();
            check(p.package().startsWith("IMOD0012"), "Extended NPC package lacks version gate");
        });
        test("Map positions preserve bytes, floors, entrance pairs, packages and native placement",
             [&] {
                 QTemporaryDir dir;
                 auto p = fixture(dir.path());

                 auto original = p.data("INIT.GAM");
                 auto start = Workshop::moveMapPosition(p, 0, 0, -1, {9, 10});
                 auto expected = original;
                 expected[0x2ed] = 0;
                 expected[0x2ef] = char(255);
                 expected[0x2f0] = expected[0x6b6] = 9;
                 expected[0x2f1] = expected[0x6b7] = 10;
                 expected[0x6b8] = char(255);
                 check(start["INIT.GAM"] == expected,
                       "Party placement changed unrelated starting state");
                 auto moon = Workshop::moveMapPosition(p, 3, 0, -1, {12, 13});
                 expected = original;
                 expected[0x28c] = 12;
                 expected[0x294] = 13;
                 expected[0x29c] = 0;
                 expected[0x2a4] = char(255);
                 check(moon["INIT.GAM"] == expected, "Moonstone placement changed another phase");

                 auto entrance = Workshop::moveMapPosition(p, 100, 0, 0, {20, 21});
                 check(Workshop::TableDocument::value(entrance["RULES.TBL"], 7, 0) == 20 &&
                           Workshop::TableDocument::value(entrance["RULES.TBL"], 8, 0) == 21,
                       "Entrance placement did not update both coordinates");
                 rejects([&] { Workshop::moveMapPosition(p, 100, 0, -1, {20, 21}); });
                 rejects([&] { Workshop::moveMapPosition(p, 1, 17, 0, {20, 21}); });
                 rejects([&] { Workshop::moveMapPosition(p, 0, 33, 0, {8, 7}); });
                 check(Workshop::moveMapPosition(p, 0, 33, 7, {6, 7})["INIT.GAM"][0x2ed] == 33,
                       "Dungeon party starts are missing");
                 p.resources["INIT.GAM"].edited = start["INIT.GAM"];
                 p.resources["RULES.TBL"].edited = entrance["RULES.TBL"];

                 auto package = p.package();
                 auto copy = fixture(dir.path());
                 copy.importPackage(package);
                 check(copy.data("INIT.GAM") == p.data("INIT.GAM") &&
                           copy.data("RULES.TBL") == p.data("RULES.TBL"),
                       "Position edits did not roundtrip a mod package");

                 Project graphical = p;
                 QByteArray world(65536, 1), brit(256, 1), overlay(0x3986, 0);
                 world[21 * 256 + 20] = char(0x14);
                 U5::writeWorld("BRIT.DAT", world, brit, overlay);
                 graphical.resources["BRIT.DAT"] = {brit, brit};
                 graphical.resources["DATA.OVL"] = {overlay, overlay};
                 auto relocated = Workshop::moveMapPosition(graphical, 100, 0, 0, {22, 23});
                 auto rebuilt =
                     U5::worldMap("BRIT.DAT", relocated["BRIT.DAT"], relocated["DATA.OVL"]);
                 auto expectedWorld = world;
                 expectedWorld[21 * 256 + 20] = 1;
                 expectedWorld[23 * 256 + 22] = char(0x14);
                 check(rebuilt == expectedWorld &&
                           Workshop::TableDocument::value(relocated["RULES.TBL"], 7, 0) == 22,
                       "Entrance landmark and coordinate did not move together");
                 auto coordinatesOnly =
                     Workshop::moveMapPosition(graphical, 100, 0, 0, {22, 23}, false);
                 check(!coordinatesOnly.contains("BRIT.DAT"),
                       "Coordinate-only entrance move repainted terrain");
                 auto collision =
                     Workshop::TableDocument::change(graphical.data("RULES.TBL"), 7, 1, 22);
                 collision = Workshop::TableDocument::change(collision, 8, 1, 23);
                 graphical.resources["RULES.TBL"].edited = collision;
                 rejects([&] { Workshop::moveMapPosition(graphical, 100, 0, 0, {22, 23}); });
                 check(graphical.data("BRIT.DAT") == brit && graphical.data("DATA.OVL") == overlay,
                       "Failed landmark placement changed terrain before validation");
                 U5::MapPage page{};
                 MapCanvas canvas;
                 QMap<QString, int> state{{"positions/selected", 3}};
                 std::unique_ptr<QWidget> panel(Workshop::mapPositionEditor(
                     &p, "UNDER.DAT", page, &canvas,
                     [&](const auto &changes, const QString &) {
                         for (auto it = changes.begin(); it != changes.end(); ++it)
                             p.resources[it.key()].edited = it.value();
                         return true;
                     },
                     &state, [](QPoint) {}));

                 panel->findChild<QPushButton *>("placeMapPosition")->click();
                 check(canvas.placing() && canvas.placeItem({14, 15}),
                       "Native moonstone placement failed");
                 check(U5::byte(p.data("INIT.GAM"), 0x28c) == 14 &&
                           U5::byte(p.data("INIT.GAM"), 0x294) == 15,
                       "Native placement missed the selected moonstone");

                 panel->findChild<QCheckBox *>("moonstoneCarried")->setChecked(true);
                 check(U5::byte(p.data("INIT.GAM"), 0x29c) == 255,
                       "Moonstone inventory toggle failed");
                 file(dir.path() + "/UNDER.DAT", QByteArray(65536, 1));
                 file(dir.path() + "/init.gam", start["INIT.GAM"]);

                 WorkshopWindow w;
                 check(w.openGame(dir.path()), "Position UI fixture failed");

                 w.selectResource("INIT.GAM");
                 w.findChild<QPushButton *>("showPartyStartMap")->click();
                 auto active = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
                 check(active->findChild<QWidget *>("mapPositionEditor"),
                       "Game Rules cannot open the party start on its map");
             });
        test("World and combat Add items reserve runtime slots and preserve metadata", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            p.resources["UNDER.DAT"] = {QByteArray(65536, 1), QByteArray(65536, 1)};
            for (auto r : {"INIT.OOL", "BRIT.OOL", "UNDER.OOL"})
                p.resources[r] = {QByteArray(256, 0), QByteArray(256, 0)};
            check(Workshop::availableMapItems(p, "UNDER.DAT").contains(Workshop::MapItemKind::Gold),
                  "World Add menu is missing Item");
            for (int shape : {2, 15}) {
                auto item = Workshop::addMapItem(p, "UNDER.DAT", 0, {4, 5},
                                                 shape == 2    ? Workshop::MapItemKind::Gold
                                                 : shape == 15 ? Workshop::MapItemKind::Food
                                                               : Workshop::MapItemKind::Moonstone);
                auto record = item.changes["UNDER.OOL"].mid(item.slot * 8, 8);
                check(item.mode == "Starting Objects" && U5::byte(record, 0) == shape &&
                          U5::byte(record, 1) == shape && U5::byte(record, 2) == 4 &&
                          U5::byte(record, 3) == 5 && U5::byte(record, 4) == 255 &&
                          U5::byte(record, 5) == (shape == 25 ? 0 : 1) &&
                          !item.changes.contains("UNDER.DAT"),
                      "Loose item placement corrupted type, properties, or terrain");
                Project edited = p;
                edited.resources["UNDER.OOL"].edited = item.changes["UNDER.OOL"];
                edited.resources["UNDER.OOL"].included = true;
                QMap<QString, int> state;
                std::function<bool()> flush;
                std::unique_ptr<QWidget> inspector(Workshop::worldObjectEditor(
                    &edited, 1,
                    [&](const QString &r, const QByteArray &bytes, bool included) {
                        edited.resources[r].edited = bytes;
                        edited.resources[r].included = included;
                        return true;
                    },
                    &state, &flush));
                auto value = inspector->findChild<QSpinBox *>("worldItemValue");
                check(value && !value->isHidden() && value->maximum() == (shape == 25 ? 7 : 255),
                      "Item property inspector has the wrong range");
                value->setValue(shape == 25 ? 7 : 42);
                check(U5::byte(edited.data("UNDER.OOL"), item.slot * 8 + 5) ==
                          (shape == 25 ? 7 : 42),
                      "Item property edit was not saved");
            }
            rejects([&] {
                Workshop::addMapItem(p, "UNDER.DAT", 0, {4, 5}, Workshop::MapItemKind::Npc, 144);
            });
            auto placedMoon = Workshop::addMapItem(p, "UNDER.DAT", 0, {6, 7},
                                                   Workshop::MapItemKind::Moonstone, 4);
            check(placedMoon.mode == "Positions" && !placedMoon.changes.contains("UNDER.OOL") &&
                      U5::byte(placedMoon.changes["INIT.GAM"], 0x28e) == 6 &&
                      U5::byte(placedMoon.changes["INIT.GAM"], 0x296) == 7 &&
                      U5::byte(placedMoon.changes["INIT.GAM"], 0x2a6) == 255,
                  "Add Moonstone did not use the buried stone's initial position");
            for (auto kind :
                 {Workshop::MapItemKind::Potion, Workshop::MapItemKind::Scroll,
                  Workshop::MapItemKind::Equipment, Workshop::MapItemKind::Gem,
                  Workshop::MapItemKind::Key, Workshop::MapItemKind::SkeletonKey,
                  Workshop::MapItemKind::Torch, Workshop::MapItemKind::SandalwoodBox,
                  Workshop::MapItemKind::MoldyCorpse, Workshop::MapItemKind::PirateShip,
                  Workshop::MapItemKind::Whirlpool, Workshop::MapItemKind::ShadowLord,
                  Workshop::MapItemKind::Shard, Workshop::MapItemKind::Crown,
                  Workshop::MapItemKind::Sceptre, Workshop::MapItemKind::QuestAmulet}) {
                auto item = Workshop::addMapItem(p, "UNDER.DAT", 0, {6, 7}, kind);
                check(U5::byte(item.changes["UNDER.OOL"], 8) == Workshop::mapItemShape(kind) &&
                          !item.changes.contains("UNDER.DAT"),
                      "Additional native item uses wrong artwork or paints terrain");
            }
            rejects([&] {
                Workshop::addMapItem(p, "UNDER.DAT", 0, {6, 7}, Workshop::MapItemKind::Potion, 8);
            });
            rejects([&] {
                Workshop::addMapItem(p, "UNDER.DAT", 0, {6, 7}, Workshop::MapItemKind::Equipment,
                                     48);
            });
            for (auto kind : {Workshop::MapItemKind::Potion, Workshop::MapItemKind::Equipment,
                              Workshop::MapItemKind::Scroll, Workshop::MapItemKind::SkeletonKey,
                              Workshop::MapItemKind::Shard, Workshop::MapItemKind::ShadowLord}) {
                auto item = Workshop::addMapItem(p, "UNDER.DAT", 0, {6, 7}, kind);
                Project edited = p;
                edited.resources["UNDER.OOL"].edited = item.changes["UNDER.OOL"];
                edited.resources["UNDER.OOL"].included = true;
                QMap<QString, int> state;
                std::function<bool()> flush;
                std::unique_ptr<QWidget> panel(Workshop::worldObjectEditor(
                    &edited, 1,
                    [&](const QString &r, const QByteArray &b, bool included) {
                        edited.resources[r].edited = b;
                        edited.resources[r].included = included;
                        return true;
                    },
                    &state, &flush));
                if (kind == Workshop::MapItemKind::SkeletonKey) {
                    auto count = panel->findChild<QSpinBox *>("worldItemValue");
                    check(panel->findChild<QCheckBox *>("worldSkeletonKey")->isChecked(),
                          "Skeleton key identity lost");
                    count->setValue(3);
                    check(U5::byte(edited.data("UNDER.OOL"), 13) == 131,
                          "Skeleton-key quantity lost its flag");
                } else {
                    auto variant = panel->findChild<QComboBox *>("worldItemVariant");
                    int id = kind == Workshop::MapItemKind::Potion   ? 7
                             : kind == Workshop::MapItemKind::Scroll ? 255
                             : kind == Workshop::MapItemKind::Shard ||
                                     kind == Workshop::MapItemKind::ShadowLord
                                 ? 2
                                 : 45;
                    variant->setCurrentIndex(variant->findData(id));
                    check(U5::byte(edited.data("UNDER.OOL"),
                                   kind == Workshop::MapItemKind::ShadowLord ? 15 : 13) ==
                              unsigned(id),
                          "Native item subtype edit failed");
                    if (kind == Workshop::MapItemKind::Equipment)
                        check(U5::byte(edited.data("UNDER.OOL"), 8) == 12,
                              "Equipment subtype did not update its artwork");
                }
            }
            QByteArray signSeed(67, 0);
            signSeed[66] = char(255);
            p.resources["SIGNS.DAT"] = {signSeed, signSeed};
            auto underSign =
                Workshop::addMapItem(p, "UNDER.DAT", 0, {18, 19}, Workshop::MapItemKind::Sign);
            auto records = Workshop::readSigns(underSign.changes["SIGNS.DAT"]);
            check(records.size() == 1 && records[0].map == 0 && records[0].floor == -1 &&
                      records[0].x == 18 && records[0].y == 19 &&
                      U5::byte(U5::worldMap("UNDER.DAT", underSign.changes["UNDER.DAT"], {}),
                               19 * 256 + 18) == 0xf8,
                  "Underworld sign placement is missing or uses the wrong world");
            auto sleeper =
                Workshop::addMapItem(p, "UNDER.DAT", 0, {4, 5}, Workshop::MapItemKind::Corpse);
            check(U5::byte(sleeper.changes["UNDER.OOL"], 8) == 30 &&
                      sleeper.mode == "Starting Objects",
                  "Corpse uses the wrong world object shape");
            auto horse =
                Workshop::addMapItem(p, "UNDER.DAT", 0, {2, 3}, Workshop::MapItemKind::Horse);
            check(horse.slot == 1 && U5::byte(horse.changes["UNDER.OOL"], 8) == 16 &&
                      U5::byte(horse.changes["UNDER.OOL"], 12) == 255,
                  "Horse shape or world is wrong");
            p.resources["UNDER.OOL"].edited = horse.changes["UNDER.OOL"];
            p.resources["UNDER.OOL"].included = true;
            for (int i = 1; i < 28; i++) {
                p.resources["UNDER.OOL"].edited[i * 8] = 16;
                p.resources["UNDER.OOL"].edited[i * 8 + 1] = 16;
                p.resources["UNDER.OOL"].edited[i * 8 + 4] = char(255);
            }
            rejects([&] {
                Workshop::addMapItem(p, "UNDER.DAT", 0, {2, 3}, Workshop::MapItemKind::Skiff);
            });
            QByteArray cbt(112 * 352, char(255));
            for (int page = 0; page < 112; page++)
                for (int i = 0; i < 16; i++)
                    cbt[page * 352 + 5 * 32 + 11 + i] = 0;
            p.resources["DUNGEON.CBT"] = {cbt, cbt};
            auto monster = Workshop::addMapItem(p, "DUNGEON.CBT", 4, {3, 4},
                                                Workshop::MapItemKind::Monster, 192);
            check(monster.slot == 6 &&
                      U5::byte(monster.changes["DUNGEON.CBT"], 4 * 352 + 5 * 32 + 11) == 192,
                  "Combat monster type wrong");
            check(monster.changes["DUNGEON.CBT"].left(4 * 352) == cbt.left(4 * 352) &&
                      monster.changes["DUNGEON.CBT"].mid(5 * 352) == cbt.mid(5 * 352),
                  "Adding monster changed other rooms");
            p.resources["DUNGEON.CBT"].edited = monster.changes["DUNGEON.CBT"];
            auto trigger =
                Workshop::addMapItem(p, "DUNGEON.CBT", 4, {5, 6}, Workshop::MapItemKind::Trigger);
            check(trigger.slot == 22 &&
                      U5::byte(trigger.changes["DUNGEON.CBT"], 4 * 352 + 8 * 32 + 11) == 5,
                  "Trigger was not initialized");
            auto remove = Workshop::deleteMapItem(p, "DUNGEON.CBT", 4, "Encounter", 6, {3, 4});
            check(!U5::byte(remove["DUNGEON.CBT"], 4 * 352 + 5 * 32 + 11) &&
                      U5::byte(remove["DUNGEON.CBT"], 4 * 352 + 6 * 32 + 11) == 255,
                  "Deleted monster remains active");
            rejects([&] { Workshop::deleteMapItem(p, "DUNGEON.CBT", 4, "Encounter", 0, {0, 0}); });
        });
        test("Add placement previews, repeated stamping, cancellation and map undo", [&] {
            MapCanvas canvas;
            canvas.side = 8;
            canvas.ids = QByteArray(64, 5);
            canvas.zoom = 1;
            canvas.resizeMap();
            int placed = 0;
            auto callback = [&](QPoint cell) {
                check(cell == QPoint(2, 3), "Placement cell wrong");
                placed++;
                return true;
            };
            auto mouse = [&](QEvent::Type type) {
                QPointF pos(40, 56);
                QMouseEvent e(type, pos, pos, Qt::LeftButton,
                              type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                              Qt::NoModifier);
                QApplication::sendEvent(&canvas, &e);
            };
            canvas.beginPlacement(-1, false, callback);
            mouse(QEvent::MouseMove);
            check(!placed, "Preview committed placement");
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &escape);
            mouse(QEvent::MouseButtonRelease);
            check(!placed && !canvas.placing(), "Cancelled placement committed");
            canvas.beginPlacement(-1, true, callback);
            mouse(QEvent::MouseButtonPress);
            mouse(QEvent::MouseButtonRelease);
            mouse(QEvent::MouseButtonPress);
            check(placed == 2 && canvas.placing() && canvas.ids == QByteArray(64, 5),
                  "Repeat placement paints terrain or stops prematurely");
            canvas.cancelStroke();
            canvas.beginPlacement(-1, false, callback);
            mouse(QEvent::MouseButtonPress);
            check(!canvas.placing(), "Single placement remained active");
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 5));
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Placement fixture failed");
            w.selectResource("TOWNE.DAT");
            w.show();
            app.processEvents();
            auto root = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            auto map = dynamic_cast<MapCanvas *>(root->findChild<QWidget *>("mapCanvas"));
            root->findChild<QComboBox *>("mapPaletteFilter")->setCurrentText("Special");
            auto palette = root->findChild<QListWidget *>("mapPalette");
            for (int i = 0; i < palette->count(); ++i)
                if (map->specialBrushKinds.value(palette->item(i)->data(Qt::UserRole).toInt(),
                                                 -1) == int(Workshop::MapItemKind::Container))
                    palette->setCurrentRow(i);
            check(map->specialBrushKinds.contains(map->brush),
                  "Special container brush is missing");
            QPointF pos(2.5 * 16 * map->zoom, 3.5 * 16 * map->zoom);
            QMouseEvent press(QEvent::MouseButtonPress, pos, pos, Qt::LeftButton, Qt::LeftButton,
                              Qt::NoModifier);
            QApplication::sendEvent(map, &press);
            auto defs = Workshop::readContents(w.projectForTests().data("CONTENTS.OVR"));
            check(defs.size() == 1 && defs[0].x == 2 && defs[0].y == 3 &&
                      root->findChild<QComboBox *>("contentsMode"),
                  "Add did not select container inspector");
            check(std::any_of(map->actors.begin(), map->actors.end(),
                              [](const auto &a) {
                                  return a.x == 2 && a.y == 3 && a.tile == 257 && a.label.isEmpty();
                              }),
                  "Placed chest has no visible chest artwork in its inspector");
            map->cancelStroke();
            for (auto action : w.findChildren<QAction *>())
                if (action->shortcut() == QKeySequence::Undo) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            check(w.projectForTests().data("CONTENTS.OVR").isEmpty(),
                  "Undo did not remove placed container");
        });
        test("Lossless dialogue structure", [] {
            auto bytes =
                U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry>"
                               "tele<Entry><Or><Entry>star<Entry>Hello<Label 1><Entry><Any><Label "
                               "1>Question?<Entry>No<Entry>y<Entry>Yes<End "
                               "Conversation><Entry><Any><Label 15>@");
            auto d = Dialogue::parse(bytes);
            check(d.bytes() == bytes, "Dialogue no-op changed bytes");
            check(d.structuralError.isEmpty(), "Structured parse failed");
            check(d.topics.size() == 1 && d.topics[0].keywords.size() == 2,
                  "Alias chain not grouped");
            check(d.questions.size() == 1 && d.tail >= 0, "Tail mistaken for question");
            check(d.questions[0].replies.size() == 1, "Reply missing");
            auto changed = Dialogue::replaceAliases(d, d.topics[0], {"moon", "sun"});
            auto next = Dialogue::parse(changed);
            check(next.entries[next.topics[0].response].bytes ==
                      d.entries[d.topics[0].response].bytes,
                  "Alias edit touched response");
            rejects([&] { Dialogue::removeQuestion(d, 1); });
            check(Dialogue::parse(Dialogue::addQuestion(d)).questions.size() == 2,
                  "Add question failed");
            auto malformed = QByteArray::fromHex("85b1");
            check(Dialogue::parse(malformed).bytes() == malformed, "Malformed bytes lost");
            auto operands = Dialogue::parse(QByteArray::fromHex("8c919100"));
            check(operands.bytes() == QByteArray::fromHex("8c919100"),
                  "Operand/control collision lost");
            Dialogue::Simulator sim(d, {});
            sim.start();
            auto lines = sim.respond("star");
            check(!lines.isEmpty(), "Simulation produced no lines");
            sim.respond("yes");
            check(sim.ended, "Question reply did not end conversation");
        });
        test("Dialogue semantics and engine keyword matching", [] {
            auto make = [](const QString &body) {
                return Dialogue::parse(U5::encodeText(
                    "Name<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry>" + body +
                    "<Any><Label 15>@"));
            };
            for (auto key : QStringList{"y", "tele", "planet", "password"})
                for (auto input : QStringList{"YES", "TELESCOPE", "PLANETS", "PASSWORD", "NO"}) {
                    auto encoded = Dialogue::encodeKeyword(key);
                    auto upper = input.toLatin1();
                    bool expected = WorkshopEngineMatch(reinterpret_cast<const unsigned char *>(
                                                            encoded.constData()),
                                                        upper.data()) == 0;
                    auto d = make(key + "<Entry>Matched<End Conversation><Entry>");
                    Dialogue::Simulator sim(d, {});
                    sim.start();
                    auto lines = sim.respond(input);
                    bool matched = false;
                    for (auto line : lines)
                        if (line.trace && line.text.startsWith("Matched keyword"))
                            matched = true;
                    check(expected == matched, "Sandbox keyword matching differs from engine");
                }
            auto d = make("help<Entry><Label 1><Entry><Any><Label "
                          "1>Question?<Entry>No<Entry>y<Entry><Gold><Byte 176><Byte "
                          "176><Byte 180>Thanks<End Conversation><Entry>");
            Dialogue::Sandbox state;
            state.gold = 3;
            Dialogue::Simulator poor(d, state);
            poor.start();
            poor.respond("help");
            poor.respond("yes");
            check(!poor.ended && poor.state.gold == 3,
                  "Insufficient payment changed state or ended conversation");
            state.gold = 10;
            Dialogue::Simulator rich(d, state);
            rich.start();
            rich.respond("help");
            rich.respond("yes");
            check(rich.ended && rich.state.gold == 6, "Payment did not use encoded amount");
            auto loop = make("help<Entry><Label 1><Entry><Any><Label 1><Label 1><Entry>No<Entry>");
            Dialogue::Simulator bounded(loop, {});
            bounded.start();
            bounded.respond("help");
            check(bounded.ended, "Recursive question was not bounded");
            check(Dialogue::parse(QByteArray::fromHex("8c919000")).bytes() ==
                      QByteArray::fromHex("8c919000"),
                  "Label-like operand changed");
            auto renamed = Dialogue::addQuestion(d);
            auto next = Dialogue::parse(renamed);
            auto removed = Dialogue::parse(Dialogue::removeQuestion(next, 1, 2));
            check(removed.question(1) < 0 && Dialogue::references(removed, 2).size() > 0,
                  "Retargeted deletion lost callers");
            rejects([] { Dialogue::encodeKeyword(" "); });
            auto missing = make("help<Entry><Label 8><Entry>");
            bool error = false;
            for (auto issue : missing.issues())
                error |= issue.error;
            check(error, "Missing question reference not diagnosed");
        });
        test("LZW width changes and dictionary resets", [] {
            std::mt19937 rng(1948);
            for (int size : {0, 1, 254, 255, 256, 257, 512, 4096, 65536, 150000}) {
                QByteArray bytes;
                for (int i = 0; i < size; i++)
                    bytes.append(char(rng()));
                auto compressed = U5::compress(bytes);
                check(U5::decompress(compressed) == bytes, "LZW roundtrip");
                void *output = nullptr;
                unsigned length = 0;
                check(WorkshopEngineDecode(compressed.constData(), compressed.size(), &output,
                                           &length),
                      "Engine decoder rejected native output");
                check(length == unsigned(bytes.size()) &&
                          QByteArray(static_cast<char *>(output), length) == bytes,
                      "Engine/native codec mismatch");
                free(output);
                check(U5::decompress(U5::compress(QByteArray(size, 'x'))) == QByteArray(size, 'x'),
                      "Repeated dictionary roundtrip");
            }
            rejects([] { U5::decompress(QByteArray::fromHex("0500000000")); });
        });
        test("Graphics palette, mask padding and roundtrip", [] {
            QByteArray raw(6, 0);
            U5::setWord(raw, 0, 1);
            U5::setWord(raw, 2, 6);
            raw += QByteArray::fromHex("09000100");
            raw += QByteArray(8, char(0x11));
            U5::setWord(raw, 4, raw.size());
            raw += QByteArray::fromHex("090001008000");
            auto g = U5::readGraphics("ITEMS.16", U5::compress(raw));
            check(qAlpha(g.images[0].pixel(0, 0)) == 0, "Mask alpha");
            check(qAlpha(g.images[0].pixel(8, 0)) == 255, "Padded alpha");
            check(U5::writeGraphics(g) == g.original, "Graphics no-op identity");
            g.images[0].setPixel(8, 0, qRgba(0, 0, 0, 0));
            auto next = U5::readGraphics("ITEMS.16", U5::writeGraphics(g));
            check(qAlpha(next.images[0].pixel(8, 0)) == 0, "Changed alpha");
            g.images[0].setPixel(0, 0, qRgba(1, 2, 3, 255));
            rejects([&] { U5::writeGraphics(g); });
        });
        test("Raw tiles and PNG alpha rejection", [] {
            auto g = U5::readGraphics("TILES.16", QByteArray(65536, 0));
            check(g.images.size() == 512, "Tile count");
            g.images[0].setPixel(0, 0, qRgba(0, 0, 0, 0));
            rejects([&] { U5::writeGraphics(g); });
        });
        test("Dialogue control operands and token identity", [] {
            QByteArray bytes = QByteArray::fromHex("0181c18285");
            rejects([&] { U5::decodeText(bytes); });
            bytes = QByteArray::fromHex("0181c18285b0b1b286ff8cfffe01ffc000909fc00000");
            check(U5::encodeText(U5::decodeText(bytes)) == bytes, "Dialogue exact byte identity");
            check(U5::encodeText("<thee><thee,><great><Great>") == QByteArray::fromHex("0d393861"),
                  "Canonical word indices");
            rejects([] { U5::encodeText("<Set Flag>"); });
            rejects([] { U5::encodeText("<Byte 133><Byte 0><Byte 1><Byte 2>"); });
        });
        test("TLK rebuild, tails, IDs and limits", [] {
            QVector<U5::Conversation> entries = {{1, QByteArray::fromHex("c100909fc000")},
                                                 {2, QByteArray::fromHex("c200")}};
            auto b = U5::writeDialogue(entries);
            auto decoded = U5::readDialogue(b);
            check(decoded.size() == 2 && decoded[0].bytes == entries[0].bytes, "TLK segments");
            entries[1].id = 1;
            rejects([&] { U5::writeDialogue(entries); });
            entries[1].id = 2;
            entries[1].bytes = QByteArray(70000, 'x');
            auto extended = U5::writeDialogue(entries);
            check(extended.startsWith("ITLK0001") &&
                      U5::readDialogue(extended)[1].bytes == entries[1].bytes,
                  "Extended dialogue bank lost long conversations");
            auto truncated = extended;
            truncated.chop(1);
            rejects([&] { U5::readDialogue(truncated); });
            auto overlap = extended;
            overlap[28] = char(1);
            overlap[29] = overlap[30] = overlap[31] = 0;
            rejects([&] { U5::readDialogue(overlap); });
        });
        test("Britannia implicit water and shared chunks", [] {
            QByteArray ovl(0x3986, 0);
            ovl.replace(0x3886, 256, QByteArray(256, char(255)));
            ovl[0x3886] = 0;
            ovl[0x3887] = 0;
            QByteArray chunks(256, char(2));
            auto map = U5::worldMap("BRIT.DAT", chunks, ovl);
            map[0] = 3;
            map[32] = 4;
            U5::writeWorld("BRIT.DAT", map, chunks, ovl);
            check(U5::worldMap("BRIT.DAT", chunks, ovl) == map,
                  "Shared chunks and water preserved");
            check((unsigned char)map[16] == 2, "Original alias unaffected");
        });
        test("Underworld extensions and settlement basement mapping", [] {
            QByteArray b(65536, 2);
            b.append(QByteArray(256, 9));
            auto map = U5::worldMap("UNDER.DAT", b, {});
            map[0] = 4;
            QByteArray unused;
            U5::writeWorld("UNDER.DAT", map, b, unused);
            check(b.right(256) == QByteArray(256, 9), "Underworld tail");
            auto pages = U5::mapPages("TOWNE.DAT", QByteArray(16384, 0));
            check(pages[6].floor == -1 && pages[6].settlement == 3, "Yew basement");
            auto keep = U5::mapPages("KEEP.DAT", QByteArray(16384, 0));
            check(keep[13].floor == -1 && keep[13].settlement == 7, "Serpent's Hold basement");
        });
        test("Fixed story windows and rejection", [] {
            auto offsets = U5::storyOffsets();
            QByteArray b(offsets.last() + 20, 0);
            for (int offset : offsets)
                b.replace(offset, 5, "HELLO");
            auto pages = U5::storyPages(b);
            check(pages.size() == 20, "Story pages");
            auto changed = U5::changeStory(b, 3, "HI");
            check(changed.size() == b.size() && U5::storyPages(changed)[4] == "HELLO",
                  "Fixed offsets");
            rejects([&] { U5::changeStory(b, 0, "TOO LONG"); });
        });
        test("Sparse packages, projects, checksum and original preservation", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            p.title = "Test mod";
            p.resources["INIT.GAM"].edited[0x202] = 9;
            auto bytes = p.package();
            check(bytes.size() < 150, "Sparse package");
            auto next = fixture(dir.path());
            next.importPackage(bytes);
            check(next.data("INIT.GAM") == p.data("INIT.GAM"), "Shared C decoder");
            p.save(dir.path() + "/mod.imperaproject");
            Project restored;
            restored.load(dir.path() + "/mod.imperaproject");
            check(restored.package() == bytes, "Project roundtrip");
            auto corrupt = bytes;
            corrupt[corrupt.size() - 1] ^= 1;
            rejects([&] { next.importPackage(corrupt); });
            check(next.data("INIT.GAM") == p.data("INIT.GAM"), "Rejected package changed project");
            next.resources["INIT.GAM"].original[0] = 1;
            rejects([&] { next.importPackage(bytes); });
            QFile f(dir.path() + "/init.gam");
            f.open(QIODevice::ReadOnly);
            check(f.readAll() == p.resources["INIT.GAM"].original, "Original file mutated");
        });
        test("Map document edits preserve floor bytes and combat metadata", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray settlement(16384, 4), combat(704, char(173));
            p.resources["TOWNE.DAT"] = {settlement, settlement};
            p.resources["TEST.CBT"] = {combat, combat};
            MapDocument town(&p, "TOWNE.DAT");
            auto cells = town.terrain(6);
            cells[45] = 9;
            auto changed = town.changes(6, cells)["TOWNE.DAT"];
            check(changed[6 * 1024 + 45] == 9 &&
                      changed.left(6 * 1024) == settlement.left(6 * 1024),
                  "Settlement floor changed unrelated data");
            MapDocument encounter(&p, "TEST.CBT");
            auto terrain = encounter.terrain(1);
            terrain[2 * 11 + 3] = 8;
            auto after = encounter.changes(1, terrain)["TEST.CBT"];
            for (int i = 0; i < combat.size(); ++i)
                check(after[i] == (i == 352 + 2 * 32 + 3 ? char(8) : combat[i]),
                      "Combat terrain damaged metadata");
            rejects([&] { encounter.changes(1, QByteArray(120, 0)); });
            check(p.changed().isEmpty(), "Map proposals mutated project before commitment");
            QByteArray ovl(0x3986, 0);
            ovl.replace(0x3886, 256, QByteArray(256, char(255)));
            p.resources["DATA.OVL"] = {ovl, ovl};
            p.resources["BRIT.DAT"] = {{}, {}};
            MapDocument world(&p, "BRIT.DAT");
            auto water = world.terrain(0);
            water[0] = 2;
            auto pair = world.changes(0, water);
            check(pair.size() == 2 &&
                      U5::worldMap("BRIT.DAT", pair["BRIT.DAT"], pair["DATA.OVL"]) == water,
                  "World proposal lost paired-resource edit");
            auto tooMany = water;
            for (int i = 0; i < 256; ++i) {
                int off = (i / 16 * 16) * 256 + (i % 16 * 16);
                tooMany[off] = char(i);
                tooMany[off + 1] = char(3);
            }
            rejects([&] { world.changes(0, tooMany); });
            check(p.changed().isEmpty(), "Failed capacity check modified project");
        });
        test("Continuous map gestures, boundary picking and cancellation", [&] {
            MapCanvas canvas;
            canvas.side = 16;
            canvas.zoom = 1;
            canvas.ids = QByteArray(256, 0);
            canvas.brush = 7;
            canvas.resizeMap();
            int commits = 0;
            canvas.commit = [&](const QByteArray &) {
                ++commits;
                return true;
            };
            auto mouse = [&](QEvent::Type type, QPointF pos, Qt::MouseButton button,
                             Qt::MouseButtons buttons) {
                QMouseEvent event(type, pos, pos, button, buttons, Qt::NoModifier);
                QApplication::sendEvent(&canvas, &event);
            };
            mouse(QEvent::MouseButtonPress, {8, 8}, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseMove, {248, 248}, Qt::NoButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {248, 248}, Qt::LeftButton, Qt::NoButton);
            check(commits == 1, "Continuous drag committed multiple commands");
            for (int i = 0; i < 16; ++i)
                check(canvas.ids[i * 16 + i] == 7, "Fast diagonal stroke has gaps");
            auto original = canvas.ids;
            mouse(QEvent::MouseButtonPress, {-0.1, 8}, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {-0.1, 8}, Qt::LeftButton, Qt::NoButton);
            check(canvas.ids == original && commits == 1, "Outside click changed edge cell");
            mouse(QEvent::MouseButtonPress, {24, 8}, Qt::LeftButton, Qt::LeftButton);
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &escape);
            mouse(QEvent::MouseButtonRelease, {24, 8}, Qt::LeftButton, Qt::NoButton);
            check(canvas.ids == original && commits == 1, "Escape did not discard whole gesture");
            mouse(QEvent::MouseButtonPress, {24, 8}, Qt::LeftButton, Qt::LeftButton);
            QFocusEvent lost(QEvent::FocusOut);
            QApplication::sendEvent(&canvas, &lost);
            check(canvas.ids == original && !canvas.painting(),
                  "Lost focus left uncommitted painting");
            mouse(QEvent::MouseButtonPress, {24, 8}, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {500, 500}, Qt::LeftButton, Qt::NoButton);
            check(commits == 2 && !canvas.painting(), "Release outside did not end stroke");
            original = canvas.ids;
            canvas.commit = [&](const QByteArray &) { return false; };
            mouse(QEvent::MouseButtonPress, {40, 8}, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {40, 8}, Qt::LeftButton, Qt::NoButton);
            check(canvas.ids == original, "Rejected edit left painted cells visible");
            int picked = -1;
            canvas.pick = [&](int id) { picked = id; };
            mouse(QEvent::MouseButtonPress, {8, 8}, Qt::RightButton, Qt::RightButton);
            check(picked == 7 && canvas.ids == original, "Eyedropper painted terrain");
            int hovered = -1;
            canvas.inspect = [&](int x, int, int) { hovered = x; };
            mouse(QEvent::MouseMove, {40, 8}, Qt::NoButton, Qt::NoButton);
            check(hovered == 2, "Hover requires a pressed button");
            canvas.zoom = 0.35;
            canvas.resizeMap();
            check(canvas.cellAt({16 * 0.35 * 3 + 0.1, 16 * 0.35 * 5 + 0.1}) == QPoint(3, 5),
                  "Fractional zoom picking disagrees with canvas");
        });
        test("Terrain selection, rectangle previews and bounded iterative fill", [&] {
            MapCanvas canvas;
            canvas.side = 8;
            canvas.zoom = 1;
            canvas.ids = QByteArray(64, 0);
            canvas.brush = 7;
            canvas.resizeMap();
            int commits = 0;
            canvas.commit = [&](const QByteArray &) {
                ++commits;
                return true;
            };
            auto mouse = [&](QEvent::Type type, int x, int y, Qt::MouseButtons buttons) {
                QPointF pos(x * 16 + 8, y * 16 + 8);
                QMouseEvent event(type, pos, pos,
                                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                                  buttons, Qt::NoModifier);
                QApplication::sendEvent(&canvas, &event);
            };
            auto drag = [&](int x1, int y1, int x2, int y2) {
                mouse(QEvent::MouseButtonPress, x1, y1, Qt::LeftButton);
                mouse(QEvent::MouseMove, x2, y2, Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease, x2, y2, Qt::NoButton);
            };
            canvas.tool = MapCanvas::Select;
            drag(4, 4, 2, 2);
            check(canvas.selection == QRect(2, 2, 3, 3) && commits == 0 &&
                      canvas.ids == QByteArray(64, 0),
                  "Reverse selection changed terrain or bounds");
            mouse(QEvent::MouseButtonPress, 0, 0, Qt::LeftButton);
            QFocusEvent focusOut(QEvent::FocusOut);
            QApplication::sendEvent(&canvas, &focusOut);
            check(canvas.selection == QRect(2, 2, 3, 3), "Cancelled selection lost prior bounds");
            canvas.clearSelection();
            canvas.tool = MapCanvas::Rectangle;
            mouse(QEvent::MouseButtonPress, 1, 1, Qt::LeftButton);
            mouse(QEvent::MouseMove, 4, 4, Qt::LeftButton);
            check(commits == 0 && canvas.ids[4 * 8 + 4] == 7, "Rectangle committed before release");
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &escape);
            check(canvas.ids == QByteArray(64, 0), "Rectangle cancellation did not roll back");
            canvas.outline = true;
            drag(4, 4, 1, 1);
            check(commits == 1 && canvas.ids[2 * 8 + 2] == 0 && canvas.ids[1 * 8 + 2] == 7,
                  "Outline rectangle filled its interior or split history");
            drag(4, 4, 1, 1);
            check(commits == 1, "No-op rectangle added history");
            canvas.selection = QRect(2, 2, 2, 2);
            canvas.tool = MapCanvas::Fill;
            canvas.brush = 9;
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            check(commits == 2 && canvas.ids[3 * 8 + 3] == 9 && canvas.ids[2 * 8 + 4] == 7 &&
                      canvas.ids[5 * 8 + 5] == 0,
                  "Fill escaped selection or crossed a barrier");
            canvas.clearSelection();
            canvas.ids = QByteArray(64, 1);
            canvas.ids[0] = canvas.ids[9] = 0;
            mouse(QEvent::MouseButtonPress, 0, 0, Qt::LeftButton);
            check(canvas.ids[0] == 9 && canvas.ids[9] == 0,
                  "Fill connected diagonal cells or wrapped edges");
            canvas.side = 256;
            canvas.ids = QByteArray(65536, 0);
            mouse(QEvent::MouseButtonPress, 0, 0, Qt::LeftButton);
            check(canvas.ids == QByteArray(65536, 9), "World-sized fill failed");
        });
        test("Typed terrain clipboard, paste preview, cancellation and bounds", [&] {
            MapCanvas source, target;
            source.side = 8;
            source.ids = QByteArray(64, 0);
            source.ids[9] = 5;
            source.ids[10] = 6;
            source.ids[17] = 7;
            source.ids[18] = 8;
            source.selection = QRect(1, 1, 2, 2);
            check(source.copySelection(), "Terrain copy failed");
            target.side = 11;
            target.zoom = 1;
            target.ids = QByteArray(121, 0);
            target.resizeMap();
            target.actors.append({3, 3, 256, 2});
            int commits = 0;
            target.commit = [&](const QByteArray &) {
                ++commits;
                return true;
            };
            auto mouse = [&](QEvent::Type type, int x, int y) {
                QPointF pos(x * 16 + 8, y * 16 + 8);
                QMouseEvent event(
                    type, pos, pos, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                    type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&target, &event);
            };
            check(target.beginPaste(), "Cross-map paste did not read typed clipboard");
            mouse(QEvent::MouseMove, 3, 3);
            check(target.pasteChangedCells() == 4 && target.ids == QByteArray(121, 0) &&
                      commits == 0,
                  "Paste preview modified destination");
            mouse(QEvent::MouseButtonPress, 10, 10);
            check(target.pasting() && commits == 0 && target.ids == QByteArray(121, 0),
                  "Out-of-bounds paste was silently clipped");
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&target, &escape);
            check(!target.pasting() && target.ids == QByteArray(121, 0),
                  "Paste cancellation changed data");
            target.beginPaste();
            target.tool = MapCanvas::Eyedropper;
            mouse(QEvent::MouseButtonPress, 3, 3);
            check(commits == 1 && target.ids[3 * 11 + 3] == 5 && target.ids[4 * 11 + 4] == 8 &&
                      target.actors.size() == 1 && target.actors[0].npc == 2,
                  "Paste failed or copied actors");
            auto previous = target.ids;
            target.commit = [&](const QByteArray &) { return false; };
            target.beginPaste();
            mouse(QEvent::MouseButtonPress, 5, 5);
            check(target.ids == previous, "Rejected paste left preview terrain behind");
            auto mime = new QMimeData;
            mime->setData("application/x-impera-terrain",
                          QByteArray("IMPTILE1", 8) + QByteArray(4, char(255)));
            QApplication::clipboard()->setMimeData(mime);
            check(!target.beginPaste(), "Malformed clipboard dimensions accepted");
            QApplication::clipboard()->setText("Not terrain");
            check(!target.beginPaste(), "Plain text mistaken for terrain");
        });
        test("World paste capacity failure is atomic and combat paste preserves records", [&] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray overlay(0x3986, 0);
            overlay.replace(0x3886, 256, QByteArray(256, char(255)));
            project.resources["DATA.OVL"] = {overlay, overlay};
            project.resources["BRIT.DAT"] = {{}, {}};
            MapDocument world(&project, "BRIT.DAT");
            MapCanvas stamp;
            stamp.side = 256;
            stamp.ids = world.terrain(0);
            for (int i = 0; i < 256; ++i) {
                int offset = (i / 16 * 16) * 256 + i % 16 * 16;
                stamp.ids[offset] = char(i);
                stamp.ids[offset + 1] = 3;
            }
            stamp.selection = QRect(0, 0, 256, 256);
            stamp.copySelection();
            MapCanvas target;
            target.side = 256;
            target.zoom = 1;
            target.ids = world.terrain(0);
            auto before = target.ids;
            bool rejected = false;
            target.commit = [&](const QByteArray &cells) {
                try {
                    world.changes(0, cells);
                } catch (const std::exception &) {
                    rejected = true;
                    return false;
                }
                return true;
            };
            target.beginPaste();
            QMouseEvent place(QEvent::MouseButtonPress, {8, 8}, {8, 8}, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&target, &place);
            check(rejected && target.ids == before && project.changed().isEmpty() &&
                      project.data("DATA.OVL") == overlay,
                  "Over-capacity paste mutated paired resources");
            QByteArray combat(352, char(173));
            project.resources["TEST.CBT"] = {combat, combat};
            MapDocument encounter(&project, "TEST.CBT");
            stamp.side = 8;
            stamp.ids = QByteArray(64, 9);
            stamp.selection = QRect(0, 0, 2, 2);
            stamp.copySelection();
            target.side = 11;
            target.ids = encounter.terrain(0);
            target.commit = [&](const QByteArray &cells) {
                project.resources["TEST.CBT"].edited = encounter.changes(0, cells)["TEST.CBT"];
                return true;
            };
            target.beginPaste();
            QApplication::sendEvent(&target, &place);
            auto after = project.data("TEST.CBT");
            for (int i = 0; i < 352; ++i)
                check(after[i] == (i == 0 || i == 1 || i == 32 || i == 33 ? char(9) : combat[i]),
                      "Combat paste touched metadata or padding");
            check(encounter.terrain(0, true) == QByteArray(121, char(173)),
                  "Original comparison reads edited terrain");
        });
        test("Pointer-anchored map zoom and read-only panning", [&] {
            auto canvas = new MapCanvas;
            canvas->side = 32;
            canvas->zoom = 2;
            canvas->ids = QByteArray(1024, 0);
            canvas->resizeMap();
            MapView view(canvas);
            view.resize(460, 380);
            view.show();
            app.processEvents();
            view.centerMap({16, 16});
            QPoint anchor(130, 90);
            auto before = QPointF(canvas->mapFrom(view.viewport(), anchor)) / (16 * canvas->zoom);
            view.setZoom(4, anchor);
            app.processEvents();
            auto after = QPointF(canvas->mapFrom(view.viewport(), anchor)) / (16 * canvas->zoom);
            check(QLineF(before, after).length() < 0.03, "Zoom moved map point under pointer");
            auto original = canvas->ids;
            int old = view.horizontalScrollBar()->value();
            auto pan = [&](QEvent::Type type, QPointF pos, Qt::MouseButton button,
                           Qt::MouseButtons buttons) {
                QMouseEvent event(type, pos, pos, button, buttons, Qt::NoModifier);
                QApplication::sendEvent(canvas, &event);
            };
            pan(QEvent::MouseButtonPress, {100, 100}, Qt::MiddleButton, Qt::MiddleButton);
            pan(QEvent::MouseMove, {140, 100}, Qt::NoButton, Qt::MiddleButton);
            pan(QEvent::MouseButtonRelease, {140, 100}, Qt::MiddleButton, Qt::NoButton);
            check(view.horizontalScrollBar()->value() == old - 40 && canvas->ids == original,
                  "Pan painted or failed to move view");
            auto centered = view.mapCenter();
            for (int i = 0; i < 10; ++i) {
                view.resize(461, 397);
                app.processEvents();
                view.resize(460, 380);
                app.processEvents();
            }
            check(QLineF(centered, view.mapCenter()).length() < 0.01,
                  "Resizing the resource layout drifts the map view");
            view.fitMap();
            app.processEvents();
            check(view.fitting && canvas->width() <= view.viewport()->width() &&
                      canvas->height() <= view.viewport()->height(),
                  "Fit clipped map");
        });
        test("Map tile picker width survives switching maps and workspaces", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 5));
            file(dir.path() + "/KEEP.DAT", QByteArray(16384, 5));
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Panel width fixture load");
            window.resize(1400, 900);
            window.show();
            window.selectResource("TOWNE.DAT");
            app.processEvents();
            auto splitter = [&] {
                auto active = window.findChild<QStackedWidget *>("workspaces")->currentWidget();
                return active->findChild<QSplitter *>("mapPanelSplitter");
            };
            auto split = splitter();
            check(split, "Map panel splitter missing");
            // Finish the deferred workspace restore before simulating a user resize.
            // Native fonts and control metrics can make the center pane wider on Windows.
            bool settled = false;
            QTimer::singleShot(0, &window, [&] { settled = true; });
            waitFor([&] { return settled; });
            window.resize(qMax(window.width(), window.minimumSizeHint().width() + 400),
                          window.height());
            app.processEvents();
            split = splitter();
            const auto sizes = split->sizes();
            int available = 0;
            for (int size : sizes)
                available += size;
            const int centerMinimum =
                qMax(split->widget(1)->minimumWidth(), split->widget(1)->minimumSizeHint().width());
            const int panelMinimum =
                qMax(split->widget(2)->minimumWidth(), split->widget(2)->minimumSizeHint().width());
            const int requested = qMin(panelMinimum + 100, available - centerMinimum);
            check(requested >= panelMinimum + 40,
                  "Panel width fixture has insufficient room to resize");
            // setSizes redistributes any surplus proportionally: request the actual total.
            split->setSizes({0, available - requested, requested});
            app.processEvents();
            auto canvas = dynamic_cast<MapCanvas *>(split->findChild<QWidget *>("mapCanvas"));
            auto filter = split->findChild<QComboBox *>("mapPaletteFilter");
            filter->setCurrentText("Locations");
            split->findChild<QListWidget *>("mapPalette")->setCurrentRow(5);
            split->findChild<QToolButton *>("mapTool6")->click();
            check(canvas->tool == MapCanvas::Fill, "Tool fixture failed");
            const int width = split->sizes().last();
            check(qAbs(width - requested) <= 2,
                  qPrintable(
                      QString("Panel resize was not applied: requested %1, got %2 (available %3, "
                              "center minimum %4, panel minimum %5)")
                          .arg(requested)
                          .arg(width)
                          .arg(available)
                          .arg(centerMinimum)
                          .arg(panelMinimum)));
            window.selectResource("KEEP.DAT");
            app.processEvents();
            waitFor([&] { return qAbs(splitter()->sizes().last() - width) <= 2; });
            check(qAbs(splitter()->sizes().last() - width) <= 2,
                  "Changing map resets the tile picker width");
            check(splitter()->findChild<QComboBox *>("mapPaletteFilter")->currentText() ==
                          "Locations" &&
                      dynamic_cast<MapCanvas *>(splitter()->findChild<QWidget *>("mapCanvas"))
                              ->brush == 5 &&
                      dynamic_cast<MapCanvas *>(splitter()->findChild<QWidget *>("mapCanvas"))
                              ->tool == MapCanvas::Fill,
                  "Changing maps resets filter, tool or tile");
            window.selectResource("TILES.16");
            app.processEvents();
            window.resize(window.width() + 140, window.height());
            app.processEvents();
            window.selectResource("TOWNE.DAT");
            app.processEvents();
            waitFor([&] { return qAbs(splitter()->sizes().last() - width) <= 2; });
            check(qAbs(splitter()->sizes().last() - width) <= 2,
                  "Returning from another workspace resets the tile picker width");
        });
        test("Map workspace brush, navigation and undo preserve view state", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Map view fixture load");
            w.show();
            w.selectResource("TOWNE.DAT");
            app.processEvents();
            auto active = [&] {
                return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto findCanvas = [&] {
                return dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            };
            check(!active()->findChild<QToolButton *>("mapAddMenu"),
                  "Add menu remains after Special filter migration");
            auto palette = active()->findChild<QListWidget *>("mapPalette");
            palette->setCurrentRow(9);

            active()->findChild<QComboBox *>("mapZoom")->setCurrentIndex(4);
            auto view = dynamic_cast<MapView *>(active()->findChild<QScrollArea *>("canvasView"));
            view->centerMap({12, 13});
            auto center = view->mapCenter();
            auto canvas = findCanvas();
            canvas->selection = QRect(0, 0, 3, 3);
            canvas->selectionChanged();
            active()->findChild<QCheckBox *>("mapComparison")->setChecked(true);
            QMouseEvent down(QEvent::MouseButtonPress, {40, 40}, {40, 40}, Qt::LeftButton,
                             Qt::LeftButton, Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease, {40, 40}, {40, 40}, Qt::LeftButton,
                           Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &down);
            QApplication::sendEvent(canvas, &up);
            check(U5::byte(w.projectForTests().data("TOWNE.DAT"), 0) == 9,
                  "Selected brush did not paint");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents(); // Rebuild and queued viewport restoration use separate event
                                 // turns.
            canvas = findCanvas();
            view = dynamic_cast<MapView *>(active()->findChild<QScrollArea *>("canvasView"));
            check(canvas->selection == QRect(0, 0, 3, 3) && canvas->comparison &&
                      canvas->brush == 9 && !canvas->grid && canvas->zoom == 4 &&
                      QLineF(center, view->mapCenter()).length() < 0.1,
                  "Undo reset view settings");
            check(w.projectForTests().data("TOWNE.DAT") == QByteArray(16384, 0),
                  "Undo failed terrain restoration");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Redo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            view = dynamic_cast<MapView *>(active()->findChild<QScrollArea *>("canvasView"));
            check(U5::byte(w.projectForTests().data("TOWNE.DAT"), 0) == 9 &&
                      QLineF(center, view->mapCenter()).length() < 0.1,
                  "Redo lost terrain or view center");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            auto page = active()->findChild<QComboBox *>("mapPage");
            page->setCurrentIndex(page->findData(1));
            app.processEvents();
            active()->findChild<QListWidget *>("mapPalette")->setCurrentRow(11);
            page->setCurrentIndex(0);
            app.processEvents();
            check(findCanvas()->brush == 11, "Floor navigation reset the shared brush");
            auto filter = active()->findChild<QComboBox *>("mapPaletteFilter");
            auto tiles = active()->findChild<QListWidget *>("mapPalette");
            filter->setCurrentText("Locations");
            check(!tiles->item(0)->isHidden() && tiles->item(9)->isHidden() &&
                      tiles->item(0x27)->isHidden() && findCanvas()->brush == 11,
                  "Usage filter must reflect restored maps without resetting the brush");
            filter->setCurrentIndex(0);
            auto mini = active()->findChild<QWidget *>("mapMinimap");
            view = dynamic_cast<MapView *>(active()->findChild<QScrollArea *>("canvasView"));
            auto oldCenter = view->mapCenter();
            auto pos = QPointF(mini->width() / 2, mini->height() / 2);
            QMouseEvent miniClick(QEvent::MouseButtonPress, pos, pos, Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(mini, &miniClick);
            check(QLineF(oldCenter, view->mapCenter()).length() > 1 &&
                      w.projectForTests().changed().isEmpty(),
                  "Minimap failed read-only navigation");

            QMouseEvent pick(QEvent::MouseButtonPress, {40, 40}, {40, 40}, Qt::RightButton,
                             Qt::RightButton, Qt::NoModifier);
            QApplication::sendEvent(findCanvas(), &pick);
            check(active()->findChild<QListWidget *>("mapPalette")->currentRow() == 0 &&
                      active()->findChild<QLabel *>("mapBrushLabel")->text().contains("Tile 0"),
                  "Eyedropper and palette disagree");
            w.selectResource("TILES.16");
            app.processEvents();
            auto back = active()->findChild<QPushButton *>("backToMap");
            check(back, "Linked workspace lacks return to map");
            back->click();
            app.processEvents();
            check(!findCanvas()->grid && findCanvas()->zoom == 4 && findCanvas()->brush == 0,
                  "Return to map lost view state");
            check(w.projectForTests().changed().isEmpty(),
                  "Navigation or view settings changed resources");
        });
        test("Native terrain fill and paste each undo as one operation", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Native terrain fixture failed");
            window.show();
            window.selectResource("TOWNE.DAT");
            app.processEvents();
            auto active = [&] {
                return window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto canvas = [&] {
                return dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            };
            auto click = [&](int x, int y) {
                QPointF point((x + 0.5) * 16 * canvas()->zoom, (y + 0.5) * 16 * canvas()->zoom);
                QMouseEvent down(QEvent::MouseButtonPress, point, point, Qt::LeftButton,
                                 Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas(), &down);
                QMouseEvent up(QEvent::MouseButtonRelease, point, point, Qt::LeftButton,
                               Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(canvas(), &up);
            };
            auto history = [&](const QString &prefix) {
                bool triggered = false;
                for (auto action : window.findChildren<QAction *>())
                    if (action->text().startsWith(prefix + " ") && action->isEnabled()) {
                        action->trigger();
                        triggered = true;
                        break;
                    }
                check(triggered, "Missing terrain history command");
                app.processEvents();
                app.processEvents();
            };
            active()->findChild<QListWidget *>("mapPalette")->setCurrentRow(9);
            canvas()->selection = QRect(1, 1, 2, 2);
            canvas()->selectionChanged();
            active()->findChild<QButtonGroup *>("mapTools")->button(MapCanvas::Fill)->click();
            click(1, 1);
            auto filled = window.projectForTests().data("TOWNE.DAT");
            check(filled.count(char(9)) == 4, "Native fill escaped selection");
            history("Undo");
            check(window.projectForTests().data("TOWNE.DAT") == QByteArray(16384, 0),
                  "Fill did not undo atomically");
            history("Redo");
            check(window.projectForTests().data("TOWNE.DAT") == filled &&
                      canvas()->selection == QRect(1, 1, 2, 2),
                  "Fill redo lost data or selection");
            active()->findChild<QToolButton *>("mapCopy")->click();
            active()->findChild<QToolButton *>("mapClearSelection")->click();
            active()->findChild<QToolButton *>("mapPaste")->click();
            check(canvas()->pasting(), "Toolbar did not start paste");
            click(6, 6);
            check(window.projectForTests().data("TOWNE.DAT").count(char(9)) == 8,
                  "Native paste failed");
            history("Undo");
            check(window.projectForTests().data("TOWNE.DAT") == filled,
                  "Paste did not undo as one operation");
            history("Undo");
            check(window.projectForTests().changed().isEmpty(),
                  "Terrain history did not return to original");
        });
        test("NPC document shared slots, signed floors and exact byte preservation", [&] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray bytes(4608, char(173));
            project.resources["TOWNE.NPC"] = {bytes, bytes};
            MapNpcDocument npc(&project, "TOWNE.NPC");
            int settlement = 3, actor = 7, base = settlement * 576 + actor * 16;
            auto moved = npc.move(settlement, actor, 3, {4, 5, -1, 0});
            for (int i = 0; i < bytes.size(); ++i)
                check(moved[i] == (i == base + 4    ? char(4)
                                   : i == base + 7  ? char(5)
                                   : i == base + 10 ? char(255)
                                                    : bytes[i]),
                      "NPC move touched unrelated bytes");
            project.resources["TOWNE.NPC"].edited = moved;
            auto first = npc.location(settlement, actor, 1),
                 third = npc.location(settlement, actor, 3);
            check(first.x == 4 && third.x == 4 && first.floor == -1 && third.floor == -1 &&
                      first.ai == 173 && npc.hour(settlement, actor, 3) == 173,
                  "Shared schedule or unusual values were normalized");
            rejects([&] { npc.move(settlement, actor, 0, {32, 5, 0, 0}); });
            rejects([&] { npc.move(settlement, actor, 0, {1, 5, -129, 0}); });
            rejects([&] { npc.location(8, actor, 0); });
            rejects([&] { npc.location(settlement, 32, 0); });
            rejects([&] { npc.location(settlement, actor, 4); });
        });
        test("NPC drag preview, overlapping selection and cancellation protect terrain", [&] {
            MapCanvas canvas;
            canvas.tool = MapCanvas::InspectNpc;
            canvas.side = 32;
            canvas.zoom = 1;
            canvas.ids = QByteArray(1024, 4);
            canvas.actors = {{2, 2, 256, 1}, {2, 2, 257, 2}};
            canvas.resizeMap();
            QList<int> choices;
            int moved = 0, selected = -1;
            QPoint destination;
            canvas.chooseActor = [&](const QList<int> &actors) {
                choices = actors;
                return 2;
            };
            canvas.chooseNpc = [&](int npc) { selected = npc; };
            canvas.moveNpc = [&](int npc, QPoint cell) {
                check(npc == 2, "Moved wrong overlapping actor");
                ++moved;
                destination = cell;
                return true;
            };
            auto mouse = [&](QEvent::Type type, int x, int y, Qt::MouseButtons buttons) {
                QPointF pos(x * 16 + 8, y * 16 + 8);
                QMouseEvent event(type, pos, pos,
                                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                                  buttons, Qt::NoModifier);
                QApplication::sendEvent(&canvas, &event);
            };
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            check(!canvas.painting(), "Overlap chooser left a drag active after modal release");
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            mouse(QEvent::MouseMove, 5, 6, Qt::LeftButton);
            check(selected == 2 && choices == QList<int>({1, 2}) && moved == 0 &&
                      canvas.actors[1].x == 2,
                  "NPC preview changed records or ignored overlap chooser");
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &escape);
            mouse(QEvent::MouseButtonRelease, 5, 6, Qt::NoButton);
            check(moved == 0 && !canvas.painting(), "Escape committed NPC movement");
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            QFocusEvent focus(QEvent::FocusOut);
            QApplication::sendEvent(&canvas, &focus);
            mouse(QEvent::MouseButtonRelease, 5, 6, Qt::NoButton);
            check(moved == 0, "Focus loss committed NPC movement");
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, -1, -1, Qt::NoButton);
            check(moved == 0, "Outside NPC destination accepted");
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, 5, 6, Qt::NoButton);
            check(moved == 1 && destination == QPoint(5, 6) && canvas.ids == QByteArray(1024, 4),
                  "NPC move changed terrain or split its transaction");
            check(!canvas.beginPaste(), "NPC mode permitted terrain paste");
        });
        test("Native NPC schedules, floors, undo and conversation return", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            QByteArray terrain(16384, 4), npcs(4608, char(73));
            for (int settlement = 0; settlement < 8; ++settlement)
                for (int npc = 0; npc < 32; ++npc) {
                    int base = settlement * 576 + npc * 16;
                    for (int loc = 0; loc < 3; ++loc) {
                        npcs[base + 3 + loc] = char(255);
                        npcs[base + 6 + loc] = char(255);
                        npcs[base + 9 + loc] = 0;
                    }
                    npcs[settlement * 576 + 512 + npc] = 1;
                    npcs[settlement * 576 + 544 + npc] = char(255);
                }
            int base = 16;
            for (int loc = 0; loc < 3; ++loc) {
                npcs[base + 3 + loc] = char(2 + loc);
                npcs[base + 6 + loc] = char(2 + loc);
            }
            npcs[base + 10] = 1;
            npcs[544 + 1] = 37;
            // Yew's basement and shared record are exercised independently.
            int yew = 3 * 576 + 16;
            npcs[yew + 5] = 4;
            npcs[yew + 8] = 5;
            npcs[yew + 11] = char(255);
            file(dir.path() + "/TOWNE.DAT", terrain);
            file(dir.path() + "/TOWNE.NPC", npcs);
            file(dir.path() + "/TOWNE.TLK",
                 U5::writeDialogue(
                     {{37,
                       U5::encodeText("Chamfort the traveller with an unusually long but valid "
                                      "character name<Entry>Description<Entry>Greeting<Entry>Job<"
                                      "Entry>Bye<Entry><Any><Label 15>@")}}));
            WorkshopWindow window;
            check(window.openGame(dir.path()), "NPC fixture load failed");
            window.show();
            window.selectResource("TOWNE.DAT");
            app.processEvents();
            auto active = [&] {
                return window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto canvas = [&] {
                return dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            };
            active()->findChild<QButtonGroup *>("mapTools")->button(MapCanvas::InspectNpc)->click();
            active()->findChild<QComboBox *>("mapNpcList")->setCurrentIndex(1);
            check(active()->findChild<QLabel *>("mapNpcInfo")->text().contains("Chamfort"),
                  "NPC name not resolved through dialogue ID");
            app.processEvents();
            for (auto scroll : active()->findChildren<QScrollArea *>())
                if (scroll->findChild<QLabel *>("mapNpcInfo"))
                    check(scroll->horizontalScrollBar()->maximum() == 0,
                          "Long NPC names forced inspector horizontal overflow");
            active()->findChild<QComboBox *>("mapSchedule")->setCurrentIndex(1);
            app.processEvents();
            active()->findChild<QPushButton *>("mapNpcLocate")->click();
            app.processEvents();
            app.processEvents();
            check(active()->findChild<QComboBox *>("mapPage")->currentData().toInt() == 1 &&
                      active()->findChild<QComboBox *>("mapSchedule")->currentIndex() == 1 &&
                      canvas()->selectedNpc == 1,
                  "Locate reset actor or shared schedule slot");
            auto drag = [&](int x1, int y1, int x2, int y2) {
                auto send = [&](QEvent::Type type, int x, int y, Qt::MouseButtons buttons) {
                    QPointF pos((x + 0.5) * 16 * canvas()->zoom, (y + 0.5) * 16 * canvas()->zoom);
                    QMouseEvent event(type, pos, pos,
                                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                                      buttons, Qt::NoModifier);
                    QApplication::sendEvent(canvas(), &event);
                };
                send(QEvent::MouseButtonPress, x1, y1, Qt::LeftButton);
                send(QEvent::MouseMove, x2, y2, Qt::LeftButton);
                send(QEvent::MouseButtonRelease, x2, y2, Qt::NoButton);
            };
            drag(3, 3, 6, 7);
            auto moved = window.projectForTests().data("TOWNE.NPC");
            for (int i = 0; i < npcs.size(); ++i)
                check(moved[i] == (i == base + 4   ? char(6)
                                   : i == base + 7 ? char(7)
                                                   : npcs[i]),
                      "Native drag damaged schedule fields");
            auto history = [&](QString prefix) {
                bool found = false;
                for (auto action : window.findChildren<QAction *>())
                    if (action->isEnabled() && action->text().startsWith(prefix + " ")) {
                        action->trigger();
                        found = true;
                        break;
                    }
                check(found, "NPC move lacked history");
                app.processEvents();
                app.processEvents();
            };
            history("Undo");
            check(window.projectForTests().data("TOWNE.NPC") == npcs &&
                      canvas()->selectedNpc == 1 && canvas()->tool == MapCanvas::InspectNpc,
                  "Undo lost NPC data or selection");
            history("Redo");
            active()->findChild<QComboBox *>("mapSchedule")->setCurrentIndex(3);
            app.processEvents();
            check(active()->findChild<QSpinBox *>("mapNpcX")->value() == 6 &&
                      active()->findChild<QLabel *>("mapNpcInfo")->text().contains("Schedule change 4"),
                  "Slot 3 failed shared location display");
            check(active()->findChild<QComboBox *>("mapSchedule")->currentText() == "4" &&
                      !active()->findChild<QLabel *>("mapNpcInfo")->text().contains("AI byte"),
                  "Mixed NPC timings should show compact numbered schedule changes");
            active()->findChild<QComboBox *>("mapPage")->setCurrentIndex(0);
            app.processEvents();
            check(canvas()->selectedNpc == 1 && canvas()->tool == MapCanvas::InspectNpc &&
                      active()->findChild<QComboBox *>("mapSchedule")->currentIndex() == 3,
                  "Manual floor navigation lost actor or slot");
            active()->findChild<QPushButton *>("mapNpcLocate")->click();
            app.processEvents();
            app.processEvents();
            active()->findChild<QCheckBox *>("mapNpcGhosts")->setChecked(true);
            active()->findChild<QPushButton *>("mapNpcConversation")->click();
            app.processEvents();
            check(active()->findChild<QWidget *>("conversationEditor"), "Conversation link failed");
            active()->findChild<QPushButton *>("backToMap")->click();
            app.processEvents();
            app.processEvents();
            check(canvas()->selectedNpc == 1 && canvas()->tool == MapCanvas::InspectNpc &&
                      active()->findChild<QComboBox *>("mapSchedule")->currentIndex() == 3 &&
                      active()->findChild<QCheckBox *>("mapNpcGhosts")->isChecked(),
                  "Dialogue return lost NPC state");
            auto tree = active()->findChild<QTreeWidget *>("mapNavigation");
            tree->setCurrentItem(treeItem(tree, "Yew", true));
            app.processEvents();
            app.processEvents();
            active()->findChild<QComboBox *>("mapNpcList")->setCurrentIndex(1);
            active()->findChild<QComboBox *>("mapSchedule")->setCurrentIndex(2);
            app.processEvents();
            active()->findChild<QPushButton *>("mapNpcLocate")->click();
            app.processEvents();
            check(active()->findChild<QSpinBox *>("mapNpcFloor")->value() == -1,
                  "Basement floor was unsigned");
            active()->findChild<QSpinBox *>("mapNpcX")->setValue(8);
            active()->findChild<QSpinBox *>("mapNpcFloor")->setValue(0);
            active()->findChild<QPushButton *>("mapNpcMove")->click();
            app.processEvents();
            app.processEvents();
            check(active()->findChild<QComboBox *>("mapPage")->currentData().toInt() == 7 &&
                      active()->findChild<QComboBox *>("mapSchedule")->currentIndex() == 2 &&
                      U5::byte(window.projectForTests().data("TOWNE.NPC"), yew + 11) == 0,
                  "Cross-floor move failed");
            check(window.projectForTests().data("TOWNE.DAT") == terrain, "NPC UI changed terrain");
            auto finalNpc = window.projectForTests().data("TOWNE.NPC");
            for (int i = 0; i < npcs.size(); ++i)
                check(finalNpc[i] == (i == base + 4   ? char(6)
                                      : i == base + 7 ? char(7)
                                      : i == yew + 5  ? char(8)
                                      : i == yew + 11 ? char(0)
                                                      : npcs[i]),
                      "Cross-floor movement changed unrelated record bytes");
            active()->findChild<QComboBox *>("mapNpcList")->setCurrentIndex(2);
            check(active()->findChild<QPushButton *>("mapNpcConversation")->isEnabled() &&
                      active()->findChild<QPushButton *>("mapNpcConversation")->text() ==
                          "Create conversation" &&
                      active()->findChild<QSpinBox *>("mapNpcX")->value() == 255,
                  "Missing conversation or unusual position was rewritten");
            auto scheduleBefore = window.projectForTests().data("TOWNE.NPC");
            auto hour = active()->findChild<QSpinBox *>("mapNpcStartHour");
            check(hour && hour->isEnabled(), "Start hour control missing");
            hour->setValue(13);
            QMetaObject::invokeMethod(hour, "editingFinished", Qt::DirectConnection);
            auto expectedSchedule = scheduleBefore;
            expectedSchedule[3 * 576 + 2 * 16 + 14] = 13;
            check(window.projectForTests().data("TOWNE.NPC") == expectedSchedule,
                  "Start hour edit changed another schedule or destination");
            hour->setValue(25);
            QMetaObject::invokeMethod(hour, "editingFinished", Qt::DirectConnection);
            check(window.projectForTests().data("TOWNE.NPC") == expectedSchedule &&
                      hour->value() == 13,
                  "Invalid new start hour was accepted");
            history("Undo");
            check(window.projectForTests().data("TOWNE.NPC") == scheduleBefore,
                  "Start hour undo did not restore exact schedule bytes");
            auto createButton = active()->findChild<QPushButton *>("mapNpcConversation");
            auto talkBefore = window.projectForTests().data("TOWNE.TLK");
            createButton->click();
            app.processEvents();
            check(active()->findChild<QWidget *>("conversationEditor"),
                  "Create conversation did not open dialogue editor");
            check(window.projectForTests().data("TOWNE.TLK") != talkBefore,
                  "Create conversation did not save dialogue");
            history("Undo");
            check(window.projectForTests().data("TOWNE.NPC") == scheduleBefore &&
                      window.projectForTests().data("TOWNE.TLK") == talkBefore,
                  "Conversation creation did not undo atomically");
        });
        test("Native widgets are read-only until an edit", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Window game load");
            for (auto name : p.resources.keys()) {
                w.selectResource(name);
                app.processEvents();
            }
            check(w.projectForTests().changed().isEmpty(), "GUI load mutated resources");
            w.selectResource("INIT.GAM");
            auto field = w.findChild<QStackedWidget *>("workspaces")
                             ->currentWidget()
                             ->findChild<QSpinBox *>("startingField_206");
            check(field, "Typed starting supplies control missing");
            auto statePage = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            auto tabs = statePage->findChild<QTabWidget *>();
            check(
                tabs &&
                    [&] {
                        for (int i = 0; i < tabs->count(); ++i)
                            if (tabs->tabText(i) == "Party members")
                                return true;
                        return false;
                    }(),
                "Party member tab label is wrong");
            check(tabs->widget(0)->findChildren<QSpinBox *>().size() == 13,
                  "Party, time, and supplies page is empty or incomplete");
            check(statePage->findChild<QSpinBox *>("startingField_21a") &&
                      statePage->findChild<QSpinBox *>("startingField_24a") &&
                      statePage->findChild<QSpinBox *>("startingField_282") &&
                      statePage->findChild<QCheckBox *>("startingOwned_209") &&
                      statePage->findChild<QComboBox *>("startingActiveMember"),
                  "Shared inventory or starting settings are still missing");
            check(!statePage->findChild<QSpinBox *>("startingField_28a") &&
                      !statePage->findChild<QSpinBox *>("startingField_292"),
                  "Moonstone coordinates still clutter Game Rules");
            auto hour = tabs->widget(0)->findChild<QSpinBox *>("startingField_2d9");
            auto before = w.projectForTests().data("INIT.GAM");
            hour->setValue(12);
            auto expected = before;
            expected[0x2d9] = expected[0x2da] = 12;
            check(w.projectForTests().data("INIT.GAM") == expected,
                  "Starting hour edit changed unrelated bytes or missed the hour copy");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            check(w.projectForTests().data("INIT.GAM") == before, "Starting time undo failed");

            field = w.findChild<QStackedWidget *>("workspaces")
                        ->currentWidget()
                        ->findChild<QSpinBox *>("startingField_206");
            field->setValue(1);
            check(U5::byte(w.projectForTests().data("INIT.GAM"), 0x206) == 1,
                  "Typed edit did not persist");
            app.processEvents();
        });
        test("Native painting, undo and unapplied dialogue safety", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            file(
                dir.path() + "/TOWNE.TLK",
                U5::writeDialogue({{1, U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>"
                                                      "Job<Entry>Bye<Entry><Any><Label 15>@")}}));
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Fixture load");
            w.show();
            app.processEvents();
            auto active = [&] {
                return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto click = [&](QWidget *widget, QPointF position) {
                QMouseEvent down(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton,
                                 Qt::NoModifier);
                QMouseEvent up(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton,
                               Qt::NoModifier);
                QApplication::sendEvent(widget, &down);
                QApplication::sendEvent(widget, &up);
                app.processEvents();
            };
            w.selectResource("TOWNE.DAT");
            auto canvas = active()->findChild<QWidget *>("mapCanvas");
            check(canvas, "Map canvas absent");
            click(canvas, {40, 40});
            check(U5::byte(w.projectForTests().data("TOWNE.DAT"), 33) == 1, "Paint stroke lost");
            auto undo = [&] {
                for (auto action : w.findChildren<QAction *>())
                    if (action->text().startsWith("Undo ")) {
                        action->trigger();
                        app.processEvents();
                        return;
                    }
                throw std::runtime_error("Undo action absent");
            };
            undo();
            check(w.projectForTests().data("TOWNE.DAT") == QByteArray(16384, 0),
                  "Map undo lost original bytes");
            w.selectResource("TILES.16");
            canvas = active()->findChild<QWidget *>("pixelCanvas");
            check(canvas, "Pixel canvas absent");
            click(canvas, {3, 3});
            auto graphics = U5::readGraphics("TILES.16", w.projectForTests().data("TILES.16"));
            check(graphics.images[0].pixel(0, 0) == qRgb(255, 255, 255),
                  "Native pixel brush failed");
            undo();
            check(w.projectForTests().data("TILES.16") == QByteArray(65536, 0),
                  "Pixel undo changed original encoding");
            w.selectResource("TOWNE.TLK");
            auto scope = active()->findChild<QComboBox *>("conversationSourceScope");
            scope->setCurrentIndex(1);
            auto text = active()->findChild<QPlainTextEdit *>("dialogueText");
            check(text, "Dialogue editor absent");
            text->setPlainText("<Gold><Byte 177>");
            auto answer = [&](QMessageBox::StandardButton which) {
                QTimer::singleShot(0, [which] {
                    for (auto widget : QApplication::topLevelWidgets())
                        if (auto box = qobject_cast<QMessageBox *>(widget))
                            box->button(which)->click();
                });
            };
            answer(QMessageBox::Cancel);
            w.selectResource("TILES.16");
            check(active()->findChild<QPlainTextEdit *>("dialogueText"), "Cancel discarded draft");
            check(!w.projectForTests().changed().contains("TOWNE.TLK"),
                  "Draft applied without consent");
            text->setPlainText("Hello<Entry>Description<Entry>Greeting<Entry>Job<"
                               "Entry>Bye<Entry><Any><Label 15>@");
            w.selectResource("TILES.16");
            check(w.projectForTests().changed().contains("TOWNE.TLK"),
                  "Validated automatic apply on navigation failed");
            auto package = w.projectForTests().package();
            Project restored;
            restored.openGame(dir.path());
            restored.importPackage(package);
            check(restored.data("TOWNE.TLK") == w.projectForTests().data("TOWNE.TLK"),
                  "Native edited package roundtrip");
        });
        test("Protected inline dialogue controls, literal paste and lossless spans", [&] {
            auto original = U5::encodeText("Speak to <Avatar> in <Rune>Yew<Rune>! <the> end.");
            DialogueTextSurface surface(Dialogue::blocks(original));
            surface.resize(600, 150);
            surface.show();
            app.processEvents();
            check(surface.bytes() == original,
                  "Inline controls normalized untouched words or bytes");
            auto cursor = surface.textCursor();
            cursor.setPosition(0);
            cursor.insertText("Please ");
            check(surface.bytes().endsWith(U5::encodeText("<Rune>Yew<Rune>! <the> end.")),
                  "Editing one span changed later untouched text or rune controls");
            const auto valid = surface.bytes();
            const int token = surface.toPlainText().indexOf(QChar::ObjectReplacementCharacter);
            cursor = surface.textCursor();
            cursor.setPosition(token);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            cursor.removeSelectedText(); // Also exercise non-keyboard deletion protection.
            check(surface.bytes() == valid, "Programmatic deletion destroyed an inline control");
            cursor = surface.textCursor();
            cursor.setPosition(token + 1);
            surface.setTextCursor(cursor);
            QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
            QApplication::sendEvent(&surface, &backspace);
            check(surface.bytes() == valid, "Backspace removed a protected control");
            surface.selectAll();
            surface.copy();
            check(
                QApplication::clipboard()->text().contains("[Avatar name]") &&
                    !QApplication::clipboard()->text().contains(QChar::ObjectReplacementCharacter),
                "Text copy leaked document objects or executable tags");
            cursor = surface.textCursor();
            cursor.clearSelection();
            cursor.movePosition(QTextCursor::End);
            surface.setTextCursor(cursor);
            QApplication::clipboard()->setText("<End Conversation>");
            surface.paste();
            int ends = 0;
            for (const auto &block : Dialogue::blocks(surface.bytes()))
                ends += block.opcode == 130;
            check(ends == 0, "External pasted text became a conversation action");
            surface.insertToken(129);
            int avatars = 0;
            for (const auto &block : Dialogue::blocks(surface.bytes()))
                avatars += block.opcode == 129;
            check(avatars == 2, "Explicit inline insertion did not add an Avatar control");
            cursor = surface.textCursor();
            cursor.insertText(QString::fromUtf8("é"));
            rejects([&] { surface.bytes(); });
            check(surface.toPlainText().contains(QString::fromUtf8("é")),
                  "Invalid draft was silently discarded");
        });
        test("Game dialogue word layout matches extracted engine routines", [] {
            QVector<QByteArray> inputs;
            inputs.append(QByteArray());
            inputs.append(QByteArray::fromHex("a0a0c1a0c2a0a08a"));
            for (int length = 1; length <= 65; ++length) {
                QByteArray bytes(length, char(0xc1));
                bytes += char(0xa0);
                bytes += QByteArray::fromHex("e2e38ae4a0");
                inputs.append(bytes);
            }
            quint32 seed = 17;
            for (int n = 0; n < 100; ++n) {
                QByteArray bytes;
                for (int i = 0; i < 250; ++i) {
                    seed = seed * 1664525u + 1013904223u;
                    bytes += char((seed & 7) == 0    ? 0xa0
                                  : (seed & 15) == 1 ? 0x8a
                                                     : ((seed & 127) | 128));
                }
                inputs.append(bytes);
            }
            for (const auto &bytes : inputs) {
                QVector<int> expected(16384 * 4);
                const int count = WorkshopEngineTalkWords(
                    reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size(),
                    expected.data());
                auto actual = Dialogue::layoutTalkWords(bytes);
                check(actual.size() == count, "Preview word buffering differs from engine");
                for (int i = 0; i < count; ++i)
                    check(actual[i].x == expected[i * 4] && actual[i].y == expected[i * 4 + 1] &&
                              actual[i].character == expected[i * 4 + 2] &&
                              actual[i].runic == bool(expected[i * 4 + 3]),
                          "Preview glyph position or font differs from engine");
            }
            QByteArray normal(1024, 0), runes(1024, 0);
            for (int row = 0; row < 8; ++row) {
                normal['A' * 8 + row] = char(0x80);
                runes['A' * 8 + row] = char(1);
            }
            auto preview =
                Dialogue::previewResponse(U5::encodeText("A<Rune>A<Rune>A"), 3, normal, runes, {});
            check(preview.glyphs.size() == 5 && preview.glyphs[2].runic &&
                      !preview.glyphs[1].runic && !preview.glyphs[3].runic,
                  "Rune toggles or automatic quotes were lost");
            check(preview.image.pixel(2 * 8 + 7, 0) == qRgb(255, 255, 255) &&
                      preview.image.pixel(1 * 8, 0) == qRgb(255, 255, 255),
                  "Game preview used the wrong rune or normal glyph");
            auto terminal = Dialogue::previewResponse(U5::encodeText("A<End Conversation>A"), 3,
                                                      normal, runes, {});
            check(terminal.glyphs.size() == 2 && !terminal.notes.isEmpty(),
                  "Game preview displayed unreachable text after end conversation");
        });
        test("Native pixel viewport keeps DOS geometry, scrolling and isolated fonts", [&] {
            QByteArray normal(1024, 0), runes(1024, 0);
            for (int row = 0; row < 8; ++row) {
                normal['A' * 8 + row] = char(128);
                runes['A' * 8 + row] = char(1);
            }
            const auto original = normal;
            Dialogue::NativeTextWindow window(normal, runes), other(normal, runes);
            check(window.cursor().currentY == 12 && window.image().size() == QSize(128, 104),
                  "Native session does not start at the bottom of the real window");
            window.print('A');
            check(window.image().pixel(0, 96) == qRgb(255, 255, 255),
                  "Native font bit order or starting row changed");
            window.paintCursor('A');
            check(window.cursor().currentX == 1 && window.cursor().currentY == 12,
                  "Cursor painting advanced the native cursor");
            window.print('\n');
            check(window.image().pixel(0, 88) == qRgb(255, 255, 255) &&
                      window.image().pixel(0, 96) == qRgb(0, 0, 0),
                  "Native window failed to scroll by exactly one glyph row");
            window.print(254);
            window.print(253);
            window.print('A');
            check(normal == original && other.cursor().currentX == 0 &&
                      other.image().pixel(0, 96) == qRgb(0, 0, 0),
                  "Glyph effects mutated project fonts or another session");
            window.print(254);
            window.print(253);
            window.selectFont(1);
            window.print('A');
            check(window.image().pixel(15, 96) == qRgb(255, 255, 255),
                  "Native rune font was substituted or mirrored");
            window.print(255);
            check(window.cursor().currentX == 0 && window.cursor().currentY == 0,
                  "Clear did not reset native cursor");
            Dialogue::NativeTextWindow missing(normal, {});
            bool rejected = false;
            try {
                missing.selectFont(1);
            } catch (const std::exception &) {
                rejected = true;
            }
            check(rejected, "Missing rune font silently used replacement artwork");
            Dialogue::NativeTextViewport viewport;
            viewport.resize(640, 480);
            viewport.setImage(other.image());
            const auto pixels = other.indexedPixels();
            for (const QSize size : {QSize(640, 480), QSize(300, 700), QSize(1000, 200)}) {
                viewport.resize(size);
                const QRect area = viewport.displayRect();
                check(viewport.rect().contains(area) &&
                          qAbs(area.width() / double(area.height()) - 128.0 / (104 * 1.2)) < 0.01 &&
                          other.indexedPixels() == pixels,
                      "Resizing reflowed native pixels or lost DOS aspect");
            }
        });
        test("Native framed viewport preserves pixels at fixed zoom, Fit and different DPI", [&] {
            QByteArray font(1024, char(128));
            auto border = Dialogue::nativeConversationFrame(font);
            check(border.size() == QSize(135, 112) && border.pixel(0, 0) == qRgb(0, 0, 170) &&
                      border.pixel(6, 7) == qRgb(255, 255, 255) &&
                      border.pixel(6, 111) == qRgb(255, 255, 255) &&
                      border.pixel(134, 111) == qRgb(0, 0, 0),
                  "Conversation border adds edges absent from Impera");
            QImage text(128, 104, QImage::Format_RGB32);
            text.fill(Qt::black);
            text.setPixel(0, 0, qRgb(255, 255, 255));
            text.setPixel(127, 103, qRgb(255, 85, 85));
            Dialogue::NativeTextViewport viewport;
            viewport.setConversationFrame(border);
            viewport.setImage(text);
            auto logical = viewport.logicalImage();
            check(logical.copy(7, 8, 128, 104) == text,
                  "Frame shifted or clipped the native text pixels");
            for (int factor = 1; factor <= 5; ++factor) {
                viewport.setZoom(factor);
                viewport.resize(1000, 1000);
                check(viewport.displayRect().size() ==
                          QSize(135 * factor, qRound(112 * 1.2 * factor)),
                      "Fixed zoom stretches to available space");
                viewport.resize(200, 180);
                check(viewport.rect().contains(viewport.displayRect()) &&
                          viewport.logicalImage() == logical,
                      "Fixed zoom clips or reflows the framebuffer");
            }
            viewport.setZoom(0);
            for (const auto size : {QSize(300, 700), QSize(900, 200)}) {
                viewport.resize(size);
                auto area = viewport.displayRect();
                check(viewport.rect().contains(area) &&
                          qAbs(area.width() / double(area.height()) - 135.0 / (112 * 1.2)) < 0.01,
                      "Fit loses the DOS pixel aspect ratio");
                for (double dpi : {1.0, 1.25, 2.0}) {
                    QImage painted(qRound(viewport.width() * dpi), qRound(viewport.height() * dpi),
                                   QImage::Format_RGB32);
                    painted.setDevicePixelRatio(dpi);
                    viewport.render(&painted);
                    for (int y = 0; y < painted.height(); ++y)
                        for (int x = 0; x < painted.width(); ++x) {
                            auto color = painted.pixel(x, y);
                            check(color == qRgb(0, 0, 0) || color == qRgb(0, 0, 170) ||
                                      color == qRgb(255, 255, 255) || color == qRgb(255, 85, 85),
                                  "DPI scaling interpolates native pixels");
                        }
                    check(viewport.logicalImage() == logical, "DPI scaling changed logical pixels");
                }
            }
        });
        test("Dedicated Playtest freezes edits and fonts until Restart and cancels on close", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            p.resources["INIT.GAM"].edited.replace(2, 9, QByteArray("Avatar\0\0\0", 9));
            QByteArray font(1024, char(0));
            for (int ch = 0; ch < 128; ++ch)
                for (int row = 0; row < 8; ++row)
                    font[ch * 8 + row] = char(ch * 37 + row * 11);
            p.resources["IBM.CH"] = {font, font, false};
            p.resources["RUNES.CH"] = {font, font, false};
            const auto original =
                U5::encodeText("Nora<Entry>A traveller.<Entry>Old greeting<Key Wait><Entry>Old "
                               "work<Key Wait><Entry>Farewell<Entry><Any><Label 15>@");
            auto tlk = U5::writeDialogue({{1, original}});
            p.resources["TOWNE.TLK"] = {tlk, tlk, false};
            ConversationEditor editor(
                &p, "TOWNE.TLK",
                [&](const QByteArray &bytes, const QString &) {
                    p.resources["TOWNE.TLK"].edited = bytes;
                },
                [](const QString &, const QString &) {}, 0, 0, [](int, int) {});
            editor.resize(1000, 700);
            editor.show();
            auto playtest = editor.findChild<QDialog *>("conversationTools");
            auto viewport =
                editor.findChild<Dialogue::NativeTextViewport *>("conversationNativeViewport");
            auto trace = editor.findChild<QTreeWidget *>("conversationTranscript");
            auto await = [&](const char *stage, const std::function<bool()> &ready) {
                try {
                    waitFor(ready);
                } catch (const std::exception &) {
                    throw std::runtime_error(
                        QString("Playtest %1: %2; focus=%3; image=%4x%5")
                            .arg(stage)
                            .arg(editor.findChild<QLabel *>("conversationPlaytestStatus")->text())
                            .arg(QApplication::focusWidget()
                                     ? QApplication::focusWidget()->objectName()
                                     : "none")
                            .arg(viewport->logicalImage().width())
                            .arg(viewport->logicalImage().height())
                            .toStdString());
                }
            };
            auto play = [&] {
                for (auto button : editor.findChildren<QPushButton *>())
                    if (button->text() == "Playtest") {
                        button->click();
                        return;
                    }
                throw std::runtime_error("Playtest action missing");
            };
            for (auto button : editor.findChildren<QPushButton *>())
                check(button->text() != "Game preview", "Redundant static preview remains visible");
            check(playtest->findChildren<QTabWidget *>().isEmpty() &&
                      editor.findChild<QDialog *>("conversationDiagnosticsWindow")->isHidden(),
                  "Playtest still mixes diagnostics and multiple previews");
            editor.findChild<QCheckBox *>("conversationIntroduced")->setChecked(true);
            auto setup = Dialogue::nativeSetup(p, "TOWNE.TLK", 0, -2);
            setup.state.introduced = true;
            auto expected = [&](const QByteArray &bytes, const QByteArray &currentFont, bool work) {
                QVector<Dialogue::NativeKey> keys;
                if (work)
                    for (char ch : std::string("xjob\r"))
                        keys.append({0, ch});
                auto result = Dialogue::runNativeVirtual(bytes, currentFont, font, setup, keys);
                check(result.error.contains("Script exhausted"), "Expected a blocked native wait");
                return result;
            };
            auto matches = [&](const Dialogue::NativeResult &result,
                               const QByteArray &currentFont) {
                const auto &image = viewport->logicalImage();
                if (image.size() != QSize(128, 104))
                    return false;
                Q_UNUSED(currentFont);
                const auto &pixels = result.pixels;
                QRect cursor(result.x * 8, result.y * 8, 8, 8);
                for (int y = 0; y < 104; ++y)
                    for (int x = 0; x < 128; ++x)
                        if (!cursor.contains(x, y) &&
                            image.pixel(x, y) !=
                                (0xff000000u | IMPERA_TextPalette[uint8_t(pixels[y * 128 + x])]))
                            return false;
                return true;
            };
            auto greeting = expected(original, font, false);
            play();
            await("opening", [&] { return viewport->hasFocus() && matches(greeting, font); });
            auto zoom = editor.findChild<QComboBox *>("conversationZoom");
            playtest->resize(480, 360);
            zoom->setCurrentIndex(5);
            auto scroll = editor.findChild<QScrollArea *>("conversationPlaytestScroll");
            await("zoom scrollbars", [&] {
                return scroll->horizontalScrollBar()->maximum() > 0 &&
                       scroll->verticalScrollBar()->maximum() > 0;
            });
            check(viewport->displayRect().size() == QSize(640, 624),
                  "Fixed zoom was reduced to fit");
            zoom->setCurrentIndex(0);
            await("Fit", [&] { return viewport->hasFocus() && matches(greeting, font); });
            auto outline = editor.findChild<QTreeWidget *>("conversationOutline");
            outline->setCurrentItem(treeItem(outline, "Job / Work"));
            app.processEvents();
            QPlainTextEdit *paragraph = nullptr;
            for (auto field : editor.findChildren<QPlainTextEdit *>("conversationBlockText"))
                if (field->property("dialogueEntry").toInt() == 3 && field->isVisible())
                    paragraph = field;
            check(paragraph, "Work paragraph missing");
            paragraph->setPlainText("New work");
            check(editor.flush(), "Cannot apply Playtest edit");
            auto changed = U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes;
            auto currentFont = font;
            currentFont['N' * 8] ^= char(127);
            p.resources["IBM.CH"].edited = currentFont;
            check(!trace->isEnabled() && editor.findChild<QLabel *>("conversationPlaytestStatus")
                                             ->text()
                                             .contains("Restart"),
                  "Edits left stale source links active or silently restarted playback");
            nativeKeys(viewport, "xjob\r");
            auto oldWork = expected(original, font, true);
            await("old snapshot", [&] { return matches(oldWork, font); });
            check(p.data("TOWNE.TLK") == U5::writeDialogue({{1, changed}}),
                  "Live session rewrote the draft");
            editor.findChild<QPushButton *>("conversationRestart")->click();
            nativeKeys(viewport, "xjob\r");
            auto newWork = expected(changed, currentFont, true);
            await("Restart", [&] { return trace->isEnabled() && matches(newWork, currentFont); });
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(viewport, &escape);
            check(playtest->isVisible(), "Native Escape closed the tools window");
            QElapsedTimer stop;
            stop.start();
            playtest->close();
            check(stop.elapsed() < 250 && playtest->isHidden() && trace->topLevelItemCount() == 0 &&
                      editor.findChild<QLabel *>("conversationPlaytestStatus")->text() == "Stopped",
                  "Close left a running wait or stale events");
            play();
            auto newGreeting = expected(changed, currentFont, false);
            await("reopening",
                  [&] { return viewport->hasFocus() && matches(newGreeting, currentFont); });
            editor.hide();
            check(playtest->isHidden() && trace->topLevelItemCount() == 0,
                  "Leaving the editor kept Playtest alive");
        });
        test("Native game context uses edited NPCs and keeps starting resources isolated", [&] {
            for (int seed : {0, 1, 42, 32768, 65535}) {
                Dialogue::NativeRandom rng;
                rng.seed(uint16_t(seed));
                std::srand(seed);
                for (int i = 0; i < 1024; ++i)
                    check(rng.next() == std::rand(),
                          "Session RNG differs from platform-native rand");
            }
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            auto &init = p.resources["INIT.GAM"].edited;
            init.replace(2, 9, QByteArray("Avatar\0\0\0", 9));
            init.replace(2 + 15 * 32, 9, QByteArray("Nora\0\0\0\0\0", 9));
            init[0x2d9] = 19;
            init[0x2e2] = 70;
            init[0x206] = 98;
            init[0x204] = 50;
            init[0x24a] = 23;
            init[0x215] = 17;
            QByteArray schedule(4608, char(0));
            schedule[512 + 5] = 0x44;
            schedule[544 + 5] = 1;
            schedule[5 * 16 + 3] = 23;
            schedule[5 * 16 + 6] = 15;
            schedule[5 * 16 + 12] = 19;
            p.resources["TOWNE.NPC"] = {schedule, schedule, false};
            p.resources["TOWNE.DAT"] = {QByteArray(16384, char(0)), QByteArray(16384, char(0)),
                                        false};
            auto talk = U5::encodeText("Nora<Entry>Description<Entry>Hello<Join "
                                       "Party><Entry>Work<Entry>Bye<Entry><Any><Label 15>@");
            auto tlk = U5::writeDialogue({{1, talk}});
            p.resources["TOWNE.TLK"] = {tlk, tlk, false};
            auto original = p.data("INIT.GAM"), originalNpc = p.data("TOWNE.NPC");
            auto setup = Dialogue::nativeSetup(p, "TOWNE.TLK", 0, 5);
            check(setup.hasNpc && setup.npc == 5 && uint8_t(setup.gameState[0x2ed]) == 1 &&
                      setup.state.gold == 50 && setup.state.karma == 70 &&
                      uint8_t(setup.npcRecords[5 * 16 + 2]) == 23,
                  "Playtest ignored Project starting state or NPC schedules");
            setup.state.introduced = true;
            const auto initialContext = setup.gameState;
            auto result = Dialogue::runNativeVirtual(talk, QByteArray(1024, char(128)),
                                                     QByteArray(1024, char(128)), setup, {});
            check(result.error.isEmpty() && result.partySize == 2 &&
                      result.gameState.mid(34, 4) == "Nora" && result.shapes[5] == 0 &&
                      (uint8_t(result.gameState[0x5b4]) & 32) &&
                      uint8_t(result.gameState[0x24a]) == 23 &&
                      uint8_t(result.gameState[0x215]) == 17,
                  "Recruitment failed or damaged unrelated game state");
            auto again = Dialogue::runNativeVirtual(talk, QByteArray(1024, char(128)),
                                                    QByteArray(1024, char(128)), setup, {});
            check(result.gameState == again.gameState && p.data("INIT.GAM") == original &&
                      p.data("TOWNE.NPC") == originalNpc && setup.gameState == initialContext,
                  "Playtest restart mutated Project or starting context");
            p.resources["TOWNE.NPC"].edited[512 + 6] = 0x44;
            p.resources["TOWNE.NPC"].edited[544 + 6] = 1;
            check(!Dialogue::nativeSetup(p, "TOWNE.TLK", 0).hasNpc &&
                      Dialogue::nativeSetup(p, "TOWNE.TLK", 0, 6).npc == 6,
                  "Shared conversation guessed an NPC identity");
            p.resources["INIT.GAM"].edited[0x322] = 1;
            auto invaded = Dialogue::nativeSetup(p, "TOWNE.TLK", 0, 5);
            check(uint8_t(invaded.shapes[31]) == 0xfc && invaded.scheduleRecords[31 * 16] == 6 &&
                      invaded.npcRecords[31 * 16 + 2] == 15,
                  "Shadowlord context omitted its active NPC");
            invaded.state.introduced = true;
            auto guarded = Dialogue::runNativeVirtual(
                U5::encodeText("Nora<Entry>Description<Entry>Hello<Call Guards><End "
                               "Conversation><Entry>Work<Entry>Bye<Entry><Any><Label 15>@"),
                QByteArray(1024, char(128)), QByteArray(1024, char(128)), invaded, {});
            check(guarded.error.isEmpty() && guarded.scheduleRecords[31 * 16] == 7,
                  "Guard reactions omitted a location's Shadowlord");
            p.resources["INIT.GAM"].edited[0x5b4] = 64;
            auto killed = Dialogue::nativeSetup(p, "TOWNE.TLK", 0, 5);
            check(killed.shapes[6] == 0 && killed.npcRecords[6 * 16] == 0 &&
                      uint8_t(killed.npcRecords[6 * 16 + 8]) == 0x44,
                  "Killed NPC context lost native despawn preservation");
        });
        test("Native Playtest cancels waits and pauses, restarts and isolates sessions", [&] {
            const auto waiting =
                U5::encodeText("Name<Entry>Description<Entry>Hello<Key "
                               "Wait><Entry>Work<Entry>Bye<Entry><Any><Label 15>@");
            const auto pausing = U5::encodeText("Name<Entry>Description<Entry>Hello<Pause><Entry>"
                                                "Work<Entry>Bye<Entry><Any><Label 15>@");
            QByteArray font(1024, char(128));
            Dialogue::NativeSetup setup;
            setup.state.introduced = true;
            Dialogue::NativeDialogueSession first(waiting, font, font, setup);
            int waits = 0, frames = 0, heartbeats = 0;
            QObject::connect(
                &first, &Dialogue::NativeDialogueSession::actionReady, &app,
                [&](const QString &name, int, int) {
                    if (name == "Wait for key")
                        ++waits;
                },
                Qt::QueuedConnection);
            QObject::connect(
                &first, &Dialogue::NativeDialogueSession::frameReady, &app,
                [&](const QImage &image) {
                    check(image.size() == QSize(128, 104), "Worker reflowed pixels");
                    ++frames;
                },
                Qt::QueuedConnection);
            QTimer heartbeat;
            QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++heartbeats; });
            heartbeat.start(5);
            first.start();
            waitFor([&] { return waits > 0 && frames > 0 && heartbeats > 2; });
            QElapsedTimer stop;
            stop.start();
            first.cancel();
            check(stop.elapsed() < 250 && !first.running(), "Wait cancellation blocked the UI");
            QApplication::processEvents();
            first.start();
            first.sendKey('x');
            for (char ch : std::string("bye\r"))
                first.sendKey(ch);
            waitFor([&] { return !first.running() && waits>=2; });
            check(waits >= 2, "Restart did not rebuild the conversation");
            Dialogue::NativeDialogueSession second(pausing, font, font, setup);
            bool pause = false;
            QObject::connect(
                &second, &Dialogue::NativeDialogueSession::actionReady, &app,
                [&](const QString &name, int, int) {
                    if (name == "Pause")
                        pause = true;
                },
                Qt::QueuedConnection);
            second.start();
            first.start();
            waitFor([&] { return pause && waits >= 3; });
            stop.restart();
            second.cancel();
            first.cancel();
            check(stop.elapsed() < 250, "Pause cancellation or simultaneous sessions hung");
            heartbeat.stop();
            auto script =
                QVector<Dialogue::NativeKey>{{0, 'x'}, {0, 'b'}, {0, 'y'}, {0, 'e'}, {0, 13}};
            const auto before = waiting;
            auto a = Dialogue::runNativeVirtual(waiting, font, font, setup, script);
            auto b = Dialogue::runNativeVirtual(waiting, font, font, setup, script);
            check(a.error.isEmpty() && b.error.isEmpty() && a.pixels == b.pixels &&
                      a.time == b.time && a.keysConsumed == 5 && before == waiting,
                  "Native restart leaked state or a wait key became the next answer");
            auto unsupported = U5::encodeText("Name<Entry>Description<Entry>Hello<Join "
                                              "Party><Entry>Work<Entry>Bye<Entry><Any><Label 15>@");
            auto rejected = Dialogue::runNativeVirtual(unsupported, font, font, setup, {});
            check(!rejected.error.isEmpty(), "Recruitment invented a successful world outcome");
            auto runic=U5::encodeText(
                "Name<Entry>Description<Entry>Hello <Rune>R<Rune><End Conversation><Entry>Work<Entry>Bye<Entry><Any><Label 15>@");
            auto missing=Dialogue::runNativeVirtual(runic,font,{},setup,{});
            check(!missing.error.isEmpty(),"Missing rune font did not stop native execution");
            for(const auto &boundary:missing.boundaries)
                check(boundary.kind!="glyph" || boundary.value!='R',
                      "Missing rune glyph was drawn with replacement artwork");
        });
        test("Wait actions hide conventional spacing and insert it automatically", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            auto bytes =
                U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>Work<New Line><New "
                               "Line><Key Wait><Entry>Bye<Entry><Any><Label 15>@");
            bytes.replace(QByteArray::fromHex("8d8d8f"), QByteArray::fromHex("8d8d86418f"));
            file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, bytes}}));
            p.openGame(dir.path());
            int commits = 0;
            ConversationEditor editor(
                &p, "TOWNE.TLK",
                [&](const QByteArray &data, const QString &) {
                    p.resources["TOWNE.TLK"].edited = data;
                    ++commits;
                },
                {}, 0, 3, [](int, int) {});
            auto outline = editor.findChild<QTreeWidget *>("conversationOutline");
            outline->setCurrentItem(treeItem(outline, "Job / Work"));
            auto field = editor.findChild<QPlainTextEdit *>("conversationBlockText");
            check(field && field->toPlainText() == "Work",
                  "Wait spacing remains visible in speech");
            check(editor.flush() && commits == 0, "Opening wait spacing rewrote original bytes");
            field->setPlainText("Edited work");
            check(editor.flush(), "Cannot edit speech before wait");
            auto doc = Dialogue::parse(U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes);
            check(doc.entries[3].bytes ==
                      Dialogue::text("Edited work") + QByteArray::fromHex("8d8d86418f"),
                  "Editing speech lost wait spacing across an intervening state action");
            outline->setCurrentItem(treeItem(outline, "Greeting (introduced)"));
            auto actions = editor.findChild<QListWidget *>("conversationAvailableActions");
            QListWidgetItem *wait = nullptr;
            for (int n = 0; n < actions->count(); ++n)
                if (actions->item(n)->text() == "Wait for key")
                    wait = actions->item(n);
            check(wait, "Wait action missing");
            actions->itemClicked(wait);
            doc = Dialogue::parse(U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes);
            check(doc.entries[2].bytes.endsWith(QByteArray::fromHex("8d8d8f")),
                  "New wait action omitted its line spacing");
        });
        test("Paragraph and brief pauses have distinct authoring actions", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            auto bytes = U5::encodeText(
                "Name<Entry><Pause>Delayed<Entry>Spaced<New Line><New Line><Pause>Continued<Entry>"
                "State<New Line><New Line><Pause>Continued<Entry>Bye<Entry><Any><Label 15>@");
            const int statePause = bytes.lastIndexOf(QByteArray::fromHex("8d8d83"));
            bytes.replace(statePause, 3, QByteArray::fromHex("8d8d864183"));
            file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, bytes}}));
            p.openGame(dir.path());
            int commits = 0;
            ConversationEditor editor(
                &p, "TOWNE.TLK",
                [&](const QByteArray &data, const QString &) {
                    p.resources["TOWNE.TLK"].edited = data;
                    ++commits;
                },
                {}, 0, 3, [](int, int) {});
            auto outline = editor.findChild<QTreeWidget *>("conversationOutline");
            auto actionLabel = [&] {
                for (auto frame : editor.findChildren<QFrame *>("conversationAction"))
                    if (frame->property("dialogueOpcode").toInt() == 131)
                        return frame->findChild<QToolButton *>()->text();
                return QString();
            };
            outline->setCurrentItem(treeItem(outline, "Description"));
            check(actionLabel() == "Brief pause (no line break)",
                  "Leading pause was presented as a paragraph pause");
            outline->setCurrentItem(treeItem(outline, "Greeting (introduced)"));
            check(actionLabel() == "Pause after paragraph",
                  "Paragraph pause was not recognized");
            outline->setCurrentItem(treeItem(outline, "Job / Work"));
            check(actionLabel() == "Pause after paragraph",
                  "State action obscured paragraph pause spacing");
            check(editor.flush() && commits == 0, "Classifying pauses rewrote original bytes");

            outline->setCurrentItem(treeItem(outline, "Farewell"));
            auto available = editor.findChild<QListWidget *>("conversationAvailableActions");
            auto findAction = [available](const QString &name) {
                for (int i = 0; i < available->count(); ++i)
                    if (available->item(i)->text() == name)
                        return available->item(i);
                return static_cast<QListWidgetItem *>(nullptr);
            };
            check(!findAction("Pause") && findAction("Pause after paragraph") &&
                      findAction("Brief pause (no line break)"),
                  "Pause choices are ambiguous in Available actions");
            available->itemClicked(findAction("Pause after paragraph"));
            auto doc = Dialogue::parse(U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes);
            check(doc.entries[4].bytes.endsWith(QByteArray::fromHex("8d8d83")),
                  "Paragraph pause omitted its hidden spacing");
            available = editor.findChild<QListWidget *>("conversationAvailableActions");
            QListWidgetItem *brief = nullptr;
            for (int i = 0; i < available->count(); ++i)
                if (available->item(i)->text() == "Brief pause (no line break)")
                    brief = available->item(i);
            available->itemClicked(brief);
            doc = Dialogue::parse(U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes);
            check(doc.entries[4].bytes.endsWith(QByteArray::fromHex("8d8d8383")),
                  "Brief pause unexpectedly inserted line spacing");
        });
        test("Dialogue handoffs manage layout without altering speech or narration", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            auto bytes =
                U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>Work<Entry>Bye<Entry>"
                               "<Any><Label 1>Question?<Entry>No.<Byte 255><Entry>"
                               "yes<Entry>Yes.<Byte 255><Entry><Any><Label 15>@");
            file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, bytes}}));
            p.openGame(dir.path());
            ConversationEditor editor(
                &p, "TOWNE.TLK",
                [&](const QByteArray &data, const QString &) {
                    p.resources["TOWNE.TLK"].edited = data;
                },
                {}, 0, 3, [](int, int) {});
            auto outline = editor.findChild<QTreeWidget *>("conversationOutline");
            auto insert = [&](const QString &name) {
                auto actions = editor.findChild<QListWidget *>("conversationAvailableActions");
                QListWidgetItem *item = nullptr;
                for (int i = 0; i < actions->count(); ++i)
                    if (actions->item(i)->text() == name)
                        item = actions->item(i);
                check(item, "Handoff action missing");
                actions->itemClicked(item);
            };
            auto read = [&] {
                return Dialogue::parse(U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes);
            };
            outline->setCurrentItem(treeItem(outline, "Job / Work"));
            insert("Enter question");
            check(read().entries[3].bytes == Dialogue::text("Work") + QByteArray::fromHex("8d8d91"),
                  "Question after speech omitted paragraph spacing");
            auto field = editor.findChild<QPlainTextEdit *>("conversationBlockText");
            check(field && field->toPlainText() == "Work",
                  "Question layout appears as editable blank lines");
            auto menu = field->parentWidget()->findChildren<QToolButton *>();
            QToolButton *more = nullptr;
            for (auto b : menu)
                if (b->text() == "⋯")
                    more = b;
            editor.resize(1100, 760);
            editor.show();
            app.processEvents();
            check(more && more->geometry().top() >= field->geometry().top() &&
                      more->geometry().top() < field->geometry().bottom(),
                  "Paragraph menu still occupies a row above the text");
            field->setPlainText("Changed speech\"");
            check(editor.flush(), "Cannot edit speech before question");
            check(read().entries[3].bytes ==
                      Dialogue::text("Changed speech\"") + QByteArray::fromHex("8d8d91"),
                  "Question editing changed quotes or lost managed spacing");
            outline->setCurrentItem(treeItem(outline, "Name"));
            insert("Ask player's name");
            check(read().entries[0].bytes.endsWith(QByteArray::fromHex("8d8d88")),
                  "Name prompt omitted paragraph spacing");
            outline->setCurrentItem(treeItem(outline, "Description"));
            insert("Attempt party joining");
            check(read().entries[1].bytes.endsWith(QByteArray::fromHex("8d8d84")),
                  "Party-joining handoff omitted paragraph spacing");
            outline->setCurrentItem(treeItem(outline, "Greeting (introduced)"));
            insert("Return to ordinary topics");
            check(read().entries[2].bytes.endsWith(QByteArray::fromHex("8d8dff")),
                  "Topic handoff omitted paragraph spacing");
            outline->setCurrentItem(treeItem(outline, "Farewell"));
            insert("End conversation");
            check(read().entries[4].bytes == Dialogue::text("Bye\"") + QByteArray::fromHex("8d82"),
                  "Conversation ending omitted its single line break");
        });
        test("Handoffs reuse boundaries and preserve imported narration", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            const auto bytes = U5::encodeText(
                "Name<Avatar><Entry><Label 1><Entry>\"<Entry>Work<New Line><New Line><Key Wait>"
                "<Entry>Bye<New Line><Entry><Any><Label 1>Prompt?<Entry>"
                "Narration!<Key Wait><New Line><New Line>\"Speech\"<Byte 255><Entry>"
                "yes<Entry>Yes.<Byte 255><Entry><Any><Label 15>@");
            file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, bytes}}));
            p.openGame(dir.path());
            ConversationEditor editor(
                &p, "TOWNE.TLK",
                [&](const QByteArray &data, const QString &) {
                    p.resources["TOWNE.TLK"].edited = data;
                },
                {}, 0, 3, [](int, int) {});
            auto outline = editor.findChild<QTreeWidget *>("conversationOutline");
            auto insert = [&](const QString &name) {
                auto actions = editor.findChild<QListWidget *>("conversationAvailableActions");
                for (int i = 0; i < actions->count(); ++i)
                    if (actions->item(i)->text() == name) {
                        actions->itemClicked(actions->item(i));
                        return;
                    }
                throw std::runtime_error("Handoff action missing");
            };
            auto read = [&] {
                return Dialogue::parse(U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes);
            };
            outline->setCurrentItem(treeItem(outline, "Otherwise / fallback"));
            check(editor.flush() && read().bytes() == bytes,
                  "Imported narration or post-wait spacing changed on opening");
            const auto narration = read().entries[6].bytes;
            outline->setCurrentItem(treeItem(outline, "Name"));
            insert("Ask player's name");
            check(read().entries[0].bytes.endsWith(QByteArray::fromHex("818d8d88")),
                  "Handoff after inline Avatar name omitted spacing");
            outline->setCurrentItem(treeItem(outline, "Description"));
            insert("End conversation");
            check(read().entries[1].bytes == QByteArray::fromHex("8291"),
                  "Branch-only response acquired an artificial line break");
            outline->setCurrentItem(treeItem(outline, "Greeting (introduced)"));
            insert("Ask player's name");
            check(read().entries[2].bytes == QByteArray::fromHex("a288"),
                  "A lone quote acquired an artificial blank paragraph");
            outline->setCurrentItem(treeItem(outline, "Job / Work"));
            insert("Enter question");
            check(read().entries[3].bytes ==
                      Dialogue::text("Work") + QByteArray::fromHex("8d8d8f91"),
                  "Question after Wait added redundant paragraph spacing");
            outline->setCurrentItem(treeItem(outline, "Farewell"));
            insert("End conversation");
            check(read().entries[4].bytes == Dialogue::text("Bye\"") + QByteArray::fromHex("8d82"),
                  "Existing final newline was duplicated");
            check(read().entries[6].bytes == narration,
                  "Unrelated edits changed narration quotes or post-wait spacing");
        });
        test("Native narration questions, answers, Farewell and quote alignment", [&] {
            QByteArray font(1024, char(128));
            Dialogue::NativeSetup setup;
            setup.state.introduced = true;
            auto base = U5::encodeText(
                "Name<Entry>Description<Entry>Hello<Entry>Work<Entry>"
                "Bye<Entry>ask<Entry><Label 1><Entry><Any><Label 1>Choose.<Entry>"
                "Fallback<End Conversation><Entry>yes<Entry>Answer<End Conversation><Entry>"
                "<Any><Label 15>@");
            auto doc = Dialogue::parse(base);
            auto q = doc.questions.first();
            doc.entries[q.prompt].bytes = QByteArray::fromHex("9091") +
                                          IMPERA_TALK_EXPLICIT_QUOTES + Dialogue::text("Choose.");
            doc.entries[q.fallback].bytes =
                QByteArray(IMPERA_TALK_EXPLICIT_QUOTES) + Dialogue::text("He waits.") + char(130);
            doc.entries[q.replies.first().response].bytes =
                QByteArray(IMPERA_TALK_EXPLICIT_QUOTES) + Dialogue::text("He nods.") + char(130);
            auto glyphs = [&](const Dialogue::NativeResult &r, int entry) {
                QString text;
                for (const auto &b : r.boundaries)
                    if (b.kind == "glyph" && b.entry == entry)
                        text += QChar(b.value);
                return text;
            };
            for (const QString &answer : {QString("yes"), QString("no")}) {
                QVector<Dialogue::NativeKey> keys;
                for (char ch : (QByteArray("ask\r") + answer.toLatin1() + '\r'))
                    keys.append({0, ch});
                auto r = Dialogue::runNativeVirtual(doc.bytes(), font, font, setup, keys);
                check(r.error.isEmpty() && glyphs(r, q.prompt).startsWith("Choose.") &&
                          !glyphs(r, q.prompt).contains('"'),
                      "Narration question has an implicit or inherited speech quote");
                const int response = answer == "yes" ? q.replies.first().response : q.fallback;
                check(!glyphs(r, response).contains('"'), "Narrated answer acquired a quote");
            }
            auto farewell = Dialogue::text("Travel safely.") + char(130);
            auto closed = Dialogue::closeFarewellSpeech(farewell);
            check(closed == Dialogue::text("Travel safely.\"") + char(130) &&
                      Dialogue::closeFarewellSpeech(closed) == closed,
                  "Farewell closure missing or repair is not idempotent");
            doc.entries[4].bytes = closed;
            QVector<Dialogue::NativeKey> bye{{0, 'b'}, {0, 'y'}, {0, 'e'}, {0, 13}};
            auto r = Dialogue::runNativeVirtual(doc.bytes(), font, font, setup, bye);
            check(r.error.isEmpty() && glyphs(r, 4).contains("Travel safely.\""),
                  "End Conversation dropped the authored Farewell closing quote");
            doc.entries[4].bytes =
                QByteArray(IMPERA_TALK_EXPLICIT_QUOTES) + Dialogue::text("He departs.") + char(130);
            r = Dialogue::runNativeVirtual(doc.bytes(), font, font, setup, bye);
            check(r.error.isEmpty() && glyphs(r, 4) == "He departs.",
                  "Narrated Farewell acquired speech quotes");
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, base}}));
            project.openGame(dir.path());
            auto originals = U5::readDialogue(project.data("TOWNE.TLK"));
            doc.entries[4].bytes = farewell;
            originals.append({127, doc.bytes()});
            project.resources["TOWNE.TLK"].edited = U5::writeDialogue(originals);
            ConversationEditor editor(
                &project, "TOWNE.TLK",
                [&](const QByteArray &data, const QString &) {
                    project.resources["TOWNE.TLK"].edited = data;
                },
                {}, originals.size() - 1, 4, [](int, int) {});
            check(editor.flush() &&
                      Dialogue::parse(U5::readDialogue(project.data("TOWNE.TLK")).last().bytes)
                              .entries[4]
                              .bytes == closed,
                  "Pre-fix Workshop NPC Farewell was not repaired before save/Playtest");
        });
        test("Conversation opening setting persists and skips both introduction states", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            const auto bytes =
                U5::encodeText("Mariah<Entry>A mage.<Entry>Old "
                               "friend!<Entry>Magic<Entry>Farewell.<Entry><Any><Label 15>@");
            file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, bytes}}));
            QByteArray npcs(4608, 0);
            npcs[513] = npcs[514] = 64;
            npcs[545] = npcs[546] = 1;
            file(dir.path() + "/TOWNE.NPC", npcs);
            p.openGame(dir.path());
            ConversationEditor editor(
                &p, "TOWNE.TLK",
                [&](const QByteArray &b, const QString &) { p.resources["TOWNE.TLK"].edited = b; },
                {}, 0, 1, [](int, int) {});
            auto checkbox = editor.findChild<QCheckBox *>("conversationSkipOpening");
            check(checkbox && !checkbox->isChecked(), "Opening checkbox missing or defaults on");
            checkbox->setChecked(true);
            check(editor.flush(), "Opening setting cannot be saved");
            auto bank = p.data("TOWNE.TLK");
            auto flagged = U5::readDialogue(bank)[0].bytes;
            check(Dialogue::parse(flagged).entries[1].bytes ==
                      QByteArray(IMPERA_TALK_SKIP_OPENING) + Dialogue::text("A mage."),
                  "Opening flag changed description text");
            for (auto field : editor.findChildren<QPlainTextEdit *>("conversationBlockText"))
                check(field->toPlainText() == "A mage.",
                      "Opening metadata leaked into description UI");
            check(p.data("TOWNE.NPC") == npcs, "Conversation setting changed NPC records");
            auto package = p.package();
            check(package.startsWith("IMOD0015"), "Opening setting lacks package version gate");
            Project imported;
            imported.openGame(dir.path());
            imported.importPackage(package);
            check(imported.data("TOWNE.TLK") == bank, "Package lost conversation opening flag");
            auto old = package;
            old.replace(0, 8, "IMOD0014");
            rejects([&] { imported.importPackage(old); });
            p.save(dir.path() + "/opening.imperaproject");
            Project reopened;
            reopened.load(p.projectPath);
            check(reopened.data("TOWNE.TLK") == bank, "Project lost conversation opening flag");
            for (int npc : {1, 2})
                for (bool introduced : {false, true}) {
                    auto setup =
                        Dialogue::nativeSetup(p, "TOWNE.TLK", 0, Workshop::npcCharacter(0, npc));
                    setup.state.introduced = introduced;
                    auto result = Dialogue::runNativeVirtual(flagged, QByteArray(1024, char(128)),
                                                             QByteArray(1024, char(128)), setup,
                                                             {{0, 'n'},
                                                              {0, 'a'},
                                                              {0, 'm'},
                                                              {0, 'e'},
                                                              {0, 13},
                                                              {0, 'b'},
                                                              {0, 'y'},
                                                              {0, 'e'},
                                                              {0, 13}});
                    check(result.error.isEmpty(), "Suppressed opening failed native execution");
                    QString name, greeting, description;
                    for (const auto &event : result.boundaries)
                        if (event.kind == "glyph") {
                            if (event.entry == 0)
                                name += QChar(event.value);
                            if (event.entry == 1)
                                description += QChar(event.value);
                            if (event.entry == 2)
                                greeting += QChar(event.value);
                        }
                    check(name.contains("Mariah") && description.contains("A mage.") &&
                              greeting.isEmpty(),
                          "Shared opening setting removed Name/description or retained greeting");
                }
            checkbox->setChecked(false);
            check(editor.flush() && U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes == bytes,
                  "Clearing opening setting did not restore original conversation");
        });
        test("Gargoyle conversation speech is shared, packaged and rendered by native runtime",
             [&] {
                 QTemporaryDir dir;
                 auto p = fixture(dir.path());
                 const auto bytes =
                     U5::encodeText("Beh Lem<Entry>A gargoyle.<Entry>Old "
                                    "friend!<Entry>Work<Entry>Bye.<Entry><Any><Label 15>@");
                 file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, bytes}}));
                 QByteArray npcs(4608, 0);
                 npcs[513] = npcs[514] = 64;
                 npcs[545] = npcs[546] = 1;
                 file(dir.path() + "/TOWNE.NPC", npcs);
                 p.openGame(dir.path());
                 ConversationEditor editor(
                     &p, "TOWNE.TLK",
                     [&](const QByteArray &b, const QString &) {
                         p.resources["TOWNE.TLK"].edited = b;
                     },
                     {}, 0, 1, [](int, int) {});
                 auto gargoyle = editor.findChild<QCheckBox *>("conversationGargoyleSpeech");
                 auto skip = editor.findChild<QCheckBox *>("conversationSkipOpening");
                 check(gargoyle && skip && !gargoyle->isChecked(),
                       "Gargoyle setting missing or defaults on");
                 gargoyle->setChecked(true);
                 check(editor.flush(), "Cannot apply gargoyle speech");
                 auto flagged = U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes;
                 check(IMPERA_TalkConversationFlags(flagged.constData(), flagged.size()) ==
                           IMPERA_TALK_FLAG_GARGOYLE_SPEECH,
                       "Conversation flag not stored");
                 for (auto field : editor.findChildren<QPlainTextEdit *>("conversationBlockText"))
                     check(field->toPlainText() == "A gargoyle.",
                           "Speech flag leaked into authored text");
                 auto package = p.package();
                 check(package.startsWith("IMOD0016"), "Gargoyle speech missing version gate");
                 Project imported;
                 imported.openGame(dir.path());
                 imported.importPackage(package);
                 check(imported.data("TOWNE.TLK") == p.data("TOWNE.TLK"),
                       "Package lost speech flag");
                 package.replace(0, 8, "IMOD0015");
                 rejects([&] { imported.importPackage(package); });
                 p.save(dir.path() + "/gargoyle.imperaproject");
                 Project reopened;
                 reopened.load(p.projectPath);
                 check(reopened.data("TOWNE.TLK") == p.data("TOWNE.TLK"),
                       "Project lost speech flag");
                 auto run = [&](const QByteArray &source, int npc, bool introduced, int seed,
                                bool askName) {
                     auto setup =
                         Dialogue::nativeSetup(p, "TOWNE.TLK", 0, Workshop::npcCharacter(0, npc));
                     setup.state.introduced = introduced;
                     setup.seed = seed;
                     QList<Dialogue::NativeKey> keys;
                     for (char c : askName ? QByteArray("name\rbye\r") : QByteArray("bye\r"))
                         keys.append({0, c});
                     auto result =
                         Dialogue::runNativeVirtual(source, QByteArray(1024, char(128)),
                                                    QByteArray(1024, char(128)), setup, keys);
                     check(result.error.isEmpty(), "Native gargoyle playtest failed");
                     QString text;
                     for (const auto &event : result.boundaries)
                         if (event.kind == "glyph")
                             text += QChar(event.value);
                     return text;
                 };
                 for (int npc : {1, 2})
                     for (bool introduced : {false, true}) {
                         auto text = run(flagged, npc, introduced, 42, true);
                         check(text.contains("To be called Beh Lem") &&
                                   !text.contains("My name is") && !text.contains("I am called"),
                               "Shared Name response did not use gargoyle speech");
                         if (introduced)
                             check(text.contains("Old friend!"),
                                   "Gargoyle speech changed authored greeting");
                     }
                 bool sawIntroduction = false;
                 for (int seed = 0; seed < 32 && !sawIntroduction; ++seed) {
                     auto normal = run(bytes, 1, false, seed, false);
                     if (!normal.contains("I am called"))
                         continue;
                     sawIntroduction = true;
                     check(run(flagged, 1, false, seed, false).contains("To be called"),
                           "Automatic introduction did not use gargoyle speech");
                 }
                 check(sawIntroduction, "Random introduction path was not exercised");
                 skip->setChecked(true);
                 auto combined = U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes;
                 check(IMPERA_TalkConversationFlags(combined.constData(), combined.size()) == 3,
                       "Opening flag displaced gargoyle speech");
                 check(run(combined, 2, false, 42, true).contains("To be called Beh Lem"),
                       "Skip opening disabled gargoyle Name response");
                 gargoyle->setChecked(false);
                 check(IMPERA_TalkConversationFlags(
                           U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes.constData(),
                           combined.size() - 4) == 1,
                       "Clearing speech flag lost opening flag");
                 skip->setChecked(false);
                 check(editor.flush() && U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes == bytes &&
                           p.data("TOWNE.NPC") == npcs,
                       "Clearing flags changed original conversation or NPCs");
             });
        test("Large mod conversations persist, package and playtest beyond DOS offsets", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            auto bytes =
                Dialogue::text(QString(70000, 'x')) +
                U5::encodeText("<Entry>A creature.<Entry>Hello.<End "
                               "Conversation><Entry>Work<Entry>Bye<Entry><Any><Label 15>@");
            auto bank = U5::writeDialogue({{1, bytes}, {2, bytes}});
            file(
                dir.path() + "/TOWNE.TLK",
                U5::writeDialogue({{1, U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>"
                                                      "Work<Entry>Bye<Entry><Any><Label 15>@")}}));
            p.openGame(dir.path());
            p.resources["TOWNE.TLK"].edited = bank;
            check(Dialogue::parse(bytes).issues().isEmpty(),
                  "Long conversation rejected by editor");
            auto package = p.package();
            check(package.startsWith("IMOD0014"), "Long dialogue lacks version protection");
            Project imported;
            imported.openGame(dir.path());
            imported.importPackage(package);
            check(imported.data("TOWNE.TLK") == bank, "Long dialogue package roundtrip");
            auto oldEngine = package;
            oldEngine.replace(0, 8, "IMOD0013");
            rejects([&] { imported.importPackage(oldEngine); });
            p.save(dir.path() + "/long.imperaproject");
            Project reopened;
            reopened.load(dir.path() + "/long.imperaproject");
            check(reopened.data("TOWNE.TLK") == bank, "Long dialogue project roundtrip");
            Dialogue::NativeSetup setup;
            setup.state.introduced = true;
            auto result = Dialogue::runNativeVirtual(bytes, QByteArray(1024, char(128)),
                                                     QByteArray(1024, char(128)), setup, {});
            check(result.error.isEmpty(), "Native playtest failed beyond 16-bit offsets");
            QString rendered;
            for (const auto &glyph : result.boundaries)
                if (glyph.kind == "glyph" && glyph.entry == 2)
                    rendered += QChar(glyph.value);
            check(rendered == "\"Hello.", "Long dialogue selected the wrong response");
        });
        test("Speech insertion after actions creates exactly one paragraph", [&] {
            for (int code : {143, 131, 137}) {
                QTemporaryDir dir;
                auto p = fixture(dir.path());
                auto response =
                    Dialogue::text(code == 137 ? "First speech." : "First speech.\n\n") +
                    Dialogue::action(code);
                auto bytes = U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>") +
                             response + U5::encodeText("<Entry>Bye<Entry><Any><Label 15>@");
                file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, bytes}}));
                p.openGame(dir.path());
                ConversationEditor editor(
                    &p, "TOWNE.TLK",
                    [&](const QByteArray &data, const QString &) {
                        p.resources["TOWNE.TLK"].edited = data;
                    },
                    {}, 0, 3, [](int, int) {});
                auto actions = editor.findChild<QListWidget *>("conversationAvailableActions");
                for (int i = 0; i < actions->count(); ++i)
                    if (actions->item(i)->text() == "Speech Text") {
                        actions->itemClicked(actions->item(i));
                        break;
                    }
                auto fields = editor.findChildren<QPlainTextEdit *>("conversationBlockText");
                check(fields.size() == 2, "Speech action inserted an extra text box");
                check(fields[0]->toPlainText() == "First speech." &&
                          fields[1]->toPlainText() == "New speech.",
                      "Speech insertion changed text or exposed generated quotes/spacing");
                fields[1]->setPlainText("Second speech.");
                check(editor.flush(), "Inserted speech cannot be saved");
                auto saved = Dialogue::parse(U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes);
                check(saved.entries[3].bytes.contains(Dialogue::text("Second speech.")),
                      "New speech was not saved");
            }
        });
        test("Explicit narration and speech preserve native paragraph quotes", [&] {
            const auto speech = Dialogue::text("I speak.");
            auto mixed = Dialogue::insertTextParagraph(speech, speech.size(),
                                                       Dialogue::ParagraphKind::Narration,
                                                       "He gestures.", true, true);
            check(mixed == QByteArray(IMPERA_TALK_EXPLICIT_QUOTES) +
                               Dialogue::text("\"I speak.\"\n\nHe gestures."),
                  "Narration failed to close speech or suppress the automatic final quote");
            mixed = Dialogue::insertTextParagraph(
                mixed, mixed.size(), Dialogue::ParagraphKind::Speech, "Listen.", false, true);
            check(mixed == QByteArray(IMPERA_TALK_EXPLICIT_QUOTES) +
                               Dialogue::text("\"I speak.\"\n\nHe gestures.\n\n\"Listen.\""),
                  "Resumed speech did not reopen its quotation");
            Dialogue::NativeSetup setup;
            setup.state.introduced = true;
            auto runGreeting = [&](const QByteArray &response) {
                auto bytes = U5::encodeText("Name<Entry>Description<Entry>") + response +
                             U5::encodeText(
                                 "<End Conversation><Entry>Work<Entry>Bye<Entry><Any><Label 15>@");
                return Dialogue::runNativeVirtual(bytes, QByteArray(1024, char(128)),
                                                  QByteArray(1024, char(128)), setup, {});
            };
            auto preview = runGreeting(mixed);
            check(preview.error.isEmpty(), "Explicit response failed native execution");
            QString rendered;
            for (const auto &glyph : preview.boundaries)
                if (glyph.kind == "glyph" && glyph.entry == 2)
                    rendered += QChar(glyph.value);
            check(rendered == "\"I speak.\"He gestures.\"Listen.\"",
                  "Mixed paragraphs have missing or spurious quotation marks");
            const auto description = Dialogue::insertTextParagraph(
                {}, 0, Dialogue::ParagraphKind::Narration, "A quiet room.", false, false);
            check(description == Dialogue::text("A quiet room."),
                  "Description narration acquired quotation marks or control flow");
            const auto onlyNarration = Dialogue::insertTextParagraph(
                {}, 0, Dialogue::ParagraphKind::Narration, "Only narration.", true, true);
            check(onlyNarration ==
                      QByteArray(IMPERA_TALK_EXPLICIT_QUOTES) + Dialogue::text("Only narration."),
                  "Leading narration is wrapped in speech quotes");
            auto narrated = runGreeting(onlyNarration);
            rendered.clear();
            for (const auto &glyph : narrated.boundaries)
                if (glyph.kind == "glyph" && glyph.entry == 2)
                    rendered += QChar(glyph.value);
            check(narrated.error.isEmpty() && rendered == "Only narration.",
                  "Narration-only response generated quotation marks");
            auto questionNarration = Dialogue::insertTextParagraph(
                speech, speech.size(), Dialogue::ParagraphKind::Narration, "Question narration.",
                true, false);
            check(!questionNarration.endsWith(char(255)),
                  "Narration changed a question into a topic return");

            QTemporaryDir dir;
            auto p = fixture(dir.path());
            auto bytes = U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>I "
                                        "speak.<Entry>Bye<Entry><Any><Label 15>@");
            file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, bytes}}));
            p.openGame(dir.path());
            ConversationEditor editor(
                &p, "TOWNE.TLK",
                [&](const QByteArray &data, const QString &) {
                    p.resources["TOWNE.TLK"].edited = data;
                },
                {}, 0, 3, [](int, int) {});
            auto outline = editor.findChild<QTreeWidget *>("conversationOutline");
            outline->setCurrentItem(treeItem(outline, "Job / Work"));
            auto insert = [&](const QString &name) {
                auto actions = editor.findChild<QListWidget *>("conversationAvailableActions");
                for (int i = 0; i < actions->count(); ++i) {
                    check(actions->item(i)->text() != "Text paragraph",
                          "Ambiguous text action remains");
                    if (actions->item(i)->text() == name) {
                        actions->itemClicked(actions->item(i));
                        return;
                    }
                }
                throw std::runtime_error("Typed text action missing");
            };
            insert("Narration Text");
            insert("Speech Text");
            QPlainTextEdit *speechField = nullptr, *narrationField = nullptr;
            for (auto field : editor.findChildren<QPlainTextEdit *>("conversationBlockText")) {
                if (field->toPlainText() == "New speech.")
                    speechField = field;
                if (field->toPlainText() == "New narration.")
                    narrationField = field;
            }
            check(speechField && narrationField &&
                      speechField->property("dialogueTextKind") == "speech" &&
                      narrationField->property("dialogueTextKind") == "narration",
                  "Typed paragraphs were merged or expose their generated quote boundaries");
            for (auto label : editor.findChildren<QLabel *>())
                check(label->text() != "Speech" && label->text() != "Narration",
                      "Paragraph kind labels remain visible");
            check(speechField->parentWidget()->findChildren<QLabel *>().size() >= 2 &&
                      narrationField->parentWidget()->findChildren<QLabel *>().isEmpty(),
                  "Speech quotation marks or unquoted narration presentation missing");
            for (auto label :
                 speechField->parentWidget()->findChildren<QLabel *>("conversationParagraphQuote"))
                check(label->contentsMargins().top() ==
                          qRound(speechField->document()->documentMargin()),
                      "Quote baseline does not match the speech text margin");
            speechField->setPlainText("Listen carefully.");
            narrationField->setPlainText("He gestures.");
            check(editor.flush(), "Cannot edit mixed text paragraphs");
            const auto encoded = p.data("TOWNE.TLK");
            check(p.package().startsWith("IMOD0005"),
                  "Narration extension package is not protected from older engines");
            p.save(dir.path() + "/mixed.imperaproject");
            Project reopened;
            reopened.load(dir.path() + "/mixed.imperaproject");
            check(reopened.data("TOWNE.TLK") == encoded,
                  "Project persistence lost text types or quote boundaries");
            Project imported;
            imported.openGame(dir.path());
            imported.importPackage(p.package());
            check(imported.data("TOWNE.TLK") == encoded,
                  "Native narration package lost text paragraph formatting");
        });
        test("Conversation surface, unified questions and source-linked playtest", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            file(dir.path() + "/IBM.CH", QByteArray(1024, char(255)));
            file(dir.path() + "/RUNES.CH", QByteArray(1024, char(255)));
            auto bytes =
                U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>Work<Entry>Bye<Entry>"
                               "help<Entry><Or><Entry>hint<Entry>Seek <Avatar><Label 1><Entry>"
                               "<Any><Label 1>Password?<Entry>No.<End Conversation><Entry>"
                               "dawn<Entry>Yes.<Key Wait><End Conversation><Entry>"
                               "no<Entry>Try again.<Label 1><Entry><Any><Label 15>@");
            file(dir.path() + "/TOWNE.TLK", U5::writeDialogue({{1, bytes}}));
            p.openGame(dir.path());
            auto baseline = p.data("TOWNE.TLK");
            auto original = Dialogue::parse(bytes);
            check(Dialogue::displayName(U5::encodeText("Malik\"<Key Wait>")) == "Malik" &&
                      Dialogue::displayName(U5::encodeText("Lord Stuart the Hungry\"")) == "Lord Stuart the Hungry" &&
                      Dialogue::displayName(U5::encodeText("Eb\"")) == "Eb",
                  "NPC names retain spoken introduction closing quotes");
            int commits = 0;
            ConversationEditor editor(
                &p, "TOWNE.TLK",
                [&](const QByteArray &data, const QString &) {
                    p.resources["TOWNE.TLK"].edited = data;
                    ++commits;
                },
                [&](const QString &key, const QString &value) { p.dialogueNames[key] = value; }, 0,
                3, [](int, int) {});
            editor.resize(1050, 720);
            editor.show();
            app.processEvents();
            auto tools = editor.findChild<QDialog *>("conversationTools");
            auto authoring = editor.findChild<QTabWidget *>("conversationAuthoring");
            check(tools && tools->isHidden() && authoring->tabBar()->isHidden(),
                  "Preview or advanced navigation still competes with the authoring surface");
            for (auto tabs : editor.findChildren<QTabWidget *>())
                for (int i = 0; i < tabs->count(); ++i)
                    check(tabs->tabText(i) != "Readable preview",
                          "Duplicate readable preview remains");
            for (auto button : editor.findChildren<QPushButton *>())
                check(button->text() != "Apply", "Redundant ordinary Apply button remains");
            auto outline = editor.findChild<QTreeWidget *>("conversationOutline");
            outline->setCurrentItem(treeItem(outline, "help / hint"));
            check(editor.findChild<DialogueTextSurface *>("conversationInlineText"),
                  "Avatar substitution is not inline with speech");
            check(p.data("TOWNE.TLK") == baseline && commits == 0,
                  "Opening a conversation rewrote its encoded bytes");
            outline->setCurrentItem(treeItem(outline, "Name"));
            editor.resize(1320, 960);
            app.processEvents();
            app.processEvents();
            auto nameField = editor.findChild<QPlainTextEdit *>("conversationBlockText");
            check(nameField && nameField->geometry().top() < 80 &&
                      nameField->parentWidget()->height() < nameField->height() + 80,
                  "Speech card expands to fill the viewport and pushes text downward");
            editor.resize(1050, 720);

            const auto question = original.questions[0];
            auto chooseEntry = [&](int entry) {
                QTreeWidgetItemIterator it(outline);
                while (*it) {
                    if ((*it)->data(0, Qt::UserRole).toInt() == entry) {
                        outline->setCurrentItem(*it);
                        app.processEvents();
                        return;
                    }
                    ++it;
                }
                throw std::runtime_error("Question entry missing");
            };
            QTreeWidgetItemIterator outlineItems(outline);
            while (*outlineItems) {
                check((*outlineItems)->text(0) != "Format" &&
                          (*outlineItems)->data(0, Qt::UserRole).toInt() != original.tail,
                      "File-format housekeeping remains in the dialogue outline");
                ++outlineItems;
            }
            check(editor.findChild<QWidget *>("conversationActionPane") &&
                      editor.findChild<QListWidget *>("conversationAvailableActions") &&
                      !editor.findChild<QToolButton *>("conversationInsert"),
                  "Available actions are not in a persistent right-hand pane");
            chooseEntry(question.prompt);
            auto questionRow = treeItem(outline, "Otherwise / fallback")->parent();
            check(questionRow->child(questionRow->childCount() - 1)->text(0) ==
                      "Otherwise / fallback",
                  "Fallback precedes explicit answers");
            int sections = 0;
            for (auto group : editor.findChildren<QGroupBox *>())
                sections += group->property("dialogueSectionEntry").isValid();
            check(sections == 4, "Question prompt, ordered answers and fallback are not together");
            auto paragraph = [&](int entry) -> QPlainTextEdit * {
                for (auto field : editor.findChildren<QPlainTextEdit *>("conversationBlockText"))
                    if (field->property("dialogueEntry").toInt() == entry)
                        return field;
                throw std::runtime_error("Question paragraph missing");
            };
            paragraph(question.prompt)->setPlainText("What is the password?");
            paragraph(question.fallback)->setPlainText("I do not believe thee.");
            check(editor.flush() && commits == 1,
                  "Question edits were not one validated transaction");
            auto modified = Dialogue::parse(U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes);
            check(modified.entries[question.prompt].bytes.startsWith(QByteArray::fromHex("9091")) &&
                      modified.entries[question.replies[0].response].bytes ==
                          original.entries[question.replies[0].response].bytes &&
                      modified.entries[original.tail].bytes ==
                          original.entries[original.tail].bytes,
                  "Question edit changed reply behavior, label or structural tail");
            // Incomplete drafts remain visible and cannot partially apply other sections.
            const auto valid = p.data("TOWNE.TLK");
            paragraph(question.prompt)->setPlainText("Valid draft");
            paragraph(question.fallback)->setPlainText(QString::fromUtf8("é"));
            QTimer::singleShot(0, [] {
                auto dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                check(dialog, "Invalid draft did not offer navigation cancellation");
                dialog->done(QMessageBox::Cancel);
            });
            check(!editor.flush() && p.data("TOWNE.TLK") == valid &&
                      paragraph(question.fallback)->toPlainText() == QString::fromUtf8("é"),
                  "Invalid question draft was lost or partially committed");
            paragraph(question.fallback)->setPlainText("Otherwise.");
            check(editor.flush(), "Corrected question draft did not commit");
            for (auto button : editor.findChildren<QPushButton *>())
                if (button->text() == "Playtest") {
                    button->click();
                    break;
                }
            check(!tools->isHidden() &&
                      editor.findChild<QWidget *>("conversationTestSetup")->isHidden(),
                  "Playtest is not optional or its setup crowds the transcript");
            auto viewport =
                editor.findChild<Dialogue::NativeTextViewport *>("conversationNativeViewport");
            check(!editor.findChild<QLineEdit *>("conversationInput"),
                  "Qt reply input still bypasses native editing");
            nativeKeys(viewport, "help\rdawn\r");
            auto transcript = editor.findChild<QTreeWidget *>("conversationTranscript");
            QTreeWidgetItem *wait = nullptr;
            waitFor([&] {
                for (int i = 0; i < transcript->topLevelItemCount(); ++i)
                    if (transcript->topLevelItem(i)->text(0).contains("Wait for key"))
                        wait = transcript->topLevelItem(i);
                return wait != nullptr;
            });
            check(wait && wait->isHidden(), "Action traces clutter ordinary NPC speech");
            editor.findChild<QCheckBox *>("conversationShowTrace")->setChecked(true);
            check(!wait->isHidden() && wait->data(0, Qt::UserRole + 2).toInt() >= 0,
                  "Execution trace lacks a source byte offset");
            QMetaObject::invokeMethod(transcript, "itemClicked", Qt::DirectConnection,
                                      Q_ARG(QTreeWidgetItem *, wait), Q_ARG(int, 0));
            auto target = editor.findChild<QFrame *>("conversationAction");
            check(target && target->property("dialogueOpcode").toInt() == 143 &&
                      target->frameShape() == QFrame::Box,
                  "Clicking trace did not identify its exact source action");
            const auto tested = p.data("TOWNE.TLK");
            tools->hide();
            check(p.data("TOWNE.TLK") == tested, "Playtest altered the mod");
            chooseEntry(question.replies[0].response);
            app.processEvents();
            app.processEvents();
            for (auto field : editor.findChildren<QPlainTextEdit *>("conversationBlockText"))
                if (field->isVisible())
                    check(field->parentWidget()->rect().contains(field->geometry()),
                          "Question paragraph was squeezed outside its card");
            for (auto card : editor.findChildren<QFrame *>("conversationAction"))
                if (card->isVisible())
                    for (auto button : card->findChildren<QToolButton *>())
                        if (button->isVisible())
                            check(card->rect().contains(button->geometry()) &&
                                      button->height() >= button->minimumSizeHint().height(),
                                  "Question action controls were vertically clipped");
            auto documentScroll = qobject_cast<QScrollArea *>(authoring->widget(0));
            check(documentScroll && documentScroll->verticalScrollBar()->maximum() > 0,
                  "Long question sections do not scroll as one document");
            const QString screenshots = qEnvironmentVariable("IMPERA_WORKSHOP_SCREENSHOT_DIR");
            if (!screenshots.isEmpty())
                editor.grab().save(screenshots + "/dialogue-redesign.png");
            auto available = editor.findChild<QListWidget *>("conversationAvailableActions");
            QListWidgetItem *avatarAction = nullptr;
            for (int i = 0; i < available->count(); ++i)
                if (available->item(i)->data(Qt::UserRole).toInt() == 129)
                    avatarAction = available->item(i);
            check(avatarAction && (avatarAction->flags() & Qt::ItemIsEnabled),
                  "Avatar insertion is not available in the action pane");
            QMetaObject::invokeMethod(available, "itemClicked", Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem *, avatarAction));
            const auto inserted = Dialogue::parse(U5::readDialogue(p.data("TOWNE.TLK"))[0].bytes);
            check(inserted.entries[question.replies[0].response].bytes.endsWith(QByteArray::fromHex("8182")),
                  "Action pane inserted unreachable text after the answer's terminal action");

        });
        test("Inline structure changes retain action identity and project undo", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            auto original = U5::writeDialogue(
                {{1,
                  U5::encodeText(
                      "Name<Entry>Description<Entry>Greeting<Entry>Speak to <Avatar> now.<Key Wait>"
                      "Fare thee well.<End Conversation><Entry>Bye<Entry><Any><Label 15>@")}});
            file(dir.path() + "/TOWNE.TLK", original);
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Inline history fixture failed");
            window.show();
            window.selectResource("TOWNE.TLK");
            auto activeEditor = [&] {
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                return dynamic_cast<ConversationEditor *>(
                    window.findChild<QStackedWidget *>("workspaces")
                        ->currentWidget()
                        ->findChild<QWidget *>("conversationEditor"));
            };
            auto editor = activeEditor();
            auto outline = editor->findChild<QTreeWidget *>("conversationOutline");
            outline->setCurrentItem(treeItem(outline, "Job / Work"));
            auto surface = editor->findChild<DialogueTextSurface *>("conversationInlineText");
            check(surface, "Inline authoring surface missing");
            surface->setFocus();
            auto cursor = surface->textCursor();
            cursor.movePosition(QTextCursor::End);
            surface->setTextCursor(cursor);
            surface->insertToken(129);
            auto actionMenu = [&](int opcode) -> QMenu * {
                for (auto card : editor->findChildren<QFrame *>("conversationAction")) {
                    if (card->property("dialogueOpcode").toInt() != opcode)
                        continue;
                    for (auto button : card->findChildren<QToolButton *>())
                        if (button->menu())
                            return button->menu();
                }
                throw std::runtime_error("Action card menu missing");
            };
            auto trigger = [](QMenu *menu, const QString &text) {
                for (auto action : menu->actions())
                    if (action->text() == text) {
                        action->trigger();
                        return;
                    }
                throw std::runtime_error("Expected action menu item missing");
            };
            trigger(actionMenu(143), "Copy action");
            const auto inserted = window.projectForTests().data("TOWNE.TLK");
            check(inserted != original, "Copy action did not commit the pending inline insertion");
            check(QApplication::clipboard()->mimeData()->data(
                      "application/x-impera-dialogue-action") == QByteArray(1, char(143)),
                  "Inline insertion shifted an action menu onto the wrong block");
            trigger(actionMenu(143), "Remove action");
            auto removed = window.projectForTests().data("TOWNE.TLK");
            check(removed != inserted, "Explicit action removal did not commit");
            auto undo = [&] {
                for (auto action : window.findChildren<QAction *>())
                    if (action->text().startsWith("Undo ")) {
                        action->trigger();
                        app.processEvents();
                        return;
                    }
                throw std::runtime_error("Undo action missing");
            };
            undo();
            check(window.projectForTests().data("TOWNE.TLK") == inserted,
                  "Action removal undo did not restore exact bytes");
            undo();
            check(window.projectForTests().data("TOWNE.TLK") == original,
                  "Inline insertion undo did not restore the original conversation");
        });
        test("Structured conversation widgets, aliases, undo and annotations", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/IBM.CH", QByteArray(1024, char(255)));
            file(dir.path() + "/RUNES.CH", QByteArray(1024, char(255)));
            auto bytes =
                U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry>"
                               "help<Entry><Or><Entry>hint<Entry>Hello<Label 1><Entry><Any><Label "
                               "1>Question?<Entry>No<Entry>y<Entry>Yes<End "
                               "Conversation><Entry><Any><Label 15>@");
            auto original = U5::writeDialogue({{1, bytes}});
            file(dir.path() + "/TOWNE.TLK", original);
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Cannot open structured fixture");
            w.show();
            w.selectResource("TOWNE.TLK");
            app.processEvents();
            auto active = [&] {
                return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto editor = dynamic_cast<ConversationEditor *>(
                active()->findChild<QWidget *>("conversationEditor"));
            check(editor, "Structured conversation editor absent");
            auto block = editor->findChild<QPlainTextEdit *>("conversationBlockText");
            check(block && block->toPlainText() == "Name", "Basic text not readable");
            auto framing = editor->findChildren<QLabel *>("conversationEngineText");
            check(framing.size() == 1 && framing[0]->text() == "My name is " &&
                      framing[0]->toolTip().isEmpty(),
                  "Name is missing its read-only engine dialogue framing");

            block->setPlainText("New Name");
            check(editor->flush(), "Valid basic text did not apply");
            auto next =
                Dialogue::parse(U5::readDialogue(w.projectForTests().data("TOWNE.TLK"))[0].bytes);
            check(next.entries[1].bytes == Dialogue::parse(bytes).entries[1].bytes,
                  "Basic edit changed unrelated entry");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            check(w.projectForTests().data("TOWNE.TLK") == original,
                  "Structured undo did not restore exact bytes");
            editor = dynamic_cast<ConversationEditor *>(
                active()->findChild<QWidget *>("conversationEditor"));
            auto tree = editor->findChild<QTreeWidget *>("conversationOutline");
            tree->setCurrentItem(treeItem(tree, "Job / Work"));
            framing = editor->findChildren<QLabel *>("conversationEngineText");
            check(framing.isEmpty() && w.projectForTests().data("TOWNE.TLK") == original,
                  "Job contains quote framing or viewing it changed dialogue bytes");
            tree->setCurrentItem(treeItem(tree, "help / hint"));
            auto aliases = editor->findChild<QLineEdit *>("conversationAliases");
            check(aliases && aliases->text() == "help, hint", "Shared aliases absent");
            aliases->setText("help, clue");
            QMetaObject::invokeMethod(aliases, "editingFinished", Qt::DirectConnection);
            next =
                Dialogue::parse(U5::readDialogue(w.projectForTests().data("TOWNE.TLK"))[0].bytes);
            check(Dialogue::keyword(next.entries[next.topics[0].keywords[1]].bytes) == "clue",
                  "Alias edit failed");
            check(
                next.entries[next.topics[0].response].bytes ==
                    Dialogue::parse(bytes).entries[Dialogue::parse(bytes).topics[0].response].bytes,
                "Alias edit changed shared response");
            auto before = w.projectForTests().data("TOWNE.TLK");
            aliases->setText("help, ");
            QMetaObject::invokeMethod(aliases, "editingFinished", Qt::DirectConnection);
            check(w.projectForTests().data("TOWNE.TLK") == before,
                  "Invalid alias draft changed the project");
            aliases->setText("help, clue");
            QMetaObject::invokeMethod(aliases, "editingFinished", Qt::DirectConnection);
            check(editor->flush(), "Corrected alias draft did not apply");
            for (auto b : editor->findChildren<QPushButton *>())
                if (b->text() == "Restart") {
                    b->click();
                    break;
                }
            nativeKeys(
                editor->findChild<Dialogue::NativeTextViewport *>("conversationNativeViewport"),
                "help\r");
            auto transcript = editor->findChild<QTreeWidget *>("conversationTranscript");
            waitFor([&] { return transcript->topLevelItemCount() > 1; });
            check(w.projectForTests().data("TOWNE.TLK") == before, "Simulation changed project");
            w.projectForTests().dialogueNames["TOWNE.TLK/1/1"] = "Password question";
            QString path = dir.path() + "/test.imperaproject";
            w.projectForTests().save(path);
            Project loaded;
            loaded.load(path);
            check(loaded.dialogueNames.value("TOWNE.TLK/1/1") == "Password question",
                  "Question names not saved");
            check(loaded.data("TOWNE.TLK") == before, "Annotation changed game data");
            check(!Dialogue::inventoryOptions().value(65).isEmpty() &&
                      Dialogue::inventoryOptions().value(16) == "Dagger",
                  "Engine inventory labels incorrect");
        });
        test("New Mod reuses the selected game folder without a picker", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            WorkshopWindow window;
            check(window.openGame(dir.path()), "New Mod fixture open");
            for (auto action : window.findChildren<QAction *>())
                if (action->text() == "New Mod") {
                    action->trigger();
                    break;
                }
            check(window.projectForTests().sourceDirectory == QDir(dir.path()).absolutePath() &&
                      window.projectForTests().changed().isEmpty(),
                  "New Mod did not reuse the remembered source");
        });
        test("Mod validation, decoder verification and installed-package conflicts", [&] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            project.resources["INIT.GAM"].edited[0] = 1;
            auto report = validateMod(project);
            check(std::none_of(report.begin(), report.end(),
                               [](const auto &d) { return d.severity == ModDiagnostic::Error; }),
                  "Valid mod failed validation");
            QDir(dir.path()).mkdir("Mods");
            file(dir.path() + "/Mods/A.imperamod", project.package());
            file(dir.path() + "/Mods/B.imperamod", project.package());
            file(dir.path() + "/Mods/broken.imperamod", "broken");
            report = validateMod(project, dir.path() + "/Mods");
            check(std::any_of(report.begin(), report.end(),
                              [](const auto &d) {
                                  return d.resource == "INIT.GAM" &&
                                         d.message.contains("also replaces");
                              }),
                  "Installed resource conflict not reported");
            check(std::any_of(report.begin(), report.end(),
                              [](const auto &d) {
                                  return d.message.contains("earlier installed package");
                              }),
                  "Installed package rejection order not reported");
            check(std::any_of(report.begin(), report.end(),
                              [](const auto &d) { return d.message.contains("cannot load"); }),
                  "Broken installed package not reported");
            project.resources["INIT.GAM"].edited.resize(3);
            report = validateMod(project);
            check(std::any_of(report.begin(), report.end(),
                              [](const auto &d) {
                                  return d.resource == "INIT.GAM" &&
                                         d.severity == ModDiagnostic::Error;
                              }),
                  "Validation error does not link to resource");
        });
        test("Isolated test sessions preserve originals and exclude personal data", [&] {
            QTemporaryDir source, cache;
            auto project = fixture(source.path());
            project.resources["INIT.GAM"].edited[0] = 42;
            QDir(source.path()).mkdir("Mods");
            QDir(source.path()).mkdir("SAVEGAME");
            file(source.path() + "/Mods/other.imperamod", "other");
            file(source.path() + "/SAVEGAME/slot", "personal");
            file(source.path() + "/DATA.CFG", "personal-config");
            file(source.path() + "/SAVED.GAM", "personal-save");
            auto before = project.resources["INIT.GAM"].original;
            QString first = prepareModTest(project, cache.path());
            QString second = prepareModTest(project, cache.path());
            check(first != second && QDir(first).exists(), "Test sessions are not separate");
            auto read = [](const QString &path) {
                QFile file(path);
                check(file.open(QIODevice::ReadOnly), "Cannot read test output");
                return file.readAll();
            };
            check(read(source.path() + "/init.gam") == before &&
                      read(first + "/INIT.GAM") == before,
                  "Test creation changed source or applied edits to copied base");
            Project test;
            test.openGame(first);
            test.importPackage(read(first + "/Mods/workshop.imperamod"));
            check(test.data("INIT.GAM") == project.data("INIT.GAM"),
                  "Isolated package does not reproduce editor data");
            check(!QFile::exists(first + "/SAVED.GAM") && !QDir(first + "/SAVEGAME").exists() &&
                      QDir(first + "/Mods").entryList(QDir::Files).size() == 1 &&
                      read(first + "/DATA.CFG") == (first + "\n\n").toUtf8(),
                  "Test session copied personal saves, settings or installed mods");
            file(source.path() + "/init.gam", QByteArray(4192, 7));
            rejects([&] { prepareModTest(project, cache.path()); });
            check(QDir(cache.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() == 2,
                  "Failed test preparation left a partial runtime");
        });
        test("Compact map-wide schedule hours and floor selector alignment", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 5));
            QByteArray npcs(4608, 0);
            for (int actor = 0; actor < 32; ++actor)
                for (int destination = 0; destination < 3; ++destination) {
                    npcs[actor * 16 + 3 + destination] = char(255);
                    npcs[actor * 16 + 6 + destination] = char(255);
                }
            for (int destination = 0; destination < 3; ++destination) {
                npcs[3 + destination] = 2;
                npcs[6 + destination] = 2;
            }
            for (int change = 0; change < 4; ++change)
                npcs[12 + change] = char(8 + change * 4);
            file(dir.path() + "/TOWNE.NPC", npcs);
            auto project = fixture(dir.path());
            project.openGame(dir.path());
            QMap<QString, MapViewState> states;
            QMap<QString, int> navigation;
            MapWorkspace workspace(
                &project, "TOWNE.DAT", &states, &navigation,
                [](const auto &, const auto &) { return true; }, [](const auto &) {});
            workspace.resize(1024, 720);
            workspace.show();
            app.processEvents();
            auto schedule = workspace.findChild<QComboBox *>("mapSchedule");
            auto tools = workspace.findChild<QButtonGroup *>("mapTools");
            check(schedule->itemText(0) == "08:00" && schedule->itemText(3) == "20:00",
                  "Consistent map-wide hours are not shown");
            auto floors = workspace.findChild<QComboBox *>("mapPage");
            check(qAbs(schedule->mapTo(&workspace, QPoint()).y() -
                       floors->mapTo(&workspace, QPoint()).y()) < 10,
                  "Schedule selector is not beside the floor selector");
            check(!workspace.findChild<QCheckBox *>("mapGrid") &&
                      !workspace.findChild<QCheckBox *>("mapRectangleOutline"),
                  "Removed map controls are still present");
            for (const char *name : {"mapCopy", "mapPaste", "mapClearSelection"}) {
                auto control = workspace.findChild<QWidget *>(name);
                check(control &&
                          qAbs(control->mapTo(&workspace, QPoint()).y() -
                               tools->button(MapCanvas::Pencil)->mapTo(&workspace, QPoint()).y()) <
                              10,
                      "Terrain action is not on the tool row");
            }
            WorkshopWindow viewWindow;
            check(viewWindow.openGame(dir.path()), "View menu fixture open");
            viewWindow.selectResource("TOWNE.DAT");
            auto viewWorkspace = viewWindow.findChild<QStackedWidget *>("workspaces")->currentWidget();
            auto viewMenu = viewWorkspace->findChild<QToolButton *>("mapViewOptions");
            check(viewMenu && viewMenu->text() == "View" && viewMenu->menu()->actions().size() == 2 &&
                      !viewWorkspace->findChild<QToolButton *>("mapItemsMenu"),
                  "View menu should contain only the two display toggles");
            auto highlight = viewWorkspace->findChild<QAction *>("viewHighlightChanges");
            auto markers = viewWorkspace->findChild<QAction *>("viewContentsMarkers");
            check(highlight && markers && highlight->isCheckable() && markers->isCheckable(),
                  "View settings are not checkable");
            auto canvas = dynamic_cast<MapCanvas *>(viewWorkspace->findChild<QWidget *>("mapCanvas"));
            highlight->setChecked(true);
            check(canvas->comparison, "View menu did not enable change highlighting");
            markers->setChecked(false);
            check(!canvas->showContentsMarkers, "View menu did not hide contents markers");
            highlight->setChecked(false);
            markers->setChecked(true);
            check(floors->count() == 2 && floors->findData(0) >= 0 && floors->findData(1) >= 0 &&
                      floors->findData(6) < 0,
                  "Floor selector includes other locations");
            auto tree = workspace.findChild<QTreeWidget *>("mapNavigation");
            tree->setCurrentItem(treeItem(tree, "Yew", true));
            app.processEvents();
            check(floors->currentData().toInt() == 7 && floors->count() == 2 &&
                      floors->findData(0) < 0,
                  "Location heading did not open its main floor");
            tree->setCurrentItem(treeItem(tree, "Moonglow", true));
            app.processEvents();
            npcs[16 + 3] = 3;
            npcs[16 + 6] = 3;
            npcs[16 + 12] = 9;
            project.resources["TOWNE.NPC"].edited = npcs;
            MapWorkspace mixed(
                &project, "TOWNE.DAT", &states, &navigation,
                [](const auto &, const auto &) { return true; }, [](const auto &) {});
            auto mixedSchedule = mixed.findChild<QComboBox *>("mapSchedule");
            for (int change = 0; change < 4; ++change)
                check(mixedSchedule->itemText(change) == QString::number(change + 1),
                      "Different NPC hours should produce numbered schedule labels");
            mixed.findChild<QComboBox *>("mapNpcList")->setCurrentIndex(0);
            mixed.findChild<QComboBox *>("mapNpcList")->setCurrentIndex(1);
            check(mixedSchedule->itemText(0) == "1",
                  "Selected NPC must not redefine map-wide schedule labels");
        });
        test("Verified tile descriptions, category search and document-only exports", [&] {
            check(MapDocument::tileName(5) == "Grass" && MapDocument::tileCategory(5) == "Ground",
                  "Verified grass label/category missing");
            check(MapDocument::tileName(0x4f) == "Wall" &&
                      MapDocument::tileCategory(0x4f) == "Buildings",
                  "Verified wall category missing");
            check(MapDocument::tileName(6) == "Brush" && MapDocument::tileCategory(6) == "Ground",
                  "Grass variation missing from catalog");
            for (int id = 0; id < 256; ++id)
                check(QStringList{"Overworld", "Ground", "Buildings", "Objects", "Other"}.contains(
                          MapDocument::tileCategory(id)) &&
                          MapDocument::tileName(id) != "Unidentified tile",
                      "Tile catalog is incomplete");
            for (int id : {0x27, 0x28, 0x43, 0x4a, 0x4b, 0x4d, 0x5a, 0x87, 0xca, 0xcb})
                check(MapDocument::tileCategory(id) == "Buildings",
                      "Architectural tile was misclassified");
            for (int id : {0x46, 0x58, 0x59, 0x5e, 0x5f, 0x84, 0x85, 0x88, 0x8b, 0x8d, 0x8e, 0xa2,
                           0xa3, 0xaa, 0xbf, 0xdc, 0xe8, 0xec})
                check(MapDocument::tileCategory(id) == "Objects",
                      "Object artwork leaked into Ground or Buildings");
            for (int id : {0x10, 0x11, 0x16, 0x18, 0x1a, 0x39, 0x3a, 0x3e, 0xc0, 0xcf, 0xdf, 0xe0, 0xe1, 0xe2})
                check(MapDocument::tileCategory(id) == "Other",
                      "Location icon or rendering fragment treated as ordinary terrain");
            check(MapDocument::tileName(0x8d) == "Harpsichord" &&
                      MapDocument::tileName(0x88) == "Cannonballs" &&
                      MapDocument::tileName(0x27) == "Roof",
                  "Audited object labels regressed");
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray bytes(352, char(0xee));
            for (int y = 0; y < 11; ++y)
                for (int x = 0; x < 11; ++x)
                    bytes[y * 32 + x] = char((y * 11 + x) % 256);
            file(dir.path() + "/BRIT.CBT", bytes);
            QByteArray location = QByteArray(16384, 5);
            location[16383] = char(0x50);
            file(dir.path() + "/TOWNE.DAT", location);
            QByteArray worldChunks(512, char(0xfa));
            worldChunks.replace(0, 256, QByteArray(256, 5));
            worldChunks[0] = char(0x4f);
            worldChunks[1] = char(0xd4);
            QByteArray overlay(0x3986, char(255));
            overlay[0x3886] = 0;
            file(dir.path() + "/BRIT.DAT", worldChunks);
            file(dir.path() + "/DATA.OVL", overlay);
            project.openGame(dir.path());
            MapDocument document(&project, "BRIT.CBT");
            QVector<QImage> tiles;
            for (int id = 0; id < 256; ++id) {
                QImage tile(16, 16, QImage::Format_RGB32);
                tile.fill(QColor(id, 0, 0));
                tiles.append(tile);
            }
            auto image = document.terrainImage(0, tiles, false);
            auto ids = document.terrainImage(0, tiles, true);
            check(image.size() == QSize(176, 176) && ids.size() == QSize(11, 11),
                  "Export dimensions wrong");
            for (int y = 0; y < 11; ++y)
                for (int x = 0; x < 11; ++x)
                    check(ids.constScanLine(y)[x] == y * 11 + x &&
                              image.pixelColor(x * 16, y * 16).red() == y * 11 + x,
                          "Exports do not reflect stored terrain");
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Palette fixture open");
            window.selectResource("BRIT.CBT");
            window.resize(1024, 720);
            window.show();
            app.processEvents();
            app.processEvents();
            auto palette = window.findChild<QListWidget *>("mapPalette");
            auto search = window.findChild<QLineEdit *>("mapTileSearch");
            auto category = window.findChild<QComboBox *>("mapPaletteFilter");
            check(palette && search && category, "Tile browser absent");
            for (int id = 0; id < 256; ++id)
                check(palette->item(id)->text().isEmpty() &&
                          !palette->item(id)->toolTip().isEmpty(),
                      "Palette tiles must be icon-only with descriptive tooltips");
            auto tools = window.findChild<QButtonGroup *>("mapTools");
            check(tools && tools->buttons().size() == 7 &&
                      !window.findChild<QComboBox *>("mapTool"),
                  "Icon toolbar missing");
            for (auto button : tools->buttons())
                check(!button->toolTip().isEmpty() && !button->accessibleName().isEmpty(),
                      "Tool is missing help or an accessible name");
            search->setText("grass");
            check(!palette->item(5)->isHidden() && palette->item(0x4f)->isHidden(),
                  "Name search failed");
            search->clear();
            check(category->count() == 6 && category->itemText(1) == "World" &&
                      category->itemText(2) == "Locations" && category->itemText(3) == "NPCs" &&
                      category->itemText(4) == "Monsters" && category->itemText(5) == "Special",
                  "Palette must offer terrain usage and placement filters");
            category->setCurrentText("World");
            check(!palette->item(5)->isHidden() && !palette->item(0x4f)->isHidden() &&
                      !palette->item(0xd4)->isHidden() && palette->item(0xd7)->isHidden() &&
                      palette->item(0xfa)->isHidden() && !palette->item(0x39)->isHidden(),
                  "World membership must use terrain cells, without inferred frames or unused "
                  "chunks");
            category->setCurrentText("Locations");
            check(!palette->item(5)->isHidden() && !palette->item(0x50)->isHidden() &&
                      palette->item(0xfa)->isHidden(),
                  "Location filter missed floor terrain or counted combat map metadata");
            check(palette->item(5)->toolTip().contains("World · Locations"),
                  "Tile usage categories must overlap");
            category->setCurrentText("All tiles");
            check(!palette->item(0xfa)->isHidden(), "All tiles must include unused artwork");

            check(!search->accessibleName().isEmpty() && !category->accessibleName().isEmpty(),
                  "Accessible control labels absent");
        });
        test("Keyboard canvas editing, selection, paste and character movement", [&] {
            MapCanvas canvas;
            canvas.side = 4;
            canvas.ids = QByteArray(16, 1);
            canvas.original = canvas.ids;
            canvas.brush = 5;
            canvas.resizeMap();
            canvas.show();
            canvas.setFocus();
            app.processEvents();
            int commits = 0;
            canvas.commit = [&](const QByteArray &) {
                ++commits;
                return true;
            };
            auto key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
                QKeyEvent event(QEvent::KeyPress, code, modifiers);
                QApplication::sendEvent(&canvas, &event);
            };
            key(Qt::Key_Right);
            key(Qt::Key_Down);
            key(Qt::Key_Return);
            check(commits == 1 && U5::byte(canvas.ids, 5) == 5 && U5::byte(canvas.ids, 0) == 1,
                  "Keyboard pencil failed or painted wrong tile");
            key(Qt::Key_Right, Qt::ShiftModifier);
            check(canvas.selection == QRect(1, 1, 2, 1), "Keyboard selection not extended");
            key(Qt::Key_C, Qt::ControlModifier);
            key(Qt::Key_Down);
            key(Qt::Key_V, Qt::ControlModifier);
            check(canvas.pasting(), "Keyboard paste preview missing");
            key(Qt::Key_Return);
            check(commits == 2 && !canvas.pasting(), "Enter did not commit paste once");
            canvas.tool = MapCanvas::InspectNpc;
            canvas.selectedNpc = 7;
            QPoint destination;
            int selected = -1;
            canvas.moveNpc = [&](int actor, QPoint point) {
                selected = actor;
                destination = point;
                return true;
            };
            auto before = canvas.ids;
            key(Qt::Key_Left);
            key(Qt::Key_Return);
            check(selected == 7 && destination == QPoint(1, 2) && canvas.ids == before,
                  "Keyboard actor movement changed terrain or wrong position");
        });
        test("Persistent game directory restoration and stale folder fallback", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            WorkshopWindow first;
            check(first.openGame(dir.path()), "Remembered folder open");
            WorkshopWindow second;
            check(second.restoreGameDirectory(), "Game folder did not restore in new window");
            check(second.projectForTests().sourceDirectory ==
                      first.projectForTests().sourceDirectory,
                  "Restored wrong game folder");
            QSettings().setValue("paths/game", dir.path() + "/missing");
            WorkshopWindow missing;
            check(!missing.restoreGameDirectory() && missing.projectForTests().resources.isEmpty(),
                  "Stale game folder did not return to welcome screen");
        });
        test("Combat encounter coordinates and read-only trigger preview", [&] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray bytes(352, char(0xee));
            for (int row = 0; row < 11; ++row)
                for (int x = 0; x < 11; ++x)
                    bytes[row * 32 + x] = 1;
            const int rows[] = {4, 1, 3, 2};
            for (int row : rows) {
                bytes[row * 32 + 11] = 2;
                bytes[row * 32 + 17] = 3;
            }
            bytes[6 * 32 + 11] = 2;
            bytes[7 * 32 + 11] = 3;
            bytes[8 * 32 + 11] = 2;
            bytes[8 * 32 + 19] = 3;
            bytes[11] = 42;
            bytes[9 * 32 + 11] = 4;
            bytes[9 * 32 + 19] = 5;
            bytes[10 * 32 + 11] = 6;
            bytes[10 * 32 + 19] = 7;
            file(dir.path() + "/BRIT.CBT", bytes);
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Combat fixture open");
            check(QSettings().value("paths/game").toString() ==
                      window.projectForTests().sourceDirectory,
                  "Selected game directory was not persisted");
            window.selectResource("BRIT.CBT");
            window.show();
            app.processEvents();
            app.processEvents();
            // QWidget subclasses without Q_OBJECT use dynamic_cast rather than Qt casts.
            MapCanvas *canvas = nullptr;
            for (auto widget : window.findChildren<QWidget *>())
                if (auto candidate = dynamic_cast<MapCanvas *>(widget))
                    canvas = candidate;
            check(canvas != nullptr, "Combat canvas missing");
            window.findChild<QButtonGroup *>("mapTools")->button(MapCanvas::InspectNpc)->click();
            auto entry = window.findChild<QComboBox *>("combatEntry");
            auto entity = window.findChild<QComboBox *>("combatEntity");
            check(entry && entity && entity->count() == 30, "Encounter record list missing");
            auto mouse = [&](QEvent::Type type, QPoint cell, Qt::MouseButtons held) {
                QPointF point((cell.x() + 0.5) * 16 * canvas->zoom,
                              (cell.y() + 0.5) * 16 * canvas->zoom);
                QMouseEvent event(type, point, point, Qt::LeftButton, held, Qt::NoModifier);
                QApplication::sendEvent(canvas, &event);
            };
            for (int direction = 0; direction < 4; ++direction) {
                entry->setCurrentIndex(direction);
                entity->setCurrentIndex(0);
                auto before = window.projectForTests().data("BRIT.CBT");
                mouse(QEvent::MouseButtonPress, {2, 3}, Qt::LeftButton);
                mouse(QEvent::MouseMove, {8, 9}, Qt::LeftButton);
                check(window.projectForTests().data("BRIT.CBT") == before, "Drag committed early");
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(canvas, &escape);
                mouse(QEvent::MouseButtonRelease, {8, 9}, Qt::NoButton);
                check(window.projectForTests().data("BRIT.CBT") == before,
                      "Cancelled drag mutated data");
                mouse(QEvent::MouseButtonPress, {2, 3}, Qt::LeftButton);
                mouse(QEvent::MouseMove, {8, 9}, Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease, {8, 9}, Qt::NoButton);
                before[rows[direction] * 32 + 11] = 8;
                before[rows[direction] * 32 + 17] = 9;
                check(window.projectForTests().data("BRIT.CBT") == before,
                      "Party coordinate edit changed unrelated bytes");
            }
            for (int id : {6, 22}) {
                entity->setCurrentIndex(id);
                auto expected = window.projectForTests().data("BRIT.CBT");
                mouse(QEvent::MouseButtonPress, {2, 3}, Qt::LeftButton);
                mouse(QEvent::MouseMove, {8, 9}, Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease, {8, 9}, Qt::NoButton);
                int xo = id == 6 ? 6 * 32 + 11 : 8 * 32 + 11;
                int yo = id == 6 ? 7 * 32 + 11 : 8 * 32 + 19;
                expected[xo] = 8;
                expected[yo] = 9;
                check(window.projectForTests().data("BRIT.CBT") == expected,
                      "Encounter edit changed unrelated bytes");
            }
            auto beforePreview = window.projectForTests().data("BRIT.CBT");
            window.findChild<QCheckBox *>("combatPreview")->setChecked(true);
            check(U5::byte(canvas->ids, 5 * 11 + 4) == 42 &&
                      U5::byte(canvas->ids, 7 * 11 + 6) == 42,
                  "Trigger preview did not replace both linked cells");
            check(window.projectForTests().data("BRIT.CBT") == beforePreview,
                  "Trigger preview mutated stored data");
            MapDocument exportDocument(&window.projectForTests(), "BRIT.CBT");
            check(exportDocument.terrainImage(0, canvas->tiles, true).constScanLine(5)[4] == 1,
                  "Terrain export leaked displayed trigger preview");
            window.findChild<QButtonGroup *>("mapTools")->button(MapCanvas::Pencil)->click();
            check(U5::byte(canvas->ids, 5 * 11 + 4) == 1,
                  "Trigger preview leaked into terrain tools");
            window.findChild<QButtonGroup *>("mapTools")->button(MapCanvas::InspectNpc)->click();
            for (auto action : window.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            auto undone = beforePreview;
            undone[8 * 32 + 11] = 2;
            undone[8 * 32 + 19] = 3;
            check(window.projectForTests().data("BRIT.CBT") == undone,
                  "Encounter undo changed unrelated bytes");
            check(window.findChild<QButtonGroup *>("mapTools")->checkedId() ==
                          MapCanvas::InspectNpc &&
                      window.findChild<QComboBox *>("combatEntity")->currentIndex() == 22 &&
                      window.findChild<QComboBox *>("combatEntry")->currentIndex() == 3,
                  "Encounter undo lost selected record, mode or entry direction");
            for (auto action : window.findChildren<QAction *>())
                if (action->text().startsWith("Redo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            check(window.projectForTests().data("BRIT.CBT") == beforePreview,
                  "Encounter redo failed");
        });
        if (qEnvironmentVariableIsSet("U5_GAME_DIR"))
            test("Original resource compatibility and native workspaces", [&] {
                Project p;
                p.openGame(qEnvironmentVariable("U5_GAME_DIR"));
                for (auto name : p.resources.keys()) {
                    auto b = p.data(name);
                    if (name.endsWith(".TLK"))
                        for (auto e : U5::readDialogue(b)) {
                            auto d = Dialogue::parse(e.bytes);
                            check(d.bytes() == e.bytes, "Original dialogue structure no-op");
                            check(d.structuralError.isEmpty(),
                                  "Original dialogue has unsupported structure");
                            for (auto issue : d.issues())
                                check(!issue.error,
                                      "Original dialogue rejected by semantic validation");
                        }
                    if (name.endsWith(".16")) {
                        auto g = U5::readGraphics(name, b);
                        check(U5::writeGraphics(g) == b, "Original graphics no-op");
                    } else if (name.endsWith(".TLK")) {
                        for (auto e : U5::readDialogue(b))
                            check(U5::encodeText(U5::decodeText(e.bytes)) == e.bytes,
                                  "Original dialogue text roundtrip");
                    }
                }
                U5::storyPages(p.data("STORY.DAT"));
                WorkshopWindow w;
                check(w.openGame(p.sourceDirectory), "Original folder load");
                for (auto name : p.resources.keys()) {
                    w.selectResource(name);
                    app.processEvents();
                }
                w.show();
                w.findChild<QComboBox *>("contentWorkspace")->setCurrentText("Dialogue");
                auto characterTree =
                    w.findChild<QWidget *>("resourceLibrary")->findChild<QTreeWidget *>();
                const auto originalCharacters = Workshop::characterDestinations(p);
                for (const auto &character : originalCharacters) {
                    if (character.character < 0 || character.conversation < 0)
                        continue;
                    QTreeWidgetItemIterator it(characterTree);
                    while (*it && (*it)->data(0, Qt::UserRole + 2).toString() != character.id)
                        ++it;
                    check(bool(*it), "Original character destination missing");
                    characterTree->setCurrentItem(*it);
                    app.processEvents();
                    auto root = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
                    auto modes = root->findChild<QTabWidget *>("characterModes");
                    check(modes && modes->count() == 1 && modes->tabBar()->isHidden(), "Original NPC still has a schedule tab");
                    const auto captures = qEnvironmentVariable("IMPERA_WORKSHOP_SCREENSHOT_DIR");
                    for (int mode = 0; mode < modes->count(); ++mode) {
                        modes->setCurrentIndex(mode);
                        app.processEvents();
                        if (!captures.isEmpty()) {
                            QDir().mkpath(captures);
                            w.resize(1320, 860);
                            app.processEvents();
                            check(w.grab().save(captures + "/workshop-original-character-" +
                                                QString::number(mode) + ".png"),
                                  "Could not capture character UI");
                        }
                    }
                    break;
                }
                for (const auto &npc : originalCharacters) {
                    if (npc.title != "Telila") continue;
                    QTreeWidgetItemIterator it(characterTree);
                    while (*it && (*it)->data(0, Qt::UserRole + 2).toString() != npc.id) ++it;
                    check(bool(*it), "Telila navigation missing");
                    characterTree->setCurrentItem(*it);
                    app.processEvents();
                    auto root = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
                    auto outline = root->findChild<QTreeWidget *>("conversationOutline");
                    outline->setCurrentItem(treeItem(outline, "bard"));
                    app.processEvents();
                    app.processEvents();
                    auto speech = root->findChild<QPlainTextEdit *>("conversationBlockText");
                    check(speech && speech->geometry().top() < 80 &&
                              speech->parentWidget()->height() < speech->height() + 80,
                          "Original Telila response is vertically stretched");
                    const auto captures = qEnvironmentVariable("IMPERA_WORKSHOP_SCREENSHOT_DIR");
                    if (!captures.isEmpty()) w.grab().save(captures + "/workshop-telila-response.png");
                    break;
                }
                for (const QString &resource : {QString("TOWNE.DAT"), QString("BRIT.DAT"),
                                                QString("UNDER.DAT"), QString("BRIT.CBT")}) {
                    if (!p.resources.contains(resource))
                        continue;
                    w.selectResource(resource);
                    app.processEvents();
                    auto root = w.findChild<QStackedWidget *>("workspaces")->currentWidget();
                    auto modes = root->findChild<QTabBar *>("locationModes");
                    check(modes, "Original map lacks location modes");
                    for (int mode = 0; mode < modes->count(); ++mode) {
                        modes->setCurrentIndex(mode);
                        app.processEvents();
                        check(root->findChild<QComboBox *>("mapPage"),
                              "Location layer replaced floor selector");
                        const auto captures =
                            qEnvironmentVariable("IMPERA_WORKSHOP_SCREENSHOT_DIR");
                        if (!captures.isEmpty()) {
                            QDir().mkpath(captures);
                            if (modes->tabText(mode) == "Signs") {
                                auto entries =
                                    root->findChild<QListWidget *>("locationTextEntries");
                                if (entries && entries->count() > 1)
                                    entries->setCurrentRow(1);
                                app.processEvents();
                            }
                            QString title = modes->tabText(mode);
                            title.replace(" / ", "-");
                            title.replace(' ', '-');
                            check(w.grab().save(captures + "/workshop-" +
                                                QString(resource).replace('.', '-').toLower() +
                                                "-" + title.toLower() + ".png"),
                                  "Could not capture location-mode UI");
                        }
                    }
                }
                check(w.projectForTests().changed().isEmpty(), "Original GUI mutation");
            });
        std::cout << count << " test groups passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
