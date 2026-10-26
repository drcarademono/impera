#include "dungeon_names.h"
#include "mod/location_names.h"
#include "mod/npcs.h"
#include "project.h"
#include "gates_editor.h"
#include "../src/mod/gates.h"
#include "extended_maps.h"
#include "contents_editor.h"
#include "custom_tiles.h"
#include "dialogue.h"
#include "dungeon_editor.h"
#include "mod/contents.h"
#include "mod/package.h"
#include "mod/tileset_resources.h"
#include "mod/world_resources.h"
#include "resource_document.h"
#include "text/dialogue_runtime.h"
#include "world_objects.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <algorithm>
using U5::require;
static QByteArray readLimited(const QString &path, qint64 cap) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "Cannot open " + path + ": " + f.errorString());
    require(f.size() <= cap, "File exceeds supported size: " + path);
    QByteArray bytes = f.readAll();
    require(bytes.size() == f.size(), "Failed reading " + path);
    return bytes;
}
void Project::openGame(const QString &directory) {
    static const bool embedded = [] {
        Q_INIT_RESOURCE(alternate_tilesets);
        return true;
    }();
    Q_UNUSED(embedded);
    Project next;
    next.sourceDirectory = QDir(directory).absolutePath();
    QDir dir(next.sourceDirectory);
    require(dir.exists(), "Game folder does not exist");
    for (const auto &name : dir.entryList(QDir::Files, QDir::Name)) {
        QString upper = name.toUpper();
        if (upper == "TYPES.OVR" || upper == "TYPEMAP.OVR" || upper == "CUTSCENE.OVR" ||
            upper == "MAPS.OVR" || MAPS_LocationResource(upper.toLatin1().constData()) ||
            upper == "LOCATIONS.OVR" || upper == "TEXT.OVR" || upper == "RULES.TBL" ||
            upper == "MODINFO.DAT" || upper == "INTERFACE.OVR" || upper == "GATES.OVR" || upper == "CONTENTS.OVR")
            continue; // Companion resources are package-only.
        if (!MOD_AllowedResource(upper.toLatin1().constData()))
            continue;
        require(!next.resources.contains(upper), "Ambiguous filename case in game folder: " + name);
        QByteArray b = readLimited(dir.filePath(name), MOD_MAX_RESOURCE);
        next.resources.insert(upper, {b, b});
    }
    require(next.resources.contains("TILES.16") && next.resources.contains("INIT.GAM"),
            "Select a DOS Ultima 5 folder containing TILES.16 and INIT.GAM");
    U5::readGraphics("TILES.16", next.data("TILES.16"));
    require(next.data("INIT.GAM").size() == 4192, "INIT.GAM must use the 4192-byte DOS layout");
    for (const auto &sheet : modTilesetSheets) {
        QByteArray bytes;
        if (!strcmp(sheet.resource, "MASK_DOS.PNG") || !strcmp(sheet.resource, "MASK_AMI.PNG") ||
            !strcmp(sheet.resource, "MASK_GRY.PNG")) {
            bytes = readLimited(":/tilesets/Ultima_5_Tiles_TransparencyMask.png", MOD_MAX_RESOURCE);
        } else if (MOD_IsTilesetMask(sheet.resource)) {
            QImage mask(sheet.columns * sheet.width, 16 * sheet.height, QImage::Format_RGB32);
            mask.fill(Qt::black);
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            mask.save(&buffer, "PNG");
        } else
            bytes =
                readLimited(":/tilesets/" + QString::fromUtf8(sheet.filename), MOD_MAX_RESOURCE);
        next.resources.insert(QString::fromLatin1(sheet.resource), {bytes, bytes});
    }
    next.resources.insert("TYPES.OVR", {{}, {}});
    next.resources.insert("TYPEMAP.OVR", {{}, {}});
    next.resources.insert("CUTSCENE.OVR", {{}, {}});
    next.resources.insert("LOCATIONS.OVR", {{}, {}});
    next.resources.insert("MAPS.OVR", {{}, {}});
    next.resources.insert("TEXT.OVR", {{}, {}});
    next.resources.insert("RULES.TBL", {{}, {}});
    next.resources.insert("CONTENTS.OVR", {{}, {}});
    next.resources.insert("GATES.OVR", {{}, {}});
    next.resources.insert("INTERFACE.OVR", {{}, {}});
    next.resources.insert("MODINFO.DAT", {{}, {}});
    *this = next;
}
const QByteArray &Project::data(const QString &name) const {
    auto it = resources.constFind(name);
    require(it != resources.cend(), "Required companion resource is missing: " + name);
    return it->edited;
}
QString Project::locationName(int map) const {
    require(map >= 0 && (map < LOCATION_NAME_SLOTS || MAPS_NewLocation(map)), "Invalid location");
    const auto bytes = resources.value("LOCATIONS.OVR").edited;
    if (const char *name = LOCATIONS_Name(bytes.constData(), bytes.size(), map))
        return QString::fromLatin1(name);
    if (MAPS_NewLocation(map))
        for (const auto &entry : Workshop::extendedMaps(*this))
            if ((entry.kind == 1 || entry.kind == 4) && int(entry.id) == map)
                return entry.name;

    if (!map)
        return "Britannia";
    if (map == 41)
        return "Underworld";
    if (map >= 33 && map <= 40)
        return QString::fromLatin1(engineDungeonNames[map - 33]);
    if (map >= 42)
        return QString("Location %1").arg(map);
    const QStringList banks{"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"};
    for (const auto &page : U5::mapPages(banks[(map - 1) / 8], QByteArray(16384, 0)))
        if (page.settlement == (map - 1) % 8)
            return page.name.section(" — ", 0, 0);
    return {};
}
QByteArray Project::renameLocation(int map, const QString &name) const {
    require(map >= 0 && map < LOCATION_NAME_SLOTS, "Invalid location");
    const auto trimmed = name.trimmed();
    require(!trimmed.isEmpty() && trimmed.size() < LOCATION_NAME_BYTES,
            "Location name must contain 1–63 characters");
    for (auto ch : trimmed)
        require(ch.unicode() >= 32 && ch.unicode() <= 126,
                "Location names use printable ASCII characters");
    auto bytes = resources.value("LOCATIONS.OVR").edited;
    if (bytes.isEmpty())
        bytes = QByteArray("ILOC0001", 8) + QByteArray(LOCATION_NAMES_SIZE - 8, 0);
    require(LOCATIONS_Validate(bytes.constData(), bytes.size()), "Invalid location names");
    QByteArray slot = trimmed.toLatin1();
    slot.append(QByteArray(LOCATION_NAME_BYTES - slot.size(), 0));
    bytes.replace(8 + map * LOCATION_NAME_BYTES, LOCATION_NAME_BYTES, slot);
    return bytes;
}
QVector<U5::MapPage> Project::mapPages(const QString &resource) const {
    const auto extended = Workshop::extendedMaps(*this);
    auto pages = MAPS_LocationResource(resource.toLatin1().constData())
                     ? QVector<U5::MapPage>()
                     : U5::mapPages(resource, data(resource));
    if (MAPS_LocationResource(resource.toLatin1().constData())) {
        auto virtualPages = extended;
        std::sort(virtualPages.begin(), virtualPages.end(),
                  [](const auto &a, const auto &b) { return a.page < b.page; });
        for (const auto &map : virtualPages)
            if (map.kind == 1 && map.resource == resource) {
                require(map.page == pages.size(), "Non-contiguous location floor pages");
                pages.append({map.name, map.width, 0, map.width, 0, map.floor, map.height});
            }
    }
    if (resource == "DUNGEON.DAT") {
        auto additions = extended;
        std::sort(additions.begin(), additions.end(),
                  [](const auto &a, const auto &b) { return a.page < b.page; });
        for (const auto &m : additions)
            if (m.kind == 4 && m.id > 40) {
                require(m.page == pages.size(), "Non-contiguous dungeon floor pages");
                pages.append({m.name + QString(" — Level %1").arg(m.floor + 1), m.width, 0, m.width,
                              int(m.id) - 33, m.floor, m.height});
            }
    }
    for (int i = 0; i < pages.size(); ++i)
        if (const auto *map = Workshop::findExtendedMap(extended, resource, i)) {
            pages[i].side = map->width;
            pages[i].rows = map->height;
        }
    const QStringList banks{"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"};
    int bank = banks.indexOf(resource);
    for (auto &page : pages) {
        if (resource == "DUNGEON.DAT")
            page.name = locationName(33 + page.settlement) + " — " + page.name.section(" — ", 1);
        else if (bank >= 0)
            page.name =
                locationName(bank * 8 + page.settlement + 1) + " — " + page.name.section(" — ", 1);
        else if (resource == "BRIT.DAT" || resource == "UNDER.DAT")
            page.name = locationName(resource == "BRIT.DAT" ? 0 : 41);
    }
    return pages;
}
QStringList Project::changed() const {
    QStringList list;
    for (auto it = resources.begin(); it != resources.end(); ++it)
        if (it->edited != it->original || it->included)
            list << it.key();
    return list;
}
void Project::validate() const {
    Workshop::validateExtendedMaps(*this);
    Workshop::validateGates(*this);
    require(!title.trimmed().isEmpty() && title.toUtf8().size() <= 256 && !title.contains(QChar(0)),
            "Mod name must contain 1–256 UTF-8 bytes");
    if (changed().contains("TILE_SHW.PNG") || changed().contains("TILE_SHN.PNG")) {
        QSet<QRgb> colors;
        for (const QString &name : {QString("TILE_SHW.PNG"), QString("TILE_SHN.PNG")})
            for (const auto &image : U5::readGraphics(name, data(name)).images)
                for (int y = 0; y < image.height(); y++)
                    for (int x = 0; x < image.width(); x++)
                        colors.insert(image.pixel(x, y));
        require(colors.size() <= 240, "Sharp terrain and actors share a 240-color palette");
    }
    for (const auto &name : changed()) {
        const auto &b = data(name);
        require(!b.isEmpty() && b.size() <= MOD_MAX_RESOURCE, "Invalid resource size: " + name);
        Workshop::validateResource(*this, name);
    }
}
static void append32(QByteArray &b, unsigned v) {
    for (int i = 0; i < 4; i++)
        b.append(char(v >> (8 * i)));
}
static bool usesMasks(const Project &project) {
    bool masks = false;
    for (const auto &name : project.changed())
        masks |= MOD_IsTilesetMask(name.toLatin1().constData());
    for (const auto &record : CustomTiles::records(project))
        for (size_t off = 256; off < size_t(record.size());
             off +=
             TYPES_FrameBytes(reinterpret_cast<const unsigned char *>(record.constData()), off))
            masks |= (uint8_t(record[int(off) + 1]) & 128) != 0;
    return masks;
}
static bool usesExplicitDialogue(const Project &project) {
    for (const auto &name : project.changed()) {
        if (!name.endsWith(".TLK"))
            continue;
        for (const auto &conversation : U5::readDialogue(project.data(name)))
            for (const auto &entry : Dialogue::parse(conversation.bytes).entries) {
                const int prefix = entry.bytes.startsWith(char(144)) ? 2 : 0;
                if (entry.bytes.mid(prefix, IMPERA_TALK_EXPLICIT_QUOTES_SIZE) ==
                    IMPERA_TALK_EXPLICIT_QUOTES)
                    return true;
            }
    }
    return false;
}
static bool usesExtendedDialogue(const Project &project) {
    for (const auto &name : project.changed())
        if (name.endsWith(".TLK") && project.data(name).startsWith("ITLK0001"))
            return true;
    return false;
}
static bool conversationFlag(const Project &project, unsigned flag) {
    for (const auto &name : project.changed())
        if (name.endsWith(".TLK"))
            for (const auto &conversation : U5::readDialogue(project.data(name)))
                if (IMPERA_TalkConversationFlags(conversation.bytes.constData(),
                                                 conversation.bytes.size()) &
                    flag)
                    return true;
    return false;
}
QByteArray Project::package() const {
    validate();
    auto list = changed();
    for (const auto &name : list)
        require(MOD_AllowedResource(name.toLatin1().constData()),
                "This engine version does not support exporting " + name);
    require(!list.isEmpty(), "There are no changes to export");
    require(list.size() <= MOD_MAX_ENTRIES, "Too many modified resources");
    bool actorTypes = false, largeActors = false, animationOptions = false;
    for (const auto &record : CustomTiles::records(*this)) {
        actorTypes |= record.size() > 8 && record[8] != 0;
        animationOptions |= record.size() >= 256 &&
                            (record[255] || (record[8] && TYPES_U32(record.constData() + 4) >= 512));
        largeActors |= record.size() >= 256 && (record[253] || record[254]);
        for (const auto &frame : CustomTiles::frames(record))
            largeActors |= frame.variant >= 6;
    }
    bool extraNpcs = false;
    for (const auto &resource : list)
        extraNpcs |= resource.endsWith(".NPC") && data(resource).size() > 4608;
    for (const auto &cell : CustomTiles::placements(*this))
        extraNpcs |= cell.kind == 3 && (cell.x >= 32 || cell.y);
    bool locationObjects = false, specialActors = false;
    for (const auto &name : list) {
        const auto b = data(name);
        if (name.endsWith(".NPC"))
            for (unsigned town = 0; town < 8; town++)
                for (unsigned slot = 1; slot < NPCS_Count(b.constData(), b.size(), town); slot++) {
                    const auto off = NPCS_Offset(b.constData(), b.size(), town, slot);
                    specialActors |=
                        NPCS_ObjectKind(b.constData() + off,
                                        U5::byte(b, NPCS_ShapeOffset(b.constData(), b.size(), town,
                                                                     slot))) >= 24;
                    locationObjects |=
                        NPCS_ObjectKind(b.constData() + off,
                                        U5::byte(b, NPCS_ShapeOffset(b.constData(), b.size(), town,
                                                                     slot))) >= 0;
                }
        if (name.endsWith(".OOL"))
            for (int i = 0; i < 32; i++)
                locationObjects |=
                    NPCS_Blood(U5::byte(b, i * 8), U5::byte(b, i * 8 + 5), U5::byte(b, i * 8 + 7));
    }
    bool dungeonMaps = false, newDungeons = false, wideRooms = false;
    for (const auto &map : Workshop::extendedMaps(*this)) {
        dungeonMaps |= map.kind == 4;
        newDungeons |= map.kind == 4 && map.id > 40;
        wideRooms |= ((map.kind == 2 || map.kind == 3) && map.id > 255) || map.roomBase > 240;
    }
    for (const auto &contents : Workshop::readContents(resources.value("CONTENTS.OVR").edited))
        newDungeons |= contents.kind == 2 && contents.map >= 112;
    wideRooms |= Workshop::extendedMaps(*this).size() > 512;
    for (const auto &contents : Workshop::readContents(resources.value("CONTENTS.OVR").edited))
        wideRooms |= contents.map > 255;
    const auto bindings = resources.value("TYPEMAP.OVR").edited;
    for (int off = 32; off + 16 <= bindings.size(); off += 16)
        wideRooms |= U5::byte(bindings, off + 5) != 0;
    bool portableConditions = false;
    for (const auto &map : Workshop::extendedMaps(*this))
        portableConditions |= (map.kind == 1 || map.kind == 4) && map.id <= 40 && map.world != -2;
    for (const auto &map : Workshop::extendedMaps(*this))
        portableConditions |= map.kind == 1 && MAPS_NewLocation(map.id) && map.floor != 0;
    for (const auto &contents : Workshop::readContents(resources.value("CONTENTS.OVR").edited))
        portableConditions |= contents.refill > 2;
    const bool masks = usesMasks(*this);
    QByteArray out(
        list.contains("MODINFO.DAT") ? "IMOD0027"
        : list.contains("INTERFACE.OVR") ? "IMOD0026"
        : masks                        ? "IMOD0025"
        : list.contains("GATES.OVR") ? "IMOD0024"
        : portableConditions         ? "IMOD0023"
        : wideRooms                  ? "IMOD0022"
        : newDungeons                ? "IMOD0021"
        : dungeonMaps                ? "IMOD0020"
        : specialActors              ? "IMOD0019"
        : (locationObjects || list.contains("MAPS.OVR") ||
           resources.value("SIGNS.DAT").edited.startsWith("ISIG0001"))
            ? "IMOD0018"
        : list.contains("LOCATIONS.OVR")                             ? "IMOD0017"
        : conversationFlag(*this, IMPERA_TALK_FLAG_GARGOYLE_SPEECH)  ? "IMOD0016"
        : conversationFlag(*this, IMPERA_TALK_FLAG_SKIP_OPENING)     ? "IMOD0015"
        : usesExtendedDialogue(*this)                                ? "IMOD0014"
        : animationOptions                                           ? "IMOD0013"
        : extraNpcs                                                  ? "IMOD0012"
        : list.contains("TYPES.OVR") || list.contains("TYPEMAP.OVR") ? (largeActors  ? "IMOD0011"
                                                                        : actorTypes ? "IMOD0010"
                                                                                     : "IMOD0009")
        : list.contains("CUTSCENE.OVR")
            ? (data("CUTSCENE.OVR").startsWith("ICUT0003")
                   ? "IMOD0008"
                   : (data("CUTSCENE.OVR").startsWith("ICUT0002") ? "IMOD0007" : "IMOD0006"))
        : usesExplicitDialogue(*this)   ? "IMOD0005"
        : list.contains("CONTENTS.OVR") ? "IMOD0004"
        : list.contains("RULES.TBL")    ? "IMOD0003"
        : list.contains("TEXT.OVR")     ? "IMOD0002"
                                        : "IMOD0001",
        8),
        nameBytes = title.toUtf8();
    append32(out, nameBytes.size());
    append32(out, list.size());
    out += nameBytes;
    size_t totalOutput = 0;
    for (auto name : list) {
        totalOutput += resources[name].edited.size();
        require(totalOutput <= MOD_MAX_PACKAGE, "Decoded package exceeds 32 MiB");
        const auto &r = resources[name];
        const QByteArray base =
            MOD_TilesetSheet(name.toLatin1().constData()) ? QByteArray() : r.original;
        QByteArray spans;
        unsigned count = 0;
        for (int i = 0; i < r.edited.size();) {
            if (i < base.size() && r.edited[i] == base[i]) {
                ++i;
                continue;
            }
            int begin = i++;
            while (i < r.edited.size() && (i >= base.size() || r.edited[i] != base[i]))
                ++i;
            append32(spans, begin);
            append32(spans, i - begin);
            spans += r.edited.mid(begin, i - begin);
            ++count;
        }
        QByteArray n = name.toLatin1();
        append32(out, n.size());
        out += n;
        append32(out, base.size());
        append32(out, r.edited.size());
        append32(out, MOD_Crc32(base.constData(), base.size()));
        append32(out, MOD_Crc32(r.edited.constData(), r.edited.size()));
        append32(out, count);
        out += spans;
    }
    require(out.size() <= MOD_MAX_PACKAGE, "Package exceeds 32 MiB");
    return out;
}
static int readBase(void *context, const char *name, unsigned char **data, size_t *size) {
    auto p = static_cast<Project *>(context);
    auto it = p->resources.constFind(QString::fromLatin1(name));
    *data = nullptr;
    *size = 0;
    if (it == p->resources.cend())
        return 0;
    *size = it->original.size();
    *data = static_cast<unsigned char *>(malloc(*size ? *size : 1));
    if (!*data)
        return 0;
    memcpy(*data, it->original.constData(), *size);
    return 1;
}
void Project::importPackage(const QByteArray &bytes) {
    ModPackage decoded;
    char error[256];
    int ok =
        MOD_Decode(bytes.constData(), bytes.size(), readBase, this, &decoded, error, sizeof(error));
    require(ok, QString::fromUtf8(error));
    Project next = *this;
    next.title = QString::fromUtf8(decoded.title);
    for (unsigned i = 0; i < decoded.count; i++) {
        auto &e = decoded.entries[i];
        next.resources[QString::fromLatin1(e.name)].included =
            QStringList{"INIT.OOL", "BRIT.OOL", "UNDER.OOL"}.contains(QString::fromLatin1(e.name));
        next.resources[QString::fromLatin1(e.name)].edited =
            QByteArray(reinterpret_cast<char *>(e.data), e.size);
    }
    MOD_Free(&decoded);
    Workshop::hydrateExtendedMaps(next);
    next.validate();
    *this = next;
}
void Project::save(const QString &path) {
    validate();
    QJsonObject root{
        {"format", "impera-workshop-1"}, {"source", sourceDirectory}, {"title", title}};
    QJsonArray edits;
    for (auto name : changed()) {
        const auto &r = resources[name];
        const QByteArray base =
            MOD_TilesetSheet(name.toLatin1().constData()) ? QByteArray() : r.original;
        edits.append(QJsonObject{{"name", name},
                                 {"included", r.included},
                                 {"baseSize", base.size()},
                                 {"baseCrc", double(MOD_Crc32(base.constData(), base.size()))},
                                 {"bytes", QString::fromLatin1(r.edited.toBase64())}});
    }
    root["edits"] = edits;
    QJsonObject requirements;
    if (changed().contains("RULES.TBL")) {
        requirements["engineTables"] = 1;
        requirements["packageVersion"] = 3;
    }
    if (changed().contains("TEXT.OVR")) {
        requirements["narrativeOverrides"] = 1;
        if (!requirements.contains("packageVersion"))
            requirements["packageVersion"] = 2;
    }
    if (changed().contains("CONTENTS.OVR")) {
        requirements["mapContents"] = 1;
        requirements["packageVersion"] = 4;
    }
    if (usesExplicitDialogue(*this)) {
        requirements["dialogueQuoting"] = 1;
        requirements["packageVersion"] = 5;
    }
    if (changed().contains("CUTSCENE.OVR")) {
        requirements["cutscenePages"] = data("CUTSCENE.OVR").startsWith("ICUT0003") ? 3 : (data("CUTSCENE.OVR").startsWith("ICUT0002") ? 2 : 1);
        requirements["packageVersion"] = data("CUTSCENE.OVR").startsWith("ICUT0003") ? 8 : (data("CUTSCENE.OVR").startsWith("ICUT0002") ? 7 : 6);
    }
    if (changed().contains("TYPES.OVR") || changed().contains("TYPEMAP.OVR")) {
        bool actors = false;
        for (const auto &record : CustomTiles::records(*this))
            actors |= record.size() > 8 && record[8] != 0;
        requirements["customTypes"] = actors ? 2 : 1;
        requirements["packageVersion"] = actors ? 10 : 9;
    }
    if (usesExtendedDialogue(*this)) {
        requirements["extendedDialogue"] = 1;
        requirements["packageVersion"] = 14;
    }
    if (conversationFlag(*this, IMPERA_TALK_FLAG_SKIP_OPENING)) {
        requirements["conversationOpening"] = 1;
        requirements["packageVersion"] = 15;
    }
    if (conversationFlag(*this, IMPERA_TALK_FLAG_GARGOYLE_SPEECH)) {
        requirements["gargoyleSpeech"] = 1;
        requirements["packageVersion"] = 16;
    }
    if (changed().contains("LOCATIONS.OVR")) {
        requirements["locationNames"] = 1;
        requirements["packageVersion"] = 17;
    }
    if (changed().contains("MAPS.OVR")) {
        requirements["extendedMaps"] = 1;
        requirements["packageVersion"] = 18;
    }
    if (changed().contains("GATES.OVR")) {
        requirements["townGates"] = 1;
        requirements["packageVersion"] = 24;
    }
    if (usesMasks(*this)) {
        requirements["blackPixelMasks"] = 1;
        requirements["packageVersion"] = 25;
    }
    if (changed().contains("INTERFACE.OVR")) {
        requirements["interfaceColors"] = 1;
        requirements["packageVersion"] = 26;
    }
    if (changed().contains("MODINFO.DAT")) {
        requirements["modMetadata"] = 1;
        requirements["packageVersion"] = 27;
    }
    if (!requirements.isEmpty())
        root["requires"] = requirements;
    QJsonObject annotations;
    for (auto it = dialogueNames.cbegin(); it != dialogueNames.cend(); ++it)
        annotations.insert(it.key(), it.value());
    root["dialogueNames"] = annotations;
    QSaveFile f(path);
    require(f.open(QIODevice::WriteOnly), f.errorString());
    auto json = QJsonDocument(root).toJson();
    require(f.write(json) == json.size() && f.commit(), "Cannot save project: " + f.errorString());
    projectPath = path;
}
void Project::load(const QString &path) {
    auto bytes = readLimited(path, 64 * 1024 * 1024);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(),
            "Invalid Workshop project JSON");
    auto root = doc.object();
    require(root["format"] == "impera-workshop-1", "Unsupported Workshop project version");
    Project next;
    next.openGame(root["source"].toString());
    next.title = root["title"].toString();
    if (root.contains("dialogueNames")) {
        require(root["dialogueNames"].isObject(), "Invalid question annotations");
        auto annotations = root["dialogueNames"].toObject();
        require(annotations.size() <= 4096, "Too many question annotations");
        for (auto it = annotations.begin(); it != annotations.end(); ++it) {
            require(it.key().size() <= 100 && it.value().isString() &&
                        it.value().toString().size() <= 120,
                    "Invalid question annotation");
            next.dialogueNames.insert(it.key(), it.value().toString());
        }
    }
    QSet<QString> names;
    require(root["edits"].isArray() && root["edits"].toArray().size() <= MOD_MAX_ENTRIES,
            "Invalid project edit list");
    for (auto value : root["edits"].toArray()) {
        auto e = value.toObject();
        if (e["name"].toString() == "MAPS.OVR") {
            auto bytes = QByteArray::fromBase64(e["bytes"].toString().toLatin1());
            if (bytes.isEmpty())
                bytes = QByteArray::fromBase64(e["edited"].toString().toLatin1());
            next.resources["MAPS.OVR"].edited = bytes;
            Workshop::hydrateExtendedMaps(next);
        }
    }
    for (auto value : root["edits"].toArray()) {
        auto edit = value.toObject();
        QString name = edit["name"].toString();
        require(next.resources.contains(name) && !names.contains(name),
                "Missing or duplicate resource in project");
        names.insert(name);
        auto &r = next.resources[name];
        const QByteArray base =
            MOD_TilesetSheet(name.toLatin1().constData()) ? QByteArray() : r.original;
        require(edit["baseSize"].toInt(-1) == base.size() &&
                    edit["baseCrc"].toDouble(-1) == MOD_Crc32(base.constData(), base.size()),
                "Original files changed since project creation: " + name);
        auto decoded = QByteArray::fromBase64Encoding(edit["bytes"].toString().toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
        require(bool(decoded) && decoded.decoded.size() <= MOD_MAX_RESOURCE,
                "Invalid project resource bytes");
        r.edited = decoded.decoded;
        require(!edit.contains("included") || edit["included"].isBool(), "Invalid inclusion flag");
        r.included = edit["included"].toBool();
    }
    next.validate();
    next.projectPath = path;
    *this = next;
}

