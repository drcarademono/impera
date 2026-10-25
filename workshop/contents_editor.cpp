#include "extended_maps.h"
#include "contents_editor.h"
#include "mod/maps.h"
#include "custom_tiles.h"
#include "inventory_names.h"
#include "mod/discoveries.h"
#include "world_objects.h"
#include <QRandomGenerator>
#include <QUuid>
#include <algorithm>
#include <cstring>
namespace Workshop {
ContentsTarget contentsTarget(const QString &r, const U5::MapPage &p, int index) {
    if (r == "BRIT.CBT")
        return {1, index, 0};
    if (r == "DUNGEON.CBT")
        return {2, index, 0};
    if (auto id = MAPS_LocationResource(r.toLatin1().constData()))
        return {0, int(id), p.floor};
    if (r == "DUNGEON.DAT")
        return {0, 33 + p.settlement, p.floor};
    int group = QStringList{"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"}.indexOf(r);
    return {0, group >= 0 ? group * 8 + p.settlement + 1 : 0, r == "UNDER.DAT" ? -1 : p.floor};
}
QVector<ContentsDef> readContents(const QByteArray &b) {
    QVector<ContentsDef> out;
    if (b.isEmpty())
        return out;
    U5::require(CONTENTS_Validate(b.constData(), b.size()), "Invalid map contents");
    auto p = reinterpret_cast<const unsigned char *>(b.constData());
    for (unsigned i = 0; i < CONTENTS_Word(p + 24); i++) {
        ContentsDef d;
        CONTENTS_DecodeRecord(p + 32 + i * 96, p + 8, &d);
        out.append(d);
    }
    return out;
}
QByteArray writeContents(const QVector<ContentsDef> &defs, const QByteArray &previous) {
    if (defs.isEmpty())
        return {};
    U5::require(defs.size() <= CONTENTS_MAX, "Too many contents locations");
    QByteArray b(32 + 96 * defs.size(), 0);
    b.replace(0, 8, QByteArray("ICONT001", 8));
    b.replace(8, 16, previous.size() >= 32 ? previous.mid(8, 16) : QUuid::createUuid().toRfc4122());
    auto p = reinterpret_cast<unsigned char *>(b.data());
    CONTENTS_PutWord(p + 24, defs.size());
    for (int i = 0; i < defs.size(); i++)
        CONTENTS_EncodeRecord(p + 32 + i * 96, &defs[i]);
    U5::require(CONTENTS_Validate(b.constData(), b.size()), "Invalid contents configuration");
    return b;
}
QString contentsItemName(ContentsItem i) {
    static const QStringList basic = {"", "Gold", "Food", "Torches", "Gems", "Keys", "Skull keys"};
    if (i.type <= 6)
        return basic.value(i.type);
    if (i.type == 7)
        return QString::fromLatin1(engineEquipmentNames[i.variant]);
    if (i.type == 8)
        return QStringList{"Vas Lor",     "Rel Hur", "In Sanct",   "In An",
                           "In Quas Wis", "Kal Xen", "In Bet Xen", "An Tym"}
                   .value(i.variant) +
               " scroll";
    if (i.type == 9)
        return QStringList{"Blue", "Yellow", "Red", "Green", "Orange", "Purple", "Black", "White"}
                   .value(i.variant) +
               " potion";
    return "Special discovery";
}
QVector<ContentsDef> inheritedContents(ContentsTarget t) {
    QVector<ContentsDef> out;
    if (t.kind)
        return out;
    for (int i = 0; i < 113; i++) {
        const auto *p = U5_Discoveries[i];
        if (p[2] != t.map || (int8_t)p[3] != t.floor)
            continue;
        ContentsDef d{};
        d.id = i + 1;
        d.original = i;
        d.map = t.map;
        d.floor = t.floor;
        d.x = p[4];
        d.y = p[5];
        d.slot = 255;
        d.refill = (i >= 13 && i <= 15) ? 2 : 0;
        ContentsItem item{};
        item.quantity = p[1];
        switch (p[0]) {
        case 2:
            item.type = 1;
            break;
        case 15:
            item.type = 2;
            break;
        case 13:
            item.type = 3;
            break;
        case 8:
            item.type = 4;
            break;
        case 7:
            item.type = (p[1] & 128) ? 6 : 5;
            item.quantity = p[1] & 127;
            break;
        case 3:
            item.type = 8;
            item.variant = p[1];
            item.quantity = 1;
            break;
        case 4:
            item.type = 9;
            item.variant = p[1];
            item.quantity = 1;
            break;
        case 5:
        case 6:
        case 9:
        case 10:
        case 11:
        case 12:
            item.type = 7;
            item.variant = p[1];
            item.quantity = (p[1] == 27 || p[1] == 29) ? 5 : 1;
            break;
        }
        if (item.type && item.quantity &&
            item.variant < (item.type == 7   ? 48
                            : item.type >= 8 ? 8
                                             : 1)) {
            d.count = 1;
            d.items[0] = item;
        }
        out.append(d);
    }
    return out;
}
QVector<ContentsDef> mapContents(const Project &p, ContentsTarget t) {
    auto originals = inheritedContents(t);
    if (!t.kind && !t.map && p.resources.contains("INIT.OOL") && p.resources.contains("BRIT.OOL") &&
        p.resources.contains("UNDER.OOL")) {
        auto objects = WorldObjects::effective(p, t.floor < 0 ? 1 : 0);
        for (int slot = 1; slot < 32; slot++)
            if (U5::byte(objects, slot * 8) == 1) {
                ContentsDef d{};
                d.id = 0x40000000u + slot + (t.floor < 0 ? 32 : 0);
                d.map = 0;
                d.floor = t.floor;
                d.slot = slot;
                d.action = 1;
                d.flags = (U5::byte(objects, slot * 8 + 5) & 128) ? 2 : 0;
                d.lockDifficulty = d.trapDifficulty =
                    qMin(30u, U5::byte(objects, slot * 8 + 5) & 127);
                d.original = 65535;
                d.x = U5::byte(objects, slot * 8 + 2);
                d.y = U5::byte(objects, slot * 8 + 3);
                originals.append(d);
            }
    }
    auto custom = readContents(p.resources.value("CONTENTS.OVR").edited);
    for (const auto &d : custom) {
        for (int i = originals.size() - 1; i >= 0; i--)
            if ((d.original != 65535 && originals[i].original == d.original) ||
                (d.kind == originals[i].kind && d.map == originals[i].map &&
                 d.floor == originals[i].floor && d.x == originals[i].x && d.y == originals[i].y))
                originals.removeAt(i);
    }
    for (const auto &d : custom)
        if (d.kind == t.kind && d.map == t.map && d.floor == t.floor)
            originals.append(d);
    return originals;
}
bool contentsHit(const Project &p, ContentsTarget t, QPoint cell) {
    for (const auto &d : mapContents(p, t))
        if (d.x == cell.x() && d.y == cell.y())
            return true;
    return false;
}
ContentsEditor::ContentsEditor(Project *p, ContentsTarget t, MapCanvas *c, Commit fn,
                               QWidget *parent)
    : QWidget(parent), project(p), target(t), canvas(c), commit(std::move(fn)) {
    setObjectName("contentsEditor");
    auto layout = new QVBoxLayout(this);
    title = new QLabel("Contents");
    title->setObjectName("contentsTitle");
    title->setWordWrap(true);
    layout->addWidget(title);
    origin = new QLabel;
    origin->setWordWrap(true);
    layout->addWidget(origin);
    mode = new QComboBox;
    mode->setObjectName("contentsMode");
    mode->addItems({"Original game behavior", "Custom contents", "Empty"});
    mode->setToolTip("Keep the game's rewards, choose your own items, or leave this place empty.");
    layout->addWidget(mode);
    auto form = new QFormLayout;
    interaction = new QComboBox;
    interaction->setObjectName("contentsInteraction");
    interaction->addItems({"Search", "Open"});
    interaction->setToolTip("Search for hidden items; Open for a container.");
    form->addRow("Find items by", interaction);
    refill = new QComboBox;
    refill->setObjectName("contentsRefill");
    refill->addItems({"Once per game", "Every game day", "Original special condition"});
    refill->setToolTip(
        "Controls when collected items return. Unclaimed items never accumulate another batch.");
    form->addRow("Refill", refill);
    layout->addLayout(form);
    items = new QTableWidget;
    items->setObjectName("contentsItems");
    items->setColumnCount(2);
    items->setHorizontalHeaderLabels({"Item", "Quantity"});
    items->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    items->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    items->verticalHeader()->hide();
    items->setMinimumHeight(120);
    layout->addWidget(items);
    auto row = new QHBoxLayout;
    add = new QPushButton("Add");
    add->setObjectName("contentsAdd");
    add->setToolTip("Add an inventory item to this place.");
    remove = new QPushButton("Remove");
    remove->setToolTip("Remove the selected item.");
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch();
    layout->addLayout(row);
    properties = new QWidget;
    auto props = new QFormLayout(properties);
    locked = new QCheckBox("Locked");
    locked->setObjectName("contentsLocked");
    locked->setToolTip("Requires Jimmy or unlock magic before opening.");
    props->addRow(locked);
    lockDifficulty = new QSpinBox;
    lockDifficulty->setRange(0, 30);
    lockDifficulty->setObjectName("contentsLockDifficulty");
    lockDifficulty->setToolTip("Higher difficulty makes Jimmy less likely to succeed.");
    props->addRow("Lock difficulty", lockDifficulty);
    trapped = new QCheckBox("Trapped");
    trapped->setObjectName("contentsTrapped");
    trapped->setToolTip("Uses the game's chest-trap damage. Unlock magic disarms it.");
    props->addRow(trapped);
    trapDifficulty = new QSpinBox;
    trapDifficulty->setRange(0, 30);
    trapDifficulty->setObjectName("contentsTrapDifficulty");
    trapDifficulty->setToolTip("Higher difficulty makes the trap harder to detect with Search.");
    props->addRow("Trap difficulty", trapDifficulty);
    layout->addWidget(properties);
    deleteContainer = new QPushButton("Remove container");
    deleteContainer->setObjectName("contentsDeleteContainer");
    deleteContainer->setToolTip(
        "Remove this placed container and its contents together. Terrain stays unchanged.");
    layout->addWidget(deleteContainer);
    connect(deleteContainer, &QPushButton::clicked, this, [this] { removeContainer(); });
    locations = new QListWidget;
    locations->setObjectName("contentsLocations");
    locations->setMaximumHeight(130);
    locations->setToolTip("Select another discovery or container on this floor.");
    layout->addWidget(locations);
    layout->addStretch();
    connect(mode, &QComboBox::currentIndexChanged, this, [this] {
        if (loading)
            return;
        if (!mode->currentIndex()) {
            auto defs = readContents(project->resources.value("CONTENTS.OVR").edited);
            for (int i = defs.size() - 1; i >= 0; i--)
                if (defs[i].id == selected.id)
                    defs.removeAt(i);
            change(defs, "Restore original contents");
        } else
            apply();
    });
    for (auto box : {interaction, refill})
        connect(box, &QComboBox::currentIndexChanged, this, [this] { apply(); });
    for (auto box : {locked, trapped})
        connect(box, &QCheckBox::toggled, this, [this] { apply(); });
    for (auto box : {lockDifficulty, trapDifficulty})
        connect(box, &QSpinBox::editingFinished, this, [this] { apply(); });
    connect(add, &QPushButton::clicked, this, [this] { addItem(); });
    connect(remove, &QPushButton::clicked, this, [this] { removeItem(); });
    connect(locations, &QListWidget::currentRowChanged, this, [this](int i) {
        if (!loading && i >= 0 && i < entries.size())
            selectCell({entries[i].x, entries[i].y});
    });
    canvas->chooseNpc = [this](int i) {
        if (i >= 0 && i < entries.size())
            selectCell({entries[i].x, entries[i].y});
    };
    canvas->moveNpc = [this](int i, QPoint dest) { return move(i, dest); };
    refresh();
    selectCell(canvas->keyboardCell);
}
void ContentsEditor::refresh() {
    loading = true;
    entries = mapContents(*project, target);
    canvas->actors.clear();
    canvas->contentsMarkers.clear();
    locations->clear();
    for (int i = 0; i < entries.size(); i++) {
        const auto &d = entries[i];
        canvas->contentsMarkers.append({d.x, d.y, -1, -1, {}});
        canvas->actors.append(
            {d.x, d.y,
             d.action ? (!target.kind && isDungeon(*project, target.map) ? 0x40 : 257) : -1, i,
             d.action ? QString() : "·"});
        locations->addItem(
            QString("%1 · %2, %3").arg(d.action ? "Chest" : "Discovery").arg(d.x).arg(d.y));
    }
    loading = false;
    canvas->update();
}
void ContentsEditor::selectCell(QPoint point) {
    cell = point;
    selected = {};
    selected.map = target.map;
    selected.kind = target.kind;
    selected.floor = target.floor;
    selected.x = point.x();
    selected.y = point.y();
    selected.slot = 255;
    selected.original = 65535;
    for (const auto &d : entries)
        if (d.x == point.x() && d.y == point.y()) {
            selected = d;
            break;
        }
    int index = point.y() * canvas->side + point.x();
    if (!selected.id && isDungeon(*project, target.map) && index >= 0 &&
        index < canvas->ids.size() &&
        ((U5::byte(canvas->ids, index) & 0xf0) == 0x40 ||
         (U5::byte(canvas->ids, index) & 0xf0) == 0x70))
        selected.action = 1;
    load();
}
void ContentsEditor::load() {
    loading = true;
    locations->setCurrentRow(-1);
    canvas->selectedNpc = -1;
    bool custom = false;
    for (const auto &d : readContents(project->resources.value("CONTENTS.OVR").edited))
        if (d.id == selected.id && d.kind == selected.kind && d.map == selected.map &&
            d.floor == selected.floor && d.x == selected.x && d.y == selected.y)
            custom = true;
    int tile = cell.y() * canvas->side + cell.x();
    QString label = tile >= 0 && tile < canvas->ids.size()
                        ? MapDocument::tileName(U5::byte(canvas->ids, tile))
                        : "Map cell";
    title->setText((selected.action ? "Chest" : label) + " — Contents");
    origin->setText(custom            ? "Modified"
                    : selected.id     ? (selected.action  ? "Original game · random treasure"
                                         : selected.count ? "Original game"
                                                          : "Original game · special discovery")
                    : selected.action ? "Original game · random treasure"
                                      : "");
    mode->setCurrentIndex(custom ? (selected.count ? 1 : 2) : 0);
    interaction->setCurrentIndex(selected.action);
    refill->setCurrentIndex(selected.original == 14 && selected.refill == 2 ? 1 : selected.refill);
    auto refillModel = qobject_cast<QStandardItemModel *>(refill->model());
    refillModel->item(2)->setEnabled(selected.original >= 13 && selected.original <= 15);
    refill->setEnabled(custom && !target.kind);
    interaction->setEnabled(custom);
    properties->setVisible(selected.action);
    properties->setEnabled(custom);
    locked->setChecked(selected.flags & 1);
    trapped->setChecked(selected.flags & 2);
    lockDifficulty->setValue(selected.lockDifficulty);
    trapDifficulty->setValue(selected.trapDifficulty);
    items->setRowCount(selected.count);
    for (int i = 0; i < selected.count; i++) {
        items->setItem(i, 0, new QTableWidgetItem(contentsItemName(selected.items[i])));
        items->item(i, 0)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto quantity = new QSpinBox;
        quantity->setRange(1, selected.items[i].type <= 2 ? 9999 : 99);
        quantity->setValue(selected.items[i].quantity);
        quantity->setEnabled(custom);
        quantity->setToolTip("Number of this item available to collect.");
        items->setCellWidget(i, 1, quantity);
        connect(quantity, &QSpinBox::editingFinished, this, [this, i, quantity] {
            if (!loading && i < selected.count) {
                selected.items[i].quantity = quantity->value();
                apply();
            }
        });
    }
    add->setEnabled(selected.count < 16);
    remove->setEnabled(custom && selected.count > 0);
    for (int i = 0; i < entries.size(); i++)
        if (entries[i].x == cell.x() && entries[i].y == cell.y()) {
            locations->setCurrentRow(i);
            canvas->selectedNpc = i;
        }
    deleteContainer->setVisible(selected.action &&
                                (selected.slot != 255 || (custom && selected.original == 65535)));
    canvas->selectedNpc = locations->currentRow();
    loading = false;
}
bool ContentsEditor::change(QVector<ContentsDef> defs, const QString &description) {
    try {
        auto bytes = writeContents(defs, project->resources.value("CONTENTS.OVR").edited);
        if (commit({{"CONTENTS.OVR", bytes}}, description)) {
            refresh();
            selectCell(cell);
            return true;
        }
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Contents edit rejected", e.what());
    }
    load();
    return false;
}
void ContentsEditor::apply() {
    if (loading)
        return;
    if (!mode->currentIndex())
        return;
    auto defs = readContents(project->resources.value("CONTENTS.OVR").edited);
    auto d = selected;
    if (!d.id) {
        do {
            d.id = QRandomGenerator::global()->generate() | 0x80000000u;
        } while (!d.id || std::any_of(defs.begin(), defs.end(),
                                      [&](const ContentsDef &o) { return d.id == o.id; }));
    }
    d.action = interaction->currentIndex();
    d.refill = target.kind ? 0 : refill->currentIndex();
    if (selected.original == 14 && selected.refill == 2 && d.refill == 1)
        d.refill = 2; // The daily UI label preserves Minoc's inherited save-day condition.
    d.flags = d.action ? (locked->isChecked() ? 1 : 0) | (trapped->isChecked() ? 2 : 0) : 0;
    d.lockDifficulty = lockDifficulty->value();
    d.trapDifficulty = trapDifficulty->value();
    if (mode->currentIndex() == 2) {
        d.count = 0;
        memset(d.items, 0, sizeof(d.items));
    }
    bool replaced = false;
    for (auto &o : defs)
        if (o.id == d.id) {
            o = d;
            replaced = true;
            break;
        }
    if (!replaced)
        defs.append(d);
    change(defs, "Edit map contents");
}
void ContentsEditor::addItem() {
    auto dialog = new QDialog(this);
    dialog->setWindowTitle("Add item");
    auto layout = new QVBoxLayout(dialog);
    auto choices = new QComboBox;
    choices->setEditable(true);
    choices->setInsertPolicy(QComboBox::NoInsert);
    for (int type = 1; type <= 9; type++)
        for (int variant = 0; variant < (type == 7 ? 48 : type >= 8 ? 8 : 1); variant++)
            choices->addItem(contentsItemName({(uint8_t)type, (uint8_t)variant, 1}),
                             (type << 8) | variant);
    layout->addWidget(choices);
    auto quantity = new QSpinBox;
    quantity->setRange(1, 9999);
    quantity->setValue(1);
    layout->addWidget(quantity);
    connect(choices, &QComboBox::currentIndexChanged, dialog,
            [=] { quantity->setMaximum((choices->currentData().toInt() >> 8) <= 2 ? 9999 : 99); });
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    if (dialog->exec() == QDialog::Accepted && choices->findText(choices->currentText()) >= 0) {
        int value = choices->itemData(choices->findText(choices->currentText())).toInt();
        selected.items[selected.count++] = {(uint8_t)(value >> 8), (uint8_t)value,
                                            (uint16_t)quantity->value()};
        {
            QSignalBlocker block(mode);
            mode->setCurrentIndex(1);
        }
        apply();
    }
    delete dialog;
}
void ContentsEditor::removeContainer() {
    auto defs = readContents(project->resources.value("CONTENTS.OVR").edited);
    for (int i = defs.size() - 1; i >= 0; i--)
        if (defs[i].id == selected.id)
            defs.removeAt(i);
    if (selected.slot == 255) {
        change(defs, "Remove container");
        return;
    }
    try {
        int world = selected.floor < 0 ? 1 : 0;
        auto record = WorldObjects::effective(*project, world).mid(selected.slot * 8, 8);
        record[0] = record[1] = 0;
        auto bytes = WorldObjects::change(*project, world, selected.slot, record);
        auto contents = writeContents(defs, project->resources.value("CONTENTS.OVR").edited);
        if (commit({{WorldObjects::resource(world), bytes}, {"CONTENTS.OVR", contents}},
                   "Remove container and contents")) {
            refresh();
            selectCell(cell);
        }
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Container removal rejected", e.what());
    }
}
void ContentsEditor::removeItem() {
    int row = items->currentRow();
    if (row < 0 || row >= selected.count)
        return;
    for (int i = row; i + 1 < selected.count; i++)
        selected.items[i] = selected.items[i + 1];
    selected.items[--selected.count] = {};
    apply();
}
bool ContentsEditor::move(int entry, QPoint dest) {
    if (entry < 0 || entry >= entries.size() || dest.x() < 0 || dest.y() < 0 ||
        dest.x() >= canvas->side || dest.y() >= canvas->rowCount())
        return false;
    auto defs = readContents(project->resources.value("CONTENTS.OVR").edited);
    auto d = entries[entry];
    d.x = dest.x();
    d.y = dest.y();
    bool replaced = false;
    for (auto &o : defs)
        if (o.id == d.id) {
            o = d;
            replaced = true;
        }
    if (!replaced && d.slot == 255) {
        if (!d.count) {
            QMessageBox::information(
                this, "Special discovery",
                "This discovery has special game effects. Its original behavior is preserved.");
            return false;
        }
        defs.append(d);
    }
    cell = dest;
    if (d.slot != 255) {
        try {
            int world = d.floor < 0 ? 1 : 0;
            auto objects = WorldObjects::effective(*project, world);
            auto record = objects.mid(d.slot * 8, 8);
            record[2] = char(dest.x());
            record[3] = char(dest.y());
            auto bytes = WorldObjects::change(*project, world, d.slot, record);
            auto contents = writeContents(defs, project->resources.value("CONTENTS.OVR").edited);
            if (commit({{WorldObjects::resource(world), bytes}, {"CONTENTS.OVR", contents}},
                       "Move container and contents")) {
                refresh();
                selectCell(cell);
                return true;
            }
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Container move rejected", e.what());
        }
        return false;
    }
    if (canvas->tileId(entries[entry].y * canvas->side + entries[entry].x) >= 512) {
        try {
            QString resource = target.kind  ? (target.kind == 1 ? "BRIT.CBT" : "DUNGEON.CBT")
                               : target.map ? QStringList{"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT",
                                                          "KEEP.DAT"}[(target.map - 1) / 8]
                               : target.floor < 0 ? "UNDER.DAT"
                                                  : "BRIT.DAT";
            MapDocument map(project, resource);
            int page = target.kind ? target.map : 0;
            if (!target.kind && target.map) {
                page = -1;
                for (int i = 0; i < map.pages.size(); i++)
                    if (map.pages[i].settlement == (target.map - 1) % 8 &&
                        map.pages[i].floor == target.floor)
                        page = i;
            }
            U5::require(page >= 0, "Container floor is missing");
            auto changes = CustomTiles::moveScenery(*project, resource, page,
                                                    {entries[entry].x, entries[entry].y}, dest);
            changes["CONTENTS.OVR"] =
                writeContents(defs, project->resources.value("CONTENTS.OVR").edited);
            if (commit(changes, "Move container and contents")) {
                refresh();
                selectCell(cell);
                return true;
            }
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Container move rejected", e.what());
        }
        return false;
    }
    return change(defs, "Move map contents");
}
} // namespace Workshop
