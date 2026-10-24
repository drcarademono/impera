#include "mod/npcs.h"
#include "custom_tiles.h"
#include "world_objects.h"
#include "map_entities.h"
#include "../src/tiles.h"
#include <QtWidgets>
#include <memory>
using U5::require;
namespace Workshop {
QString WorldObjects::resource(int world) {
    require(world == 0 || world == 1, "Invalid world");
    return world == 0 ? "BRIT.OOL" : "UNDER.OOL";
}
bool WorldObjects::overridden(const Project &p, int world) {
    auto it = p.resources.constFind(resource(world));
    return it != p.resources.cend() && (it->included || it->edited != it->original);
}
QByteArray WorldObjects::effective(const Project &p, int world) {
    QByteArray out = overridden(p, world) ? p.data(resource(world))
                     : world == 0         ? QByteArray(256, 0)
                                          : p.data("INIT.OOL");
    require(out.size() == 256, "World objects need 32 eight-byte records");
    return out;
}
QByteArray WorldObjects::change(const Project &p, int world, int slot, const QByteArray &record) {
    require(slot > 0 && slot < 32 && record.size() == 8,
            "Choose an available object slot (slot 0 belongs to the party)");
    auto bytes = effective(p, world);
    auto original = bytes;
    bytes.replace(slot * 8, 8, record);
    validate(resource(world), bytes, original);
    return bytes;
}
void WorldObjects::validate(const QString &name, const QByteArray &b, const QByteArray &original) {
    require(b.size() == 256, "World objects must contain 32 eight-byte records");
    for (int i = 0; i < 32; ++i) {
        auto r = b.mid(i * 8, 8);
        if (r == original.mid(i * 8, 8) || !uchar(r[0]))
            continue;
        require(i > 0, "World object slot 0 is reserved for the active party");
        int shape = uchar(r[0]), property = uchar(r[5]);
        if (r[0] != original[i * 8] || r[5] != original[i * 8 + 5]) {
            require(shape != 3 || property < 8, "Potion color is invalid");
            require(shape != 4 || property < 8 || property == 255, "Scroll type is invalid");
            require(!(shape == 5 || shape == 6 || (shape >= 9 && shape <= 12)) || property < 48,
                    "Equipment type is invalid");
            require(shape != 25 || property < 8, "Moonstone phase is invalid");
            require(shape != 180 || property < 3, "Shard identity is invalid");
        }
        if (!QList<int>{1,  2,  15, 16, 17, 25, 27, 30, 32, 33,  34,  35,  36,  37,  38,
                        39, 40, 41, 42, 43, 44, 45, 46, 47, 180, 181, 182, 183, 236, 252}
                 .contains(uchar(r[0])))
            continue; // Unknown object types are retained, with diagnostics rather than invented
                      // semantics.
        require(uchar(r[4]) == (name == "BRIT.OOL" ? 0 : 255), "Object floor must match its world");
        // Every byte-sized coordinate is valid on a 256×256 world.
        int tile = uchar(r[0]), anim = uchar(r[1]);
        require(anim == tile || ((tile & 0xfc) == (anim & 0xfc) && tile >= 0x20),
                "Animated shape must match the object's tile family");
        if ((tile & 0xf8) == TILE_ACTOR_FRIGATE_20 || (tile & 0xfc) == TILE_ACTOR_PIRATE)
            require(uchar(r[5]) <= 100, "Ship hull condition must be between 0 and 100");
    }
}
static bool equipmentShape(int shape) {
    return shape == 5 || shape == 6 || (shape >= 9 && shape <= 12);
}
static int defaultProperty(int shape) {
    return shape >= 44 && shape <= 47                                             ? 100
           : (shape & 0xf8) == 0x20                                               ? 99
           : shape == 2 || shape == 7 || shape == 8 || shape == 13 || shape == 15 ? 1
           : shape == 5                                                           ? 16
           : shape == 6                                                           ? 4
           : shape == 10                                                          ? 42
           : shape == 11                                                          ? 9
           : shape == 12                                                          ? 45
                                                                                  : 0;
}
QString mapObjectName(int shape, int value, int aux) {
    if (shape >= 44 && shape <= 47)
        return "Pirate ship";
    if (shape == 236)
        return "Whirlpool";
    if (shape == 252)
        return "Shadow Lord";
    if (shape == 180)
        return "Shard";
    if (shape == 181)
        return "Lord British’s crown";
    if (shape == 182)
        return "Lord British’s sceptre";
    if (shape == 183)
        return "Lord British’s amulet";
    if (shape == 5)
        return "Weapon";
    if (shape == 6)
        return "Shield";
    if (shape == 9)
        return "Helm";
    if (shape == 10)
        return "Ring";
    if (shape == 11)
        return "Armour";
    if (shape == 12)
        return "Amulet";
    if (shape == 3)
        return "Potion";
    if (shape == 4)
        return "Scroll";
    if (shape == 7)
        return "Key";
    if (shape == 8)
        return "Gem";
    if (shape == 13)
        return "Torch";
    if (shape == 14)
        return "Sandalwood box";
    if (shape == 31)
        return NPCS_Blood(shape, value, aux) ? "Blood splatter" : "Corpse";
    if (shape == TILE_ACTOR_CHEST)
        return "Chest";
    if (shape == TILE_ACTOR_GOLD)
        return "Gold";
    if (shape == TILE_ACTOR_FOOD)
        return "Food";
    if (shape == TILE_ACTOR_HORSE || shape == TILE_ACTOR_HORSE + 1)
        return "Horse";
    if (shape == TILE_ACTOR_MOONSTONE)
        return "Moonstone";
    if (shape == TILE_ACTOR_CARPET)
        return "Magic carpet";
    if (shape == TILE_ACTOR_SLEEP)
        return "Corpse (intact)";
    if ((shape & 0xf8) == TILE_ACTOR_FRIGATE_20)
        return "Frigate";
    if (shape >= TILE_ACTOR_SKIFF && shape <= TILE_ACTOR_SKIFF + 3)
        return "Skiff";
    if (Workshop::monsterShapes().contains(shape))
        return Workshop::actorName(shape);
    return QString("World object (shape %1)").arg(shape);
}
QWidget *worldObjectEditor(Project *project, int initialWorld, ObjectCommit commit,
                           QMap<QString, int> *state, std::function<bool()> *flush,
                           MapCanvas *sharedCanvas, std::function<void(QPoint)> locate,
                           TypedObjectCommit typedCommit) {
    auto owner = std::make_unique<QWidget>();
    auto root = owner.get();
    root->setObjectName("worldObjectEditor");
    auto layout = new QVBoxLayout(root);
    auto toolbar = new QHBoxLayout;
    auto world = new QComboBox;
    world->setObjectName("objectWorld");
    world->addItems({project->locationName(0), project->locationName(41)});
    world->setCurrentIndex(initialWorld);
    world->setToolTip(
        "Edit the effective objects for a fresh game in this world. Existing saves are unchanged.");
    toolbar->addWidget(world);
    if (sharedCanvas)
        world->hide();
    auto add = new QPushButton("Add object");
    add->setObjectName("addWorldObject");
    add->setToolTip("Use the first empty world slot. Slot 0 is reserved for the active party.");
    toolbar->addWidget(add);
    auto remove = new QPushButton("Remove object");
    remove->setObjectName("removeWorldObject");
    remove->setToolTip("Empty the selected object slot; unrelated records are preserved.");
    toolbar->addWidget(remove);
    auto revert = new QPushButton("Revert to inherited");
    revert->setObjectName("revertWorldObjects");
    revert->setToolTip("Undoably remove this world's explicit override: Britannia becomes empty; "
                       "Underworld uses INIT.OOL.");
    if (!sharedCanvas)
        toolbar->addWidget(revert);
    toolbar->addStretch();
    layout->addLayout(toolbar);
    if (sharedCanvas)
        layout->addWidget(revert);
    auto status = new QLabel;
    status->setObjectName("objectProvenance");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto split = new QSplitter;
    auto list = new QListWidget;
    list->setObjectName("worldObjectList");
    list->setToolTip(
        "Starting world objects, not settlement NPCs. Select an object to inspect and place it.");
    split->addWidget(list);
    auto canvas = sharedCanvas ? sharedCanvas : new MapCanvas;
    QScrollArea *view = nullptr;
    if (!sharedCanvas) {
        canvas->setObjectName("worldObjectMap");
        canvas->setToolTip(
            "Drag a selected object to place it. Map terrain is read-only in this layer.");
        canvas->side = canvas->rows = 256;
        canvas->zoom = 0.5;
        canvas->tiles = U5::readGraphics("TILES.16", project->data("TILES.16")).images;
        view = new QScrollArea;
        view->setObjectName("canvasView");
        view->setWidget(canvas);
        split->addWidget(view);
        split->setStretchFactor(1, 1);
    } else {
        split->setOrientation(Qt::Vertical);
    }
    canvas->tool = MapCanvas::InspectNpc;
    canvas->terrainEditingEnabled = false;
    canvas->ghosts.clear();
    auto inspector = new QWidget;
    auto il = new QFormLayout(inspector);
    auto type = new QComboBox;
    type->setObjectName("worldObjectType");
    for (int tile : {1,  2,  3,  4,  5,  6,  7,  8,   9,   10,  11,  12,  13, 14, 15,
                     16, 17, 25, 27, 30, 31, 32, 33,  34,  35,  36,  37,  38, 39, 40,
                     41, 42, 43, 44, 45, 46, 47, 180, 181, 182, 183, 236, 252})
        type->addItem(mapObjectName(tile), tile);
    for (int tile : Workshop::monsterShapes())
        type->addItem(mapObjectName(tile), tile);
    if (typedCommit)
        for (auto record : CustomTiles::records(*project)) {
            TypeDef d;
            TYPES_Decode(reinterpret_cast<const unsigned char *>(record.constData()), &d);
            if (d.kind == 2 && d.id >= 512)
                type->addItem(QIcon(QPixmap::fromImage(CustomTiles::image(*project, d.id))),
                              QString::fromUtf8(d.name), d.id);
        }
    type->setToolTip("Choose a verified world object shape. Other inherited shapes are retained, "
                     "with unsupported properties read-only.");
    il->addRow("Object", type);
    auto x = new QSpinBox;
    auto y = new QSpinBox;
    x->setRange(0, 255);
    y->setRange(0, 255);
    x->setObjectName("worldObjectX");
    y->setObjectName("worldObjectY");
    x->setToolTip("World map column, 0–255.");
    y->setToolTip("World map row, 0–255.");
    il->addRow("Column", x);
    il->addRow("Row", y);
    auto hull = new QSpinBox;
    hull->setObjectName("shipHull");
    hull->setRange(0, 100);
    hull->setToolTip(
        "Hull condition, from 0 to 100. Frigates start at 99; pirate ships start at 100. "
        "Rough seas and combat can damage a ship.");
    il->addRow("Ship condition", hull);
    auto itemValue = new QSpinBox;
    itemValue->setObjectName("worldItemValue");
    itemValue->setToolTip("Gold or food quantity; moonstone phase is 0–7.");
    auto itemLabel = new QLabel("Quantity");
    il->addRow(itemLabel, itemValue);
    auto variant = new QComboBox;
    variant->setObjectName("worldItemVariant");
    variant->setToolTip("Choose the item variant, shard, or Shadow Lord identity.");
    auto variantLabel = new QLabel("Type");
    il->addRow(variantLabel, variant);
    auto skeletonKey = new QCheckBox("Skeleton key");
    skeletonKey->setObjectName("worldSkeletonKey");
    skeletonKey->setToolTip("Use skeleton keys rather than ordinary keys.");
    il->addRow(skeletonKey);
    auto details = new QLabel;
    details->setWordWrap(true);
    details->setToolTip("Animation frame and type-specific bytes are preserved. Unverified "
                        "properties are intentionally read-only.");
    il->addRow("Advanced details", details);
    split->addWidget(inspector);
    layout->addWidget(split, 1);
    auto loading = std::make_shared<bool>(false);
    auto load = std::make_shared<std::function<void()>>();
    auto selection = [=] {
        int slot = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toInt() : -1;
        *loading = true;
        auto b = WorldObjects::effective(*project, world->currentIndex());
        bool selected = slot > 0;
        inspector->setEnabled(selected);
        remove->setEnabled(selected);
        canvas->selectedNpc = slot;
        if (selected) {
            (*state)["objects/slot"] = slot;
            auto r = b.mid(slot * 8, 8);
            int tile = uchar(r[0]);
            QString map = world->currentIndex() ? "UNDER.DAT" : "BRIT.DAT";
            QVector<U5::MapPage> pages{{map, 256, 0, 256}};
            unsigned selectedType = CustomTiles::actorType(*project, map, pages[0], 0, slot, tile);
            int index = type->findData(selectedType);
            if (index < 0) {
                type->addItem(QString("Unusual shape %1 (preserved)").arg(tile), tile);
                index = type->count() - 1;
            }
            if (auto corpse = type->findData(31); corpse >= 0)
                type->setItemText(corpse, NPCS_Blood(tile, uchar(r[5]), uchar(r[7]))
                                              ? "Blood splatter"
                                              : "Corpse");
            type->setCurrentIndex(index);
            x->setValue(uchar(r[2]));
            y->setValue(uchar(r[3]));
            const bool item =
                tile == 2 || tile == 15 || tile == 25 || tile == 7 || tile == 8 || tile == 13;
            itemValue->setVisible(item);
            itemLabel->setVisible(item);
            itemLabel->setText(tile == 25 ? "Moon phase" : "Quantity");
            int inheritedValue = tile == 7 ? uchar(r[5]) & 127 : uchar(r[5]);
            itemValue->setRange(qMin(tile == 25 ? 0 : 1, inheritedValue), qMax(tile == 25  ? 7
                                                                               : tile == 7 ? 127
                                                                                           : 255,
                                                                               inheritedValue));
            itemValue->setValue(inheritedValue);
            skeletonKey->setVisible(tile == 7);
            skeletonKey->setChecked(uchar(r[5]) & 128);
            variant->clear();
            auto variants =
                Workshop::mapItemVariants(tile == 180            ? MapItemKind::Shard
                                          : tile == 252          ? MapItemKind::ShadowLord
                                          : equipmentShape(tile) ? MapItemKind::Equipment
                                          : tile == 3            ? MapItemKind::Potion
                                                                 : MapItemKind::Scroll);
            const bool typed =
                equipmentShape(tile) || tile == 3 || tile == 4 || tile == 180 || tile == 252;
            variant->setVisible(typed);
            variantLabel->setVisible(typed);
            if (typed) {
                const int variantValue = uchar(r[tile == 252 ? 7 : 5]);
                variantLabel->setText(tile == 252 ? "Identity" : tile == 180 ? "Shard" : "Type");
                for (auto entry : variants)
                    variant->addItem(entry.first, entry.second);
                if (variant->findData(variantValue) < 0)
                    variant->addItem("Unassigned (preserved)", variantValue);
                variant->setCurrentIndex(variant->findData(variantValue));
            }
            hull->setEnabled((tile & 0xf8) == 0x20 || (tile & 0xfc) == 0x2c);
            hull->setValue(qMin(100, int(uchar(r[5]))));
            details->setText(
                QString("Slot %1 · %2\nAnimation: %3\nProperty bytes: %4 %5 %6\nFloor: %7")
                    .arg(slot)
                    .arg(WorldObjects::overridden(*project, world->currentIndex())
                             ? "World override"
                         : world->currentIndex() == 0 ? "Normal start"
                                                      : "Initial underworld objects")
                    .arg(uchar(r[1]))
                    .arg(uchar(r[5]))
                    .arg(uchar(r[6]))
                    .arg(uchar(r[7]))
                    .arg(world->currentIndex() == 0 ? "Surface" : "Underworld"));
            if (view)
                view->ensureVisible(uchar(r[2]) * 8, uchar(r[3]) * 8, 80, 80);
        }
        canvas->update();
        *loading = false;
    };
    *load = [=] {
        *loading = true;
        int which = world->currentIndex(), selected = state->value("objects/slot", 1);
        auto b = WorldObjects::effective(*project, which);
        list->clear();
        canvas->actors.clear();
        int count = 0;
        for (int slot = 1; slot < 32; ++slot) {
            auto r = b.mid(slot * 8, 8);
            int tile = uchar(r[0]);
            if (!tile)
                continue;
            ++count;
            auto mapName = which ? QString("UNDER.DAT") : QString("BRIT.DAT");
            U5::MapPage mapPage{mapName, 256, 0, 256};
            unsigned appearance = CustomTiles::actorType(*project, mapName, mapPage, 0, slot, 256 + tile);
            QString label =
                appearance >= 512
                    ? QString::fromUtf8(CustomTiles::definition(*project, appearance).name)
                    : mapObjectName(tile, uchar(r[5]), uchar(r[7]));
            auto art = appearance >= 512 ? CustomTiles::image(*project, appearance, 6)
                                         : canvas->tiles.value(appearance);
            auto item = new QListWidgetItem(QIcon(QPixmap::fromImage(art)),
                QString("%1 · (%2,%3)").arg(label).arg(uchar(r[2])).arg(uchar(r[3])), list);
            item->setData(Qt::UserRole, slot);
            item->setToolTip(QString("World slot %1 · %2").arg(slot).arg(label));
            canvas->actors.append(
                {int(uchar(r[2])), int(uchar(r[3])), int(appearance), slot, label});
            if (slot == selected)
                list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count())
            list->setCurrentRow(0);
        const QString map = which == 0 ? "BRIT.DAT" : "UNDER.DAT";
        if (project->resources.contains(map)) {
            auto overlay =
                project->resources.contains("DATA.OVL") ? project->data("DATA.OVL") : QByteArray();
            canvas->ids = U5::worldMap(map, project->data(map), overlay);
        } else
            canvas->ids = QByteArray(65536, 0);
        if (!sharedCanvas)
            canvas->original = canvas->ids;
        canvas->resizeMap();
        if (sharedCanvas) {
            status->setText(QString("New-game objects · %1 of 31 slots used\n%2")
                                .arg(count)
                                .arg(WorldObjects::overridden(*project, which)
                                         ? "Customized for this world"
                                     : which == 0 ? "Normal start: Britannia begins without objects"
                                                  : "Inherited initial Underworld objects"));
        } else
            status->setText(
                QString("Starting world objects · %1 of 31 object slots used (%2 "
                        "available) · 32 fixed records, including reserved party slot\n%3")
                    .arg(count)
                    .arg(31 - count)
                    .arg(WorldObjects::overridden(*project, which) ? "World override"
                         : which == 0 ? "Normal start — empty Britannia list"
                                      : "Initial underworld objects — inherited from INIT.OOL"));
        revert->setEnabled(WorldObjects::overridden(*project, which));
        add->setEnabled(count < 31);
        *loading = false;
        selection();
    };
    auto write = [=](int slot, const QByteArray &r) {
        try {
            int which = world->currentIndex();
            auto bytes = WorldObjects::change(*project, which, slot, r);
            unsigned selectedType = type->currentData().toUInt();
            if (!r[0] || !list->currentItem() ||
                list->currentItem()->data(Qt::UserRole).toInt() != slot)
                selectedType = 0;
            if (!(typedCommit
                      ? typedCommit(WorldObjects::resource(which), bytes, true, slot, selectedType)
                      : commit(WorldObjects::resource(which), bytes, true)))
                return false;
            (*state)["objects/slot"] = slot;
            (*load)();
            return true;
        } catch (const std::exception &error) {
            QMessageBox::warning(root, "Cannot change starting object", error.what());
            (*load)();
            return false;
        }
    };
    auto change = [=] {
        if (*loading || !list->currentItem())
            return;
        int slot = list->currentItem()->data(Qt::UserRole).toInt();
        auto r = WorldObjects::effective(*project, world->currentIndex()).mid(slot * 8, 8);
        unsigned selectedType = type->currentData().toUInt();
        int tile = selectedType >= 512 ? CustomTiles::definition(*project, selectedType).base
                                       : selectedType;
        if (tile != uchar(r[0])) {
            r[0] = r[1] = char(tile);
            r[5] = char(defaultProperty(tile));
            r[7] = char((tile & 0xf8) == 0x20 ? 1 : tile == 252 ? 0 : 255);
        }
        r[2] = char(x->value());
        r[3] = char(y->value());
        r[4] = world->currentIndex() == 0 ? 0 : char(255);
        if (((tile & 0xf8) == 0x20 || (tile & 0xfc) == 0x2c) &&
            tile == uchar(WorldObjects::effective(*project, world->currentIndex())[slot * 8]))
            r[5] = char(hull->value());
        if (tile == uchar(WorldObjects::effective(*project, world->currentIndex())[slot * 8])) {
            if (tile == 2 || tile == 15 || tile == 25 || tile == 7 || tile == 8 || tile == 13)
                r[5] = char(itemValue->value() | (tile == 7 && skeletonKey->isChecked() ? 128 : 0));
            if ((equipmentShape(tile) || tile == 3 || tile == 4 || tile == 180 || tile == 252) &&
                variant->currentIndex() >= 0) {
                r[tile == 252 ? 7 : 5] = char(variant->currentData().toInt());
                if (equipmentShape(tile))
                    r[0] = r[1] = char(mapItemShape(MapItemKind::Equipment, uchar(r[5])));
            }
        }
        write(slot, r);
    };
    QObject::connect(list, &QListWidget::currentRowChanged, root, [=](int) {
        if (!*loading)
            selection();
    });
    QObject::connect(world, qOverload<int>(&QComboBox::currentIndexChanged), root, [=](int i) {
        (*state)["objects/world"] = i;
        (*load)();
    });
    QObject::connect(type, qOverload<int>(&QComboBox::currentIndexChanged), root,
                     [=](int) { change(); });
    QObject::connect(variant, &QComboBox::currentIndexChanged, root, [=] { change(); });
    QObject::connect(skeletonKey, &QCheckBox::toggled, root, [=] { change(); });
    for (auto spin : {x, y, hull, itemValue})
        QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), root,
                         [=](int) { change(); });
    QObject::connect(add, &QPushButton::clicked, root, [=] {
        auto b = WorldObjects::effective(*project, world->currentIndex());
        for (int slot = 1; slot < (world->currentIndex() ? 28 : 32); ++slot)
            if (!uchar(b[slot * 8])) {
                QByteArray r(8, 0);
                r[0] = r[1] = 36;
                r[4] = world->currentIndex() == 0 ? 0 : char(255);
                r[5] = 99;
                if (sharedCanvas) {
                    r[2] = char(canvas->keyboardCell.x());
                    r[3] = char(canvas->keyboardCell.y());
                }
                write(slot, r);
                return;
            }
    });
    QObject::connect(remove, &QPushButton::clicked, root, [=] {
        if (list->currentItem()) {
            int slot = list->currentItem()->data(Qt::UserRole).toInt();
            auto r = WorldObjects::effective(*project, world->currentIndex()).mid(slot * 8, 8);
            r[0] = r[1] = 0;
            write(slot, r);
        }
    });
    QObject::connect(revert, &QPushButton::clicked, root, [=] {
        auto name = WorldObjects::resource(world->currentIndex());
        if (commit(name, project->resources[name].original, false))
            (*load)();
    });
    if (sharedCanvas && locate) {
        auto show = new QPushButton("Show selected object");
        show->setToolTip("Center the shared map on the selected starting object.");
        layout->addWidget(show);
        QObject::connect(show, &QPushButton::clicked, root, [=] {
            if (list->currentItem())
                locate({x->value(), y->value()});
        });
    }
    canvas->chooseNpc = [=](int slot) {
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toInt() == slot) {
                list->setCurrentRow(i);
                break;
            }
    };
    canvas->moveNpc = [=](int slot, QPoint p) {
        auto r = WorldObjects::effective(*project, world->currentIndex()).mid(slot * 8, 8);
        r[2] = char(p.x());
        r[3] = char(p.y());
        return write(slot, r);
    };
    *flush = [=] {
        canvas->cancelStroke();
        return true;
    };
    (*load)();
    return owner.release();
}
} // namespace Workshop
