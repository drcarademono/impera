#undef NDEBUG
#include "extended_maps.h"
#include "floor_coverage_editor.h"
#include "map_editor.h"
#include "map_json.h"
#include "mod/package.h"
#include "mod/types.h"
#include <cassert>
#include <functional>
static int page(const MapDocument &doc, int level) {
    for (int i = 0; i < doc.pages.size(); i++)
        if (doc.pages[i].settlement == 0 && doc.pages[i].floor == level)
            return i;
    return -1;
}
static void apply(Project &p, const QMap<QString, QByteArray> &edits) {
    for (auto i = edits.begin(); i != edits.end(); ++i)
        p.resources[i.key()].edited = i.value();
    Workshop::hydrateExtendedMaps(p);
}
static void rejects(std::function<void()> action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    assert(rejected);
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    Project p;
    p.openGame(TEST_GAME_DIR);
    assert(FLOOR3D_Default(9, 2, 8, 16) == 1);
    assert(FLOOR3D_Default(12, 2, 11, 12) == 1);
    assert(FLOOR3D_Default(18, 2, 15, 9) == 1);
    assert(FLOOR3D_Default(30, 2, 20, 23) == 1);
    MapDocument castle(&p, "CASTLE.DAT");
    int ground = page(castle, 0), upper = page(castle, 1), roof = page(castle, 2),
        terrace = page(castle, 3);
    assert(ground >= 0 && upper >= 0 && roof >= 0 && terrace >= 0);
    auto a = Workshop::floorCoverage(p, "CASTLE.DAT", upper);
    auto r = Workshop::floorCoverage(p, "CASTLE.DAT", roof);
    auto t = Workshop::floorCoverage(p, "CASTLE.DAT", terrace);
    auto tiles = castle.rawTerrain(roof);
    int roofs = 0;
    for (int i = 0; i < 1024; i++)
        if (uchar(tiles[i]) == 0x27 || uchar(tiles[i]) == 0x28) {
            assert(r[i] && !t[i]);
            roofs++;
        }
    assert(roofs == 224 && a[11 * 32 + 16] && t[15 * 32 + 15]);
    assert(!t[0] && !Workshop::floorCoverage(p, "CASTLE.DAT", page(castle, -1))[10 * 32 + 31]);
    QByteArray flags(1024, 0);
    flags[11 * 32 + 16] = 1;
    flags[15 * 32 + 16] = 3;
    p.resources["FLOORS3D.OVR"].edited = Workshop::setFloorCoverage(p, 17, 1, 32, 32, flags);
    assert(Workshop::floorCoverage(p, "CASTLE.DAT", upper) == flags);
    p.validate();
    auto bytes = p.data("FLOORS3D.OVR");
    assert(FLOOR3D_Validate(bytes.constData(), bytes.size()));
    auto rec = FLOOR3D_Find(bytes.constData(), bytes.size(), 17, 1);
    assert(rec && FLOOR3D_Cell(rec, 16, 15) == 3 && !FLOOR3D_Cell(rec, -1, 0));
    for (int off : {0, 12, 23, 28, 32}) {
        auto bad = bytes;
        bad[off] = char(uchar(bad[off]) ^ 2);
        assert(!FLOOR3D_Validate(bad.constData(), bad.size()));
    }
    assert(!FLOOR3D_Validate(bytes.constData(), bytes.size() - 1));
    auto package = p.package();
    assert(package.startsWith("IMOD0031"));
    Project imported;
    imported.openGame(TEST_GAME_DIR);
    imported.importPackage(package);
    assert(imported.data("FLOORS3D.OVR") == bytes);
    auto oldVersion = package;
    oldVersion.replace(0, 8, "IMOD0030");
    rejects([&] { imported.importPackage(oldVersion); });
    QTemporaryDir temp;
    p.save(temp.filePath("floors.imperaproject"));
    Project loaded;
    loaded.load(temp.filePath("floors.imperaproject"));
    assert(loaded.data("FLOORS3D.OVR") == bytes);
    apply(p, Workshop::resizeMap(p, "CASTLE.DAT", upper, 40, 34, 5));
    auto resized = Workshop::floorCoverage(p, "CASTLE.DAT", upper);
    assert(resized.size() == 40 * 34 && resized[15 * 40 + 16] == 3 && !resized[33 * 40 + 39]);
    p.validate();
    auto json = Workshop::exportMapJson(p, "CASTLE.DAT", ground);
    Project target;
    target.openGame(TEST_GAME_DIR);
    apply(target,
          Workshop::addLocation(target, "BRIT.DAT", "Transferred castle", {230, 130}, 32, 32, 5));
    apply(target, Workshop::importMapJson(target, "LOC042.DAT", 0, json));
    target.validate();
    MapDocument copied(&target, "LOC042.DAT");
    assert(Workshop::floorCoverage(target, "LOC042.DAT", page(copied, 1)) == resized);
    auto invalid = QJsonDocument::fromJson(json);
    auto root = invalid.object();
    auto map = root["map"].toObject();
    auto floors = map["floors"].toArray();
    auto f = floors[0].toObject();
    f["coverage3d"] = "AA==";
    floors[0] = f;
    map["floors"] = floors;
    root["map"] = map;
    auto before = target.data("FLOORS3D.OVR");
    rejects(
        [&] { Workshop::importMapJson(target, "LOC042.DAT", 0, QJsonDocument(root).toJson()); });
    assert(target.data("FLOORS3D.OVR") == before);
    // Saving a painted stroke and undoing/redoing it uses the actual modal editor.
    QTimer::singleShot(0, [&] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        assert(dialog);
        auto canvas = dialog->findChild<QWidget *>("floor3dCanvas");
        assert(canvas);
        dialog->findChild<QToolButton *>("floor3dExclude")->click();
        QPointF point(16 * 16 + 8, 11 * 16 + 8);
        QMouseEvent down(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(canvas, &down);
        QMouseEvent up(QEvent::MouseButtonRelease, point, point, Qt::LeftButton, Qt::NoButton,
                       Qt::NoModifier);
        QApplication::sendEvent(canvas, &up);
        QPushButton *undo = nullptr, *redo = nullptr;
        for (auto button : dialog->findChildren<QPushButton *>()) {
            if (button->text() == "Undo")
                undo = button;
            if (button->text() == "Redo")
                redo = button;
        }
        assert(undo && redo && undo->isEnabled());
        undo->click();
        assert(redo->isEnabled());
        redo->click();
        dialog->accept();
    });
    apply(imported, Workshop::editFloorCoverage(nullptr, imported, "CASTLE.DAT", upper));
    assert(!Workshop::floorCoverage(imported, "CASTLE.DAT", upper)[11 * 32 + 16]);
    QTimer::singleShot(
        0, [] { qobject_cast<QDialog *>(QApplication::activeModalWidget())->reject(); });
    assert(Workshop::editFloorCoverage(nullptr, imported, "CASTLE.DAT", upper).isEmpty());
}