QString imperaModsDirectory() {
    const auto override = qEnvironmentVariable("U5D_MODS_DIR");
    if (!override.isEmpty())
        return override;
    auto engine = QSettings().value("paths/engine").toString();
    QString folder;
    if (QFileInfo(engine).exists())
        folder = QFileInfo(engine).absolutePath();
    else if (!qEnvironmentVariable("APPIMAGE").isEmpty())
        folder = QFileInfo(qEnvironmentVariable("APPIMAGE")).absolutePath();
    else
        folder = QCoreApplication::applicationDirPath();
    const int bundle = folder.indexOf(".app/Contents/");
    if (bundle >= 0)
        folder = QFileInfo(folder.left(bundle + 4)).absolutePath();
    return QDir(folder).filePath("Mods");
}
QVector<ModDiagnostic> validateMod(const Project &project, const QString &modsDirectory) {
    QVector<ModDiagnostic> result;
    try {
        project.validate();
    } catch (const std::exception &error) {
        result.append({ModDiagnostic::Error, {}, QString::fromUtf8(error.what())});
    }
    const auto changes = project.changed();
    if (changes.isEmpty())
        result.append({ModDiagnostic::Warning,
                       {},
                       "No modified resources; there is no package to export or test."});
    for (const auto &name : changes) {
        try {
            if (name == "TYPES.OVR" || name == "TYPEMAP.OVR" || name == "CUTSCENE.OVR" ||
                name == "MAPS.OVR" || MAPS_LocationResource(name.toLatin1().constData()) ||
                name == "LOCATIONS.OVR" || name == "TEXT.OVR" || name == "RULES.TBL" ||
                name == "MODINFO.DAT" || name == "INTERFACE.OVR" || name == "GATES.OVR" || name == "CONTENTS.OVR") {
                require(project.resources[name].original.isEmpty(),
                        "Companion resource must have an empty original base");
            } else if (!MOD_TilesetSheet(name.toLatin1().constData())) {
                QString actual;
                QDir source(project.sourceDirectory);
                for (const auto &file : source.entryList(QDir::Files))
                    if (file.toUpper() == name) {
                        actual = source.filePath(file);
                        break;
                    }
                require(
                    !actual.isEmpty() &&
                        readLimited(actual, MOD_MAX_RESOURCE) == project.resources[name].original,
                    "Original game file changed or is missing since this project opened. Reopen "
                    "the project against the intended base files.");
            }
        } catch (const std::exception &error) {
            result.append({ModDiagnostic::Error, name, QString::fromUtf8(error.what())});
        }
        try {
            Project single = project;
            single.title = "Validation";
            for (auto it = single.resources.begin(); it != single.resources.end(); ++it)
                if (it.key() != name && !(name == "BRIT.DAT" && it.key() == "DATA.OVL"))
                    { it->original = it->edited; it->included = false; }
            single.validate();
        } catch (const std::exception &error) {
            result.append({ModDiagnostic::Error, name, QString::fromUtf8(error.what())});
        }
        if (name == "CONTENTS.OVR")
            result.append({ModDiagnostic::Information, name,
                           "Requires contents schema v1 / package v4. Disjoint contents packages "
                           "coexist; overlapping targets or identities reject the later package."});
        else
            result.append({ModDiagnostic::Information, name,
                           QString("Package replaces this entire resource (%1 bytes → %2 bytes).")
                               .arg(project.resources[name].original.size())
                               .arg(project.data(name).size())});
        if (name == "CUTSCENE.OVR")
            result.append({ModDiagnostic::Information, name, "Requires an Impera build supporting the exported cutscene schema. Older versions reject this package."});
        if (name == "RULES.TBL")
            result.append(
                {ModDiagnostic::Information, name,
                 "Requires engine table schema v1 / package v3. Verified shop prices, stock, and "
                 "location entrances apply at runtime. Compiled defaults remain unchanged; omitted "
                 "entries inherit them. Older engines reject the package."});
        if (name == "TEXT.OVR")
            result.append({ModDiagnostic::Information, name,
                           "Requires Impera with narrative override v1 / package v2 support. Older "
                           "engines reject the package. Entries take precedence over DAT text; "
                           "another TEXT.OVR package is a whole-resource conflict."});
        if (name == "MISCMAPS.DAT")
            result.append({ModDiagnostic::Information, name,
                           "Scene terrain and verified creation bytecode apply at runtime. "
                           "Actors/timing in other scenes remain coded; use Test in Impera to "
                           "check animation and scene layout."});
        if (name.endsWith(".HCS"))
            result.append({ModDiagnostic::Warning, name, Workshop::capability(name).status});
        if (name == "INIT.GAM" || name == "INIT.OOL" || name == "BRIT.OOL" || name == "UNDER.OOL")
            result.append({ModDiagnostic::Information, name,
                           "Applies to new games only. Existing saves keep their saved party and object state. "
                           "New games use an empty Britannia list unless BRIT.OOL is overridden; "
                           "UNDER.OOL overrides take precedence over INIT.OOL. World starts use "
                           "that world's object list; settlement actors remain in INIT.GAM."});
        if (name.endsWith(".OOL")) {
            const auto objects = project.data(name);
            if (objects.size() == 256) {
                for (int slot = 0; slot < 32; ++slot) {
                    int tile = U5::byte(objects, slot*8);
                    if (!tile) continue;
                    int floor = U5::byte(objects, slot*8+4);
                    if (slot == 0 || floor != (name == "BRIT.OOL" ? 0 : 255) ||
                        !QList<int>{1,2,15,16,17,25,27,30,36,37,38,39,40,41,42,43}.contains(tile))
                        result.append({ModDiagnostic::Warning, name,
                            QString("Object slot %1 contains an unusual/reserved shape or floor. Preserved; inspect Advanced details and test a fresh game.").arg(slot),
                            QString("object/%1").arg(slot)});
                }
            }
        }
        if (name == "INIT.OOL" && changes.contains("UNDER.OOL"))
            result.append({ModDiagnostic::Warning, name,
                           "UNDER.OOL overrides this initial underworld object list in new games."});
        if (name == "DATA.OVL") {
            result.append({ModDiagnostic::Information, name,
                           "Britannia map index supported by Impera; other DOS overlay tables "
                           "are not runtime configuration. Edit Shops to export "
                           "RULES.TBL; consult the coverage inventory for unresolved spans."});
            auto before = project.resources[name].original;
            auto after = project.data(name);
            if (before.size() >= int(U5_WORLD_INDEX_OFFSET + U5_WORLD_INDEX_SIZE) &&
                after.size() >= int(U5_WORLD_INDEX_OFFSET + U5_WORLD_INDEX_SIZE)) {
                before.replace(U5_WORLD_INDEX_OFFSET, U5_WORLD_INDEX_SIZE, QByteArray(256, 0));
                after.replace(U5_WORLD_INDEX_OFFSET, U5_WORLD_INDEX_SIZE, QByteArray(256, 0));
                if (before != after)
                    result.append({ModDiagnostic::Warning, name,
                                   "Changes outside the Britannia map index have no supported runtime effect."});
            }
        }
        const auto &bytes = project.data(name);
        if (name.endsWith(".NPC") && bytes.size() >= 4608) {
            int unusual = 0;
            for (int settlement = 0; settlement < 8; ++settlement)
                for (int actor = 0;
                     actor < int(NPCS_Count(bytes.constData(), bytes.size(), settlement));
                     ++actor) {
                    int base = NPCS_Offset(bytes.constData(), bytes.size(), settlement, actor);
                    for (int time = 0; time < 4; ++time)
                        unusual += U5::byte(bytes, base + 12 + time) > 23;
                    for (int position = 0; position < 3; ++position)
                        unusual += U5::byte(bytes, base + 3 + position) > 31 ||
                                   U5::byte(bytes, base + 6 + position) > 31;
                }
            if (unusual)
                result.append({ModDiagnostic::Warning, name,
                               QString("%1 unusual schedule times or destinations. These values "
                                       "are preserved; review them in Advanced schedule details.")
                                   .arg(unusual)});
        }
    }
    // Match the engine: alphabetic scan, packages decoded against original bytes,
    // and any overlapping resource causes that package to be rejected in full.
    Project mounted = project;
    for (auto it = mounted.resources.begin(); it != mounted.resources.end(); ++it)
        it->edited = it->original;
    QSet<QString> overrides;
    QVector<ContentsDef> mountedContents;
    const auto contentsConflict = [](const ContentsDef &a, const ContentsDef &b) {
        return (a.kind == b.kind && a.map == b.map && a.floor == b.floor && a.x == b.x &&
                a.y == b.y) ||
               (!memcmp(a.space, b.space, 16) && a.id == b.id) ||
               (a.original != 65535 && a.original == b.original);
    };
    unsigned mountedCount = 0;
    size_t mountedMemory = 0;
    QDir mods(modsDirectory.isEmpty() ? imperaModsDirectory() : modsDirectory);
    if (!mods.exists()) {
        QDir source(QFileInfo(mods.path()).absolutePath());
        for (const auto &folder : source.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (folder.compare("Mods", Qt::CaseInsensitive) == 0) {
                mods.setPath(source.filePath(folder));
                break;
            }
    }
    if (mods.exists()) {
        QSet<QString> disabledMods;
        QFile preferences(mods.filePath("mods.cfg"));
        if (preferences.open(QIODevice::ReadOnly) && preferences.readLine() == "IMODS001\n") {
            while (!preferences.atEnd()) {
                auto line = preferences.readLine(2050);
                if (!line.endsWith('\n')) continue;
                line.chop(1);
                bool valid = !line.isEmpty() && line.size() % 2 == 0;
                for (char c : line) valid &= (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                if (valid) disabledMods.insert(QString::fromUtf8(QByteArray::fromHex(line)));
            }
        }
        auto installed = mods.entryList(QDir::Files, QDir::NoSort);
        std::sort(installed.begin(), installed.end(),
                  [](const QString &a, const QString &b) { return a.toUtf8() < b.toUtf8(); });
        for (const auto &file : installed) {
            if (!file.endsWith(".imperamod", Qt::CaseInsensitive))
                continue;
            if (disabledMods.contains(file)) {
                result.append({ModDiagnostic::Information, {}, file + ": disabled in Impera."});
                continue;
            }
            try {
                require(mountedCount < 128, "Engine package limit (128) exceeded");
                auto bytes = readLimited(mods.filePath(file), MOD_MAX_PACKAGE);
                ModPackage decoded{};
                char error[256];
                require(MOD_Decode(bytes.constData(), bytes.size(), readBase, &mounted, &decoded,
                                   error, sizeof(error)),
                        QString::fromUtf8(error));
                QStringList names;
                size_t memory = 0;
                for (unsigned i = 0; i < decoded.count; ++i)
                    memory += decoded.entries[i].size;
                for (unsigned i = 0; i < decoded.count; ++i)
                    names.append(QString::fromLatin1(decoded.entries[i].name));
                QVector<ContentsDef> incomingContents;
                for (unsigned i = 0; i < decoded.count; i++)
                    if (!strcmp(decoded.entries[i].name, "CONTENTS.OVR"))
                        incomingContents = Workshop::readContents(
                            QByteArray(reinterpret_cast<const char *>(decoded.entries[i].data),
                                       decoded.entries[i].size));
                MOD_Free(&decoded);
                bool conflict = mountedContents.size() + incomingContents.size() > CONTENTS_MAX;
                for (const auto &a : incomingContents)
                    for (const auto &b : mountedContents)
                        conflict |= contentsConflict(a, b);
                for (const auto &name : names)
                    if (name != "CONTENTS.OVR" && name != "MODINFO.DAT")
                        conflict |= overrides.contains(name);
                if (conflict) {
                    result.append(
                        {ModDiagnostic::Warning,
                         {},
                         file + ": overlaps an earlier installed package; the engine rejects it."});
                    continue;
                }
                require(memory <= 64u * 1024u * 1024u - mountedMemory,
                        "Engine total mod memory limit (64 MiB) exceeded");
                mountedContents += incomingContents;
                ++mountedCount;
                mountedMemory += memory;
                result.append({ModDiagnostic::Information,
                               {},
                               QString("Installed load order %1: %2 — %3")
                                   .arg(mountedCount)
                                   .arg(file)
                                   .arg(names.join(", "))});
                for (const auto &name : names) {
                    if (name == "CONTENTS.OVR") {
                        const auto authored =
                            Workshop::readContents(project.resources.value(name).edited);
                        bool overlap = false;
                        for (const auto &a : incomingContents)
                            for (const auto &b : authored)
                                overlap |= contentsConflict(a, b);
                        if (overlap)
                            result.append({ModDiagnostic::Warning, name,
                                           "Installed package " + file +
                                               " overrides the same contents target or identity."});
                        continue;
                    }
                    if (name == "MODINFO.DAT") continue;
                    overrides.insert(name);
                    if (changes.contains(name))
                        result.append({ModDiagnostic::Warning, name,
                                       "Installed package " + file +
                                           " also replaces this resource. Impera rejects "
                                           "overlapping packages; remove the conflicting package "
                                           "when installing this mod."});
                }
            } catch (const std::exception &error) {
                result.append(
                    {ModDiagnostic::Warning,
                     {},
                     file + ": installed package cannot load: " + QString::fromUtf8(error.what())});
            }
        }
    }
    if (!changes.isEmpty()) {
        try {
            auto bytes = project.package();
            Project roundtrip = mounted;
            roundtrip.importPackage(bytes);
            for (const auto &name : changes)
                require(roundtrip.data(name) == project.data(name),
                        "Package verification changed " + name);
            result.append({ModDiagnostic::Information,
                           {},
                           QString("Package verified by engine decoder: %1 bytes. Test Mod uses "
                                   "only this package, not installed mods.")
                               .arg(bytes.size())});
        } catch (const std::exception &error) {
            result.append({ModDiagnostic::Error,
                           {},
                           "Package verification: " + QString::fromUtf8(error.what())});
        }
    }
    bool dungeonDimensionsChanged = false;
    if (changes.contains("MAPS.OVR"))
        for (const auto &m : Workshop::extendedMaps(project))
            dungeonDimensionsChanged |= m.kind == 4;
    if (changes.contains("DUNGEON.DAT") || dungeonDimensionsChanged) {
        try {
            Workshop::DungeonDocument dungeon(&project);
            int count = 0;
            for (int id : Workshop::dungeonIds(project))
                for (int d = id - 33, f = 0; f < 8 && count < 200; f++) {
                    auto dims = dungeon.size(d, f);
                    auto cells = dungeon.level(d, f), original = dungeon.level(d, f, true);
                    for (int y = 0; y < dims.height() && count < 200; y++)
                        for (int x = 0; x < dims.width() && count < 200; x++) {
                            int value = U5::byte(cells, y * dims.width() + x), kind = value >> 4;
                            if (kind != 1 && kind != 2 && kind != 3 && kind != 9 && kind != 10 &&
                                kind != 15 && !(value & 7))
                                continue;
                            if (x < 8 && y < 8 && value == U5::byte(original, y * 8 + x) &&
                                !dungeonDimensionsChanged) {
                                auto changedOther = [&](int other) {
                                    auto size = dungeon.size(d, other);
                                    if (x >= size.width() || y >= size.height())
                                        return true;
                                    return U5::byte(dungeon.level(d, other),
                                                    y * size.width() + x) !=
                                           U5::byte(dungeon.level(d, other, true), y * 8 + x);
                                };
                                if (!(((kind == 1 || kind == 3) && f > 0 && changedOther(f - 1)) ||
                                      ((kind == 2 || kind == 3) && f < 7 && changedOther(f + 1))))
                                    continue;
                            }
                            for (auto warning : dungeon.warnings(d, f, x, y)) {
                                result.append({ModDiagnostic::Warning, "DUNGEON.DAT",
                                               QString("Dungeon %1 · Level %2 · X %3 Y %4: %5")
                                                   .arg(d + 1)
                                                   .arg(f + 1)
                                                   .arg(x)
                                                   .arg(y)
                                                   .arg(warning),
                                               QString("dungeon/%1/level/%2/cell/%3")
                                                   .arg(d)
                                                   .arg(f)
                                                   .arg(y * dims.width() + x)});
                                count++;
                            }
                        }
                }
        } catch (const std::exception &) {
        }
    }
    return result;
}
QString prepareModTest(const Project &project, const QString &sessionsDirectory) {
    const auto package = project.package();
    require(QDir().mkpath(sessionsDirectory), "Cannot create test-session folder");
    QTemporaryDir session(QDir(sessionsDirectory).filePath("test-XXXXXX"));
    require(session.isValid(), "Cannot create isolated test session");
    QDir source(project.sourceDirectory), destination(session.path());
    // Flat DOS runtime files only. Never copy personal saves, settings or installed Mods.
    const QSet<QString> excluded{"DATA.CFG", "ENGINE.CFG", "LOG.TXT", "SAVED.GAM", "SAVED.OOL"};
    qint64 total = 0;
    for (const auto &name : source.entryList(QDir::Files, QDir::Name)) {
        if (excluded.contains(name.toUpper()) || QFileInfo(source.filePath(name)).isSymLink())
            continue;
        const QString suffix = QFileInfo(name).suffix().toUpper();
        if (!QStringList{"DAT", "OVL", "BIT", "PTH", "CH", "16", "OOL", "GAM", "TLK", "NPC", "CBT"}
                 .contains(suffix))
            continue;
        const QString target = name.toUpper();
        QFileInfo info(source.filePath(name));
        total += info.size();
        require(total <= 512 * 1024 * 1024,
                "Game folder exceeds test-copy limit; choose a clean DOS game folder");
        require(QFile::copy(source.filePath(name), destination.filePath(target)),
                "Cannot copy test resource " + name);
        require(QFile::setPermissions(destination.filePath(target),
                                      QFile::ReadOwner | QFile::WriteOwner),
                "Cannot make test file writable");
    }
    for (const auto &name : project.resources.keys()) {
        if (name == "TYPES.OVR" || name == "TYPEMAP.OVR" || name == "CUTSCENE.OVR" ||
            name == "MAPS.OVR" || MAPS_LocationResource(name.toLatin1().constData()) ||
            name == "LOCATIONS.OVR" || name == "TEXT.OVR" || name == "RULES.TBL" ||
            name == "MODINFO.DAT" || name == "INTERFACE.OVR" || name == "GATES.OVR" || name == "CONTENTS.OVR" || MOD_TilesetSheet(name.toLatin1().constData()))
            continue; // Virtual companion exists only inside the package.
        auto bytes = readLimited(destination.filePath(name), MOD_MAX_RESOURCE);
        require(bytes == project.resources[name].original,
                "Original resource changed since this project opened: " + name);
    }
    require(destination.mkpath("Mods"), "Cannot create test Mods folder");
    auto save = [&](const QString &name, const QByteArray &bytes) {
        QSaveFile file(destination.filePath(name));
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
                    file.commit(),
                "Cannot write test file " + name);
    };
    save("Mods/workshop.imperamod", package);
    save("DATA.CFG", (session.path() + "\n\n").toUtf8());
    save("WORKSHOP-TEST.txt",
         "Isolated Impera Workshop test session. Game files, Mods, settings, saves and logs here "
         "are disposable. Original files were not modified.\n");
    session.setAutoRemove(false); // Keep saves/logs available, including after Workshop exits.
    return session.path();
}
