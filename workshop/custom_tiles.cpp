#include "custom_tiles.h"
#include "mod/maps.h"
#include "extended_maps.h"
#include "color_palette.h"
#include "map_editor.h"
#include "map_entities.h"
#include "mod/package.h"
#include <QImageReader>
#include <QRandomGenerator>
#include <QClipboard>
#include <QStyle>
#include <QButtonGroup>
#include <QToolButton>
#include <QUuid>
#include <algorithm>
#include <cstring>
using U5::require;
namespace CustomTiles {
static unsigned animationPoses(const QVector<Frame> &frames) {
    unsigned local = 0, world = 0;
    for (const auto &frame : frames) {
        local += frame.variant == 0;
        world += frame.variant == 6;
    }
    return std::max(local, world);
}
class PropertySection : public QWidget {
    QString title;
    QToolButton *header;
    QLabel *summary;

  public:
    QWidget *body;
    QFormLayout *form;
    PropertySection(const QString &name, bool expanded, QWidget *parent)
        : QWidget(parent), title(name) {
        setObjectName("tileSection" + name.section(' ', 0, 0));
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 4, 0, 4);
        header = new QToolButton;
        header->setText(name);
        const QMap<QString, QString> help{
            {"Interaction", "What players can do with this tile, and which type it becomes after "
                            "opening, closing, locking or breaking."},
            {"Behavior & encounters", "Original creature behavior and the types produced by "
                                      "transformations, summoning, division and group encounters."},
            {"Movement & visibility", "Who can cross this terrain, how it slows travel, and "
                                      "whether it blocks sight or projectiles."},
            {"Appearance", "How transparent pixels and wall lighting are rendered; these settings "
                           "do not control movement."},
            {"Hazards",
             "Poison and damage caused by this terrain, and which travelers are affected."},
            {"Advanced", "Specialized movement and combat rules. Leave these unchanged unless this "
                         "type needs special behavior."},
            {"Combat",
             "Shared combat statistics and movement for every creature using this type."}};
        header->setToolTip(help.value(name) + " Click to expand or collapse.");
        header->setCheckable(true);
        header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        summary = new QLabel;
        summary->setToolTip(header->toolTip());
        summary->setWordWrap(true);
        summary->setStyleSheet("color:#abb6c7;");
        body = new QWidget;
        form = new QFormLayout(body);
        form->setContentsMargins(8, 4, 0, 0);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        layout->addWidget(header);
        layout->addWidget(summary);
        layout->addWidget(body);
        connect(header, &QToolButton::toggled, this, [=](bool open) {
            header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
            body->setVisible(open);
            summary->setVisible(!open && !summary->text().isEmpty());
        });
        header->setChecked(expanded);
        body->setVisible(expanded);
        header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        summary->hide();
    }
    void describe(const QString &text) {
        summary->setText(text);
        summary->setVisible(!header->isChecked() && !text.isEmpty());
    }
    void expand(bool open) { header->setChecked(open); }
};
QSize artworkSize(unsigned variant) {
    return variant % 6 == 2 ? QSize(14, 16) : variant % 6 == 4 ? QSize(32, 32) : QSize(16, 16);
}
QMap<unsigned, QByteArray> records(const Project &p) {
    QMap<unsigned, QByteArray> result;
    auto data = p.resources.value("TYPES.OVR").edited;
    if (data.isEmpty())
        return result;
    static thread_local QByteArray cachedBytes;
    static thread_local QMap<unsigned, QByteArray> cachedRecords;
    if (data == cachedBytes)
        return cachedRecords;
    require(TYPES_Validate(data.constData(), data.size()), "Invalid tile library");
    int off = 32;
    for (unsigned i = 0; i < TYPES_U32(data.constData() + 24); i++) {
        auto len = TYPES_U32(data.constData() + off);
        result[TYPES_U32(data.constData() + off + 4)] = data.mid(off, len);
        off += len;
    }
    cachedBytes = data;
    cachedRecords = result;
    return result;
}
TypeDef definition(const Project &p, unsigned id) {
    auto r = records(p).value(id);
    TypeDef d;
    if (r.isEmpty()) {
        TYPES_Vanilla(id >= 256, id & 255, &d);
        if (id < 256) {
            auto label = MapDocument::tileName(id).toUtf8();
            std::memset(d.name, 0, 64);
            std::memcpy(d.name, label.constData(), qMin(63, int(label.size())));
        }
        if (id < 512 && p.resources.contains("LOOK2.DAT")) {
            try {
                auto look = Workshop::description(p.data("LOOK2.DAT"), id);
                std::memcpy(d.description, look.constData(), qMin(127, int(look.size())));
            } catch (const std::exception &) {
            }
        }
    } else
        TYPES_Decode(reinterpret_cast<const unsigned char *>(r.constData()), &d);
    return d;
}
QVector<Frame> frames(const QByteArray &r) {
    QVector<Frame> result;
    if (r.isEmpty())
        return result;
    const auto *p = reinterpret_cast<const unsigned char *>(r.constData());
    int off = 256;
    for (unsigned i = 0; i < U5::word(r, 248); i++) {
        int side = p[off + 1];
        int width = TYPES_FrameWidth(p, p[off], side), height = TYPES_FrameRows(p, p[off], side);
        QImage img(p + off + 4, width, height, width * 4, QImage::Format_RGBA8888);
        result.append({img.copy(), U5::word(r, off + 2), p[off]});
        off += TYPES_FrameBytes(p, off);
    }
    return result;
}
QVector<Frame> importedFrames(const QVector<Frame> &existing, const QVector<Frame> &incoming,
                              unsigned variant, int selected) {
    require(!incoming.isEmpty() && incoming.size() <= 64, "Import needs 1–64 frames");
    const unsigned base = variant < 6 ? 0 : 6;
    QVector<Frame> current;
    for (const auto &frame : existing)
        if (frame.variant == variant)
            current.append(frame);
    if (current.isEmpty())
        for (auto frame : existing)
            if (frame.variant == base) {
                frame.variant = variant;
                frame.image = frame.image.scaled(incoming.first().image.size(),
                                                 Qt::IgnoreAspectRatio, Qt::FastTransformation);
                current.append(frame);
            }
    if (incoming.size() == 1 && !current.isEmpty()) {
        current[qBound(0, selected, int(current.size()) - 1)].image = incoming.first().image;
    } else {
        current = incoming;
        for (auto &frame : current)
            frame.variant = variant;
    }
    int defaultCount = 0;
    for (const auto &frame : existing)
        if (frame.variant == base)
            ++defaultCount;
    require(variant == base || current.size() == defaultCount,
            QString("This animation has %1 frames. Import one frame to replace the selected frame, "
                    "or a sheet containing exactly %1 frames.")
                .arg(defaultCount));
    QVector<Frame> result;
    for (unsigned v = 0; v < 12; ++v) {
        QVector<Frame> group;
        if (v == variant)
            group = current;
        else
            for (const auto &frame : existing)
                if (frame.variant == v)
                    group.append(frame);
        if (variant == base && v > base && v < base + 6 && !group.isEmpty()) {
            while (group.size() > current.size())
                group.removeLast();
            while (group.size() < current.size())
                group.append(group.last());
        }
        result += group;
    }
    return result;
}
QByteArray record(TypeDef d, const QVector<Frame> &f) {
    d.frames = f.size();
    QByteArray b(256, 0);
    TYPES_Encode(reinterpret_cast<unsigned char *>(b.data()), &d);
    for (const auto &frame : f) {
        auto img = frame.image.convertToFormat(QImage::Format_RGBA8888);
        unsigned fw = frame.variant < 6 ? TYPES_Width(&d) : 1;
        unsigned fh = frame.variant < 6 ? TYPES_Height(&d) : 1;
        unsigned cell = img.width() / fw;
        require(img.width() == int(cell * fw) &&
                    (cell == 16 || cell == 32 || (frame.variant % 6 == 2 && cell == 14)) &&
                    img.height() == int(TYPES_FrameHeight(cell) * fh),
                "Artwork dimensions do not match actor size");
        b.append(char(frame.variant));
        b.append(char(cell));
        b.append(char(frame.duration));
        b.append(char(frame.duration >> 8));
        for (int y = 0; y < img.height(); y++)
            b.append(reinterpret_cast<const char *>(img.constScanLine(y)), img.width() * 4);
    }
    TYPES_Put32(b.data(), b.size());
    return b;
}
QByteArray library(const Project &p, const QMap<unsigned, QByteArray> &r) {
    QByteArray b(32, 0);
    memcpy(b.data(), "ITYPES01", 8);
    auto old = p.resources.value("TYPES.OVR").edited;
    auto uuid = old.isEmpty() ? QUuid::createUuid().toRfc4122() : old.mid(8, 16);
    memcpy(b.data() + 8, uuid.constData(), 16);
    TYPES_Put32(b.data() + 24, r.size());
    for (auto it = r.begin(); it != r.end(); ++it)
        b += it.value();
    require(b.size() <= MOD_MAX_RESOURCE, "Tile artwork exceeds the 8 MiB library limit");
    require(TYPES_Validate(b.constData(), b.size()),
            "Invalid type settings, references or mismatched animation variants");
    return b;
}
QImage image(const Project &p, unsigned id, unsigned variant, quint64 ms) {
    auto r = records(p).value(id);
    if (!r.isEmpty()) {
        unsigned side = 0, height = 0;
        auto rgba = TYPES_FrameImage(reinterpret_cast<const unsigned char *>(r.constData()),
                                     variant, ms, &side, &height);
        if (rgba)
            return QImage(rgba, side, height, side * 4, QImage::Format_RGBA8888).copy();
    }
    auto tiles = U5::readGraphics("TILES.16", p.data("TILES.16")).images;
    auto d = definition(p, id);
    auto art = tiles.value(d.kind ? 256 + d.base : d.base).convertToFormat(QImage::Format_RGBA8888);
    return art;
}
TypeCell cell(const QString &resource, const U5::MapPage &mp, int page, int x, int y) {
    TypeCell c = {};
    c.x = x;
    c.y = y;
    c.floor = mp.floor;
    if (resource.endsWith(".CBT")) {
        c.kind = resource == "BRIT.CBT" ? 1 : 2;
        c.map = page;
        c.floor = 0;
    } else if (resource == "BRIT.DAT" || resource == "UNDER.DAT") {
        c.map = 0;
        c.floor = resource == "UNDER.DAT" ? -1 : 0;
    } else if (auto id = MAPS_LocationResource(resource.toLatin1().constData())) {
        c.map = id;
    } else {
        int bank = resource == "TOWNE.DAT"      ? 0
                   : resource == "DWELLING.DAT" ? 8
                   : resource == "CASTLE.DAT"   ? 16
                                                : 24;
        c.map = bank + mp.settlement + 1;
    }
    return c;
}
TypeCell actorCell(const QString &resource, const U5::MapPage &page, int index, int slot) {
    TypeCell c = cell(resource, page, index, 0, 0);
    c.kind = resource.endsWith(".CBT") ? c.kind + 4 : c.map ? 3 : 4;
    if (c.kind == 3 || c.kind >= 5)
        c.floor = 0;
    c.x = slot;
    c.y = c.kind == 3 ? slot >> 8 : 0;
    return c;
}
unsigned actorType(const Project &p, const QString &resource, const U5::MapPage &page, int index,
                   int slot, unsigned fallback) {
    auto c = actorCell(resource, page, index, slot);
    unsigned type = placements(p).value(TYPES_CellKey(&c), c).type;
    return type ? type : fallback;
}
QMap<QString, QByteArray> assignActor(const Project &p, const QString &resource,
                                      const U5::MapPage &page, int index, int slot, unsigned type) {
    auto cells = placements(p);
    auto c = actorCell(resource, page, index, slot);
    if (type >= 512) {
        require(definition(p, type).kind != 0, "Choose an NPC or monster type");
        c.type = type;
        cells[TYPES_CellKey(&c)] = c;
    } else
        cells.remove(TYPES_CellKey(&c));
    if (cells.isEmpty() && p.resources.value("TYPEMAP.OVR").edited.isEmpty())
        return {};
    return {{"TYPEMAP.OVR", mapBytes(p, cells)}};
}
QMap<quint64, TypeCell> placements(const Project &p) {
    QMap<quint64, TypeCell> result;
    auto b = p.resources.value("TYPEMAP.OVR").edited;
    if (b.isEmpty())
        return result;
    auto defs = p.resources.value("TYPES.OVR").edited;
    require(TYPES_ValidateMap(b.constData(), b.size(), defs.constData(), defs.size()),
            "Invalid custom tile placements");
    for (unsigned i = 0; i < TYPES_U32(b.constData() + 24); i++) {
        TypeCell c;
        TYPES_CellDecode(reinterpret_cast<const unsigned char *>(b.constData() + 32 + i * 16), &c);
        result[TYPES_CellKey(&c)] = c;
    }
    return result;
}
QByteArray mapBytes(const Project &p, const QMap<quint64, TypeCell> &cells) {
    auto defs = p.resources.value("TYPES.OVR").edited;
    require(!defs.isEmpty(), "Tile library is missing");
    QByteArray b(32, 0);
    memcpy(b.data(), "ITYMAP01", 8);
    memcpy(b.data() + 8, defs.constData() + 8, 16);
    TYPES_Put32(b.data() + 24, cells.size());
    for (const auto &c : cells) {
        QByteArray r(16, 0);
        TYPES_CellEncode(reinterpret_cast<unsigned char *>(r.data()), &c);
        b += r;
    }
    require(TYPES_ValidateMap(b.constData(), b.size(), defs.constData(), defs.size()),
            "Invalid map tile reference");
    return b;
}
QByteArray expanded(const Project &p, const QString &resource, const U5::MapPage &mp, int page,
                    const QByteArray &base) {
    QByteArray b(base.size() * 4, 0);
    auto placed = placements(p);
    for (int i = 0; i < base.size(); i++) {
        auto c = cell(resource, mp, page, i % mp.side, i / mp.side);
        TYPES_Put32(b.data() + i * 4, placed.value(TYPES_CellKey(&c), c).type);
    }
    return b;
}
QMap<QString, QByteArray> paint(const Project &p, const QString &resource, const U5::MapPage &mp,
                                int page, const QByteArray &base, const QByteArray &expanded) {
    auto cells = placements(p);
    require(expanded.size() == base.size() * 4, "Invalid custom terrain grid");
    for (int i = 0; i < base.size(); i++) {
        auto c = cell(resource, mp, page, i % mp.side, i / mp.side);
        auto k = TYPES_CellKey(&c);
        unsigned id = TYPES_U32(expanded.constData() + i * 4);
        if (id >= 512) {
            c.type = id;
            cells[k] = c;
        } else
            cells.remove(k);
    }
    if (cells.isEmpty() && p.resources.value("TYPEMAP.OVR").edited.isEmpty())
        return {};
    return {{"TYPEMAP.OVR", mapBytes(p, cells)}};
}
void validate(const Project &p) {
    auto defs = p.resources.value("TYPES.OVR").edited,
         map = p.resources.value("TYPEMAP.OVR").edited;
    if (defs.isEmpty()) {
        require(map.isEmpty(), "Custom placements need their tile library");
        return;
    }
    require(TYPES_Validate(defs.constData(), defs.size()),
            "Invalid tile definitions or animation artwork");
    if (!map.isEmpty())
        require(TYPES_ValidateMap(map.constData(), map.size(), defs.constData(), defs.size()),
                "A placed custom tile is missing or invalid");
    for (const auto &c : placements(p)) {
        QString source;
        int page = -1;
        if (c.kind == 1 || c.kind == 2 || c.kind == 5 || c.kind == 6) {
            source = c.kind == 1 || c.kind == 5 ? "BRIT.CBT" : "DUNGEON.CBT";
            page = c.map;
        } else if (!c.map || c.kind == 4) {
            source = c.floor < 0 ? "UNDER.DAT" : "BRIT.DAT";
            page = 0;
        } else if (MAPS_NewLocation(c.map)) {
            source = QString("LOC%1.DAT").arg(c.map, 3, 10, QChar('0'));
            page = 0;
        } else
            source =
                QStringList{"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"}[(c.map - 1) / 8];
        require(p.resources.contains(source), "Custom tile references a missing map");
        auto pages = p.mapPages(source);
        if (page < 0)
            for (int i = 0; i < pages.size(); i++)
                if (pages[i].settlement == (MAPS_NewLocation(c.map) ? 0 : (c.map - 1) % 8) &&
                    pages[i].floor == c.floor)
                    page = i;
        require(page >= 0 && page < pages.size(),
                "Custom tile references a missing floor or encounter");
        if (c.kind <= 2)
            require(c.x < pages[page].side && c.y < pages[page].height(),
                    "Custom tile is outside its map");
        auto actor = definition(p, c.type);
        if (actor.kind && (TYPES_Width(&actor) > 1 || TYPES_Height(&actor) > 1)) {
            if (c.kind == 3) {
                auto npcResource = source.left(source.size() - 4) + ".NPC";
                Project view = p;
                MapNpcDocument npcs(&view, npcResource);
                for (int schedule = 0; schedule < 4; ++schedule) {
                    auto pos = npcs.location((MAPS_NewLocation(c.map) ? 0 : (c.map - 1) % 8),
                                             c.x + (c.y << 8), schedule);
                    if (pos.x >= pages[page].side || pos.y >= pages[page].height())
                        continue; // Preserve scheduled off-map absence.
                    int destPage = -1;
                    for (int i = 0; i < pages.size(); ++i)
                        if (pages[i].settlement ==
                                (MAPS_NewLocation(c.map) ? 0 : (c.map - 1) % 8) &&
                            pages[i].floor == pos.floor)
                            destPage = i;
                    require(destPage >= 0, "Creature schedule references a missing floor");
                    Workshop::validateActorPlacement(p, source, destPage, {pos.x, pos.y}, c.type,
                                                     c.x + (c.y << 8), schedule);
                }
            } else if (c.kind == 5 || c.kind == 6) {
                auto data = p.data(source);
                int offset = pages[page].offset;
                int x = U5::byte(data, offset + 6 * 32 + 11 + c.x),
                    y = U5::byte(data, offset + 7 * 32 + 11 + c.x);
                if (x < pages[page].side && y < pages[page].height() &&
                    Workshop::mapMarkerActive(p,source,page,24+c.x,x,y))
                    Workshop::validateActorPlacement(p, source, page, {x, y}, c.type, c.x);
            }
        }
    }
}
QMap<QString, QByteArray> moveScenery(const Project &p, const QString &resource, int page,
                                      QPoint from, QPoint to) {
    MapDocument doc(&p, resource);
    const auto mp = doc.pages.at(page);
    auto base = doc.terrain(page);
    auto ids = expanded(p, resource, mp, page, base);
    require(QRect(0, 0, mp.side, mp.height()).contains(from) &&
                QRect(0, 0, mp.side, mp.height()).contains(to),
            "Scenery move is outside the map");
    int source = from.y() * mp.side + from.x(), dest = to.y() * mp.side + to.x();
    unsigned type = TYPES_U32(ids.constData() + source * 4);
    if (type < 512 || from == to)
        return {};
    auto d = definition(p, type);
    unsigned destType = TYPES_U32(ids.constData() + dest * 4);
    unsigned destBase = U5::byte(base, dest);
    require((destType >= 512 && !(definition(p, destType).render & TYPES_SPRITE)) ||
                (!destType &&
                 (destBase == 5 || destBase == 0x40 || destBase == 0x44 || destBase == 0x45)),
            "Scenery destination is occupied");
    unsigned ground = d.ground == TYPES_INFER_GROUND ? (destType ? destType : destBase) : d.ground;
    base[source] = char(ground >= 512 ? definition(p, ground).base : ground);
    base[dest] = char(d.base);
    TYPES_Put32(ids.data() + source * 4, ground >= 512 ? ground : 0);
    TYPES_Put32(ids.data() + dest * 4, type);
    auto changes = doc.changes(page, base, ids), ext = paint(p, resource, mp, page, base, ids);
    for (auto it = ext.begin(); it != ext.end(); it++)
        changes[it.key()] = it.value();
    return changes;
}
class Canvas : public QWidget {
  public:
    QImage pixels, before;
    QRgb color = qRgb(255, 255, 255);
    int tool = 0;
    std::function<void()> changed, colorChanged;
    Canvas() {
        setMinimumSize(384, 384);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setObjectName("customTileCanvas");
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        for (int y = 0; y < height(); y += 12)
            for (int x = 0; x < width(); x += 12)
                p.fillRect(x, y, 12, 12,
                           (x / 12 + y / 12) % 2 ? QColor("#333e4e") : QColor("#151c27"));
        if (!pixels.isNull())
            p.drawImage(rect(), pixels);
    }
    void point(QMouseEvent *e) {
        if (pixels.isNull())
            return;
        QPoint q(e->position().x() * pixels.width() / width(),
                 e->position().y() * pixels.height() / height());
        if (!pixels.rect().contains(q))
            return;
        if (e->buttons() & Qt::RightButton || tool == 3) {
            color = pixels.pixel(q);
            if (colorChanged)
                colorChanged();
            return;
        }
        if (!(e->buttons() & Qt::LeftButton))
            return;
        if (tool == 2) {
            QRgb source = pixels.pixel(q);
            if (source == color)
                return;
            QVector<QPoint> todo{q};
            pixels.setPixel(q, color);
            while (!todo.isEmpty()) {
                auto t = todo.takeLast();
                for (auto n :
                     {t + QPoint(1, 0), t + QPoint(-1, 0), t + QPoint(0, 1), t + QPoint(0, -1)})
                    if (pixels.rect().contains(n) && pixels.pixel(n) == source) {
                        pixels.setPixel(n, color);
                        todo.append(n);
                    }
            }
        } else
            pixels.setPixel(q, tool == 1 ? 0 : color);
        update();
    }
    void mousePressEvent(QMouseEvent *e) override {
        before = pixels;
        point(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (tool != 2)
            point(e);
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        if (before != pixels && changed)
            changed();
    }
};
// Original actors use four adjacent tiles, animated by the engine rather than TYPES.OVR.
static QString actorArtworkResource(unsigned variant) {
    return QStringList{"TILES.16",     "TILE_AMI.PNG", "TILE_APP.PNG",
                       "TILE_GRY.PNG", "TILE_SHN.PNG", "TILES.16"}
        .value(variant);
}
static QVector<Frame> originalActorFrames(const Project &p, unsigned id) {
    QVector<Frame> result;
    const auto overrides = records(p);
    for (unsigned variant = 0; variant < 6; ++variant) {
        auto resource = actorArtworkResource(variant);
        auto graphics = U5::readGraphics(resource, p.data(resource));
        for (unsigned frame = 0; frame < 4; ++frame) {
            auto art = graphics.images.value(int(id + frame) - graphics.tileOffset)
                           .convertToFormat(QImage::Format_RGBA8888);
            require(!art.isNull(), "Original creature animation is missing artwork");
            auto override = overrides.value(id + frame);
            unsigned side = 0;
            auto rgba =
                override.isEmpty()
                    ? nullptr
                    : TYPES_Frame(reinterpret_cast<const unsigned char *>(override.constData()),
                                  variant, 0, &side);
            if (!override.isEmpty() && rgba)
                art = QImage(rgba, side, TYPES_FrameHeight(side), side * 4, QImage::Format_RGBA8888)
                          .copy();
            result.append({art, 150, variant});
        }
    }
    return result;
}
QWidget *editor(Project *p, Workshop::TextCommit commit, QMap<QString, int> *state) {
    const int actorKind = qBound(0, state->value("customTiles/kind", 0), 2);
    const QString noun = actorKind == 1 ? "NPC type" : actorKind == 2 ? "monster type" : "tile";
    const QString selectedIdKey =
        actorKind ? QString("customTiles/%1/id").arg(actorKind) : "customTiles/id";
    auto w = new QWidget;
    w->setObjectName("customTileEditor");
    auto layout = new QHBoxLayout(w);
    auto left = new QVBoxLayout;
    auto search = new QLineEdit;
    search->setPlaceholderText("Find a " + noun + "…");
    left->addWidget(search);
    auto filter = new QComboBox;
    filter->setObjectName("customTileFilter");
    filter->addItems({"All tiles", "World", "Locations"});
    filter->setVisible(!actorKind);
    left->addWidget(filter);
    auto list = new QListWidget;
    list->setObjectName("customTileList");
    list->setIconSize({32, 32});
    left->addWidget(list, 1);
    auto add = new QPushButton("New " + noun + "…");
    add->setObjectName("newCustomTile");
    left->addWidget(add);
    auto copy = new QPushButton("Copy " + noun + "…");
    left->addWidget(copy);
    auto remove = new QPushButton("Delete / Reset");
    left->addWidget(remove);
    add->setToolTip("Create an independent tile without replacing an Ultima 5 tile.");
    copy->setToolTip("Create a new tile using the selected tile’s artwork and settings.");
    remove->setToolTip("Delete a new tile or restore a modified original tile. Used tiles must be "
                       "replaced first.");
    layout->addLayout(left);
    auto middle = new QVBoxLayout;
    auto variants = new QComboBox;
    variants->setObjectName("customTileVariant");
    variants->addItems({"DOS", "Amiga", "Apple II", "Grayscale", "Sharp", "Custom"});
    variants->setCurrentIndex(qBound(0, state->value("customTiles/variant", 0), 5));
    variants->setToolTip("Choose this tile’s artwork tileset. Empty slots use DOS artwork.");
    auto tilesetRow = new QHBoxLayout;
    tilesetRow->addWidget(new QLabel("Tileset:"));
    tilesetRow->addWidget(variants, 1);
    middle->addLayout(tilesetRow);
    auto context = new QComboBox;
    context->setObjectName("actorArtworkContext");
    context->addItems({"Locations & combat", "World maps"});
    context->setToolTip("Edit the larger appearance used inside locations and combat, or the "
                        "single-tile appearance used on Britannia and Underworld. Both have "
                        "independent animation frames and tileset artwork.");
    context->setCurrentIndex(actorKind ? qBound(0, state->value("customTiles/artworkContext", 0), 1)
                                       : 0);
    context->setVisible(actorKind != 0);
    middle->addWidget(context);
    auto selectedVariant = [=] {
        return variants->currentIndex() + (actorKind && context->currentIndex() ? 6 : 0);
    };
    auto canvas = new Canvas;
    auto toolRow = new QHBoxLayout;
    auto tools = new QButtonGroup(w);
    tools->setExclusive(true);
    QStringList toolNames{"Pencil", "Eraser", "Fill", "Eyedropper"};
    QStringList toolTips{"Pencil: paint individual pixels.", "Eraser: make pixels transparent.",
                         "Fill: replace a connected area of the same color.",
                         "Eyedropper: pick a color from the tile."};
    for (int i : {0, 2, 3}) {
        auto button = new QToolButton;
        button->setObjectName(QString("customTileTool%1").arg(i));
        button->setCheckable(true);
        button->setToolTip(toolTips[i]);
        button->setAccessibleName(toolNames[i]);
        button->setIcon(MapCanvas::toolIcon(i == 0   ? MapCanvas::Pencil
                                            : i == 3 ? MapCanvas::Eyedropper
                                                     : MapCanvas::Fill));
        button->setIconSize({24, 24});
        tools->addButton(button, i);
        toolRow->addWidget(button);
    }
    canvas->tool = qBound(0, state->value("customTiles/tool", 0), 3);
    if (canvas->tool == 1)
        canvas->tool = 0;
    tools->button(canvas->tool)->setChecked(true);
    toolRow->addStretch();
    middle->addLayout(toolRow);
    middle->addWidget(canvas, 0, Qt::AlignTop);
    canvas->color = QRgb(state->value("customTiles/color", int(qRgb(255, 255, 255))));
    auto palette = new Workshop::ColorPalette(8);
    palette->setObjectName("customTilePalette");
    palette->changed = [=](QRgb rgba) {
        canvas->color = rgba;
        canvas->colorChanged();
    };
    auto allowedColors = std::make_shared<QVector<QRgb>>(U5::palette());
    canvas->colorChanged = [=] {
        canvas->color = Workshop::paletteColor(canvas->color, *allowedColors);
        (*state)["customTiles/color"] = int(canvas->color);
        if (variants->currentIndex() == 5)
            palette->addCustomColor(canvas->color);
        palette->select(canvas->color);
    };
    auto reloadPalette = [=] {
        const QStringList resources{"TILES.16",
                                    "TILE_AMI.PNG",
                                    "TILE_APP.PNG",
                                    "TILE_GRY.PNG",
                                    actorKind ? "TILE_SHN.PNG" : "TILE_SHW.PNG",
                                    "TILES.16"};
        const bool custom = variants->currentIndex() == 5;
        palette->setEditable(custom);
        if (custom) {
            allowedColors->clear();
            canvas->colorChanged();
            return;
        }
        auto resource = resources[variants->currentIndex()];
        auto colors = Workshop::artworkColors(
            U5::readGraphics(resource, p->resources.value(resource).original));
        *allowedColors = colors;
        palette->setColors(colors, true);
        canvas->colorChanged();
    };
    reloadPalette();
    middle->addWidget(palette);
    auto artworkMenuButton = new QToolButton;
    artworkMenuButton->setText("⋯");
    artworkMenuButton->setToolTip("Artwork options");
    artworkMenuButton->setPopupMode(QToolButton::InstantPopup);
    auto artworkMenu = new QMenu(artworkMenuButton);
    auto resetArtwork = artworkMenu->addAction("Remove artwork for this tileset");
    resetArtwork->setObjectName("customTileUseDos");
    resetArtwork->setToolTip(
        "Remove this artwork; the tile will use its DOS artwork automatically.");
    artworkMenuButton->setMenu(artworkMenu);
    tilesetRow->addWidget(artworkMenuButton);
    auto frameList = new QListWidget;
    frameList->setObjectName("customTileFrames");
    frameList->setFlow(QListView::LeftToRight);
    frameList->setIconSize({32, 32});
    frameList->setMaximumHeight(72);
    middle->addWidget(frameList);
    auto row = new QHBoxLayout;
    auto frameAdd = new QPushButton("+ Frame"), frameDelete = new QPushButton("− Frame"),
         frameLeft = new QPushButton("←"), frameRight = new QPushButton("→"),
         play = new QPushButton("Play");
    for (auto b : {frameAdd, frameDelete, frameLeft, frameRight, play})
        row->addWidget(b);
    frameAdd->setObjectName("customTileAddFrame");
    frameAdd->setToolTip("Add a frame to every supplied artwork variant.");
    frameDelete->setToolTip("Delete the selected frame from every artwork variant.");
    frameLeft->setToolTip("Move this frame earlier.");
    frameRight->setToolTip("Move this frame later.");
    play->setToolTip("Preview this tile’s animation.");
    auto frameCopy = new QToolButton;
    auto framePaste = new QToolButton;
    frameCopy->setObjectName("customTileCopyFrame");
    framePaste->setObjectName("customTilePasteFrame");
    frameCopy->setIcon(
        QIcon::fromTheme("edit-copy", w->style()->standardIcon(QStyle::SP_FileIcon)));
    framePaste->setIcon(
        QIcon::fromTheme("edit-paste", w->style()->standardIcon(QStyle::SP_DialogOpenButton)));
    frameCopy->setToolTip("Copy frame: copy the selected frame’s artwork, including transparency.");
    framePaste->setToolTip(
        "Paste frame: replace the selected frame’s artwork. "
        "The image must match this frame’s pixel dimensions; colors are converted "
        "to the selected tileset’s palette.");
    row->insertWidget(4, frameCopy);
    row->insertWidget(5, framePaste);
    middle->addLayout(row);
    auto import = new QPushButton("Import PNG…");
    import->setObjectName("customTileImport");
    auto exportPng = new QToolButton;
    exportPng->setText("Export PNG…");
    exportPng->setObjectName("customTileExport");
    exportPng->setToolButtonStyle(Qt::ToolButtonTextOnly);
    exportPng->setPopupMode(QToolButton::InstantPopup);
    exportPng->setToolTip("Export the selected frame or all frames for the current tileset and "
                          "artwork view, at native pixel size with transparency preserved.");
    auto exportMenu = new QMenu(exportPng);
    auto exportFrame = exportMenu->addAction("Selected frame…");
    auto exportSheet = exportMenu->addAction("Animation sheet…");
    exportFrame->setToolTip("Save the selected frame as a PNG at its original pixel dimensions.");
    exportSheet->setToolTip(
        "Save all displayed frames in a horizontal PNG sheet, in animation order.");
    exportPng->setMenu(exportMenu);
    auto fileRow = new QHBoxLayout;
    fileRow->addWidget(import);
    fileRow->addWidget(exportPng);
    middle->addLayout(fileRow);
    middle->addStretch();
    layout->addLayout(middle, 1);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto props = new QWidget;
    auto name = new QLineEdit, description = new QLineEdit;
    name->setObjectName("customTileName");
    name->setToolTip("Name shown in the tile picker and map inspector.");
    description->setToolTip("Text shown by Impera’s Look command.");
    description->setObjectName("customTileDescription");
    auto world = new QCheckBox("World"), location = new QCheckBox("Locations");
    world->setToolTip("Include this tile in the map picker’s World filter.");
    location->setToolTip(
        "Include this tile in the Locations filter. Both filters can include the same tile.");
    auto sprite = new QCheckBox("Transparent sprite"), masonry = new QCheckBox("Wall lighting");
    sprite->setToolTip(
        "With Impera’s Transparent Sprites option enabled, show ground through PNG alpha "
        "and black pixels, using the normal sprite outline.");
    masonry->setToolTip("Keep structural edges crisp when darkness fades around walls.");
    auto ground = new QComboBox;
    ground->setObjectName("customTileGround");
    ground->setToolTip(
        "Floor shown beneath transparent pixels; Nearby ground chooses surrounding terrain.");
    auto sight = new QComboBox;
    sight->setToolTip("Whether the tile permits sight, blocks it, or behaves like a window.");
    sight->addItems({"Clear", "Blocks sight", "Window"});
    auto projectile = new QCheckBox("Projectiles pass through");
    projectile->setToolTip("Allow arrows and other projectiles to cross this tile.");
    auto speed = new QComboBox;
    speed->setToolTip("How much this terrain slows travel.");
    speed->addItems({"Normal", "Slow", "Very slow"});
    QVector<QCheckBox *> movement;
    for (auto label :
         {"Walking", "Sea creatures", "Flying", "Horse", "Phasing", "Skiff", "Frigate",
          "Swamp creatures", "Grass creatures", "Deep-water creatures", "Sand creatures"}) {
        auto box = new QCheckBox(label);
        movement.append(box);
    }
    QVector<QCheckBox *> skiffDirections;
    auto boatGroup = new QGroupBox("Skiff headings");
    auto boatLayout = new QHBoxLayout(boatGroup);
    for (auto label : {"North", "East", "South", "West"}) {
        auto box = new QCheckBox(label);
        boatLayout->addWidget(box);
        skiffDirections.append(box);
    }
    boatGroup->setToolTip("Allowed skiff headings when skiffs can use this terrain.");
    QObject::connect(movement[5], &QCheckBox::toggled, w, [=](bool enabled) {
        if (enabled && std::none_of(skiffDirections.begin(), skiffDirections.end(),
                                    [](QCheckBox *b) { return b->isChecked(); }))
            for (auto b : skiffDirections)
                b->setChecked(true);
    });
    auto humanoidSeat = new QCheckBox("Humanoid seating only");
    auto combatMap = new QComboBox;
    combatMap->addItem("From terrain", 0);
    for (int i = 0; i < 16; i++)
        combatMap->addItem(QString("Combat map %1").arg(i + 1), i + 1);
    auto poison = new QCheckBox("Poisonous"), burn = new QCheckBox("Damaging");
    QVector<QCheckBox *> effectModes;
    auto effectGroup = new QGroupBox("Who is affected");
    auto effectLayout = new QVBoxLayout(effectGroup);
    for (auto label :
         {"Walking", "Sea creatures", "Flying", "Horse", "Phasing", "Skiff", "Frigate",
          "Swamp creatures", "Grass creatures", "Deep-water creatures", "Sand creatures"}) {
        auto box = new QCheckBox(label);
        effectModes.append(box);
        effectLayout->addWidget(box);
    }
    auto damageMin = new QSpinBox, damageMax = new QSpinBox;
    damageMin->setObjectName("customTileDamageMin");
    damageMax->setObjectName("customTileDamageMax");
    damageMin->setToolTip("Lowest damage dealt when this terrain causes damage.");
    damageMax->setToolTip("Highest damage dealt when this terrain causes damage.");
    damageMin->setRange(0, 255);
    damageMax->setRange(0, 255);
    auto interaction = new QComboBox;
    interaction->setObjectName("customTileInteraction");
    for (auto pair :
         QList<QPair<QString, int>>{{"Ground", 5},           {"Decoration", 0x43},
                                    {"Wall", 0x4f},          {"Water", 2},
                                    {"Door — closed", 0xb8}, {"Door — magically locked", 0x97},
                                    {"Door — locked", 0xb9}, {"Container", 0xa6},
                                    {"Sign", 0xf8},          {"Stairs", 0xc4},
                                    {"Ladder up", 0xc8},     {"Ladder down", 0xc9},
                                    {"Mirror", 0x9d},        {"Well", 0xa1},
                                    {"Fountain", 0xd8},      {"Clock", 0xfa},
                                    {"Cannon", 0xb4},        {"Tree", 0x2e},
                                    {"Crops", 0x2d},         {"Moongate", 0xdc}})
        interaction->addItem(pair.first, pair.second);
    interaction->setToolTip(
        "Choose the game interaction independently of the tile’s artwork and movement settings.");
    QVector<QComboBox *> results;
    for (auto label :
         {"Open appearance", "Closed appearance", "Locked appearance", "Broken appearance"}) {
        auto box = new QComboBox;
        results.append(box);
    }
    auto inspector = new QVBoxLayout(props);
    inspector->setAlignment(Qt::AlignTop);
    auto details = new QFormLayout;
    details->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto actorSize = new QComboBox;
    actorSize->setObjectName("actorFootprint");
    actorSize->addItems({"1 × 1", "1 × 2", "2 × 1", "2 × 2"});
    actorSize->setToolTip(
        "Width × height occupied in locations and combat. Britannia and Underworld always use a "
        "single tile. Resizing keeps existing pixels at the top left; shrinking crops them. Copy "
        "an original type to make a larger creature.");
    actorSize->setVisible(actorKind != 0);
    if (actorKind)
        details->addRow("Size", actorSize);
    details->addRow("Name", name);
    details->addRow("Look description", description);
    auto filters = new QHBoxLayout;
    filters->addWidget(world);
    filters->addWidget(location);
    filters->addStretch();
    details->addRow("Tile picker", filters);
    inspector->addLayout(details);
    auto interactions =
        new PropertySection(actorKind ? "Behavior & encounters" : "Interaction", true, props);
    auto travel = new PropertySection("Movement & visibility", true, props);
    auto appearance = new PropertySection("Appearance", false, props);
    auto hazards = new PropertySection("Hazards", false, props);
    auto advanced = new PropertySection("Advanced", false, props);
    for (auto section : {interactions, travel, appearance, hazards, advanced})
        inspector->addWidget(section);
    interactions->form->addRow("Behavior", interaction);
    for (int i = 0; i < results.size(); i++) {
        results[i]->setObjectName(QString("tileStateAppearance%1").arg(i));
        interactions->form->addRow(QStringList{"Open appearance", "Closed appearance",
                                               "Locked appearance", "Broken appearance"}[i],
                                   results[i]);
    }
    auto commonMovement = new QHBoxLayout;
    commonMovement->addWidget(movement[0]);
    commonMovement->addWidget(movement[3]);
    travel->form->addRow("Can enter", commonMovement);
    travel->form->addRow("Travel speed", speed);
    travel->form->addRow("Sight", sight);
    travel->form->addRow(projectile);
    appearance->form->addRow(sprite);
    appearance->form->addRow("Ground underneath", ground);
    appearance->form->addRow(masonry);
    hazards->form->addRow(poison);
    hazards->form->addRow(burn);
    auto damageRow = new QHBoxLayout;
    damageRow->addWidget(damageMin);
    damageRow->addWidget(new QLabel("to"));
    damageRow->addWidget(damageMax);
    hazards->form->addRow("Damage", damageRow);
    hazards->form->addRow(effectGroup);
    auto specialized = new QWidget;
    auto movementGrid = new QGridLayout(specialized);
    movementGrid->setContentsMargins(0, 0, 0, 0);
    int movementRow = 0;
    for (int i = 0; i < movement.size(); i++)
        if (i != 0 && i != 3) {
            movementGrid->addWidget(movement[i], movementRow / 2, movementRow % 2);
            movementRow++;
        }
    advanced->form->addRow("Other movement", specialized);
    advanced->form->addRow(boatGroup);
    advanced->form->addRow(humanoidSeat);
    advanced->form->addRow("Combat background", combatMap);
    auto animation = new QComboBox(props);
    animation->setObjectName("customTileAnimation");
    animation->addItems({"Like base type", "Continuous loop"});
    animation->setToolTip("Use the base creature’s four-pose animation, including its pauses and "
                          "random timing, or repeat every frame continuously. Animations with "
                          "more than four poses use Continuous loop.");
    if (actorKind)
        appearance->form->addRow("Animation", animation);
    else
        animation->hide();
    QVector<QSpinBox *> actorStats;
    auto combat = new PropertySection("Combat", true, props);
    combat->setVisible(actorKind != 0);
    inspector->insertWidget(2, combat);
    const QStringList statNames{"Strength", "Dexterity",  "Intelligence",   "Defense",
                                "Attack",   "Hit points", "Encounter size", "Treasure"};
    for (int i = 0; i < 8; i++) {
        auto spin = new QSpinBox;
        spin->setObjectName(QString("actorStat%1").arg(i));
        spin->setRange(i == 5 || i == 6 ? 1 : 0, i == 1 ? 30 : i == 6 ? 16 : 255);
        spin->setToolTip(i == 7
                             ? "Treasure rating used by the original loot rules; not a gold amount."
                         : i == 6 ? "Maximum group size when this type starts an encounter."
                                  : statNames[i] + " for this creature type.");
        combat->form->addRow(statNames[i], spin);
        actorStats.append(spin);
    }
    auto actorMovement = new QComboBox(props);
    actorMovement->setObjectName("actorMovement");
    actorMovement->addItems({"Walking", "Swimming", "Flying", "Horse", "Phasing", "Skiff",
                             "Frigate", "Swamp only", "Grass only", "Deep water only",
                             "Sand only"});
    auto attackRange = new QSpinBox;
    attackRange->setObjectName("actorAttackRange");
    attackRange->setRange(0, 11);
    auto projectileType = new QComboBox;
    projectileType->setObjectName("actorProjectile");
    for (int effect = 0; effect < 8; effect++) {
        QStringList examples;
        for (int shape = 64; shape < 256; shape += 4) {
            TypeDef d;
            TYPES_Vanilla(1, shape, &d);
            if (d.projectileAppearance == effect && d.range > 1 && examples.size() < 2)
                examples << Workshop::actorName(shape);
        }
        projectileType->addItem(examples.isEmpty() ? QString("Projectile style %1").arg(effect + 1)
                                                   : "Like " + examples.join(" / "));
    }
    combat->form->addRow("Attack range", attackRange);
    combat->form->addRow("Ranged attack", projectileType);
    const QList<QPair<QString, unsigned>> abilities{{"No corpse", 1},
                                                    {"Steals food", 2},
                                                    {"Poisons on hit", 516},
                                                    {"Immune to damage", 8},
                                                    {"Divides when hit", 16},
                                                    {"Undead", 32},
                                                    {"Uses strength to hit", 128},
                                                    {"Possesses party members", 64},
                                                    {"Summons creatures", 1024},
                                                    {"Disappears and reappears", 2048},
                                                    {"Vanishes on death", 4096},
                                                    {"Teleports", 8192},
                                                    {"Magical ranged attack", 32768}};
    QVector<QCheckBox *> actorAbilities;
    for (auto ability : abilities) {
        auto box = new QCheckBox(ability.first);
        box->setToolTip(ability.first + ".");
        actorAbilities.append(box);
        advanced->form->addRow(box);
        box->setVisible(actorKind != 0);
    }
    if (actorKind) {
        description->setToolTip("Plural name used when introducing a group in combat.");
        qobject_cast<QLabel *>(details->labelForField(description))->setText("Plural name");
        world->hide();
        location->hide();
        if (auto label = details->labelForField(filters))
            label->hide();
        travel->hide();
        hazards->hide();
        appearance->form->setRowVisible(ground, false);
        masonry->hide();
        for (auto box : movement)
            box->hide();
        boatGroup->hide();
        humanoidSeat->hide();
        advanced->form->setRowVisible(specialized, false);
        advanced->form->setRowVisible(combatMap, false);
        combat->form->addRow("Movement", actorMovement);
        interaction->clear();
        for (int shape = 64; shape < 256; shape += 4)
            interaction->addItem(Workshop::actorName(shape), shape);
        interaction->setToolTip("Original creature behavior used for special native actions; stats "
                                "and artwork remain independent.");
        const QStringList targets{"On defeat, transform into", "Summons", "Divides into",
                                  "Encounter companion"};
        for (int i = 0; i < 4; i++)
            qobject_cast<QLabel *>(interactions->form->labelForField(results[i]))
                ->setText(targets[i]);
    } else {
        actorMovement->hide();
        attackRange->hide();
        projectileType->hide();
    }
    auto error = new QLabel;
    error->setObjectName("customTileSettingsError");
    error->setWordWrap(true);
    error->setStyleSheet("color:#ff9898;");
    error->hide();
    inspector->addWidget(error);
    for (auto box : movement)
        box->setToolTip("Allow " + box->text().toLower() + " to enter this tile.");
    for (auto box : effectModes)
        box->setToolTip("Apply this tile’s hazards to " + box->text().toLower() + ".");
    humanoidSeat->setToolTip("Restrict seating on this tile to humanoid characters.");
    poison->setToolTip("This terrain can poison the selected movement types.");
    burn->setToolTip("This terrain can damage the selected movement types.");
    combatMap->setToolTip("Battlefield used when combat begins on this terrain.");
    for (auto box : results)
        box->setToolTip(
            "Choose the tile’s appearance after this action, or keep the behavior’s default.");
    // Keep guidance at the control where it is needed, rather than in permanent notes.
    search->setToolTip("Filter this list by name to find an existing type to edit or copy.");
    filter->setToolTip("Show all tiles, or only tiles included in the World or Locations picker. A "
                       "tile can belong to both.");
    list->setToolTip("Select a type to edit its artwork and shared settings. Changes affect every "
                     "map placement using this type.");
    add->setToolTip("Create a new " + noun +
                    " with its own artwork and settings, without replacing an existing type. Place "
                    "it using Add on a map.");
    copy->setToolTip("Create an independent " + noun +
                     " from the selected type’s artwork and settings. Editing the copy leaves the "
                     "source unchanged.");
    variants->setToolTip("Choose which tileset’s artwork to edit: DOS, Amiga and Grayscale use 16 "
                         "× 16 pixels; Apple II uses 14 × 16; Sharp uses 32 × 32. An empty slot "
                         "displays DOS artwork until you paint or import artwork for it.");
    artworkMenuButton->setToolTip("Artwork options for the selected tileset, including removing an "
                                  "alternative artwork slot so it falls back to DOS.");
    canvas->setToolTip("Edit the selected animation frame at its native pixel size. Left-drag uses "
                       "the selected tool; right-click picks a drawing color. Changes affect only "
                       "the selected tileset’s artwork.");
    palette->setToolTip("Choose a drawing color from the selected tileset’s palette. The crossed "
                        "swatch paints transparent pixels; black is a visible color.");
    frameList->setToolTip("Select the animation frame to draw. Frames play from "
                          "left to right, each for its own duration.");
    frameAdd->setToolTip("Append an animation frame to all supplied tilesets. Select DOS to change "
                         "the frame count; all tilesets must have the same number of frames.");
    frameDelete->setToolTip("Remove this frame from all supplied tilesets. Select DOS to change "
                            "the frame count. At least one frame must remain.");
    frameLeft->setToolTip(
        "Move the selected frame one position earlier in this tileset’s animation.");
    frameRight->setToolTip(
        "Move the selected frame one position later in this tileset’s animation.");
    play->setToolTip(
        "Preview the selected artwork with its in-game animation pattern, including pauses.");
    for (auto button : {frameAdd, frameDelete, frameLeft, frameRight})
        button->setToolTip(button->toolTip() + " Original creatures have four fixed frame slots; "
                                               "copy the type to create a custom animation.");
    import->setToolTip(
        "Import one frame to replace the selected frame, or a grid to replace the animation, read "
        "left to right then top to bottom. Frame sizes must match this tileset. Colors are "
        "converted to its palette; PNG transparency is preserved.");
    name->setToolTip(actorKind
                         ? "Name of this NPC or monster type in pickers. Individual NPC names and "
                           "conversations are edited on the map, separately from this shared type."
                         : "Name shown in Workshop’s tile picker and map inspector. The Look "
                           "description below controls what players read in-game.");
    world->setToolTip(
        "Include this new tile in the World picker filter. This does not restrict where it can be "
        "painted. Original tiles are classified automatically from the maps.");
    location->setToolTip("Include this new tile in the Locations picker filter. Both filters can "
                         "be enabled. Original tiles are classified automatically from the maps.");
    speed->setToolTip("How much this terrain slows travel when crossed. This does not change "
                      "animation speed or decide who can enter.");
    sprite->setToolTip("With Impera’s Transparent Sprites option enabled, blend PNG alpha over "
                       "the ground and treat black as transparent, with the normal sprite outline. "
                       "Choose the ground underneath below.");
    ground->setToolTip("Choose the ground drawn behind transparent pixels. Nearby ground infers it "
                       "from surrounding terrain rather than using a fixed floor.");
    masonry->setToolTip(
        "Use Impera’s wall lighting treatment to keep architectural edges crisp where darkness "
        "fades around walls. This does not make the tile block movement or sight.");
    poison->setToolTip("Allow this terrain to inflict poison. Choose which movement types are "
                       "affected below; movement permissions are configured separately.");
    burn->setToolTip("Allow this terrain to inflict immediate damage. Set the damage range and "
                     "affected movement types below. Poison is a separate effect.");
    effectGroup->setToolTip("Choose which travelers can suffer this terrain’s poison or damage. A "
                            "checked type can still be blocked by movement permissions.");
    humanoidSeat->setToolTip("Apply the original humanoid-only seating restriction. This does not "
                             "create an NPC or assign a sitting schedule.");
    combatMap->setToolTip(
        "Choose the battlefield layout used when combat starts on this terrain. From terrain keeps "
        "the normal game choice; special combat situations may override it.");
    actorMovement->setToolTip(
        "Choose how this creature travels. Terrain permissions for that movement type determine "
        "where it can go; specialized profiles restrict it to particular terrain.");
    attackRange->setToolTip(
        "Maximum attack distance in map tiles. Values above one allow ranged attacks; projectile "
        "appearance and magical behavior are separate settings.");
    projectileType->setToolTip(
        "Choose the visual style of ranged attacks, using original creatures as examples. This "
        "does not set attack damage, distance or magical effects.");
    const QStringList statHelp{"Strength used by the original combat rules. With Uses strength to "
                               "hit enabled, it also replaces Dexterity for hit checks.",
                               "Dexterity affects combat turn timing and ordinary hit checks. "
                               "Higher values make this creature more agile.",
                               "Intelligence used by the original magic and creature behavior "
                               "rules. This is not a list of spells to cast.",
                               "Physical defense used when resolving incoming attacks. Higher "
                               "values make physical attacks less effective.",
                               "Attack rating used to determine the damage this creature deals. "
                               "Hit chance is controlled separately.",
                               "Maximum health for this creature type. This also contributes to "
                               "the experience awarded for defeating it.",
                               "Maximum group size when this type starts an encounter, subject to "
                               "available combat positions.",
                               "Treasure rating used by the original loot rules after combat. This "
                               "is not a fixed quantity of gold."};
    for (int i = 0; i < actorStats.size(); ++i)
        actorStats[i]->setToolTip(statHelp[i]);
    const QStringList abilityHelp{
        "Do not leave an ordinary corpse after this creature dies.",
        "Allow the original food-stealing behavior against the party.",
        "Allow this creature’s hits to poison their target, using the original poison rules.",
        "Use the original immunity to ordinary damage. Special scripted effects can still have "
        "their own rules.",
        "Allow this creature to split after surviving damage. Choose the offspring type in Divides "
        "into.",
        "Treat this creature as undead for undead-specific spells and combat rules.",
        "Use Strength instead of Dexterity for this creature’s attack hit checks.",
        "Allow the original possession behavior against party members.",
        "Allow this creature to summon allies. Choose their type in Summons; an empty choice uses "
        "the original daemon default.",
        "Allow the original combat behavior that makes the creature disappear and reappear.",
        "Use the original vanishing death behavior instead of an ordinary death result.",
        "Allow the original teleporting combat behavior.",
        "Treat ranged attacks as magical, including suppression by magic negation. Projectile "
        "appearance is chosen separately."};
    for (int i = 0; i < actorAbilities.size(); ++i)
        actorAbilities[i]->setToolTip(abilityHelp[i]);
    const QStringList transitionHelp =
        actorKind
            ? QStringList{"Creature type produced when this one is defeated. No transformation "
                          "keeps its ordinary death behavior.",
                          "Creature type produced by Summons creatures. The ability must also be "
                          "enabled; the default is a daemon.",
                          "Creature type produced by Divides when hit. The ability must also be "
                          "enabled; the default is another of this type.",
                          "Additional creature type that can accompany this type in an encounter. "
                          "The default keeps the original companion rules."}
            : QStringList{
                  "Type this tile becomes when opened. The target’s behavior and movement settings "
                  "apply too, not just its artwork. Default uses the original behavior.",
                  "Type this tile becomes when closed, including the target’s behavior and "
                  "movement settings. Default uses the original behavior.",
                  "Type this tile becomes when locked, including the target’s behavior and "
                  "movement settings. This does not set a particular container’s lock.",
                  "Type this tile becomes when broken, including the target’s behavior and "
                  "movement settings. Default uses the original behavior."};
    for (int i = 0; i < results.size(); ++i)
        results[i]->setToolTip(transitionHelp[i]);
    for (auto box : skiffDirections)
        box->setToolTip("Allow a skiff facing " + box->text().toLower() +
                        " to enter this terrain. Skiff movement must also be allowed.");
    const QStringList movementHelp{"people traveling on foot",
                                   "swimming sea creatures",
                                   "flying creatures",
                                   "riders on horseback",
                                   "creatures that pass through obstacles",
                                   "small boats (skiffs)",
                                   "large ships (frigates)",
                                   "swamp-dwelling creatures",
                                   "grass-dwelling creatures",
                                   "deep-water creatures",
                                   "sand-dwelling creatures"};
    for (int i = 0; i < movement.size(); ++i) {
        movement[i]->setToolTip(
            "Allow " + movementHelp[i] +
            " to enter this terrain. This does not decide whether they suffer its hazards.");
        effectModes[i]->setToolTip("Apply enabled poison and damage effects to " + movementHelp[i] +
                                   ". This does not grant permission to enter the terrain.");
        actorMovement->setItemData(i, "Use terrain permissions for " + movementHelp[i] + ".",
                                   Qt::ToolTipRole);
    }
    sight->setItemData(0, "Let the player see through this tile. Movement can still be blocked.",
                       Qt::ToolTipRole);
    sight->setItemData(1, "Hide terrain behind this tile using the game’s line-of-sight rules.",
                       Qt::ToolTipRole);
    sight->setItemData(
        2, "Use Ultima 5’s distance-dependent window visibility rules rather than a solid wall.",
        Qt::ToolTipRole);
    const QStringList interactionHelp{
        "Ordinary terrain without a special interaction.",
        "Scenery without a special interaction.",
        "Use the original wall interaction rules. Set movement and sight separately.",
        "Use the original water interaction rules. Set swimming and boat permissions separately.",
        "Use closed-door interactions, including opening and closing.",
        "Use magically locked door interactions.",
        "Use locked-door interactions.",
        "Use container interactions. Set individual contents, locks and traps by selecting the "
        "container on its map.",
        "Use sign interactions. Edit the sign’s text by selecting it on its map.",
        "Use the original stairs interaction.",
        "Use a ladder leading upward.",
        "Use a ladder leading downward.",
        "Use the original mirror interaction.",
        "Use the original well interaction.",
        "Use the original fountain interaction.",
        "Use the original clock interaction.",
        "Use the original cannon interaction.",
        "Use the original tree interaction.",
        "Use the original crops interaction.",
        "Use the original moongate interaction; this does not create a new travel destination."};
    for (int i = 0; i < interaction->count(); ++i)
        interaction->setItemData(
            i,
            actorKind ? "Use the special native behavior of " + interaction->itemText(i) +
                            ". Artwork, statistics and enabled abilities are set separately."
                      : interactionHelp.value(i),
            Qt::ToolTipRole);
    for (auto combo : {filter, variants, speed, combatMap, projectileType})
        for (int i = 0; i < combo->count(); ++i)
            combo->setItemData(i, combo->itemText(i) + ": " + combo->toolTip(), Qt::ToolTipRole);
    if (auto label = details->labelForField(filters))
        label->setToolTip("Control where this tile appears in the picker. World and Locations may "
                          "overlap; neither limits map placement.");
    if (auto label = travel->form->labelForField(commonMovement))
        label->setToolTip("Choose whether people on foot and riders can enter. Other movement "
                          "types are under Advanced.");
    if (auto label = hazards->form->labelForField(damageRow))
        label->setToolTip("Minimum and maximum damage for a damaging terrain event. These values "
                          "are used only when Damaging is enabled.");
    for (auto label : w->findChildren<QLabel *>())
        if (label->text() == "Tileset:")
            label->setToolTip(variants->toolTip());
    // Labels should offer the same explanation as their fields.
    for (auto form : props->findChildren<QFormLayout *>())
        for (int i = 0; i < form->rowCount(); ++i) {
            auto field = form->itemAt(i, QFormLayout::FieldRole);
            auto label = form->itemAt(i, QFormLayout::LabelRole);
            if (field && field->widget() && label && label->widget())
                label->widget()->setToolTip(field->widget()->toolTip());
        }
    scroll->setWidget(props);
    scroll->setMinimumWidth(340);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(scroll);
    struct Draft {
        unsigned id = 512;
        TypeDef def = {};
        QVector<Frame> frames;
        bool loading = false;
        bool originalAnimation = false;
        unsigned char previewTile = 0, previewState = 0;
        qint64 previewTick = 0;
    };
    QSet<unsigned> worldTiles, locationTiles;
    for (const auto &resource : p->resources.keys())
        if (MapDocument::supported(resource)) {
            try {
                MapDocument doc(p, resource);
                for (int page = 0; page < doc.pages.size(); page++)
                    for (unsigned char id : doc.terrain(page))
                        (resource == "BRIT.DAT" || resource == "UNDER.DAT" ? worldTiles
                                                                           : locationTiles)
                            .insert(id);
            } catch (const std::exception &) {
            }
        }
    auto filterList = [=] {
        for (int i = 0; i < list->count(); i++) {
            auto item = list->item(i);
            unsigned id = item->data(Qt::UserRole).toUInt();
            bool group = filter->currentIndex() == 0;
            if (id < 256)
                group |= (filter->currentIndex() == 1 ? worldTiles : locationTiles).contains(id);
            else
                group |= definition(*p, id).filters & (filter->currentIndex() == 1 ? 1 : 2);
            item->setHidden(!group || !item->text().contains(search->text(), Qt::CaseInsensitive));
        }
    };
    QObject::connect(filter, &QComboBox::currentIndexChanged, w, [=] { filterList(); });
    auto draft = std::make_shared<Draft>();
    auto timer = new QTimer(w);
    timer->setInterval(40);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    auto report = [=](std::function<void()> f) {
        try {
            f();
        } catch (const std::exception &e) {
            QMessageBox::warning(w, "Cannot edit " + noun, QString::fromUtf8(e.what()));
        }
    };
    auto save = [=](const QString &title) {
        auto r = records(*p);
        // Property edits must not replace the original engine animation with a timed override.
        if (draft->frames.isEmpty() && !draft->originalAnimation)
            draft->frames.append({image(*p, draft->id), 150, 0});
        if (actorKind && draft->id >= 512 && animationPoses(draft->frames) > 4) {
            draft->def.animation = 1;
            QSignalBlocker block(animation);
            animation->setCurrentIndex(1);
        }
        r[draft->id] = record(draft->def, draft->originalAnimation ? frames(r.value(draft->id))
                                                                   : draft->frames);
        return commit({{"TYPES.OVR", library(*p, r)}}, title);
    };
    auto saveArtwork = [=](const QString &title) {
        if (!draft->originalAnimation)
            return save(title);
        auto r = records(*p);
        for (unsigned frame = 0; frame < 4; ++frame) {
            auto d = frame ? definition(*p, draft->id + frame) : draft->def;
            d.render |= TYPES_SPRITE;
            QVector<Frame> artwork;
            for (unsigned slot = 0; slot < 6; ++slot) {
                auto f = std::find_if(draft->frames.begin(), draft->frames.end(),
                                      [=](const Frame &f) { return f.variant == slot; });
                require(f != draft->frames.end(), "Original creature artwork is incomplete");
                artwork.append(*(f + frame));
            }
            r[d.id] = record(d, artwork);
        }
        return commit({{"TYPES.OVR", library(*p, r)}}, title);
    };
    const auto referenceArt = U5::readGraphics("TILES.16", p->data("TILES.16")).images;
    if (p->resources.contains("BRIT.CBT")) {
        MapDocument combatDocument(p, "BRIT.CBT");
        combatMap->setIconSize({48, 48});
        for (int page = 0; page < qMin(16, combatDocument.pages.size()); page++) {
            auto cells = combatDocument.terrain(page);
            QMap<unsigned, int> counts;
            QImage preview(176, 176, QImage::Format_ARGB32);
            preview.fill(Qt::black);
            QPainter painter(&preview);
            for (int i = 0; i < cells.size(); i++) {
                unsigned tile = U5::byte(cells, i);
                counts[tile]++;
                painter.drawImage((i % 11) * 16, (i / 11) * 16, referenceArt.value(tile));
            }
            painter.end();
            unsigned dominant = 0;
            int count = -1;
            for (auto it = counts.begin(); it != counts.end(); ++it)
                if (it.value() > count) {
                    dominant = it.key();
                    count = it.value();
                }
            combatMap->setItemText(page + 1, MapDocument::tileName(dominant) +
                                                 QString(" · Layout %1").arg(page + 1));
            combatMap->setItemIcon(page + 1, QIcon(QPixmap::fromImage(preview)));
        }
    }
    auto referenceIcon = [=](unsigned id) {
        return QIcon(
            QPixmap::fromImage(records(*p).contains(id) ? image(*p, id) : referenceArt.value(id)));
    };
    for (auto box : results + QVector<QComboBox *>{ground}) {
        box->setEditable(true);
        box->setInsertPolicy(QComboBox::NoInsert);
        box->setIconSize({24, 24});
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(14);
        box->completer()->setCaseSensitivity(Qt::CaseInsensitive);
        box->completer()->setFilterMode(Qt::MatchContains);
    }
    auto fillRefs = [=] {
        ground->clear();
        ground->addItem("Nearby ground", qulonglong(TYPES_INFER_GROUND));
        for (auto box : results) {
            box->clear();
            box->addItem((actorKind
                              ? QStringList{"No transformation", "Daemon", "Same creature",
                                            "Original companion"}
                              : QStringList{"Default open appearance", "Default closed appearance",
                                            "Default locked appearance",
                                            "Default broken appearance"})[results.indexOf(box)],
                         0);
        }
        auto r = records(*p);
        if (actorKind)
            for (unsigned id = 0; id < 256; id++)
                ground->addItem(MapDocument::tileName(id), id);
        for (unsigned id = actorKind ? 320 : 0; id < (actorKind ? 512u : 256u);
             id += actorKind ? 4 : 1) {
            QString label = actorKind ? Workshop::actorName(id - 256) : MapDocument::tileName(id);
            if (!actorKind)
                ground->addItem(referenceIcon(id), label, id);
            for (auto box : results)
                box->addItem(referenceIcon(id), label, id);
        }
        for (auto it = r.begin(); it != r.end(); ++it) {
            auto d = definition(*p, it.key());
            if (bool(d.kind) != bool(actorKind))
                continue;
            QString label = QString::fromUtf8(d.name);
            if (!d.kind && !(d.render & TYPES_SPRITE))
                ground->addItem(referenceIcon(d.id), label, d.id);
            for (auto box : results)
                box->addItem(referenceIcon(d.id), label, d.id);
        }
        for (auto box : QVector<QComboBox *>{ground} + results)
            for (int i = 0; i < box->count(); ++i)
                box->setItemData(i, box->itemText(i) + ": " + box->toolTip(), Qt::ToolTipRole);
    };
    auto selectedSize = [=] {
        auto size = artworkSize(variants->currentIndex());
        return selectedVariant() < 6 ? QSize(size.width() * TYPES_Width(&draft->def),
                                             size.height() * TYPES_Height(&draft->def))
                                     : size;
    };
    auto reloadFrames = [=] {
        QSignalBlocker block(frameList);
        frameList->clear();
        if (draft->originalAnimation)
            draft->frames = originalActorFrames(*p, draft->id);
        for (auto button : {frameAdd, frameDelete, frameLeft, frameRight})
            button->setEnabled(!draft->originalAnimation);
        palette->setToolTip(
            draft->originalAnimation
                ? "Choose a drawing color. Black is opaque; only pixels with zero alpha are "
                  "transparent."
                : "Choose a drawing color from the selected tileset’s palette. The crossed swatch "
                  "paints transparent pixels; black is a visible color.");

        unsigned slot = selectedVariant();
        bool supplied = std::any_of(draft->frames.begin(), draft->frames.end(),
                                    [=](const Frame &f) { return f.variant == slot; });
        resetArtwork->setEnabled(!draft->originalAnimation && slot % 6 != 0 && supplied);
        if (!supplied) {
            bool hasWorld = std::any_of(draft->frames.begin(), draft->frames.end(),
                                        [](const Frame &f) { return f.variant == 6; });
            slot = selectedVariant() < 6 || !hasWorld ? 0 : 6;
        }
        for (int i = 0; i < draft->frames.size(); i++)
            if (draft->frames[i].variant == slot) {
                auto item = new QListWidgetItem(QIcon(QPixmap::fromImage(draft->frames[i].image)),
                                                QString::number(frameList->count() + 1), frameList);
                item->setData(Qt::UserRole, i);
                item->setToolTip(
                    QString("Frame %1. %2")
                        .arg(frameList->count())
                        .arg(
                            supplied
                                ? "Select to edit this tileset’s frame."
                                : "Using DOS artwork; painting creates artwork for this tileset."));
                if (draft->originalAnimation)
                    item->setToolTip(
                        QString("Original frame %1 of 4. Select to edit this tileset’s artwork. "
                                "In-game timing is controlled by the engine.")
                            .arg(frameList->count()));
            }
        if (frameList->count())
            frameList->setCurrentRow(
                qBound(0, state->value(QString("customTiles/%1/frame").arg(draft->id), 0),
                       frameList->count() - 1));
        if (frameList->currentItem()) {
            auto f = draft->frames[frameList->currentItem()->data(Qt::UserRole).toInt()];
            canvas->pixels =
                f.image.scaled(selectedSize(), Qt::IgnoreAspectRatio, Qt::FastTransformation);

        } else
            canvas->pixels = image(*p, draft->id, selectedVariant());
        canvas->pixels =
            canvas->pixels.scaled(selectedSize(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
        const auto size = canvas->pixels.size();
        int scale = 384 / qMax(size.width(), size.height());
        canvas->setMinimumSize(0, 0);
        canvas->setFixedSize(size * scale);
        canvas->update();
    };
    auto updateInspector = [=] {
        appearance->form->setRowVisible(ground, !actorKind && sprite->isChecked());
        hazards->form->setRowVisible(damageRow, burn->isChecked());
        effectGroup->setVisible(poison->isChecked() || burn->isChecked());
        boatGroup->setVisible(movement[5]->isChecked());
        unsigned profile = interaction->currentData().toUInt();
        bool door = profile == 0xb8 || profile == 0xb9 || profile == 0xba || profile == 0x97 ||
                    profile == 0x98;
        bool container = profile == 0xa6;
        bool breakable = door || container || profile == 0x9d || profile == 0x2e || profile == 0x2d;
        for (int i = 0; i < 4; i++)
            interactions->form->setRowVisible(
                results[i], actorKind || results[i]->currentData().toUInt() != 0 ||
                                (i < 3 ? door || container : breakable));
        interactions->describe(interaction->currentText());
        QStringList looks;
        if (sprite->isChecked())
            looks << "Transparent";
        if (masonry->isChecked())
            looks << "Wall lighting";
        appearance->describe(looks.isEmpty() ? "Opaque" : looks.join(" · "));
        QStringList dangers;
        if (poison->isChecked())
            dangers << "Poison";
        if (burn->isChecked())
            dangers << QString("%1–%2 damage").arg(damageMin->value()).arg(damageMax->value());
        if (!dangers.isEmpty()) {
            QStringList affected;
            for (auto box : effectModes)
                if (box->isChecked())
                    affected << box->text();
            dangers << (affected.isEmpty()
                            ? "Affects nobody"
                            : affected.mid(0, 2).join(", ") +
                                  (affected.size() > 2 ? QString(" +%1").arg(affected.size() - 2)
                                                       : ""));
        }
        hazards->describe(dangers.isEmpty() ? "None" : dangers.join(" · "));
        travel->describe(QString("%1 · %2").arg(
            movement[0]->isChecked() ? "Walkable" : "Blocks walking", sight->currentText()));
        int count = 0;
        for (int i = 0; i < movement.size(); i++)
            if (i != 0 && i != 3 && movement[i]->isChecked())
                count++;
        QStringList extras{QString("%1 other movement types").arg(count)};
        if (humanoidSeat->isChecked())
            extras << "Humanoid seating";
        if (combatMap->currentData().toUInt())
            extras << combatMap->currentText();
        advanced->describe(extras.join(" · "));
        if (actorKind) {
            int count = 0;
            for (auto ability : actorAbilities)
                count += ability->isChecked();
            advanced->describe(
                QString("%1 abilities · %2").arg(count).arg(actorMovement->currentText()));
            combat->describe(QString("%1 HP · %2 attack · %3 defense")
                                 .arg(actorStats[5]->value())
                                 .arg(actorStats[4]->value())
                                 .arg(actorStats[3]->value()));
            bool summons = false, divides = false;
            for (int i = 0; i < abilities.size(); i++) {
                if (abilities[i].second == 1024)
                    summons = actorAbilities[i]->isChecked();
                if (abilities[i].second == 16)
                    divides = actorAbilities[i]->isChecked();
            }
            interactions->form->setRowVisible(results[1],
                                              summons || results[1]->currentData().toUInt());
            interactions->form->setRowVisible(results[2],
                                              divides || results[2]->currentData().toUInt());
        }
    };
    const auto importHelp = import->toolTip();
    auto load = [=](unsigned id) {
        draft->loading = true;
        draft->id = id;
        (*state)[selectedIdKey] = id;
        draft->def = definition(*p, id);
        auto d = draft->def;
        {
            QSignalBlocker block(actorSize);
            actorSize->setCurrentIndex((TYPES_Width(&d) - 1) * 2 + TYPES_Height(&d) - 1);
        }
        actorSize->setEnabled(actorKind && id >= 512);
        context->setEnabled(actorKind && id >= 512);
        if (id < 512) {
            QSignalBlocker block(context);
            context->setCurrentIndex(0);
        }
        draft->frames = frames(records(*p).value(id));
        draft->originalAnimation =
            actorKind && id < 512 &&
            std::count_if(draft->frames.begin(), draft->frames.end(),
                          [](const Frame &f) { return f.variant == 0; }) <= 1;
        import->setToolTip(
            draft->originalAnimation
                ? "Replace this creature’s four original frames for the selected tileset. Import a "
                  "PNG grid containing exactly four frames at the tileset’s native size. PNG "
                  "alpha and opaque black are preserved. In-game animation "
                  "behavior stays unchanged."
                : importHelp);
        if (draft->originalAnimation)
            draft->frames = originalActorFrames(*p, id);
        fillRefs();
        animation->setItemText(0, "Like " + Workshop::actorName(d.base));
        animation->setEnabled(id >= 512);
        int poses = animationPoses(draft->frames);
        animation->setCurrentIndex(d.animation || (id >= 512 && poses > 4) ||
                                          (id < 512 && !draft->originalAnimation)
                                      ? 1 : 0);
        name->setText(QString::fromUtf8(d.name));
        description->setText(QString::fromUtf8(d.description));
        world->setEnabled(id >= 512);
        location->setEnabled(id >= 512);
        world->setChecked(id >= 512 ? (d.filters & 1) : worldTiles.contains(id));
        location->setChecked(id >= 512 ? (d.filters & 2) : locationTiles.contains(id));
        sprite->setChecked(d.render & TYPES_SPRITE);
        masonry->setChecked(d.render & TYPES_MASONRY);
        sight->setCurrentIndex(d.sight);
        projectile->setChecked(d.projectile);
        speed->setCurrentIndex(d.travel);
        for (int i = 0; i < 11; i++)
            movement[i]->setChecked(d.movement & (1 << i));
        for (int i = 0; i < 11; i++)
            effectModes[i]->setChecked(d.effectModes & (1u << i));
        for (int i = 0; i < 4; i++)
            skiffDirections[i]->setChecked(d.skiffDirections & (8u >> i));
        humanoidSeat->setChecked(d.humanoidSeat);
        combatMap->setCurrentIndex(d.combatMap);
        poison->setChecked(d.effects & TYPES_POISON);
        burn->setChecked(d.effects & TYPES_BURN);
        damageMin->setValue(d.damageMin);
        damageMax->setValue(d.damageMax);
        if (interaction->findData(d.base) < 0)
            interaction->addItem("Same interaction as " + MapDocument::tileName(d.base), d.base);
        interaction->setCurrentIndex(interaction->findData(d.base));
        interaction->setEnabled(!actorKind || id >= 512);
        if (actorKind)
            interaction->setToolTip(
                id >= 512 ? "Original creature behavior used by this type, independently of "
                            "artwork and stats."
                          : "Copy this type to choose a different original creature behavior.");
        ground->setCurrentIndex(d.ground == TYPES_INFER_GROUND ? 0 : ground->findData(d.ground));
        for (int i = 0; i < 4; i++)
            results[i]->setCurrentIndex(results[i]->findData(d.result[i]));
        for (int i = 0; i < 8; i++) {
            if (i == 5 || i == 6)
                actorStats[i]->setMinimum(id >= 512 ? 1 : 0);
            actorStats[i]->setValue(d.stats[i]);
        }
        for (int i = 0; i < abilities.size(); i++)
            actorAbilities[i]->setChecked(d.actorFlags & abilities[i].second);
        actorMovement->setCurrentIndex(d.moveProfile);
        attackRange->setValue(d.range);
        projectileType->setCurrentIndex(d.projectileAppearance);
        reloadFrames();
        draft->loading = false;
        error->hide();
        updateInspector();
        hazards->expand(d.effects != 0);
    };
    auto refresh = [=] {
        QSignalBlocker block(list);
        list->clear();
        for (unsigned id = actorKind ? (actorKind == 1 ? 320u : 384u) : 0u;
             id < (actorKind ? (actorKind == 1 ? 384u : 512u) : 256u); id += actorKind ? 4 : 1) {
            auto item = new QListWidgetItem(
                actorKind ? Workshop::actorName(id - 256) : MapDocument::tileName(id), list);
            item->setData(Qt::UserRole, id);
        }
        auto r = records(*p);
        for (auto it = r.begin(); it != r.end(); ++it)
            if (it.key() >= 512) {
                auto d = definition(*p, it.key());
                if (d.kind == unsigned(actorKind)) {
                    auto item = new QListWidgetItem(QIcon(QPixmap::fromImage(image(*p, d.id))),
                                                    QString::fromUtf8(d.name), list);
                    item->setData(Qt::UserRole, d.id);
                }
            }
        for (int i = 0; i < list->count(); i++)
            if (list->item(i)->data(Qt::UserRole).toUInt() == draft->id)
                list->setCurrentRow(i);
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setToolTip(list->item(i)->text() + ": " + list->toolTip());
        filterList();
    };
    QObject::connect(search, &QLineEdit::textChanged, w, [=] { filterList(); });
    QObject::connect(list, &QListWidget::currentItemChanged, w, [=](QListWidgetItem *item) {
        if (item)
            load(item->data(Qt::UserRole).toUInt());
    });
    auto create = [=](bool copying) {
        report([&] {
            QInputDialog dialog(w);
            dialog.setWindowTitle("New " + noun);
            dialog.setLabelText("Name");
            dialog.setTextValue(copying ? name->text() + " copy" : "New " + noun);
            for (auto input : dialog.findChildren<QLineEdit *>())
                input->setToolTip("Name used to find this new type in Workshop. You can change it "
                                  "later in the properties panel.");
            for (auto button : dialog.findChildren<QPushButton *>())
                button->setToolTip(dialog.findChild<QDialogButtonBox *>()->standardButton(button) ==
                                           QDialogButtonBox::Ok
                                       ? "Create the new type with this name, then edit its "
                                         "artwork and properties."
                                       : "Close without creating a type.");
            if (dialog.exec() != QDialog::Accepted)
                return;
            auto title = dialog.textValue();
            if (title.trimmed().isEmpty())
                return;
            auto r = records(*p);
            unsigned id = 512;
            while (r.contains(id))
                id++;
            require(id <= TYPES_LAST_CUSTOM, "Tile library is full");
            TypeDef d;
            if (copying)
                d = draft->def;
            else
                TYPES_Vanilla(actorKind ? 1 : 0, actorKind ? (actorKind == 1 ? 80 : 144) : 5, &d);
            if (copying && d.id < 256)
                d.filters =
                    (worldTiles.contains(d.id) ? 1 : 0) | (locationTiles.contains(d.id) ? 2 : 0);
            if (!copying)
                d.filters = 3;
            d.id = id;
            d.kind = actorKind;
            if (actorKind) {
                d.render |= TYPES_SPRITE;
                if (!d.stats[5])
                    d.stats[5] = 10;
                if (!d.stats[6])
                    d.stats[6] = 1;
            }
            auto utf = title.trimmed().toUtf8();
            require(utf.size() < 64, "Tile name is too long");
            memset(d.name, 0, 64);
            memcpy(d.name, utf.constData(), utf.size());
            QVector<Frame> f = copying ? draft->frames : QVector<Frame>();
            if (f.isEmpty()) {
                QImage img = copying ? image(*p, draft->id)
                                     : (actorKind ? image(*p, 256 + d.base)
                                                  : QImage(16, 16, QImage::Format_RGBA8888));
                if (!copying && !actorKind)
                    img.fill(Qt::black);
                f.append({img, 150, 0});
            }
            r[id] = record(d, f);
            if (commit({{"TYPES.OVR", library(*p, r)}}, "Add " + noun)) {
                draft->id = id;
                draft->originalAnimation = false;
                refresh();
                load(id);
            }
        });
    };
    QObject::connect(add, &QPushButton::clicked, w, [=] { create(false); });
    QObject::connect(copy, &QPushButton::clicked, w, [=] { create(true); });
    QObject::connect(remove, &QPushButton::clicked, w, [=] {
        report([&] {
            auto r = records(*p);
            if (actorKind && draft->id < 512) {
                QMap<QString, QByteArray> changes;
                bool removed = false;
                for (unsigned frame = 0; frame < 4; ++frame)
                    removed |= r.remove(draft->id + frame) != 0;
                if (removed)
                    changes["TYPES.OVR"] = library(*p, r);
                for (unsigned variant = 0; variant < 5; ++variant) {
                    auto resource = actorArtworkResource(variant);
                    auto current = U5::readGraphics(resource, p->data(resource));
                    auto original =
                        U5::readGraphics(resource, p->resources.value(resource).original);
                    int index = int(draft->id) - current.tileOffset;
                    for (int i = 0; i < 4; ++i)
                        current.images[index + i] = original.images[index + i];
                    auto data = U5::writeGraphics(current);
                    if (data != p->data(resource))
                        changes[resource] = data;
                }
                if (!changes.isEmpty() && commit(changes, "Reset " + noun)) {
                    refresh();
                    load(draft->id);
                }
                return;
            }
            if (!r.contains(draft->id))
                return;
            for (const auto &c : placements(*p))
                require(c.type != draft->id,
                        "This tile is placed on a map. Replace its placements before deleting it.");
            r.remove(draft->id);
            if (commit({{"TYPES.OVR", library(*p, r)}},
                       draft->id < 256 ? "Reset tile" : "Delete tile")) {
                draft->id = actorKind ? (actorKind == 1 ? 336 : 400) : 5;
                refresh();
                load(draft->id);
            }
        });
    });
    auto applySettings = [=] {
        if (draft->loading)
            return;
        updateInspector();
        auto previous = draft->def;
        try {
            auto d = draft->def;
            auto n = name->text().trimmed().toUtf8(), desc = description->text().toUtf8();
            require(!n.isEmpty(), "Enter a name for this tile.");
            require(n.size() < 64, "Tile names must fit within 63 UTF-8 bytes.");
            require(desc.size() < 128, "Look descriptions must fit within 127 UTF-8 bytes.");
            memset(d.name, 0, 64);
            memcpy(d.name, n.constData(), n.size());
            memset(d.description, 0, 128);
            memcpy(d.description, desc.constData(), desc.size());
            if (d.id >= 512)
                d.filters = (world->isChecked() ? 1 : 0) | (location->isChecked() ? 2 : 0);
            d.render = (sprite->isChecked() ? 1 : 0) | (masonry->isChecked() ? 2 : 0);
            if (actorKind && d.id >= 512) {
                unsigned poses = animationPoses(draft->frames);
                require(animation->currentIndex() || poses <= 4,
                        "Use Continuous loop for animations with more than four poses");
                d.animation = animation->currentIndex();
            }
            d.sight = sight->currentIndex();
            d.projectile = projectile->isChecked();
            d.travel = speed->currentIndex();
            d.movement = 0;
            for (int i = 0; i < 11; i++)
                if (movement[i]->isChecked())
                    d.movement |= 1 << i;
            d.effects = (poison->isChecked() ? 1 : 0) | (burn->isChecked() ? 2 : 0);
            d.effectModes = 0;
            for (int i = 0; i < 11; i++)
                if (effectModes[i]->isChecked())
                    d.effectModes |= 1u << i;
            d.skiffDirections = 0;
            for (int i = 0; i < 4; i++)
                if (skiffDirections[i]->isChecked())
                    d.skiffDirections |= 8u >> i;
            d.humanoidSeat = humanoidSeat->isChecked();
            d.combatMap = combatMap->currentData().toUInt();
            d.damageMin = damageMin->value();
            d.damageMax = damageMax->value();
            d.base =
                interaction->currentData().isValid() ? interaction->currentData().toUInt() : d.base;
            if (!actorKind)
                d.ground = ground->currentData().toUInt();
            for (int i = 0; i < 4; i++)
                d.result[i] = results[i]->currentData().toUInt();
            if (actorKind) {
                d.kind = actorKind;
                for (int i = 0; i < 8; i++)
                    d.stats[i] = actorStats[i]->value();
                for (int i = 0; i < abilities.size(); i++) {
                    if (actorAbilities[i]->isChecked()) {
                        if (!(d.actorFlags & abilities[i].second))
                            d.actorFlags |= abilities[i].second;
                    } else
                        d.actorFlags &= ~abilities[i].second;
                }
                d.moveProfile = actorMovement->currentIndex();
                d.range = attackRange->value();
                d.projectileAppearance = projectileType->currentIndex();
            }
            if (d.damageMin > d.damageMax)
                throw std::runtime_error("Minimum damage must not exceed maximum damage.");
            if (memcmp(&previous, &d, sizeof(d)) == 0) {
                error->hide();
                return;
            }
            draft->def = d;
            if (save("Edit " + noun + " settings")) {
                animation->setItemText(0, "Like " + Workshop::actorName(d.base));
                error->hide();
                refresh();
            } else
                draft->def = previous;
        } catch (const std::exception &e) {
            draft->def = previous;
            error->setText(QString::fromUtf8(e.what()));
            error->show();
        }
    };
    QObject::connect(name, &QLineEdit::editingFinished, w, applySettings);
    QObject::connect(description, &QLineEdit::editingFinished, w, applySettings);
    for (auto box : props->findChildren<QCheckBox *>())
        QObject::connect(box, &QCheckBox::toggled, w, [=] { applySettings(); });
    for (auto box : props->findChildren<QComboBox *>())
        if (box != actorSize)
            QObject::connect(box, &QComboBox::currentIndexChanged, w, [=] { applySettings(); });
    for (auto spin : QVector<QSpinBox *>{damageMin, damageMax, attackRange} + actorStats)
        QObject::connect(spin, &QSpinBox::editingFinished, w, applySettings);

    QObject::connect(tools, &QButtonGroup::idClicked, w, [=](int i) {
        canvas->tool = i;
        (*state)["customTiles/tool"] = i;
    });
    auto editableFrame = [=]() -> int {
        unsigned variant = selectedVariant();
        if (variant >= 6 && !std::any_of(draft->frames.begin(), draft->frames.end(),
                                         [](const Frame &f) { return f.variant >= 6; })) {
            auto originals = draft->frames;
            for (auto frame : originals) {
                frame.variant += 6;
                draft->frames.append(frame);
            }
        }
        int row = qMax(0, frameList->currentRow());
        if (draft->frames.isEmpty())
            draft->frames.append({image(*p, draft->id), 150, 0});
        bool supplied = std::any_of(draft->frames.begin(), draft->frames.end(),
                                    [=](const Frame &f) { return f.variant == variant; });
        if (!supplied && variant % 6) {
            auto defaults = draft->frames;
            for (auto f : defaults)
                if (f.variant == (variant < 6 ? 0u : 6u)) {
                    f.variant = variant;
                    f.image = f.image.scaled(selectedSize(), Qt::IgnoreAspectRatio,
                                             Qt::FastTransformation);
                    draft->frames.append(f);
                }
            std::stable_sort(draft->frames.begin(), draft->frames.end(),
                             [](const Frame &a, const Frame &b) { return a.variant < b.variant; });
        }
        for (auto &f : draft->frames)
            if (f.variant == variant)
                f.image = Workshop::paletteImage(f.image, *allowedColors);
        int n = 0;
        for (int i = 0; i < draft->frames.size(); i++)
            if (draft->frames[i].variant == variant && n++ == row)
                return i;
        draft->frames.append({canvas->pixels, 150, variant});
        return draft->frames.size() - 1;
    };
    QObject::connect(frameCopy, &QToolButton::clicked, w, [=] {
        if (frameList->currentItem())
            QApplication::clipboard()->setImage(canvas->pixels);
    });
    QObject::connect(framePaste, &QToolButton::clicked, w, [=] {
        if (!frameList->currentItem())
            return;
        report([&] {
            auto pasted = QApplication::clipboard()->image();
            require(!pasted.isNull(), "Copy a frame or image before pasting");
            require(pasted.size() == selectedSize(),
                    QString("This frame needs a %1 × %2 pixel image.")
                        .arg(selectedSize().width())
                        .arg(selectedSize().height()));
            auto old = draft->frames;
            try {
                draft->frames[editableFrame()].image =
                    Workshop::paletteImage(pasted, *allowedColors);
                saveArtwork("Paste animation frame");
            } catch (...) {
                draft->frames = old;
                throw;
            }
            reloadFrames();
        });
    });
    QObject::connect(resetArtwork, &QAction::triggered, w, [=] {
        report([&] {
            unsigned slot = selectedVariant();
            if (slot % 6 == 0)
                return;
            draft->frames.erase(std::remove_if(draft->frames.begin(), draft->frames.end(),
                                               [=](const Frame &f) { return f.variant == slot; }),
                                draft->frames.end());
            save("Use DOS tile artwork");
            reloadFrames();
        });
    });
    QObject::connect(frameList, &QListWidget::currentItemChanged, w, [=](QListWidgetItem *item) {
        if (!item)
            return;
        (*state)[QString("customTiles/%1/frame").arg(draft->id)] = frameList->row(item);
        const auto &f = draft->frames[item->data(Qt::UserRole).toInt()];
        canvas->pixels =
            f.image.scaled(selectedSize(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
        canvas->update();
    });
    canvas->changed = [=] {
        report([&] {
            draft->frames[editableFrame()].image =
                Workshop::paletteImage(canvas->pixels, *allowedColors);
            saveArtwork("Paint tile");
            reloadFrames();
        });
    };
    QObject::connect(variants, &QComboBox::currentIndexChanged, w, [=](int variant) {
        (*state)["customTiles/variant"] = variant;
        reloadPalette();
        reloadFrames();
    });
    QObject::connect(context, &QComboBox::currentIndexChanged, w, [=](int index) {
        (*state)["customTiles/artworkContext"] = index;
        reloadFrames();
    });
    QObject::connect(actorSize, &QComboBox::currentIndexChanged, w, [=](int index) {
        if (draft->loading || draft->id < 512)
            return;
        report([&] {
            auto previous = draft->def;
            auto oldFrames = draft->frames;
            auto d = previous;
            d.width = 1 + index / 2;
            d.height = 1 + index % 2;
            QVector<Frame> next = draft->frames;
            bool world = std::any_of(next.begin(), next.end(),
                                     [](const Frame &f) { return f.variant >= 6; });
            if (!world) {
                for (auto frame : draft->frames) {
                    auto cell = artworkSize(frame.variant);
                    frame.variant += 6;
                    frame.image = frame.image.copy(0, 0, cell.width(), cell.height());
                    next.append(frame);
                }
            }
            for (auto &frame : next)
                if (frame.variant < 6) {
                    auto cell = artworkSize(frame.variant);
                    QImage resized(cell.width() * d.width, cell.height() * d.height,
                                   QImage::Format_RGBA8888);
                    resized.fill(Qt::transparent);
                    QPainter painter(&resized);
                    painter.setCompositionMode(QPainter::CompositionMode_Source);
                    painter.drawImage(0, 0, frame.image);
                    painter.end();
                    frame.image = resized;
                }
            std::stable_sort(next.begin(), next.end(),
                             [](const Frame &a, const Frame &b) { return a.variant < b.variant; });
            draft->def = d;
            draft->frames = next;
            try {
                if (!save("Resize creature")) {
                    draft->def = previous;
                    draft->frames = oldFrames;
                }
            } catch (...) {
                draft->def = previous;
                draft->frames = oldFrames;
                QSignalBlocker block(actorSize);
                actorSize->setCurrentIndex((TYPES_Width(&previous) - 1) * 2 +
                                           TYPES_Height(&previous) - 1);
                throw;
            }
            {
                QSignalBlocker block(actorSize);
                actorSize->setCurrentIndex((TYPES_Width(&draft->def) - 1) * 2 +
                                           TYPES_Height(&draft->def) - 1);
            }
            reloadFrames();
        });
    });
    QObject::connect(frameAdd, &QPushButton::clicked, w, [=] {
        report([&] {
            require(variants->currentIndex() == 0,
                    "Add frames to the default animation before supplying alternative artwork");
            require(frameList->count() < 64, "An animation supports at most 64 frames");
            editableFrame();
            for (int v = selectedVariant() < 6 ? 0 : 6, endVariant = v + 6; v < endVariant; v++) {
                auto f = std::find_if(draft->frames.begin(), draft->frames.end(),
                                      [=](const Frame &f) { return f.variant == unsigned(v); });
                if (f != draft->frames.end()) {
                    auto frame = *f;
                    auto end = std::find_if(f, draft->frames.end(), [=](const Frame &g) {
                        return g.variant > unsigned(v);
                    });
                    draft->frames.insert(end, frame);
                }
            }
            save("Add animation frame");
            reloadFrames();
        });
    });
    QObject::connect(frameDelete, &QPushButton::clicked, w, [=] {
        report([&] {
            require(variants->currentIndex() == 0 && frameList->count() > 1,
                    "The default animation needs at least one frame");
            editableFrame();
            int index = frameList->currentRow();
            for (int base = selectedVariant() < 6 ? 0 : 6, v = base + 5; v >= base; v--) {
                int n = 0;
                for (int i = 0; i < draft->frames.size(); i++)
                    if (draft->frames[i].variant == unsigned(v) && n++ == index) {
                        draft->frames.removeAt(i);
                        break;
                    }
            }
            save("Delete animation frame");
            reloadFrames();
        });
    });
    for (auto pair : {qMakePair(frameLeft, -1), qMakePair(frameRight, 1)})
        QObject::connect(pair.first, &QPushButton::clicked, w, [=] {
            report([&] {
                int row = frameList->currentRow(), next = row + pair.second;
                if (row < 0 || next < 0 || next >= frameList->count())
                    return;
                editableFrame();
                reloadFrames();
                int a = frameList->item(row)->data(Qt::UserRole).toInt(),
                    b = frameList->item(next)->data(Qt::UserRole).toInt();
                draft->frames.swapItemsAt(a, b);
                save("Reorder animation frames");
                reloadFrames();
                frameList->setCurrentRow(next);
            });
        });
    auto exportArtwork = [=](bool sheet) {
        report([&] {
            require(!canvas->pixels.isNull(), "Select artwork to export");
            const QSize size = selectedSize();
            const int count = sheet ? qMax(1, frameList->count()) : 1;
            QImage output(size.width() * count, size.height(), QImage::Format_RGBA8888);
            require(!output.isNull(), "Cannot allocate the exported image");
            output.fill(Qt::transparent);
            {
                QPainter painter(&output);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                for (int i = 0; i < count; ++i) {
                    auto item = sheet ? frameList->item(i) : frameList->currentItem();
                    const auto pixels = item ? draft->frames[item->data(Qt::UserRole).toInt()].image
                                             : canvas->pixels;
                    painter.drawImage(
                        i * size.width(), 0,
                        pixels.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation));
                }
            }
            auto path = QFileDialog::getSaveFileName(
                w, sheet ? "Export animation sheet" : "Export frame",
                QString::fromUtf8(draft->def.name) + (sheet ? "-animation.png" : "-frame.png"),
                "PNG images (*.png)");
            if (path.isEmpty())
                return;
            if (!path.endsWith(".png", Qt::CaseInsensitive))
                path += ".png";
            require(output.save(path, "PNG"), "Cannot save PNG to the selected file");
        });
    };
    QObject::connect(exportFrame, &QAction::triggered, w, [=] { exportArtwork(false); });
    QObject::connect(exportSheet, &QAction::triggered, w, [=] { exportArtwork(true); });
    QObject::connect(import, &QPushButton::clicked, w, [=] {
        report([&] {
            auto path =
                QFileDialog::getOpenFileName(w, "Import tile frames", {}, "PNG images (*.png)");
            if (path.isEmpty())
                return;
            QImageReader reader(path);
            require(reader.size().width() <= 4096 && reader.size().height() <= 4096,
                    "PNG dimensions are too large");
            auto img = reader.read().convertToFormat(QImage::Format_RGBA8888);
            require(!img.isNull(), "Cannot decode PNG");
            img = Workshop::paletteImage(img, *allowedColors);
            const QSize frameSize = selectedSize();
            const int width = frameSize.width(), height = frameSize.height();
            require(img.width() % width == 0 && img.height() % height == 0,
                    QString("This tileset needs %1 × %2 pixel frames; sheet dimensions must be "
                            "multiples of that size.")
                        .arg(width)
                        .arg(height));
            int columns = img.width() / width, rows = img.height() / height, count = columns * rows;
            require(!draft->originalAnimation || count == 4,
                    "Original creatures need a PNG containing exactly four frames");
            require(count >= 1 && count <= 64,
                    "An animation supports 1–64 frames per artwork variant");
            QMessageBox preview(
                QMessageBox::Question, "Import frames",
                (count == 1
                     ? QString("Replace the selected frame with this %1 × %2 pixel image?")
                           .arg(width)
                           .arg(height)
                     : QString("Replace the animation with %1 frames of %2 × %3 pixels? "
                               "Other supplied tilesets will repeat or trim their last frames "
                               "to match.")
                           .arg(count)
                           .arg(width)
                           .arg(height)),
                QMessageBox::Yes | QMessageBox::Cancel, w);
            preview.setIconPixmap(QPixmap::fromImage(img).scaled(512, 256, Qt::KeepAspectRatio,
                                                                 Qt::FastTransformation));
            if (preview.exec() != QMessageBox::Yes)
                return;
            auto old = draft->frames;
            if (selectedVariant() >= 6)
                editableFrame();
            unsigned variant = selectedVariant();
            QVector<Frame> incoming;
            for (int i = 0; i < count; i++)
                incoming.append(
                    {img.copy((i % columns) * width, (i / columns) * height, width, height), 150,
                     variant});
            auto next = importedFrames(draft->frames, incoming, variant, frameList->currentRow());
            bool alpha = false;
            for (int y = 0; y < img.height() && !alpha; y++)
                for (int x = 0; x < img.width(); x++)
                    if (img.pixelColor(x, y).alpha() < 255) {
                        alpha = true;
                        break;
                    }
            if (alpha && !draft->originalAnimation) {
                draft->def.render |= TYPES_SPRITE;
                if (draft->def.ground == draft->id) {
                    draft->def.ground = TYPES_INFER_GROUND;
                    ground->setCurrentIndex(0);
                }
                sprite->setChecked(true);
            }
            draft->frames = next;
            try {
                saveArtwork("Import tile animation");
            } catch (...) {
                draft->frames = old;
                throw;
            }
            reloadFrames();
        });
    });
    QObject::connect(play, &QPushButton::clicked, w, [=] {
        if (timer->isActive()) {
            timer->stop();
            play->setText("Play");
            reloadFrames();
        } else {
            draft->previewTile = draft->def.base;
            draft->previewState = 0;
            draft->previewTick = clock->elapsed();
            timer->start();
            play->setText("Stop");
        }
    });
    QObject::connect(timer, &QTimer::timeout, w, [=] {
        auto r = record(draft->def, draft->frames);
        unsigned side = 0, height = 0;
        const auto *bytes = reinterpret_cast<const unsigned char *>(r.constData());
        unsigned poses = animationPoses(draft->frames);
        const unsigned char *rgba;
        if (actorKind && !draft->def.animation &&
            (draft->originalAnimation || (draft->id >= 512 && poses <= 4))) {
            while (clock->elapsed() - draft->previewTick >= 55) {
                TYPES_AdvanceAnimation(
                    draft->def.base, &draft->previewTile, &draft->previewState,
                    +[]() -> unsigned { return QRandomGenerator::global()->bounded(256u); });
                draft->previewTick += 55;
            }
            rgba =
                TYPES_FrameAt(bytes, selectedVariant(),
                              (draft->previewTile - (draft->def.base & 252)) & 3, &side, &height);
        } else
            rgba = TYPES_FrameImage(bytes, selectedVariant(), clock->elapsed(), &side, &height);
        if (rgba) {
            canvas->pixels =
                QImage(rgba, side, height, side * 4, QImage::Format_RGBA8888)
                    .scaled(selectedSize(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
            canvas->update();
        }
    });
    draft->id = state->value(selectedIdKey, actorKind ? (actorKind == 1 ? 336 : 400) : 5);
    refresh();
    load(draft->id);
    return w;
}
} // namespace CustomTiles
