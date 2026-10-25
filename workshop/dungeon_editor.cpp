#include "map_positions.h"
#include "extended_maps.h"
#include "dungeon_editor.h"
#include "map_resize_dialog.h"
#include "contents_editor.h"
#include "map_entities.h"
#include "dungeon_names.h"
namespace Workshop {
QVector<QImage> dungeonFeatureImages(const QVector<QImage> &tiles) {
    U5::require(tiles.size() >= 512, "Dungeon artwork requires the complete Ultima 5 tileset");
    QVector<QImage> images;
    // Dungeon bytes are features, not terrain IDs. Each value retains its own visual slot.
    static const int artwork[] = {0x44, 0xc8, 0xc9, 0xc8, 257,  0xd8, 0x8c, 257,
                                  0x00, 0x45, 0xb8, 0x4f, 0x4d, 0x4e, 0xb8, 0xb8};
    for (int value = 0; value < 256; ++value) {
        const int kind = value >> 4, variant = value & 15;
        QImage image = tiles[artwork[kind]]
                           .scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                           .convertToFormat(QImage::Format_ARGB32);
        // Keep the source artwork's black pixels opaque, including sprite backgrounds.
        if (kind == 8) {
            const QColor colors[] = {QColor("#ff55ff"), QColor("#55ff55"), QColor("#ff5555"),
                                     QColor("#5555ff")};
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y).rgb() != QColor(Qt::black).rgb())
                        image.setPixelColor(x, y, colors[qMin(variant & 7, 3)]);
        }
        QPainter painter(&image);
        QString badge;
        if (kind == 3)
            badge = QChar(0x2195);
        if (kind == 6 && (variant & 7))
            badge = (variant & 7) == 2 ? "B" : "T";
        if (kind == 7)
            badge = "O";
        if (kind == 9)
            badge = "?";
        if (kind == 10 || kind == 15)
            badge = QString::number(variant + 1);
        if (kind == 11 && variant)
            badge = "M";
        if (kind == 12 || kind == 14)
            badge = "S";
        if (kind == 13)
            badge = "H";
        if (kind == 8 && (variant & 7) >= 4)
            badge = "?";
        if (!badge.isEmpty()) {
            // Fixed pixel glyphs keep badges legible and identical on every platform.
            // Native font metrics can clip every glyph in a small badge on Windows.
            static const QMap<QChar, QString> glyphs = {
                {'0', "111101101101111"}, {'1', "010110010010111"},
                {'2', "111001111100111"}, {'3', "111001111001111"},
                {'4', "101101111001001"}, {'5', "111100111001111"},
                {'6', "111100111101111"}, {'7', "111001010010010"},
                {'8', "111101111101111"}, {'9', "111101111001111"},
                {'B', "110101110101110"}, {'T', "111010010010010"},
                {'O', "111101101101111"}, {'M', "101111111101101"},
                {'S', "111100111001111"}, {'H', "101101111101101"},
                {'?', "110001010000010"}, {QChar(0x2195), "010111010111010"}};
            const int width = badge.size() * 8 + 2;
            const QRect box(32 - width, 20, width, 12);
            painter.fillRect(box, Qt::black);
            for (int character = 0; character < badge.size(); ++character) {
                const auto pixels = glyphs.value(badge[character]);
                for (int pixel = 0; pixel < pixels.size(); ++pixel)
                    if (pixels[pixel] == '1')
                        painter.fillRect(box.x() + 1 + character * 8 + (pixel % 3) * 2,
                                         box.y() + 1 + (pixel / 3) * 2, 2, 2, Qt::white);
            }
        }
        if (value < 0x90 && (value & 8)) {
            painter.setPen(QPen(QColor("#55ffff"), 2));
            painter.drawEllipse(QRect(24, 2, 5, 5));
        }
        images.append(image);
    }
    return images;
}
DungeonDocument::DungeonDocument(const Project *p) : project(p) {
    U5::require(project->data("DUNGEON.DAT").size() == 4096,
                "Dungeons must contain eight 512-byte blocks");
}
int DungeonDocument::offset(int dungeon, int floor) {
    U5::require(dungeon >= 0 && dungeon < 8 && floor >= 0 && floor < 8,
                "Dungeon or level is out of range");
    return dungeon * 512 + floor * 64;
}
QSize DungeonDocument::size(int dungeon, int floor) const {
    U5::require(floor >= 0 && floor < 8 && isDungeon(*project, 33 + dungeon),
                "Invalid dungeon floor");
    const auto b = project->resources.value("MAPS.OVR").edited;
    const auto r = MAPS_Find(b.constData(), b.size(), 4, 33 + dungeon, floor);
    return r ? QSize(MAPS_Word(r + 10), MAPS_Word(r + 12)) : QSize(8, 8);
}
QByteArray DungeonDocument::level(int dungeon, int floor, bool original) const {
    size(dungeon, floor);
    const auto b = project->resources.value("MAPS.OVR").edited;
    const auto r = original ? nullptr : MAPS_Find(b.constData(), b.size(), 4, 33 + dungeon, floor);
    if (r)
        return b.mid(r - reinterpret_cast<const unsigned char *>(b.constData()) + MAPS_RECORD,
                     MAPS_Word(r + 10) * MAPS_Word(r + 12));
    if (dungeon >= 8)
        return QByteArray(64, char(0xb0));
    return (original ? project->resources["DUNGEON.DAT"].original : project->data("DUNGEON.DAT"))
        .mid(offset(dungeon, floor), 64);
}
int DungeonDocument::roomIndex(int dungeon, int cell) {
    offset(dungeon, 0);
    U5::require((cell & 0xf0) == 0xa0 || (cell & 0xf0) == 0xf0,
                "This feature does not enter a combat room");
    // DUNGEON_0000: map ID minus 0x21, then subtract one except for Deceit.
    return qMax(0, dungeon - 1) * 16 + (cell & 15);
}
int DungeonDocument::pageIndex(int dungeon, int floor) const {
    const auto pages = project->mapPages("DUNGEON.DAT");
    for (int i = 0; i < pages.size(); ++i)
        if (pages[i].settlement == dungeon && pages[i].floor == floor)
            return i;
    throw std::runtime_error("Missing dungeon floor");
}
int DungeonDocument::room(int dungeon, int cell) const {
    if (dungeon < 8)
        return roomIndex(dungeon, cell);
    for (const auto &m : extendedMaps(*project))
        if (m.kind == 4 && m.id == unsigned(33 + dungeon) && !m.floor)
            return m.roomBase + (cell & 15);
    throw std::runtime_error("Missing dungeon rooms");
}
QString DungeonDocument::feature(int cell) {
    static const QStringList names = {"Passage",
                                      "Ladder up",
                                      "Ladder down",
                                      "Ladder up and down",
                                      "Chest",
                                      "Fountain",
                                      "Pit / trap",
                                      "Open chest",
                                      "Field",
                                      "Unused / impossible",
                                      "Encounter-room door",
                                      "Wall / wall message",
                                      "Special wall",
                                      "Hidden door",
                                      "Special door",
                                      "Encounter-room door (alternate)"};
    U5::require(cell >= 0 && cell <= 255, "Dungeon feature byte out of range");
    return names[cell >> 4];
}
QString DungeonDocument::variant(int cell) {
    const int kind = cell >> 4, i = cell & 15;
    if (kind == 10 || kind == 15)
        return QString("Room %1").arg(i + 1);
    if (kind == 8) {
        if ((i & 7) < 4)
            return QStringList{"Sleep", "Poison gas", "Fire", "Electricity"}[i & 7] +
                   (i & 8 ? " · ceiling opening" : "");
        return QString("Advanced energy field variant %1").arg(i);
    }
    if (kind == 5)
        return QStringList{"Cure poison", "Heal", "Poison", "Bad taste / injury"}.value(
            i, "Bad taste / injury (advanced)");
    if (kind == 6 && (i & 7) < 3)
        return QStringList{"Open pit", "Hidden pit trap", "Bomb trap"}[i & 7] +
               (i & 8 ? " · ceiling opening" : "");
    if (kind == 11)
        return i == 0 ? "Plain wall" : QString("Wall message %1").arg(i);
    if (i == 0)
        return "Default";
    if (i == 8 && kind < 9)
        return "Opening in ceiling";
    return QString("Advanced variant %1").arg(i);
}
QMap<QString, QByteArray> DungeonDocument::changes(int dungeon, int floor,
                                                   const QByteArray &cells) const {
    const auto dimensions = size(dungeon, floor);
    U5::require(cells.size() == dimensions.width() * dimensions.height(),
                "Dungeon floor dimensions changed");
    auto maps = extendedMaps(*project);
    for (auto &m : maps)
        if (m.kind == 4 && m.id == unsigned(33 + dungeon) && m.floor == floor) {
            m.cells = cells;
            return {{"MAPS.OVR", writeExtendedMaps(maps)}};
        }
    auto bytes = project->data("DUNGEON.DAT");
    bytes.replace(offset(dungeon, floor), 64, cells);
    return {{"DUNGEON.DAT", bytes}};
}
void DungeonDocument::validate(const Project &p, bool allRoomReferences) {
    DungeonDocument document(&p);
    for (int id : dungeonIds(p))
        for (int d = id - 33, f = 0; f < 8; f++) {
            const auto cells = document.level(d, f), original = document.level(d, f, true);
            const auto dims = document.size(d, f);
            for (int y = 0; y < dims.height(); y++)
                for (int x = 0; x < dims.width(); x++) {
                    int value = U5::byte(cells, y * dims.width() + x);
                    bool changed = x >= 8 || y >= 8 || value != U5::byte(original, y * 8 + x);
                    if (!changed && !allRoomReferences)
                        continue;
                    U5::require(!changed || (value & 0xf0) != 0x90,
                                "Cannot introduce an unused / impossible dungeon feature");
                    if ((value & 0xf0) == 0xa0 || (value & 0xf0) == 0xf0) {
                        int room = document.room(d, value);
                        U5::require(p.resources.contains("DUNGEON.CBT") &&
                                        p.data("DUNGEON.CBT").size() >= (room + 1) * 352,
                                    "Encounter-room reference has no matching DUNGEON.CBT room");
                    }
                }
        }
}

