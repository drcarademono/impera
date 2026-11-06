#include "floor_coverage_editor.h"
#include "extended_maps.h"
#include "map_editor.h"
#include "map_positions.h"
#include "mod/types.h"
#include <functional>
using U5::require;
namespace Workshop {
static void word(QByteArray &b, int off, unsigned n) {
    b[off] = char(n);
    b[off + 1] = char(n >> 8);
}
static QByteArray record(unsigned map, int floor, int w, int h, const QByteArray &flags) {
    require(w > 0 && h > 0 && w <= 256 && h <= 256 && flags.size() == w * h,
            "Invalid 3D floor area dimensions");
    QByteArray r(16 + int(FLOOR3D_Bytes(w, h)), 0);
    TYPES_Put32(r.data(), r.size());
    word(r, 4, map);
    r[6] = char(floor);
    word(r, 8, w);
    word(r, 10, h);
    for (int i = 0; i < flags.size(); i++) {
        unsigned value = uchar(flags[i]);
        require(value == 0 || value == 1 || value == 3, "Invalid 3D floor area cell");
        r[16 + i / 4] = char(uchar(r[16 + i / 4]) | (value << ((i % 4) * 2)));
    }
    return r;
}
QByteArray setFloorCoverage(const Project &p, unsigned map, int floor, int w, int h,
                            const QByteArray &flags) {
    require(MAPS_Town(map) && floor >= -1 && floor <= 3, "Invalid 3D floor identity");
    auto original = p.resources.value("FLOORS3D.OVR").edited;
    require(original.isEmpty() || FLOOR3D_Validate(original.constData(), original.size()),
            "Invalid 3D floor areas");
    QMap<int, QByteArray> records;
    for (int off = 16; off + 16 <= original.size(); off += TYPES_U32(original.constData() + off)) {
        auto r = original.mid(off, TYPES_U32(original.constData() + off));
        records[MAPS_Word(r.constData() + 4) * 256 + int(int8_t(r[6])) + 128] = r;
    }
    int key = map * 256 + floor + 128;
    if (flags.isEmpty())
        records.remove(key);
    else
        records[key] = record(map, floor, w, h, flags);
    if (records.isEmpty())
        return {};
    QByteArray out(16, 0);
    memcpy(out.data(), "I3DFLR01", 8);
    TYPES_Put32(out.data() + 8, records.size());
    for (auto r : records)
        out += r;
    require(FLOOR3D_Validate(out.constData(), out.size()), "Invalid 3D floor areas");
    return out;
}
QByteArray defaultFloorCoverage(const Project &p, const QString &resource, int index,
                                bool suggest) {
    MapDocument doc(&p, resource);
    require(index >= 0 && index < doc.pages.size(), "Choose a location floor");
    auto page = doc.pages[index];
    unsigned map = positionMap(resource, page);
    require(MAPS_Town(map) && resource != "DUNGEON.DAT" && !isDungeon(p, map),
            "3D floor areas apply to locations");
    auto terrain = doc.rawTerrain(index);
    QByteArray below;
    for (int i = 0; i < doc.pages.size(); i++)
        if (doc.pages[i].settlement == page.settlement && doc.pages[i].floor == page.floor - 1 &&
            doc.pages[i].side == page.side && doc.pages[i].height() == page.height())
            below = doc.rawTerrain(i);
    QByteArray flags(terrain.size(), 0);
    require(
        FLOOR3D_Suggest(
            reinterpret_cast<const unsigned char *>(terrain.constData()),
            below.isEmpty() ? nullptr : reinterpret_cast<const unsigned char *>(below.constData()),
            page.side, page.height(), page.floor, reinterpret_cast<unsigned char *>(flags.data())),
        "Cannot suggest a 3D floor area");
    if (!suggest)
        for (int y = 0; y < page.height(); y++)
            for (int x = 0; x < page.side; x++) {
                unsigned value = FLOOR3D_Default(map, page.floor, x, y);
                if (value != 255)
                    flags[y * page.side + x] = char(value);
            }
    return flags;
}
QByteArray floorCoverage(const Project &p, const QString &resource, int index) {
    auto flags = defaultFloorCoverage(p, resource, index);
    auto page = p.mapPages(resource).value(index);
    auto bytes = p.resources.value("FLOORS3D.OVR").edited;
    require(bytes.isEmpty() || FLOOR3D_Validate(bytes.constData(), bytes.size()),
            "Invalid 3D floor areas");
    auto r = FLOOR3D_Find(bytes.constData(), bytes.size(), positionMap(resource, page), page.floor);
    if (r)
        for (int y = 0; y < page.height(); y++)
            for (int x = 0; x < page.side; x++)
                flags[y * page.side + x] = char(FLOOR3D_Cell(r, x, y));
    return flags;
}
void validateFloorCoverage(const Project &p) {
    auto bytes = p.resources.value("FLOORS3D.OVR").edited;
    if (bytes.isEmpty())
        return;
    require(FLOOR3D_Validate(bytes.constData(), bytes.size()), "Invalid 3D floor areas");
    for (int off = 16; off + 16 <= bytes.size(); off += TYPES_U32(bytes.constData() + off)) {
        const auto *r = reinterpret_cast<const unsigned char *>(bytes.constData() + off);
        unsigned map = MAPS_Word(r + 4);
        int level = int8_t(r[6]);
        require(!isDungeon(p, map), "A dungeon cannot have a location 3D floor area");
        QString resource = map <= 32 ? QStringList{"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT",
                                                   "KEEP.DAT"}[(map - 1) / 8]
                                     : QString("LOC%1.DAT").arg(map, 3, 10, QChar('0'));
        bool found = false;
        if (p.resources.contains(resource))
            for (const auto &page : p.mapPages(resource))
                if (positionMap(resource, page) == int(map) && page.floor == level) {
                    require(page.side == int(MAPS_Word(r + 8)) &&
                                page.height() == int(MAPS_Word(r + 10)),
                            "3D floor area dimensions do not match the map");
                    found = true;
                }
        require(found, "3D floor area refers to a missing location floor");
    }
}
class AreaCanvas : public QWidget {
  public:
    QByteArray flags, before;
    QImage image;
    int width, height, cell = 16, brush = 1;
    bool dragging = false;
    std::function<void()> stroke, changed;
    AreaCanvas(QImage im, QByteArray f, int w, int h) : flags(f), image(im), width(w), height(h) {
        setObjectName("floor3dCanvas");
        setToolTip("Paint the part of this floor that exists at this height. Right-drag excludes "
                   "cells. This changes only the 3D view.");
        setSize();
    }
    void setSize() {
        setFixedSize(width * cell, height * cell);
        update();
    }
    void paintEvent(QPaintEvent *event) override {
        QPainter p(this);
        p.drawImage(rect(), image);
        QRect cells = event->rect();
        int x0 = qMax(0, cells.left() / cell), x1 = qMin(width - 1, cells.right() / cell),
            y0 = qMax(0, cells.top() / cell), y1 = qMin(height - 1, cells.bottom() / cell);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                unsigned f = uchar(flags[y * width + x]);
                QRect r(x * cell, y * cell, cell, cell);
                p.fillRect(r, f == 0   ? QColor(0, 0, 0, 195)
                              : f == 3 ? QColor(40, 200, 240, 120)
                                       : QColor(40, 220, 100, 65));
                if (cell >= 8) {
                    p.setPen(QColor(255, 255, 255, 45));
                    p.drawRect(r);
                }
                if (f == 3 && cell >= 12) {
                    p.setPen(QColor(180, 255, 255));
                    p.drawLine(r.topLeft(), r.bottomRight());
                    p.drawLine(r.topRight(), r.bottomLeft());
                }
            }
    }
    void paintCell(QMouseEvent *event) {
        int x = int(event->position().x()) / cell, y = int(event->position().y()) / cell;
        if (event->position().x() < 0 || event->position().y() < 0 || x >= width || y >= height)
            return;
        flags[y * width + x] = char(event->buttons() & Qt::RightButton ? 0 : brush);
        update(QRect(x * cell, y * cell, cell, cell));
        if (changed)
            changed();
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
            return;
        before = flags;
        dragging = true;
        paintCell(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (dragging)
            paintCell(event);
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        if (dragging && flags != before && stroke)
            stroke();
        dragging = false;
    }
};
QMap<QString, QByteArray> editFloorCoverage(QWidget *parent, const Project &project,
                                            const QString &resource, int index) {
    MapDocument doc(&project, resource);
    auto page = doc.pages.value(index);
    auto initial = floorCoverage(project, resource, index);
    auto graphics = U5::readGraphics("TILES.16", project.data("TILES.16"));
    QDialog dialog(parent);
    dialog.setWindowTitle("3D Floor Area — " + page.name);
    dialog.setObjectName("floor3dDialog");
    dialog.resize(760, 720);
    auto layout = new QVBoxLayout(&dialog);
    auto explanation =
        new QLabel("Include the structure at this height. Exclude lower scenery already shown on "
                   "another floor.\nGreen: geometry · Dark: no extra geometry · Cyan: floor "
                   "opening. 2D maps and movement stay unchanged.");
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto canvas = new AreaCanvas(doc.terrainImage(index, graphics.images, false), initial,
                                 page.side, page.height());
    auto tools = new QHBoxLayout;
    auto group = new QButtonGroup(&dialog);
    group->setExclusive(true);
    for (auto pair : {qMakePair(QString("Include"), 1), qMakePair(QString("Exclude"), 0),
                      qMakePair(QString("Opening"), 3)}) {
        auto button = new QToolButton;
        button->setText(pair.first);
        button->setCheckable(true);
        button->setChecked(pair.second == 1);
        group->addButton(button, pair.second);
        tools->addWidget(button);
        button->setObjectName("floor3d" + pair.first);
        button->setToolTip(
            pair.second == 1
                ? "Render this tile's floor, walls, roof or scenery at the selected floor's height."
            : pair.second == 0 ? "Omit this floor's copy of the tile. Lower geometry remains "
                                 "visible; the map tile itself is preserved."
                               : "Keep scenery here but leave a hole in the floor and ceiling for "
                                 "stairs, ladders or an open shaft.");
        QObject::connect(button, &QToolButton::clicked, &dialog,
                         [=] { canvas->brush = pair.second; });
    }
    tools->addStretch();
    auto zoom = new QComboBox;
    zoom->setObjectName("floor3dZoom");
    zoom->setToolTip("Zoom the floor area preview without changing the map or coverage.");
    for (int scale : {50, 100, 150, 200})
        zoom->addItem(QString::number(scale) + "%", scale);
    zoom->setCurrentIndex(1);
    tools->addWidget(zoom);
    QObject::connect(zoom, &QComboBox::currentIndexChanged, &dialog, [=] {
        canvas->cell = 16 * zoom->currentData().toInt() / 100;
        canvas->setSize();
    });
    layout->addLayout(tools);
    auto scroll = new QScrollArea;
    scroll->setWidget(canvas);
    layout->addWidget(scroll, 1);
    QVector<QByteArray> history{initial};
    int cursor = 0;
    auto actions = new QHBoxLayout;
    auto undo = new QPushButton("Undo"), redo = new QPushButton("Redo");
    undo->setShortcut(QKeySequence::Undo);
    redo->setShortcut(QKeySequence::Redo);
    undo->setToolTip("Undo the last coverage stroke or suggestion in this dialog.");
    redo->setToolTip("Restore an undone coverage edit.");
    actions->addWidget(undo);
    actions->addWidget(redo);
    auto count = new QLabel;
    actions->addWidget(count);
    actions->addStretch();
    auto update = [&] {
        undo->setEnabled(cursor > 0);
        redo->setEnabled(cursor + 1 < history.size());
        int included = 0;
        for (char f : canvas->flags)
            included += f != 0;
        count->setText(QString("%1 of %2 cells included").arg(included).arg(canvas->flags.size()));
    };
    auto save = [&] {
        if (canvas->flags != history[cursor]) {
            history.resize(cursor + 1);
            history.append(canvas->flags);
            cursor++;
        }
        update();
    };
    canvas->stroke = save;
    canvas->changed = update;
    QObject::connect(undo, &QPushButton::clicked, &dialog, [&] {
        if (cursor) {
            canvas->flags = history[--cursor];
            canvas->update();
            update();
        }
    });
    QObject::connect(redo, &QPushButton::clicked, &dialog, [&] {
        if (cursor + 1 < history.size()) {
            canvas->flags = history[++cursor];
            canvas->update();
            update();
        }
    });
    for (auto pair : {qMakePair(QString("Default area"), false),
                      qMakePair(QString("Suggest from terrain"), true)}) {
        auto button = new QPushButton(pair.first);
        actions->addWidget(button);
        button->setToolTip(
            pair.second ? "Create a starting suggestion from structural tiles and enclosed ladder "
                          "landings. Review it before saving; copied scenery can be ambiguous."
                        : "Restore the reviewed vanilla floor area, or the starting suggestion for "
                          "a new custom location.");
        QObject::connect(button, &QPushButton::clicked, &dialog, [&, pair] {
            canvas->flags = defaultFloorCoverage(project, resource, index, pair.second);
            canvas->update();
            save();
        });
    }
    layout->addLayout(actions);
    update();
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)
        ->setToolTip("Save this floor's 3D area as one undoable project change.");
    buttons->button(QDialogButtonBox::Cancel)
        ->setToolTip("Discard coverage edits made in this dialog.");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || canvas->flags == initial)
        return {};
    return {{"FLOORS3D.OVR", setFloorCoverage(project, positionMap(resource, page), page.floor,
                                              page.side, page.height(), canvas->flags)}};
}
} // namespace Workshop
