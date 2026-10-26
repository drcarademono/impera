#include "custom_tiles.h"
#include "color_palette.h"
#include "mod/tileset_resources.h"
#include "window.h"
#include "map_entities.h"
#include "mod/npcs.h"
#include "native_dialogue_session.h"
#include <QApplication>
#include <QClipboard>
#include <QToolButton>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QSettings>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv) {
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QApplication app(argc, argv);
    try {
        // Native pose patterns, random idle holds, deliberate guard delays and
        // independently selected custom artwork all share one interpreter.
        auto accept = +[]() -> unsigned { return 255; };
        auto skip = +[]() -> unsigned { return 0; };
        unsigned char pose = 0x50, animationState = 0;
        TYPES_AdvanceAnimation(0x50, &pose, &animationState, skip);
        CHECK(pose == 0x50 && animationState == 0);
        for (unsigned expected : {0x51u, 0x52u, 0x53u, 0x51u}) {
            TYPES_AdvanceAnimation(0x50, &pose, &animationState, accept);
            CHECK(pose == expected);
        }
        pose = 0x53;
        animationState = 3 * 16; // End of the villager's active sequence.
        auto rest = +[]() -> unsigned {
            static unsigned calls = 0;
            return ++calls % 2 ? 255 : 0;
        };
        TYPES_AdvanceAnimation(0x50, &pose, &animationState, rest);
        CHECK(pose == 0x50 && (animationState & 15) == 6);
        for (int tick = 0; tick < 6; ++tick) {
            TYPES_AdvanceAnimation(0x50, &pose, &animationState, accept);
            CHECK(pose == 0x50);
        }
        pose = 0x5c;
        animationState = 0;
        TYPES_AdvanceAnimation(0x5c, &pose, &animationState, skip);
        CHECK(pose == 0x5d); // This bard bypasses the ordinary random skip.
        pose = 0x70;
        animationState = 0;
        TYPES_AdvanceAnimation(0x70, &pose, &animationState, accept);
        CHECK(pose == 0x71);
        TYPES_AdvanceAnimation(0x70, &pose, &animationState, accept);
        CHECK(pose == 0x71 && (animationState & 15) == 4);
        for (int tick = 0; tick < 4; ++tick) {
            TYPES_AdvanceAnimation(0x70, &pose, &animationState, accept);
            CHECK(pose == 0x71);
        }
        TYPES_AdvanceAnimation(0x70, &pose, &animationState, accept);
        CHECK(pose == 0x72);
        pose = 0xd8;
        animationState = 0;
        for (unsigned expected : {0xd8u, 0xd9u, 0xdau, 0xdbu, 0xd8u}) {
            TYPES_AdvanceAnimation(0xd8, &pose, &animationState, accept);
            CHECK(pose == expected);
        }
        {
            Project animated;
            animated.openGame(TEST_GAME_DIR);
            TypeDef actor;
            TYPES_Vanilla(1, 80, &actor);
            actor.id = 62000;
            actor.kind = 1;
            actor.stats[5] = 20;
            actor.stats[6] = 1;
            QVector<CustomTiles::Frame> poses;
            for (int i = 0; i < 4; ++i) {
                QImage art(16, 16, QImage::Format_RGBA8888);
                art.fill(QColor(50 * (i + 1), 0, 0));
                poses.append({art, 150, 0});
            }
            auto library =
                CustomTiles::library(animated, {{actor.id, CustomTiles::record(actor, poses)}});
            TYPES_Mount(library.constData(), library.size(), nullptr, 0);
            unsigned width, height;
            unsigned slot = TYPES_ArtSlot(actor.id);
            for (unsigned phase = 0; phase < 4; ++phase) {
                unsigned handle = TYPES_ActorArt(actor.id, 256 + 80 + phase);
                CHECK(TYPES_ArtworkType(handle) == actor.id && (handle >> 16) == phase);
                CHECK(TYPES_ActorArt(actor.id, handle) == handle);
                const auto *pixels =
                    TYPES_SampleArtImage(slot | (phase << 16), 0, 0, &width, &height);
                CHECK(pixels && pixels[0] == 50 * (phase + 1));
                pixels = TYPES_SampleArtImage(slot | (phase << 16), 3, 9999, &width, &height);
                CHECK(pixels && pixels[0] == 50 * (phase + 1)); // DOS fallback keeps the pose.
            }
            actor.animation = 1;
            library =
                CustomTiles::library(animated, {{actor.id, CustomTiles::record(actor, poses)}});
            TYPES_Mount(library.constData(), library.size(), nullptr, 0);
            CHECK(TYPES_ActorArt(actor.id, 256 + 83) == actor.id);
            CHECK(TYPES_SampleArtImage(slot, 0, 0, &width, &height)[0] == 50);
            CHECK(TYPES_SampleArtImage(slot, 0, 150, &width, &height)[0] == 100);
            TYPES_Mount(nullptr, 0, nullptr, 0);
            animated.resources["TYPES.OVR"].edited = library;
            auto exported = animated.package();
            CHECK(exported.startsWith("IMOD0013"));
            Project restored;
            restored.openGame(TEST_GAME_DIR);
            restored.importPackage(exported);
            CHECK(CustomTiles::definition(restored, actor.id).animation == 1);
            exported.replace(0, 8, "IMOD0012");
            bool rejected = false;
            try {
                restored.importPackage(exported);
            } catch (const std::exception &) {
                rejected = true;
            }
            CHECK(rejected);
        }
        Project p;
        p.openGame(TEST_GAME_DIR);
        QImage tile(16, 16, QImage::Format_RGBA8888);
        tile.fill(Qt::transparent);
        tile.setPixelColor(0, 0, Qt::black);
        tile.setPixelColor(1, 0, QColor(20, 80, 220, 128));
        TypeDef d;
        TYPES_Vanilla(0, 5, &d);
        d.id = 40000;
        d.filters = 3;
        strcpy(d.name, "Crystal floor");
        d.render = TYPES_MASONRY;
        auto frames =
            QVector<CustomTiles::Frame>{{tile, 100, 0}, {tile.mirrored(true, false), 200, 0}};
        QMap<unsigned, QByteArray> records;
        records[d.id] = CustomTiles::record(d, frames);
        p.resources["TYPES.OVR"].edited = CustomTiles::library(p, records);
        p.validate();
        {
            Project masked = p;
            auto protectedFrames = frames;
            protectedFrames[0].mask = QImage(16, 16, QImage::Format_RGB32);
            protectedFrames[0].mask.fill(Qt::black);
            protectedFrames[0].mask.setPixelColor(0, 0, Qt::white);
            auto encoded = CustomTiles::record(d, protectedFrames);
            CHECK(CustomTiles::frames(encoded)[0].mask.pixelColor(0, 0) == Qt::white);
            masked.resources["TYPES.OVR"].edited = CustomTiles::library(masked, {{d.id, encoded}});
            auto masks = U5::readGraphics("MASK_DOS.PNG", masked.data("MASK_DOS.PNG"));
            masks.images[508].setPixelColor(8, 8, Qt::white);
            masked.resources["MASK_DOS.PNG"].edited = U5::writeGraphics(masks);
            masked.validate();
            auto bytes = masked.package();
            CHECK(bytes.startsWith("IMOD0025"));
            Project imported;
            imported.openGame(TEST_GAME_DIR);
            imported.importPackage(bytes);
            CHECK(imported.data("MASK_DOS.PNG") == masked.data("MASK_DOS.PNG"));
            CHECK(CustomTiles::frames(CustomTiles::records(imported)[d.id])[0].mask ==
                  protectedFrames[0].mask);
            auto old = bytes;
            old.replace(0, 8, "IMOD0024");
            bool rejected = false;
            try {
                imported.importPackage(old);
            } catch (...) {
                rejected = true;
            }
            CHECK(rejected);
            QTemporaryDir store;
            auto path = store.filePath("masks.imperaproject");
            masked.save(path);
            Project reopened;
            reopened.load(path);
            CHECK(reopened.data("MASK_DOS.PNG") == masked.data("MASK_DOS.PNG"));
            for (const auto &sheet : modTilesetSheets)
                if (MOD_IsTilesetMask(sheet.resource)) {
                    auto graphics = U5::readGraphics(sheet.resource, p.data(sheet.resource));
                    CHECK(Workshop::artworkColors(graphics).size() == 2);
                }
        }
        CHECK(CustomTiles::image(p, 40000).pixelColor(0, 0) == Qt::black);
        CHECK(CustomTiles::image(p, 40000).pixelColor(1, 0).alpha() == 128);
        CHECK(CustomTiles::image(p, 40000, 4, 100).pixelColor(15, 0) == Qt::black);
        MapDocument doc(&p, "DWELLING.DAT");
        int page = -1;
        for (int i = 0; i < doc.pages.size(); i++)
            if (doc.pages[i].name.startsWith("Iolo"))
                page = i;
        CHECK(page >= 0);
        auto base = doc.terrain(page);
        auto extended = CustomTiles::expanded(p, "DWELLING.DAT", doc.pages[page], page, base);
        TYPES_Put32(extended.data() + 4 * (4 * 32 + 3), 40000);
        base[4 * 32 + 3] = 5;
        base[4 * 32 + 4] = 5;
        auto changes = doc.changes(page, base, extended);
        auto extra = CustomTiles::paint(p, "DWELLING.DAT", doc.pages[page], page, base, extended);
        for (auto it = extra.begin(); it != extra.end(); it++)
            changes[it.key()] = it.value();
        for (auto it = changes.begin(); it != changes.end(); it++)
            p.resources[it.key()].edited = it.value();
        p.validate();
        auto cells = CustomTiles::placements(p);
        CHECK(cells.size() == 1);
        CHECK(cells.first().map == 13);
        CHECK(cells.first().type == 40000);
        auto moved = CustomTiles::moveScenery(p, "DWELLING.DAT", page, {3, 4}, {4, 4});
        CHECK(moved.contains("TYPEMAP.OVR"));
        Project movedProject = p;
        for (auto it = moved.begin(); it != moved.end(); it++)
            movedProject.resources[it.key()].edited = it.value();
        movedProject.validate();
        CHECK(CustomTiles::placements(movedProject).first().x == 4);
        CHECK(CustomTiles::placements(p).first().x == 3);
        auto idImage =
            doc.terrainImage(page, U5::readGraphics("TILES.16", p.data("TILES.16")).images, true);
        CHECK(idImage.format() == QImage::Format_Grayscale16);
        CHECK(reinterpret_cast<const quint16 *>(idImage.constScanLine(4))[3] == 40000);
        MapDocument world(&p, "BRIT.DAT");
        auto water = world.terrain(0);
        auto worldIds = CustomTiles::expanded(p, "BRIT.DAT", world.pages[0], 0, water);
        TYPES_Put32(worldIds.data(), 40000);
        water[0] = 5;
        auto worldChanges = world.changes(0, water, worldIds);
        CHECK(worldChanges["BRIT.DAT"] == p.data("BRIT.DAT"));
        CHECK(worldChanges["DATA.OVL"] == p.data("DATA.OVL"));
        auto package = p.package();
        CHECK(package.startsWith("IMOD0009"));
        Project imported;
        imported.openGame(TEST_GAME_DIR);
        imported.importPackage(package);
        CHECK(CustomTiles::placements(imported).first().type == 40000);
        QTemporaryDir temp;
        CHECK(temp.isValid());
        p.save(temp.filePath("tile.imperaproject"));
        Project restored;
        restored.load(temp.filePath("tile.imperaproject"));
        CHECK(restored.data("TYPES.OVR") == p.data("TYPES.OVR"));
        CHECK(restored.data("TYPEMAP.OVR") == p.data("TYPEMAP.OVR"));
        MapCanvas canvas;
        canvas.side = 2;
        canvas.ids = QByteArray(4, 5);
        canvas.extended = QByteArray(16, 0);
        canvas.libraryUuid = p.data("TYPES.OVR").mid(8, 16);
        canvas.legacyTiles[40000] = 5;
        canvas.setTileId(0, 40000);
        CHECK(canvas.tileId(0) == 40000);
        canvas.selection = QRect(0, 0, 1, 1);
        CHECK(canvas.copySelection());
        canvas.setTileId(0, 5);
        CHECK(canvas.beginPaste());
        canvas.commit = [&](const QByteArray &) { return true; };
        canvas.resizeMap();
        canvas.show();
        QPointF pos(16 * canvas.zoom + 2, 16 * canvas.zoom + 2);
        QMouseEvent press(QEvent::MouseButtonPress, pos, pos, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(&canvas, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, pos, pos, Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(&canvas, &release);
        CHECK(canvas.tileId(3) == 40000);
        canvas.libraryUuid = QByteArray(16, 'x');
        CHECK(!canvas.beginPaste());
        QMap<QString, int> state;
        state["customTiles/id"] = 40000;
        int edits = 0;
        auto edit = [&](const QMap<QString, QByteArray> &c, const QString &) {
            Project candidate = p;
            for (auto it = c.begin(); it != c.end(); it++)
                candidate.resources[it.key()].edited = it.value();
            candidate.validate();
            p = candidate;
            edits++;
            return true;
        };
        std::unique_ptr<QWidget> ui(CustomTiles::editor(&p, edit, &state));
        CHECK(ui->findChild<QListWidget *>("customTileList"));
        auto name = ui->findChild<QLineEdit *>("customTileName");
        CHECK(name && name->text() == "Crystal floor");
        name->setText("Polished crystal");
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
        CHECK(edits == 1);
        CHECK(QString::fromUtf8(CustomTiles::definition(p, 40000).name) == "Polished crystal");
        CHECK(!ui->findChild<QPushButton *>("applyCustomTile"));
        for (const auto &section : {"Interaction", "Movement", "Appearance", "Hazards", "Advanced"})
            CHECK(ui->findChild<QWidget *>(QString("tileSection%1").arg(section)));
        auto groundPicker = ui->findChild<QComboBox *>("customTileGround");
        CHECK(groundPicker && groundPicker->isHidden());
        CHECK(groundPicker->isEditable() && !groundPicker->itemIcon(1).isNull());
        auto behavior = ui->findChild<QComboBox *>("customTileInteraction");
        auto openAppearance = ui->findChild<QComboBox *>("tileStateAppearance0");
        CHECK(openAppearance->isHidden());
        behavior->setCurrentIndex(behavior->findData(0xb8));
        CHECK(!openAppearance->isHidden());
        CHECK(CustomTiles::definition(p, 40000).base == 0xb8);
        behavior->setCurrentIndex(behavior->findData(5));
        CHECK(openAppearance->isHidden());
        openAppearance->setCurrentIndex(openAppearance->findData(40000));
        CHECK(!openAppearance->isHidden());
        CHECK(CustomTiles::definition(p, 40000).result[0] == 40000);
        openAppearance->setCurrentIndex(0);
        CHECK(openAppearance->isHidden());
        auto error = ui->findChild<QLabel *>("customTileSettingsError");
        int validEdits = edits;
        name->clear();
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
        CHECK(edits == validEdits && !error->isHidden());
        CHECK(QString::fromUtf8(CustomTiles::definition(p, 40000).name) == "Polished crystal");
        name->setText("Polished crystal");
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
        CHECK(error->isHidden());
        auto variants = ui->findChild<QComboBox *>("customTileVariant");
        auto colors = ui->findChild<QWidget *>("customTilePalette");
        CHECK(variants && colors);
        CHECK(!ui->findChild<QToolButton *>("customTileTool1"));
        for (int i : {0, 2, 3}) {
            auto tool = ui->findChild<QToolButton *>(QString("customTileTool%1").arg(i));
            CHECK(tool && !tool->icon().isNull() && !tool->toolTip().isEmpty());
            tool->click();
            CHECK(state["customTiles/tool"] == i && tool->isChecked());
        }
        ui->findChild<QToolButton *>("customTileTool0")->click();
        const QStringList sheets{"TILES.16", "TILE_AMI.PNG", "TILE_APP.PNG", "TILE_GRY.PNG",
                                 "TILE_SHW.PNG"};
        for (int i = 0; i < sheets.size(); i++) {
            variants->setCurrentIndex(i);
            auto canvasSize = ui->findChild<QWidget *>("customTileCanvas")->size();
            auto nativeSize = CustomTiles::artworkSize(i);
            CHECK(canvasSize.width() * nativeSize.height() ==
                  canvasSize.height() * nativeSize.width());
            auto expected = Workshop::artworkColors(U5::readGraphics(sheets[i], p.data(sheets[i])));
            QSet<QRgb> actual;
            for (auto b : colors->findChildren<QToolButton *>())
                if (b->property("rgba").isValid())
                    actual.insert(b->property("rgba").toUInt());
            CHECK(actual.size() == expected.size() + 1);
            CHECK(colors->findChild<QToolButton *>("customPaletteAddColor")
                      ->parentWidget()
                      ->isHidden());
            if (i == 4) {
                CHECK(expected.size() == 8);
                for (int rgb = 0; rgb < 8; rgb++)
                    CHECK(actual.contains(
                        qRgb(rgb & 1 ? 255 : 0, rgb & 2 ? 255 : 0, rgb & 4 ? 255 : 0)));
            }
            for (auto rgba : expected)
                CHECK(actual.contains(rgba));
            auto strip = ui->findChild<QListWidget *>("customTileFrames");
            CHECK(strip->count() == 2); // Empty slots expose the DOS animation.
        }
        variants->setCurrentIndex(5);
        auto customPalette = dynamic_cast<Workshop::ColorPalette *>(colors);
        CHECK(customPalette &&
              !customPalette->findChild<QToolButton *>("customPaletteAddColor")->isHidden());
        customPalette->addCustomColor(qRgb(13, 72, 123));
        // Picking a new custom swatch drives the brush just like an imported swatch.
        for (auto swatch : customPalette->findChildren<QToolButton *>())
            if (swatch->property("rgba").isValid() &&
                swatch->property("rgba").toUInt() == qRgb(13, 72, 123))
                swatch->click();
        auto customCanvas = ui->findChild<QWidget *>("customTileCanvas");
        QMouseEvent customDown(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent customUp(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(1, 1),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(customCanvas, &customDown);
        QApplication::sendEvent(customCanvas, &customUp);
        CHECK(CustomTiles::image(p, 40000, 5).pixelColor(0, 0) == QColor(13, 72, 123));
        auto maskMode = ui->findChild<QToolButton *>("customTileMaskMode");
        CHECK(maskMode);
        auto beforeMaskArtwork = CustomTiles::image(p, 40000, 5);
        maskMode->click();
        for (auto swatch : customPalette->findChildren<QToolButton *>())
            if (swatch->property("rgba").isValid() &&
                swatch->property("rgba").toUInt() == qRgb(255, 255, 255))
                swatch->click();
        QApplication::sendEvent(customCanvas, &customDown);
        QApplication::sendEvent(customCanvas, &customUp);
        bool hasMask = false;
        int maskRow = ui->findChild<QListWidget *>("customTileFrames")->currentRow();
        for (const auto &frame : CustomTiles::frames(CustomTiles::records(p)[40000]))
            if (frame.variant == 5 && maskRow-- == 0) {
                hasMask = frame.mask.pixelColor(0, 0) == Qt::white;
                break;
            }
        CHECK(hasMask && CustomTiles::image(p, 40000, 5) == beforeMaskArtwork);
        maskMode->click();

        QTemporaryDir palettes;
        QImage palettePng(2, 1, QImage::Format_ARGB32);
        palettePng.setPixel(0, 0, qRgb(1, 2, 3));
        palettePng.setPixel(1, 0, qRgb(4, 5, 6));
        CHECK(palettePng.save(palettes.path() + "/test.png"));
        customPalette->importPalette(palettes.path() + "/test.png");
        CHECK(CustomTiles::image(p, 40000, 5).pixelColor(0, 0) == QColor(13, 72, 123));
        CHECK(Workshop::readPalette(palettes.path() + "/test.png") ==
              QVector<QRgb>({qRgb(1, 2, 3), qRgb(4, 5, 6)}));
        const QMap<QString, QByteArray> paletteFiles{
            {"test.gpl",
             "GIMP Palette\nName: Test\nColumns: 2\n# Colors\n1 2 3 First\n4 5 6 Second\n"},
            {"test.pal", "JASC-PAL\n0100\n2\n1 2 3\n4 5 6\n"},
            {"test.hex", "#010203\n040506\n"}};
        for (auto it = paletteFiles.begin(); it != paletteFiles.end(); ++it) {
            QFile f(palettes.path() + "/" + it.key());
            CHECK(f.open(QIODevice::WriteOnly));
            f.write(it.value());
            f.close();
            CHECK(Workshop::readPalette(f.fileName()) ==
                  Workshop::readPalette(palettes.path() + "/test.png"));
        }
        variants->setCurrentIndex(4);
        CHECK(customPalette->findChild<QToolButton *>("customPaletteAddColor")
                  ->parentWidget()
                  ->isHidden());
        // Editing an empty Sharp slot copies DOS frames without changing DOS artwork.
        auto dosBefore = CustomTiles::image(p, 40000);
        auto paintCanvas = ui->findChild<QWidget *>("customTileCanvas");
        QMouseEvent pixelPress(QEvent::MouseButtonPress, QPointF(50, 50), QPointF(50, 50),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent pixelRelease(QEvent::MouseButtonRelease, QPointF(50, 50), QPointF(50, 50),
                                 Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(paintCanvas, &pixelPress);
        QApplication::sendEvent(paintCanvas, &pixelRelease);
        CHECK(CustomTiles::image(p, 40000) == dosBefore);
        CHECK(CustomTiles::image(p, 40000, 4) != dosBefore);
        auto sharp = CustomTiles::image(p, 40000, 4);
        auto sharpColors =
            Workshop::artworkColors(U5::readGraphics("TILE_SHW.PNG", p.data("TILE_SHW.PNG")));
        for (int y = 0; y < sharp.height(); y++)
            for (int x = 0; x < sharp.width(); x++) {
                auto rgba = sharp.pixel(x, y);
                CHECK(!qAlpha(rgba) ||
                      sharpColors.contains(qRgb(qRed(rgba), qGreen(rgba), qBlue(rgba))));
            }
        CHECK(!ui->findChild<QPushButton *>("customTileUseDos"));
        auto useDos = ui->findChild<QAction *>("customTileUseDos");
        CHECK(useDos && useDos->isEnabled());
        useDos->trigger();
        CHECK(CustomTiles::image(p, 40000, 4) == dosBefore);
        CHECK(!useDos->isEnabled());
        variants->setCurrentIndex(2);
        QApplication::sendEvent(paintCanvas, &pixelPress);
        QApplication::sendEvent(paintCanvas, &pixelRelease);
        CHECK(CustomTiles::image(p, 40000, 2).size() == QSize(14, 16));
        auto appleFrames = CustomTiles::frames(CustomTiles::records(p).value(40000));
        CHECK(appleFrames.size() == 6); // DOS, Apple II and Custom each retain two frames.
        CHECK(appleFrames[2].image.size() == QSize(14, 16));
        CHECK(appleFrames[3].image.size() == QSize(14, 16));
        CHECK(CustomTiles::image(p, 40000) == dosBefore);
        useDos->trigger();

        // Independent NPC/monster definitions share art but not identity or stats.
        auto actorRecords = CustomTiles::records(p);
        for (unsigned kind = 1; kind <= 2; kind++) {
            TypeDef actor;
            TYPES_Vanilla(1, kind == 1 ? 80 : 144, &actor);
            actor.kind = kind;
            actor.id = 40009 + kind;
            actor.render = TYPES_SPRITE;
            actor.stats[5] = kind == 1 ? 30 : 60;
            actor.stats[6] = 1;
            strcpy(actor.name, kind == 1 ? "Scholar" : "Crystal beast");
            strcpy(actor.description, kind == 1 ? "Scholars" : "Crystal beasts");
            actorRecords[actor.id] = CustomTiles::record(actor, {{tile, 150, 0}});
        }
        p.resources["TYPES.OVR"].edited = CustomTiles::library(p, actorRecords);
        auto applyChanges = [&](const QMap<QString, QByteArray> &changes) {
            for (auto it = changes.begin(); it != changes.end(); ++it)
                p.resources[it.key()].edited = it.value();
        };
        auto placedNpc = Workshop::addMapItem(p, "DWELLING.DAT", page, {8, 8},
                                              Workshop::MapItemKind::Npc, 40010, -1, "Mira");
        applyChanges(placedNpc.changes);
        CHECK(CustomTiles::actorType(p, "DWELLING.DAT", doc.pages[page], page, placedNpc.slot, 0) ==
              40010);
        MapNpcDocument npcDocument(&p, "DWELLING.NPC");
        CHECK(npcDocument.sprite(doc.pages[page].settlement, placedNpc.slot) == 40010);
        CHECK(npcDocument.dialogue(doc.pages[page].settlement, placedNpc.slot) > 0);
        {
            Project crowded = p;
            auto records = crowded.data("DWELLING.NPC");
            int town = doc.pages[page].settlement;
            for (int i = 1; i < 32; i++)
                records[town * 576 + 512 + i] = 80;
            crowded.resources["DWELLING.NPC"].edited = records;
            auto conversation = npcDocument.dialogue(town, placedNpc.slot);
            auto apply = [&](const QMap<QString, QByteArray> &changes) {
                for (auto it = changes.begin(); it != changes.end(); ++it)
                    crowded.resources[it.key()].edited = it.value();
            };
            auto extraNpc =
                Workshop::addMapItem(crowded, "DWELLING.DAT", page, {9, 9},
                                     Workshop::MapItemKind::Npc, 40010, conversation, "Extra");
            CHECK(extraNpc.slot == 32);
            apply(extraNpc.changes);
            MapNpcDocument extraDoc(&crowded, "DWELLING.NPC");
            CHECK(extraDoc.count(town) == 33 && extraDoc.sprite(town, 32) == 40010);
            CHECK(extraDoc.dialogue(town, 32) == conversation);
            apply(
                {{"DWELLING.NPC", extraDoc.move(town, 32, 0, {10, 9, doc.pages[page].floor, 0})}});
            CHECK(extraDoc.location(town, 32, 0).x == 10);
            // Exercise both bytes of instance IDs, keeping existing records intact.
            records = crowded.data("DWELLING.NPC");
            for (int slot = 33; slot <= 257; slot++)
                MapNpcDocument::ensureSlot(records, town, slot);
            records[NPCS_ShapeOffset(records.constData(), records.size(), town, 257)] = 80;
            records[NPCS_DialogueOffset(records.constData(), records.size(), town, 257)] =
                char(conversation);
            crowded.resources["DWELLING.NPC"].edited = records;
            apply(CustomTiles::assignActor(crowded, "DWELLING.DAT", doc.pages[page], page, 257,
                                           40010));
            CHECK(extraDoc.count(town) == 258 && extraDoc.sprite(town, 257) == 40010);
            auto destinations = Workshop::characterDestinations(crowded);
            bool found = false;
            for (auto d : destinations)
                found |= d.scheduleResource == "DWELLING.NPC" &&
                         Workshop::npcSlot(d.character) == 257 &&
                         Workshop::npcTown(d.character) == town && d.conversation >= 0;
            CHECK(found);
            int conversationIndex = -1;
            auto talks = U5::readDialogue(crowded.data("DWELLING.TLK"));
            for (int i = 0; i < talks.size(); i++)
                if (int(talks[i].id) == conversation)
                    conversationIndex = i;
            auto playtest = Dialogue::nativeSetup(crowded, "DWELLING.TLK", conversationIndex,
                                                  Workshop::npcCharacter(town, 257));
            CHECK(playtest.hasNpc && playtest.npc == 257 && playtest.shapes.size() == 258);
            CHECK(playtest.npcRecords.size() == 258 * 16 &&
                  playtest.actorRecords.size() == 258 * 8);
            auto packaged = crowded.package();
            CHECK(packaged.startsWith("IMOD0025"));
            auto oldPackage = packaged;
            oldPackage.replace(0, 8, "IMOD0011");
            bool oldRejected = false;
            try {
                Project old;
                old.openGame(TEST_GAME_DIR);
                old.importPackage(oldPackage);
            } catch (...) {
                oldRejected = true;
            }
            CHECK(oldRejected);
            Project imported;
            imported.openGame(TEST_GAME_DIR);
            imported.importPackage(packaged);
            MapNpcDocument loaded(&imported, "DWELLING.NPC");
            CHECK(loaded.count(town) == 258 && loaded.sprite(town, 257) == 40010);
            CHECK(loaded.location(town, 32, 0).x == 10);
            apply(Workshop::deleteMapItem(crowded, "DWELLING.DAT", page, "NPCs / Schedules", 32,
                                          {10, 9}));
            CHECK(!U5::byte(crowded.data("DWELLING.NPC"), extraDoc.shapeOffset(town, 32)));
            auto reused =
                Workshop::addMapItem(crowded, "DWELLING.DAT", page, {9, 9},
                                     Workshop::MapItemKind::Npc, 40010, conversation, "Extra");
            CHECK(reused.slot == 32);
        }
        auto worldMonster =
            Workshop::addMapItem(p, "BRIT.DAT", 0, {20, 20}, Workshop::MapItemKind::Monster, 40011);
        applyChanges(worldMonster.changes);
        auto worldPage = U5::mapPages("BRIT.DAT", p.data("BRIT.DAT"))[0];
        CHECK(CustomTiles::actorType(p, "BRIT.DAT", worldPage, 0, worldMonster.slot, 0) == 40011);
        auto roomPage = U5::mapPages("DUNGEON.CBT", p.data("DUNGEON.CBT"))[0];
        applyChanges(CustomTiles::assignActor(p, "DUNGEON.CBT", roomPage, 0, 0, 40011));
        CHECK(CustomTiles::actorType(p, "DUNGEON.CBT", roomPage, 0, 0, 0) == 40011);
        p.validate();
        CHECK(p.package().startsWith("IMOD0025"));
        Project actorImported;
        actorImported.openGame(TEST_GAME_DIR);
        actorImported.importPackage(p.package());
        CHECK(CustomTiles::actorType(actorImported, "DWELLING.DAT", doc.pages[page], page,
                                     placedNpc.slot, 0) == 40010);
        QMap<QString, int> actorState{{"customTiles/kind", 2}, {"customTiles/2/id", 40011}};
        std::unique_ptr<QWidget> actorUi(CustomTiles::editor(&p, edit, &actorState));
        auto hitPoints = actorUi->findChild<QSpinBox *>("actorStat5");
        CHECK(hitPoints && hitPoints->value() == 60);
        hitPoints->setValue(75);
        QMetaObject::invokeMethod(hitPoints, "editingFinished", Qt::DirectConnection);
        CHECK(CustomTiles::definition(p, 40011).stats[5] == 75);
        CHECK(CustomTiles::definition(p, 40010).stats[5] == 30);
        auto removedNpc = Workshop::deleteMapItem(p, "DWELLING.DAT", page, "NPCs / Schedules",
                                                  placedNpc.slot, {8, 8});
        applyChanges(removedNpc);
        CHECK(CustomTiles::actorType(p, "DWELLING.DAT", doc.pages[page], page, placedNpc.slot, 0) ==
              0);
        p.validate();
        QMap<QString, int> npcTypeState{{"customTiles/kind", 1}, {"customTiles/1/id", 40010}};
        std::unique_ptr<QWidget> npcTypesUi(CustomTiles::editor(&p, edit, &npcTypeState));
        for (auto button : npcTypesUi->findChildren<QPushButton *>())
            if (button->text() == "Delete / Reset")
                button->click();
        CHECK(!CustomTiles::records(p).contains(40010));
        CHECK(npcTypeState["customTiles/1/id"] == 336);
        CHECK(!npcTypesUi->findChild<QLineEdit *>("customTileName")->text().isEmpty());
        // Original actor frames belong to the native sheets, not a timed type override.
        for (int kind : {1, 2}) {
            const unsigned nativeId = kind == 1 ? 336 : 400;
            QMap<QString, int> nativeState{{"customTiles/kind", kind},
                                           {QString("customTiles/%1/id").arg(kind), int(nativeId)}};
            std::unique_ptr<QWidget> nativeUi(CustomTiles::editor(&p, edit, &nativeState));
            auto nativeFrames = nativeUi->findChild<QListWidget *>("customTileFrames");
            auto nativeVariants = nativeUi->findChild<QComboBox *>("customTileVariant");
            CHECK(nativeFrames && nativeFrames->count() == 4);
            CHECK(!nativeUi->findChild<QSpinBox *>("customTileFrameDuration"));
            const QStringList nativeSheets{"TILES.16",     "TILE_AMI.PNG", "TILE_APP.PNG",
                                           "TILE_GRY.PNG", "TILE_SHN.PNG", "TILES.16"};
            for (int variant = 0; variant < nativeSheets.size(); ++variant) {
                nativeVariants->setCurrentIndex(variant);
                CHECK(nativeFrames->count() == 4);
                nativeFrames->setCurrentRow(2);
                auto before =
                    U5::readGraphics(nativeSheets[variant], p.data(nativeSheets[variant]));
                int index = int(nativeId) - before.tileOffset;
                // Untouched source pixels, including DOS black and Sharp gray, remain opaque.
                const auto source = before.images[index + 3];
                const auto thumbnail =
                    nativeFrames->item(3)->icon().pixmap(source.size()).toImage();
                CHECK(thumbnail.size() == source.size());
                for (int y = 0; y < source.height(); ++y)
                    for (int x = 0; x < source.width(); ++x)
                        CHECK(thumbnail.pixelColor(x, y) == source.pixelColor(x, y));
                if (variant == 0)
                    for (int frame = 0; frame < 4; ++frame) {
                        auto original = CustomTiles::image(p, nativeId + frame);
                        for (int y = 0; y < original.height(); ++y)
                            for (int x = 0; x < original.width(); ++x)
                                CHECK(original.pixelColor(x, y) ==
                                      before.images[index + frame].pixelColor(x, y));
                    }
                auto color = qRgb(0, 0, 0);
                auto nativePalette = nativeUi->findChild<QWidget *>("customTilePalette");
                bool picked = false;
                for (auto swatch : nativePalette->findChildren<QToolButton *>())
                    if (swatch->property("rgba").isValid() &&
                        swatch->property("rgba").toUInt() == color) {
                        swatch->click();
                        picked = true;
                    }
                CHECK(picked);
                nativeUi->findChild<QToolButton *>("customTileTool0")->click();
                auto nativeCanvas = nativeUi->findChild<QWidget *>("customTileCanvas");
                QMouseEvent down(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1),
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent up(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(1, 1),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                // Change an existing pixel, then paint opaque black back over it.
                for (auto swatch : nativePalette->findChildren<QToolButton *>())
                    if (swatch->property("rgba").isValid() &&
                        swatch->property("rgba").toUInt() == qRgb(255, 255, 255))
                        swatch->click();
                QApplication::sendEvent(nativeCanvas, &down);
                QApplication::sendEvent(nativeCanvas, &up);
                for (auto swatch : nativePalette->findChildren<QToolButton *>())
                    if (swatch->property("rgba").isValid() &&
                        swatch->property("rgba").toUInt() == color)
                        swatch->click();
                QApplication::sendEvent(nativeCanvas, &down);
                QApplication::sendEvent(nativeCanvas, &up);
                auto after = U5::readGraphics(nativeSheets[variant], p.data(nativeSheets[variant]));
                auto authored = CustomTiles::image(p, nativeId + 2, variant);
                CHECK(authored.pixelColor(0, 0).rgba() == color);
                for (int i = 0; i < before.images.size(); ++i)
                    if (i != index + 2)
                        for (int y = 0; y < before.images[i].height(); ++y)
                            for (int x = 0; x < before.images[i].width(); ++x)
                                CHECK(before.images[i].pixel(x, y) == after.images[i].pixel(x, y));
                CHECK(CustomTiles::frames(CustomTiles::records(p).value(nativeId)).size() == 6);
                CHECK(nativeFrames->count() == 4 && nativeFrames->currentRow() == 2);
            }
            auto alphaPalette = nativeUi->findChild<QWidget *>("customTilePalette");
            for (auto swatch : alphaPalette->findChildren<QToolButton *>())
                if (swatch->property("rgba").isValid() && swatch->property("rgba").toUInt() == 0)
                    swatch->click();
            auto alphaCanvas = nativeUi->findChild<QWidget *>("customTileCanvas");
            QMouseEvent eraseDown(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent eraseUp(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(1, 1),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(alphaCanvas, &eraseDown);
            QApplication::sendEvent(alphaCanvas, &eraseUp);
            CHECK(CustomTiles::image(p, nativeId + 2, 5).pixelColor(0, 0).alpha() == 0);
            auto nativePackage = p.package();
            Project alphaImported;
            alphaImported.openGame(TEST_GAME_DIR);
            alphaImported.importPackage(nativePackage);
            CHECK(CustomTiles::image(alphaImported, nativeId + 2, 5).pixelColor(0, 0).alpha() == 0);
            auto runtime = p.data("TYPES.OVR");
            TYPES_Mount(runtime.constData(), runtime.size(), nullptr, 0);
            CHECK(TYPES_ActorArt(nativeId, nativeId + 2) == nativeId + 2);
            unsigned nativeSide = 0;
            auto rgba = TYPES_Art(nativeId + 2, 5, 1234, &nativeSide);
            CHECK(rgba && rgba[3] == 0);
            TYPES_Mount(nullptr, 0, nullptr, 0);
            auto nativeHealth = nativeUi->findChild<QSpinBox *>("actorStat5");
            nativeHealth->setValue(nativeHealth->value() + 1);
            QMetaObject::invokeMethod(nativeHealth, "editingFinished", Qt::DirectConnection);
            CHECK(CustomTiles::frames(CustomTiles::records(p).value(nativeId)).size() == 6);
            CHECK(nativeFrames->count() == 4);
            for (auto button : nativeUi->findChildren<QPushButton *>())
                if (button->text() == "Delete / Reset")
                    button->click();
            CHECK(!CustomTiles::records(p).contains(nativeId));
            for (const auto &resource : nativeSheets) {
                auto restored = U5::readGraphics(resource, p.data(resource));
                auto original = U5::readGraphics(resource, p.resources.value(resource).original);
                int index = int(nativeId) - restored.tileOffset;
                for (int frame = 0; frame < 4; ++frame)
                    for (int y = 0; y < restored.images[index + frame].height(); ++y)
                        for (int x = 0; x < restored.images[index + frame].width(); ++x)
                            CHECK(restored.images[index + frame].pixel(x, y) ==
                                  original.images[index + frame].pixel(x, y));
            }
            p.validate();
        }
        {
            Project large;
            large.openGame(TEST_GAME_DIR);
            TypeDef giant;
            TYPES_Vanilla(1, 80, &giant);
            giant.id = 61000;
            giant.kind = 1;
            giant.stats[5] = 50;
            giant.stats[6] = 1;
            strcpy(giant.name, "Large test NPC");
            QImage art(16, 16, QImage::Format_RGBA8888);
            art.fill(QColor(170, 0, 0));
            large.resources["TYPES.OVR"].edited = CustomTiles::library(
                large, {{giant.id, CustomTiles::record(giant, {{art, 150, 0}})}});
            auto commitLarge = [&](const QMap<QString, QByteArray> &changes, const QString &) {
                Project candidate = large;
                for (auto it = changes.begin(); it != changes.end(); ++it)
                    candidate.resources[it.key()].edited = it.value();
                candidate.validate();
                large = candidate;
                return true;
            };
            QMap<QString, int> largeState{{"customTiles/kind", 1}, {"customTiles/1/id", 61000}};
            std::unique_ptr<QWidget> editor(CustomTiles::editor(&large, commitLarge, &largeState));
            CHECK(!editor->findChild<QSpinBox *>("customTileFrameDuration"));
            auto mode = editor->findChild<QComboBox *>("customTileAnimation");
            CHECK(mode && mode->itemText(0) == "Like villager" && mode->currentIndex() == 0);
            mode->setCurrentIndex(1);
            CHECK(CustomTiles::definition(large, 61000).animation == 1);
            mode->setCurrentIndex(0);
            CHECK(CustomTiles::definition(large, 61000).animation == 0);
            auto footprint = editor->findChild<QComboBox *>("actorFootprint");
            auto appearance = editor->findChild<QComboBox *>("actorArtworkContext");
            auto tileset = editor->findChild<QComboBox *>("customTileVariant");
            CHECK(footprint && footprint->isEnabled() && appearance);
            for (int size = 1; size <= 3; ++size) {
                footprint->setCurrentIndex(size);
                auto type = CustomTiles::definition(large, 61000);
                CHECK(TYPES_Width(&type) == unsigned(1 + size / 2) &&
                      TYPES_Height(&type) == unsigned(1 + size % 2));
                CHECK(CustomTiles::image(large, 61000).size() ==
                      QSize(16 * (1 + size / 2), 16 * (1 + size % 2)));
                CHECK(CustomTiles::image(large, 61000, 6).size() == QSize(16, 16));
                for (int variant : {0, 2, 4}) {
                    tileset->setCurrentIndex(variant);
                    auto canvas = editor->findChild<QWidget *>("customTileCanvas");
                    auto cell = CustomTiles::artworkSize(variant);
                    CHECK(canvas->width() * cell.height() * (1 + size % 2) ==
                          canvas->height() * cell.width() * (1 + size / 2));
                    if (variant != 0) {
                        QMouseEvent paint(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1),
                                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QMouseEvent finish(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(1, 1),
                                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                        QApplication::sendEvent(canvas, &paint);
                        QApplication::sendEvent(canvas, &finish);
                        CHECK(CustomTiles::image(large, 61000, variant).size() ==
                              QSize(cell.width() * (1 + size / 2), cell.height() * (1 + size % 2)));
                    }
                }
            }
            tileset->setCurrentIndex(0);
            // A single PNG replaces one frame, including when copied artwork has all tilesets.
            auto sourceFrames = CustomTiles::frames(CustomTiles::records(large).value(61000));
            QVector<CustomTiles::Frame> copied;
            for (auto frame : sourceFrames)
                for (int i = 0; i < 4; ++i)
                    copied.append(frame);
            QImage imported(32, 32, QImage::Format_RGBA8888);
            imported.fill(Qt::transparent);
            imported.setPixelColor(15, 15, QColor(170, 0, 0));
            auto replaced = CustomTiles::importedFrames(copied, {{imported, 150, 0}}, 0, 2);
            CHECK(replaced.size() == copied.size());
            CHECK(replaced[2].image == imported);
            CHECK(replaced[0].image == copied[0].image);
            auto importedRecords = CustomTiles::records(large);
            importedRecords[61000] =
                CustomTiles::record(CustomTiles::definition(large, 61000), replaced);
            CustomTiles::library(large, importedRecords);
            QVector<CustomTiles::Frame> sheet{{imported, 150, 0}, {imported, 150, 0}};
            auto resizedFrames = CustomTiles::importedFrames(copied, sheet, 0, 0);
            importedRecords[61000] =
                CustomTiles::record(CustomTiles::definition(large, 61000), resizedFrames);
            CustomTiles::library(large, importedRecords);
            CHECK(std::count_if(resizedFrames.begin(), resizedFrames.end(),
                                [](const CustomTiles::Frame &f) { return f.variant == 0; }) == 2);
            CHECK(std::count_if(resizedFrames.begin(), resizedFrames.end(),
                                [](const CustomTiles::Frame &f) { return f.variant == 6; }) == 4);
            auto frameList = editor->findChild<QListWidget *>("customTileFrames");
            auto copyFrame = editor->findChild<QToolButton *>("customTileCopyFrame");
            auto pasteFrame = editor->findChild<QToolButton *>("customTilePasteFrame");
            auto addFrame = editor->findChild<QPushButton *>("customTileAddFrame");
            CHECK(frameList && copyFrame && pasteFrame && addFrame);
            auto originalFrame = CustomTiles::image(large, 61000);
            addFrame->click();
            CHECK(frameList->count() == 2);
            frameList->setCurrentRow(1);
            QApplication::clipboard()->setImage(imported);
            pasteFrame->click();
            auto pastedFrames = CustomTiles::frames(CustomTiles::records(large).value(61000));
            CHECK(pastedFrames[0].image == originalFrame);
            CHECK(pastedFrames[1].image.pixelColor(15, 15) == QColor(170, 0, 0));
            CHECK(pastedFrames[1].image.pixelColor(0, 0).alpha() == 0);
            auto duration = pastedFrames[1].duration;
            frameList->setCurrentRow(0);
            copyFrame->click();
            frameList->setCurrentRow(1);
            pasteFrame->click();
            pastedFrames = CustomTiles::frames(CustomTiles::records(large).value(61000));
            CHECK(pastedFrames[1].image == originalFrame);
            CHECK(pastedFrames[1].duration == duration);
            auto saved = large.data("TYPES.OVR");
            appearance->setCurrentIndex(1);
            CHECK(large.data("TYPES.OVR") == saved);
            auto canvas = editor->findChild<QWidget *>("customTileCanvas");
            CHECK(canvas->width() == canvas->height());
            auto swatches = editor->findChild<QWidget *>("customTilePalette");
            for (auto button : swatches->findChildren<QToolButton *>())
                if (button->property("rgba").isValid() &&
                    button->property("rgba").toUInt() == qRgb(0, 0, 170))
                    button->click();
            QMouseEvent worldDown(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent worldUp(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(1, 1),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &worldDown);
            QApplication::sendEvent(canvas, &worldUp);
            CHECK(CustomTiles::image(large, 61000, 6).pixelColor(0, 0) == QColor(0, 0, 170));
            CHECK(CustomTiles::image(large, 61000, 0).pixelColor(0, 0) == QColor(170, 0, 0));
            QMap<QString, MapViewState> mapStates;
            QMap<QString, int> nav;
            MapWorkspace worldMap(&large, "BRIT.DAT", &mapStates, &nav, commitLarge, {});
            auto worldCanvas =
                dynamic_cast<MapCanvas *>(worldMap.findChild<QWidget *>("mapCanvas"));
            CHECK(worldCanvas && worldCanvas->actorSize(61000) == QSize(1, 1));
            CHECK(worldCanvas->tiles[61000].size() == QSize(16, 16));
            CHECK(worldCanvas->tiles[61000].pixelColor(0, 0) == QColor(0, 0, 170));
            CHECK(large.package().startsWith("IMOD0013"));
            Project roundtrip;
            roundtrip.openGame(TEST_GAME_DIR);
            roundtrip.importPackage(large.package());
            CHECK(CustomTiles::image(roundtrip, 61000).size() == QSize(32, 32));
            CHECK(CustomTiles::image(roundtrip, 61000, 6).size() == QSize(16, 16));
            // Prepare clear terrain and NPC slots for deterministic placement checks.
            MapDocument location(&large, "DWELLING.DAT");
            auto clean = location.changes(page, QByteArray(1024, 5));
            for (auto it = clean.begin(); it != clean.end(); ++it)
                large.resources[it.key()].edited = it.value();
            auto npcBytes = large.data("DWELLING.NPC");
            npcBytes.replace(location.pages[page].settlement * 576, 576, QByteArray(576, 0));
            large.resources["DWELLING.NPC"].edited = npcBytes;
            auto added = Workshop::addMapItem(large, "DWELLING.DAT", page, {8, 8},
                                              Workshop::MapItemKind::Npc, 61000, -1, "Giant");
            for (auto it = added.changes.begin(); it != added.changes.end(); ++it)
                large.resources[it.key()].edited = it.value();
            large.validate();
            bool rejected = false;
            try {
                Workshop::addMapItem(large, "DWELLING.DAT", page, {31, 31},
                                     Workshop::MapItemKind::Npc, 61000);
            } catch (const std::exception &) {
                rejected = true;
            }
            CHECK(rejected);
            rejected = false;
            try {
                Workshop::addMapItem(large, "DWELLING.DAT", page, {9, 9},
                                     Workshop::MapItemKind::Npc, 61000);
            } catch (const std::exception &) {
                rejected = true;
            }
            CHECK(rejected);
            MapCanvas map;
            map.tiles.resize(61001);
            map.tiles[61000] = CustomTiles::image(large, 61000);
            map.actorFootprints[61000] = QSize(2, 2);
            map.actors = {{8, 8, 61000, 1, {}}};
            map.tool = MapCanvas::InspectNpc;
            map.ids = QByteArray(1024, 5);
            map.side = 32;
            map.resizeMap();
            int selected = -1;
            map.chooseNpc = [&](int npc) { selected = npc; };
            QMouseEvent click(QEvent::MouseButtonPress, QPointF(9 * 32 + 1, 9 * 32 + 1),
                              QPointF(9 * 32 + 1, 9 * 32 + 1), Qt::LeftButton, Qt::LeftButton,
                              Qt::NoModifier);
            QApplication::sendEvent(&map, &click);
            CHECK(selected == 1);
            auto expandedBytes = CustomTiles::records(large).value(61000);
            expandedBytes[253] = 2;
            auto invalid = CustomTiles::records(large);
            invalid[61000] = expandedBytes;
            rejected = false;
            try {
                CustomTiles::library(large, invalid);
            } catch (const std::exception &) {
                rejected = true;
            }
            CHECK(rejected);
            puts("PASS multi-tile editor sizes, independent world art, package v11, placement "
                 "bounds/overlap and selection across the footprint");
        }
        WorkshopWindow window;
        CHECK(window.openGame(TEST_GAME_DIR));
        window.projectForTests() = p;
        for (const auto &sheet : sheets) {
            window.selectResource(sheet);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto palette = window.findChild<QWidget *>("artworkPalette");
            CHECK(palette);
            auto expected = Workshop::artworkColors(U5::readGraphics(sheet, p.data(sheet)));
            QSet<QRgb> actual;
            for (auto b : palette->findChildren<QToolButton *>())
                if (b->property("rgba").isValid())
                    actual.insert(b->property("rgba").toUInt());
            CHECK(actual.size() == expected.size());
            for (auto rgba : expected) {
                if (!actual.contains(rgba))
                    fprintf(stderr, "Palette mismatch %s color %08x\n", qPrintable(sheet), rgba);
                CHECK(actual.contains(rgba));
            }
        }

        window.selectResource("DWELLING.DAT");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto palette = window.findChild<QListWidget *>("mapPalette");
        CHECK(palette);
        bool found = false;
        for (int i = 0; i < palette->count(); i++)
            if (palette->item(i)->data(Qt::UserRole).toUInt() == 40000) {
                palette->setCurrentRow(i);
                found = true;
            }
        CHECK(found);
        auto mapCanvas = dynamic_cast<MapCanvas *>(window.findChild<QWidget *>("mapCanvas"));
        CHECK(mapCanvas && mapCanvas->brush == 40000);
        window.selectResource("TYPES.OVR");
        CHECK(window.findChild<QWidget *>("customTileEditor"));
        puts("PASS custom tile artwork, map painting, stable IDs, package/project persistence, "
             "clipboard and native UI");
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "FAIL exception: %s\n", e.what());
        return 1;
    }
}