QStringList DungeonDocument::warnings(int dungeon, int floor, int x, int y) const {
    const auto dims = size(dungeon, floor);
    U5::require(x >= 0 && x < dims.width() && y >= 0 && y < dims.height(),
                "Dungeon coordinates out of range");
    int value = U5::byte(level(dungeon, floor), y * dims.width() + x), kind = value >> 4;
    QStringList result;
    if ((kind == 1 || kind == 3) && floor > 0) {
        auto otherSize = size(dungeon, floor - 1);
        int other = x < otherSize.width() && y < otherSize.height()
                        ? U5::byte(level(dungeon, floor - 1), y * otherSize.width() + x) >> 4
                        : -1;
        if (other != 2 && other != 3)
            result << "Ladder up has no matching ladder down on the level above.";
    }
    if (kind == 2 || kind == 3) {
        if (floor == 7)
            result << "Ladder down points below the deepest level.";
        else {
            auto otherSize = size(dungeon, floor + 1);
            int other = x < otherSize.width() && y < otherSize.height()
                            ? U5::byte(level(dungeon, floor + 1), y * otherSize.width() + x) >> 4
                            : -1;
            if (other != 1 && other != 3)
                result << "Ladder down has no matching ladder up on the level below.";
        }
    }
    if (kind == 9)
        result << "The engine describes this feature as impossible; preserved from the original.";
    if (kind == 10 || kind == 15) {
        int room = this->room(dungeon, value);
        if (!project->resources.contains("DUNGEON.CBT") ||
            project->data("DUNGEON.CBT").size() < (room + 1) * 352)
            result << "The linked combat room is missing.";
        if (dungeon < 2)
            result << "Deceit and Despise share this combat-room bank.";
    }
    if (kind != 10 && kind != 15 && (value & 7))
        result << "Subtype / message bits are retained. Their effect depends on this feature.";
    return result;
}
DungeonWorkspace::DungeonWorkspace(
    Project *p, QMap<QString, int> *state,
    std::function<bool(const QMap<QString, QByteArray> &, const QString &)> commit,
    std::function<void(int)> room, QWidget *parent)
    : QWidget(parent) {
    setObjectName("dungeonWorkspace");
    auto document = std::make_shared<DungeonDocument>(p);
    auto layout = new QVBoxLayout(this);
    auto row = new QHBoxLayout;
    const int dungeon = state->value("DUNGEON.DAT/dungeon");
    auto dungeonName = new QLabel(p->locationName(33 + dungeon));
    dungeonName->setObjectName("dungeonName");
    auto floor = new QComboBox;
    floor->setObjectName("dungeonLevelSelector");
    for (int i = 0; i < 8; ++i)
        floor->addItem(QString("Level %1").arg(i + 1));
    floor->setToolTip(
        "Level 1 is nearest the surface; Level 8 is deepest. Engine indices are 0–7.");
    floor->setCurrentIndex(qBound(0, state->value("DUNGEON.DAT/level"), 7));
    row->addWidget(dungeonName);
    auto rename = new QToolButton;
    rename->setObjectName("renameLocation");
    rename->setIcon(MapCanvas::toolIcon(MapCanvas::Pencil));
    rename->setToolTip("Rename dungeon");
    row->addWidget(rename);
    connect(rename, &QToolButton::clicked, this, [=] {
        const int index = dungeon;
        bool ok = false;
        const auto name = QInputDialog::getText(this, "Rename dungeon", "Name", QLineEdit::Normal,
                                                p->locationName(33 + index), &ok);
        if (!ok || name.trimmed() == p->locationName(33 + index))
            return;
        try {
            QMap<QString, QByteArray> edits;
            if (MAPS_NewLocation(33 + index)) {
                auto maps = extendedMaps(*p);
                for (auto &m : maps)
                    if (m.kind == 4 && m.id == unsigned(33 + index))
                        m.name = name.trimmed();
                edits["MAPS.OVR"] = writeExtendedMaps(maps);
            } else
                edits["LOCATIONS.OVR"] = p->renameLocation(33 + index, name);
            if (commit(edits, "Rename dungeon"))
                dungeonName->setText(p->locationName(33 + index));
        } catch (const std::exception &error) {
            QMessageBox::warning(this, "Cannot rename dungeon", QString::fromUtf8(error.what()));
        }
    });

    row->addWidget(new QLabel("Level:"));
    row->addWidget(floor);
    auto resize = new QToolButton;
    resize->setText("Resize…");
    resize->setObjectName("resizeDungeonFloor");
    resize->setToolTip(
        "Change this floor’s width and height while keeping existing cells at their coordinates.");
    row->addWidget(resize);
    row->addStretch();
    layout->addLayout(row);
    auto tools = new QHBoxLayout;
    canvas = new MapCanvas;
    canvas->setObjectName("dungeonCanvas");
    canvas->clipboardMime = "application/x-impera-dungeon-features";
    canvas->clipboardMagic = "IMPDUNG1";
    auto toolGroup = new QButtonGroup(this);
    auto inspectors = new QStackedWidget;
    auto featurePanel = new QWidget;
    auto contentsPanel = std::make_shared<ContentsEditor *>(nullptr);
    canvas->side = 8;
    canvas->zoom = 3;
    canvas->grid = true;
    for (auto pair : QList<QPair<QString, int>>{{"Pencil", MapCanvas::Pencil},
                                                {"Pick", MapCanvas::Eyedropper},
                                                {"Pan", MapCanvas::Pan},
                                                {"Select / Move", MapCanvas::InspectNpc},
                                                {"Select", MapCanvas::Select},
                                                {"Rectangle", MapCanvas::Rectangle},
                                                {"Fill", MapCanvas::Fill}}) {
        auto button = new QToolButton;
        button->setIcon(MapCanvas::toolIcon(pair.second));
        button->setIconSize({22, 22});
        button->setFixedSize(28, 28);
        button->setCheckable(true);
        toolGroup->addButton(button, pair.second);
        button->setObjectName(QString("dungeonTool%1").arg(pair.second));
        button->setAccessibleName(pair.first);
        button->setStyleSheet("QToolButton:checked { background:#365f8d; border:2px solid #86b9ed; "
                              "border-radius:3px; }");
        const QMap<int, QString> hints = {
            {MapCanvas::Pencil, "Pencil (B): drag to paint the current feature and variant."},
            {MapCanvas::Eyedropper, "Eyedropper (I): pick a feature with its variant / room link. "
                                    "Right-click also picks."},
            {MapCanvas::Pan, "Pan: drag the view without changing dungeon cells."},
            {MapCanvas::Select,
             "Select (V): drag an area to copy. Shift+arrow keys extend the selection."},
            {MapCanvas::Rectangle,
             "Rectangle (R): paint a filled rectangle with the current brush."},
            {MapCanvas::Fill,
             "Fill (F): replace connected cells with the same feature and variant."}};
        button->setToolTip(hints.value(pair.second));
        connect(button, &QPushButton::clicked, this, [=] {
            canvas->cancelStroke();
            canvas->tool = pair.second;
            if (pair.second != MapCanvas::InspectNpc) {
                inspectors->setCurrentWidget(featurePanel);
                canvas->actors.clear();
                canvas->chooseNpc = {};
                canvas->moveNpc = {};
            }
        });
        tools->addWidget(button);
    }
    for (auto text : QStringList{"Copy", "Paste", "Clear selection"}) {
        auto button = new QPushButton(text);
        button->setToolTip(text + " feature bytes; copying preserves subtypes and room links.");
        connect(button, &QPushButton::clicked, this, [=] {
            if (text == "Copy")
                canvas->copySelection();
            else if (text == "Paste")
                canvas->beginPaste();
            else
                canvas->clearSelection();
        });
        tools->addWidget(button);
    }
    auto highlight = new QCheckBox("Highlight changes");
    highlight->setToolTip("Compare this level with the original game file.");
    connect(highlight, &QCheckBox::toggled, this, [=](bool value) {
        canvas->comparison = value;
        canvas->update();
    });
    tools->addWidget(highlight);
    auto zoom = new QComboBox;
    zoom->setObjectName("dungeonZoom");
    zoom->addItems({"100%", "200%", "300%", "400%", "600%"});
    zoom->setCurrentIndex(2);
    zoom->setToolTip("Dungeon map zoom");
    connect(zoom, &QComboBox::currentIndexChanged, this, [=](int index) {
        const int scales[] = {1, 2, 3, 4, 6};
        canvas->zoom = scales[index];
        canvas->resizeMap();
    });
    tools->addWidget(zoom);
    layout->addLayout(tools);
    auto split = new QHBoxLayout;
    auto palette = new QListWidget;
    palette->setObjectName("dungeonPalette");
    palette->setMaximumWidth(240);
    for (int i = 0; i < 16; ++i) {
        if (i == 9)
            continue;
        auto item = new QListWidgetItem(DungeonDocument::feature(i * 16), palette);
        item->setData(Qt::UserRole, i * 16);
        item->setToolTip(item->text());
    }
    split->addWidget(palette);
    auto scroll = new MapView(canvas);
    split->addWidget(scroll, 1);
    auto side = new QVBoxLayout(featurePanel);
    auto details = new QLabel;
    details->setWordWrap(true);
    details->setObjectName("dungeonCellDetails");
    side->addWidget(details);
    auto brushPreview = new QLabel;
    brushPreview->setObjectName("dungeonBrushPreview");
    side->addWidget(brushPreview);
    auto subtype = new QComboBox;
    subtype->setObjectName("dungeonSubtype");
    subtype->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    subtype->setMinimumContentsLength(12);
    subtype->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    subtype->setToolTip("Low feature bits: room number 1–16 for room doors (encoded 0–15); "
                        "otherwise feature-specific subtype, message, or ceiling opening flag.");
    side->addWidget(new QLabel("Brush variant / room:"));
    side->addWidget(subtype);
    auto apply = new QPushButton("Apply brush to selected cell");
    apply->setToolTip("Replace the inspected cell with the selected feature and subtype.");
    side->addWidget(apply);
    auto open = new QPushButton("Open combat room");
    open->setObjectName("openDungeonRoom");
    open->setToolTip("Edit the combat room entered through the inspected door. Back to dungeon "
                     "restores this cell.");
    side->addWidget(open);
    auto stack = new QListWidget;
    stack->setObjectName("dungeonFloorStack");
    stack->setToolTip(
        "Vertical connections at the inspected X/Y position. Select a level to inspect it.");
    side->addWidget(stack);
    auto warning = new QLabel;
    warning->setWordWrap(true);
    side->addWidget(warning);
    side->addStretch();
    inspectors->addWidget(featurePanel);
    inspectors->setObjectName("dungeonInspector");
    inspectors->setFixedWidth(300);
    split->addWidget(inspectors);
    canvas->prepareSelection = [=](QPoint cell) {
        if (inspectors->currentWidget()->objectName() == "mapPositionEditor")
            return true;
        if (*contentsPanel) {
            inspectors->removeWidget(*contentsPanel);
            delete *contentsPanel;
        }
        *contentsPanel = new ContentsEditor(
            p, {0, 33 + dungeon, floor->currentIndex()}, canvas, commit);
        inspectors->addWidget(*contentsPanel);
        inspectors->setCurrentWidget(*contentsPanel);
        (*contentsPanel)->selectCell(cell);
        return true;
    };
    auto positions = new QToolButton;
    positions->setText("Positions");
    positions->setObjectName("mapPositions");
    positions->setToolTip("Place the party start on this dungeon level.");
    tools->addWidget(positions);
    connect(positions, &QToolButton::clicked, this, [=] {
        canvas->cancelStroke();
        U5::MapPage page{};
        page.settlement = dungeon;
        page.floor = floor->currentIndex();
        auto old = inspectors->findChild<QWidget *>("mapPositionEditor");
        if (old) {
            inspectors->removeWidget(old);
            delete old;
        }
        canvas->tool = MapCanvas::InspectNpc;
        auto panel =
            mapPositionEditor(p, "DUNGEON.DAT", page, canvas, commit, state, [=](QPoint cell) {
                canvas->keyboardCell = cell;
                canvas->update();
            });
        inspectors->addWidget(panel);
        inspectors->setCurrentWidget(panel);
    });
    auto addMenuButton = new QToolButton;
    addMenuButton->setText("Add…");
    addMenuButton->setObjectName("dungeonAddMenu");
    addMenuButton->setPopupMode(QToolButton::InstantPopup);
    addMenuButton->setToolTip("Place a chest or container on this dungeon level.");
    auto addMenu = new QMenu(addMenuButton);
    for (bool container : {true, false}) {
        auto action = addMenu->addAction(container ? "Chest" : "Container");
        connect(action, &QAction::triggered, this, [=] {
            canvas->beginPlacement(container ? 0x40 : -1, true, [=](QPoint cell) {
                try {
                    auto bytes =
                        addContentsAt(*p, {0, 33 + dungeon, floor->currentIndex()},
                                      cell, container);
                    if (!commit({{"CONTENTS.OVR", bytes}},
                                container ? "Add dungeon chest" : "Add container"))
                        return false;
                    canvas->tool = MapCanvas::InspectNpc;
                    toolGroup->button(MapCanvas::InspectNpc)->setChecked(true);
                    canvas->prepareSelection(cell);
                    canvas->update();
                    return true;
                } catch (const std::exception &e) {
                    QMessageBox::warning(this, "Cannot add map item", e.what());
                    return false;
                }
            });
        });
    }
    addMenuButton->setMenu(addMenu);
    tools->insertWidget(7, addMenuButton);
    canvas->deleteItem = [=] {
        try {
            auto target = ContentsTarget{0, 33 + dungeon, floor->currentIndex()};
            auto cell = canvas->keyboardCell;
            for (auto actor : canvas->actors)
                if (actor.npc == canvas->selectedNpc)
                    cell = {actor.x, actor.y};
            auto defs = readContents(p->data("CONTENTS.OVR"));
            bool removed = false;
            for (int i = defs.size() - 1; i >= 0; i--)
                if (defs[i].kind == target.kind && defs[i].map == target.map &&
                    defs[i].floor == target.floor && defs[i].x == cell.x() &&
                    defs[i].y == cell.y()) {
                    defs.removeAt(i);
                    removed = true;
                }
            if (removed && commit({{"CONTENTS.OVR", writeContents(defs, p->data("CONTENTS.OVR"))}},
                                  "Delete dungeon contents"))
                canvas->prepareSelection(cell);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Cannot delete contents", e.what());
        }
    };
    layout->addLayout(split, 1);
    auto status = new QLabel;
    status->setToolTip("Feedback for copy, paste, and map gestures.");
    layout->addWidget(status);
    canvas->feedback = [=](const QString &text) { status->setText(text); };
    canvas->tiles = dungeonFeatureImages(U5::readGraphics("TILES.16", p->data("TILES.16")).images);
    palette->setIconSize({32, 32});
    for (int i = 0; i < palette->count(); ++i) {
        auto item = palette->item(i);
        item->setIcon(QPixmap::fromImage(canvas->tiles[item->data(Qt::UserRole).toInt()]));
    }
    canvas->brush = qBound(0, state->value("DUNGEON.DAT/brush", 0), 255);
    canvas->chooseTool = [=](int tool) {
        canvas->tool = tool;
        if (auto button = toolGroup->button(tool))
            button->setChecked(true);
    };
    toolGroup->button(MapCanvas::Pencil)->setChecked(true);
    auto selected = std::make_shared<QPoint>(
        QPoint(qMax(0, state->value("DUNGEON.DAT/x")), qMax(0, state->value("DUNGEON.DAT/y"))));
    auto inspect = [=] {
        int d = dungeon, f = floor->currentIndex(),
            v = U5::byte(canvas->ids, selected->y() * canvas->side + selected->x());
        (*state)["DUNGEON.DAT/x"] = selected->x();
        (*state)["DUNGEON.DAT/y"] = selected->y();
        details->setText(QString("X %1 · Y %2\n%3\nBrush: %4")
                             .arg(selected->x())
                             .arg(selected->y())
                             .arg(DungeonDocument::feature(v) + " · " + DungeonDocument::variant(v))
                             .arg(DungeonDocument::feature(canvas->brush)));
        open->setEnabled((v >> 4 == 10 || v >> 4 == 15) && p->resources.contains("DUNGEON.CBT") &&
                         p->data("DUNGEON.CBT").size() >= (document->room(d, v) + 1) * 352);
        warning->setText(document->warnings(d, f, selected->x(), selected->y()).join("\n"));
        QSignalBlocker block(stack);
        stack->clear();
        for (int i = 0; i < 8; i++) {
            const auto otherSize = document->size(d, i);
            stack->addItem(
                QString("Level %1 — %2")
                    .arg(i + 1)
                    .arg(selected->x() < otherSize.width() && selected->y() < otherSize.height()
                             ? DungeonDocument::feature(
                                   U5::byte(document->level(d, i),
                                            selected->y() * otherSize.width() + selected->x()))
                             : QString("Outside floor")));
        }
        stack->setCurrentRow(f);
    };
    auto load = [=] {
        canvas->cancelStroke();
        if (*contentsPanel) {
            inspectors->removeWidget(*contentsPanel);
            delete *contentsPanel;
            *contentsPanel = nullptr;
        }
        inspectors->setCurrentWidget(featurePanel);
        canvas->chooseNpc = {};
        canvas->moveNpc = {};
        canvas->actors.clear();
        canvas->positionMarkers.clear();
        (*state)["DUNGEON.DAT/dungeon"] = dungeon;
        (*state)["DUNGEON.DAT/level"] = floor->currentIndex();
        canvas->clearSelection();
        const auto dims = document->size(dungeon, floor->currentIndex());
        canvas->side = dims.width();
        canvas->rows = dims.height();
        *selected = {qBound(0, selected->x(), dims.width() - 1),
                     qBound(0, selected->y(), dims.height() - 1)};
        canvas->ids = document->level(dungeon, floor->currentIndex());
        auto original = document->level(dungeon, floor->currentIndex(), true);
        canvas->original = QByteArray(dims.width() * dims.height(), 0);
        for (int y = 0; y < qMin(8, dims.height()); y++)
            memcpy(canvas->original.data() + y * dims.width(), original.constData() + y * 8,
                   qMin(8, dims.width()));
        resize->setText(QString("%1 × %2…").arg(dims.width()).arg(dims.height()));
        canvas->keyboardCell = *selected;
        canvas->resizeMap();
        inspect();
    };
    auto brush = [=](int value) {
        canvas->brush = value;
        brushPreview->setPixmap(QPixmap::fromImage(canvas->tiles[value])
                                    .scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation));
        (*state)["DUNGEON.DAT/brush"] = value;
        QSignalBlocker block(subtype);
        subtype->clear();
        for (int i = 0; i < 16; ++i)
            subtype->addItem(DungeonDocument::variant((value & 0xf0) | i), i);
        {
            QSignalBlocker paletteBlock(palette);
            palette->setCurrentRow(-1);
            for (int i = 0; i < palette->count(); ++i)
                if (palette->item(i)->data(Qt::UserRole).toInt() == (value & 0xf0))
                    palette->setCurrentRow(i);
        }
        subtype->setCurrentIndex(value & 15);
        inspect();
    };
    canvas->pick = brush;
    canvas->inspect = [=](int x, int y, int) {
        *selected = {x, y};
        inspect();
    };
    canvas->commit = [=](const QByteArray &cells) {
        return commit(document->changes(dungeon, floor->currentIndex(), cells),
                      "Edit dungeon level");
    };
    canvas->changed = inspect;
    connect(palette, &QListWidget::currentItemChanged, this, [=](QListWidgetItem *item) {
        if (item)
            brush(item->data(Qt::UserRole).toInt());
    });
    connect(subtype, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [=](int value) { brush((canvas->brush & 0xf0) | value); });
    connect(apply, &QPushButton::clicked, this, [=] {
        auto cells = canvas->ids;
        cells[selected->y() * canvas->side + selected->x()] = char(canvas->brush);
        if (commit(document->changes(dungeon, floor->currentIndex(), cells),
                   "Configure dungeon cell"))
            load();
    });
    connect(open, &QPushButton::clicked, this, [=] {
        int value = U5::byte(canvas->ids, selected->y() * canvas->side + selected->x());
        room(document->room(dungeon, value));
    });
    connect(stack, &QListWidget::currentRowChanged, this, [=](int i) {
        if (i >= 0)
            floor->setCurrentIndex(i);
    });
    connect(resize, &QToolButton::clicked, this, [=] {
        canvas->cancelStroke();
        const int d = dungeon, f = floor->currentIndex();
        const auto dims = document->size(d, f);
        MapResizeDialog dialog(window(), dims);
        QPointer<DungeonWorkspace> owner(this);
        for (int value = 0; value < 256; value++)
            if ((value >> 4) != 9)
                dialog.fill.addItem(QIcon(QPixmap::fromImage(canvas->tiles.value(value))),
                                    DungeonDocument::feature(value) + " · " +
                                        DungeonDocument::variant(value), value);
        dialog.fill.setCurrentIndex(dialog.fill.findData(canvas->brush));
        if (dialog.exec() != QDialog::Accepted || !owner)
            return;
        try {
            if (commit(resizeMap(*p, "DUNGEON.DAT", document->pageIndex(d, f), dialog.width.value(),
                                 dialog.height.value(), dialog.fill.currentData().toInt()),
                       "Resize dungeon floor") &&
                owner)
                load();
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Cannot resize dungeon floor", e.what());
        }
    });
    connect(floor, qOverload<int>(&QComboBox::currentIndexChanged), this, [=](int) { load(); });
    load();
    brush(canvas->brush);
}
} // namespace Workshop
