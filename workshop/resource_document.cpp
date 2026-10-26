#include "mod/metadata.h"
#include "mod/interface.h"
#include "extended_maps.h"
#include "mod/location_names.h"
#include "mod/npcs.h"
#include "resource_document.h"
#include "contents_editor.h"
#include "gates_editor.h"
#include "custom_tiles.h"
#include "dialogue.h"
#include "dungeon_editor.h"
#include "engine_tables.h"
#include "font_editor.h"
#include "location_text.h"
#include "mod/contents.h"
#include "mod/cutscenes.h"
#include "mod/tileset_resources.h"
#include "mod/world_resources.h"
#include "narrative_editor.h"
#include "scene_editor.h"
#include "world_objects.h"
using U5::require;
namespace Workshop {
Capability capability(const QString &resource) {
    if (resource == "MODINFO.DAT")
        return {"Mod details", "Project", "Edited in Mod Overview", EditorKind::Inspector};
    if (resource == "INTERFACE.OVR")
        return {"Interface", "Graphics", "Frame color", EditorKind::Inspector};
    if (resource == "TYPES.OVR")
        return {"Custom types", "Types", "Tile and creature artwork and properties", EditorKind::TileTypes};
    const QString n = resource.toUpper();
    if (QStringList{"IBM.CH", "RUNES.CH", "IBM.HCS", "RUNES.HCS"}.contains(n))
        return {n.startsWith("RUNES") ? "Runic font" : "Normal font", "Fonts",
                n.endsWith(".HCS") ? "Legacy Hercules font — not used by the SDL color renderer"
                                    : "Editable game font", EditorKind::Fonts};
    if (n == "DATA.OVL" || n == "RULES.TBL")
        return {n == "DATA.OVL" ? "Engine tables and coverage" : "Shops", "Engine tables",
                "Verified concepts use RULES.TBL; raw overlay coverage is explicitly classified",
                EditorKind::Tables};
    if (MOD_TilesetSheet(n.toLatin1().constData()))
        return {n, "Tilesets", "Editable alternate tileset", EditorKind::Artwork};
    if (n.endsWith(".16"))
        return {n, "Artwork", "Editable palette artwork", EditorKind::Artwork};
    if (n.endsWith(".TLK"))
        return {n, "Conversations", "Editable structured dialogue", EditorKind::Conversation};
    if (n.endsWith(".NPC"))
        return {n, "NPC schedules", "Editable daily routines", EditorKind::Schedule};
    if (QStringList{"INIT.OOL", "BRIT.OOL", "UNDER.OOL"}.contains(n))
        return {n == "INIT.OOL" ? "Initial underworld objects" : n == "BRIT.OOL" ? "Britannia starting objects" : "Underworld starting objects",
                "Starting state", "Effective world objects for new games; existing saves unchanged", EditorKind::WorldObjects};
    if (n == "INIT.GAM")
        return {"Starting state", "Starting state", "Applied to new games", EditorKind::State};
    if (QStringList{"QUESTION.DAT", "END.DAT", "KARMA.DAT", "SHOPPE.DAT"}.contains(n))
        return {n == "QUESTION.DAT" ? "Character creation"
                : n == "END.DAT"    ? "Ending"
                : n == "KARMA.DAT"  ? "Karma speeches"
                                    : "Shops",
                "Narrative", "Editable fixed entries and optional extended text",
                EditorKind::Narrative};
    if (n == "MISCMAPS.DAT")
        return {"Special scenes", "Scenes",
                "Editable backgrounds and verified creation instructions", EditorKind::Scenes};
    if (n == "MISCMSG.DAT" || n == "ENDMSG.DAT")
        return {n == "MISCMSG.DAT" ? "Shrine / Codex / Blackthorn messages"
                                   : "Final rescue messages",
                "Scenes", "Editable runtime message entries", EditorKind::Narrative};
    if (n == "GATES.OVR")
        return {"Town gates", "World", "Edited through the map inspector", EditorKind::Inspector};
    if (n == "MAPS.OVR")
        return {"Maps", "World", "Map geometry", EditorKind::Inspector};
    if (n == "LOCATIONS.OVR")
        return {"Location names", "World", "Edited through map headers", EditorKind::Inspector};
    if (n == "TEXT.OVR")
        return {"Extended narrative text", "Narrative",
                "Versioned companion; edit through its source narrative document",
                EditorKind::Inspector};
    if (n == "STORY.DAT")
        return {"Introduction story", "Story", "Illustrated page sequence", EditorKind::Story};
    if (n == "SIGNS.DAT" || n == "LOOK2.DAT")
        return {n == "SIGNS.DAT" ? "Signs" : "Look descriptions", "Text", "Editable location text", EditorKind::LocationText};
    if (n == "DUNGEON.DAT")
        return {"Dungeons", "Maps", "Editable dungeon features and room links",
                EditorKind::Dungeon};
    if ((MAPS_LocationResource(n.toLatin1().constData()) && n.endsWith(".DAT")) ||
        n.endsWith(".CBT") ||
        QStringList{"BRIT.DAT", "UNDER.DAT", "TOWNE.DAT", "DWELLING.DAT", "KEEP.DAT", "CASTLE.DAT"}
            .contains(n))
        return {n, "Maps", "Editable terrain", EditorKind::Map};
    static const QMap<QString, QString> titles = {{"DUNGEON.DAT", "Dungeon levels"},
                                                  {"SIGNS.DAT", "Signs"},
                                                  {"LOOK2.DAT", "Object descriptions"},
                                                  {"QUESTION.DAT", "Character creation questions"},
                                                  {"END.DAT", "Ending story"},
                                                  {"ENDMSG.DAT", "Ending messages"},
                                                  {"KARMA.DAT", "Virtue messages"},
                                                  {"SHOPPE.DAT", "Shop dialogue"},
                                                  {"MISCMSG.DAT", "Game messages"},
                                                  {"MISCMAPS.DAT", "Special scenes"},
                                                  {"IBM.CH", "Normal font"},
                                                  {"RUNES.CH", "Runic font"},
                                                  {"IBM.HCS", "Legacy normal font"},
                                                  {"RUNES.HCS", "Legacy runic font"},
                                                  {"INIT.OOL", "Starting world objects"},
                                                  {"BRIT.OOL", "Britannia world objects"},
                                                  {"UNDER.OOL", "Underworld objects"},
                                                  {"DATA.OVL", "Engine tables"}};
    if (n.endsWith(".HCS"))
        return {titles.value(n, n), "Other resources",
                "Legacy font data: Impera currently uses .CH fonts, so changes here have no "
                "verified runtime effect.",
                EditorKind::Inspector};
    return {titles.value(n, n), "Other resources",
            titles.contains(n)
                ? "Advanced inspection; a dedicated editor is planned. Unknown bytes are preserved."
                : "Unrecognized layout; advanced inspection remains available. Unknown bytes are "
                  "preserved.",
            EditorKind::Inspector};
}
ResourceDocument::ResourceDocument(const Project &p, QString r)
    : project(p), resource(r.toUpper()) {
    project.data(resource);
}
QVector<Entry> ResourceDocument::entries() const {
    const auto size = project.data(resource).size();
    if (resource == "MISCMAPS.DAT") {
        QVector<Entry> result;
        for (int i = 0; i < 4; ++i)
            result.append({QString("scene/%1").arg(i), SceneDocument::names()[i], i * 176, 176});
        for (int i = 0; i < 4; ++i)
            result.append({QString("creation/background/%1").arg(i), SceneDocument::stages()[i],
                           704 + i * 128, 128});
        for (auto step : SceneDocument::steps(project.data(resource)))
            result.append({QString("creation/step/%1").arg(step.offset), step.title, step.offset,
                           step.bytes.size()});
        return result;
    }
    if (resource == "TYPES.OVR") {
        QVector<Entry> entries;
        int off = 32;
        auto records = CustomTiles::records(project);
        for (auto it = records.begin(); it != records.end(); it++) {
            auto d = CustomTiles::definition(project, it.key());
            entries.append(
                {QString("type/%1").arg(d.id), QString::fromUtf8(d.name), off, it.value().size()});
            off += it.value().size();
        }
        return entries;
    }
    if (capability(resource).editor == EditorKind::Narrative) {
        QVector<Entry> result;
        for (auto entry : narrativeEntries(resource))
            result.append({entry->id, entry->label, entry->offset,
                           narrativeBudget(project.data(resource), *entry)});
        return result;
    }
    if(resource=="SIGNS.DAT") {
        QVector<Entry> result;
        int offset = 66;
        for (const auto &sign : readSigns(project.data(resource))) {
            result.append(
                {QString("sign/%1/%2/%3/%4").arg(sign.map).arg(sign.floor).arg(sign.x).arg(sign.y),
                 QString("Sign at (%1,%2), floor %3").arg(sign.x).arg(sign.y).arg(sign.floor),
                 offset, 5 + sign.text.size()});
            offset += 5 + sign.text.size();
        }
        return result;
    }
    if (resource == "LOOK2.DAT") {
        QVector<Entry> result;
        for (int i = 0; i < 512; i++) {
            auto text = description(project.data(resource), i);
            result.append({QString("description/%1/%2").arg(i / 256).arg(i % 256),
                           QString("%1 %2: %3")
                               .arg(i < 256 ? "Terrain/object" : "Actor")
                               .arg(i % 256)
                               .arg(QString::fromLatin1(text)),
                           U5::word(project.data(resource), i * 2), text.size() + 1});
        }
        return result;
    }
    if (resource == "DUNGEON.DAT") {
        DungeonDocument document(&project);
        QVector<Entry> result;
        for (int d = 0; d < 8; ++d)
            for (int f = 0; f < 8; ++f)
                result.append({QString("dungeon/%1/level/%2").arg(d).arg(f),
                               QString("Dungeon %1 · Level %2").arg(d + 1).arg(f + 1),
                               DungeonDocument::offset(d, f), 64});
        return result;
    }
    QVector<Entry> result;
    if (capability(resource).editor == EditorKind::Fonts) {
        FontDocument::validate(resource, project.data(resource));
        int stride = FontDocument::stride(resource);
        for (int i = 0; i < 128; ++i)
            result.append({QString("glyph/%1").arg(i), FontDocument::label(i), i * stride, stride});
    } else if (resource.endsWith(".OOL")) {
        require(size == 256, "World objects must contain 32 eight-byte records");
        for (int i = 0; i < 32; ++i)
            result.append(
                {QString("object/%1").arg(i), QString("World object %1").arg(i + 1), i * 8, 8});
    } else
        result.append({"resource", capability(resource).title, 0, size});
    return result;
}
QByteArray ResourceDocument::read(const Entry &e) const {
    const auto &b = project.data(resource);
    require(e.offset >= 0 && e.length >= 0 && e.offset <= b.size() &&
                e.length <= b.size() - e.offset,
            resource + ": entry extends beyond resource");
    return b.mid(e.offset, e.length);
}
QMap<QString, QByteArray> ResourceDocument::replace(const Entry &e, const QByteArray &bytes) const {
    read(e);
    require(bytes.size() == e.length, "Fixed entries cannot change size");
    QByteArray result = project.data(resource);
    result.replace(e.offset, e.length, bytes);
    Project candidate = project;
    candidate.resources[resource].edited = result;
    candidate.validate();
    return {{resource, result}};
}
void validateResource(const Project &project, const QString &name) {
    const auto &b = project.data(name);
    const auto kind = capability(name).editor;
    if (name == "MODINFO.DAT") {
        require(MODINFO_Validate(b.constData(), b.size()), "Invalid mod description or screenshot");
        return;
    }
    if (name == "INTERFACE.OVR") {
        require(INTERFACE_Validate(b.constData(), b.size()), "Invalid interface colors");
        return;
    }
    if (name == "TYPES.OVR" || name == "TYPEMAP.OVR") {
        CustomTiles::validate(project);
        return;
    }
    if (name == "GATES.OVR") {
        validateGates(project);
        return;
    }
    if (name == "MAPS.OVR") {
        validateExtendedMaps(project);
        return;
    }
    if (name == "LOCATIONS.OVR") {
        require(LOCATIONS_Validate(b.constData(), b.size()), "Invalid location names");
        return;
    }
    if (name == "CUTSCENE.OVR") {
        require(U5_ValidateCutscenes(b.constData(), b.size()), "Invalid cutscene pages");
        return;
    }
    if (name == "CONTENTS.OVR") {
        U5::require(CONTENTS_Validate(b.constData(), b.size()), "Invalid map contents");
        for (const auto &d : readContents(b)) {
            auto size = mapSize(project, d.map, d.floor, d.kind);
            require(d.x < size.width() && d.y < size.height(), "Contents are outside their map");
            if (d.kind) {
                const QString map = d.kind == 1 ? "BRIT.CBT" : "DUNGEON.CBT";
                U5::require(project.resources.contains(map),
                            "Contents need the combat map: " + map);
                U5::require(d.map < project.mapPages(map).size(),
                            "Contents reference a missing encounter");
            } else if (MAPS_NewLocation(d.map) && !isDungeon(project, d.map)) {
                require(
                    project.resources.contains(QString("LOC%1.DAT").arg(d.map, 3, 10, QChar('0'))),
                    "Contents reference a missing location");
            } else if (d.map >= 33) {
                U5::require(project.resources.contains("DUNGEON.DAT"),
                            "Contents need dungeon maps");
                DungeonDocument dungeon(&project);
                dungeon.level(d.map - 33, d.floor);
            } else {
                const QString map = d.map == 0
                                        ? (d.floor < 0 ? "UNDER.DAT" : "BRIT.DAT")
                                        : QStringList{"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT",
                                                      "KEEP.DAT"}[(d.map - 1) / 8];
                U5::require(project.resources.contains(map),
                            "Contents need the location map: " + map);
                bool found = false;
                for (const auto &page : project.mapPages(map))
                    if ((d.map == 0 || page.settlement == (d.map - 1) % 8) &&
                        (d.map == 0 || page.floor == d.floor))
                        found = true;
                U5::require(found, "Contents reference a missing location floor");
                if (d.slot != 255) {
                    auto objects = WorldObjects::effective(project, d.floor < 0 ? 1 : 0);
                    U5::require(U5::byte(objects, d.slot * 8) == 1 &&
                                    U5::byte(objects, d.slot * 8 + 2) == d.x &&
                                    U5::byte(objects, d.slot * 8 + 3) == d.y,
                                "Container contents no longer match their placed chest");
                }
            }
        }
        return;
    }
    if (name == "RULES.TBL") {
        TableDocument::validate(b);
        return;
    }
    if (kind == EditorKind::Scenes) {
        SceneDocument::validate(b);
        return;
    }
    if (kind == EditorKind::Narrative || name == "TEXT.OVR") {
        validateNarrative(project, name);
        return;
    }
    if (kind == EditorKind::LocationText) {
        if (name == "SIGNS.DAT") {
            const QStringList maps = {"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"};
            for (const auto &r : readSigns(b)) {
                if (r.map == 0)
                    continue;
                QString map = MAPS_NewLocation(r.map)
                                  ? QString("LOC%1.DAT").arg(r.map, 3, 10, QChar('0'))
                                  : maps[(r.map - 1) / 8];
                require(project.resources.contains(map), "Sign refers to an absent location map");
                bool found = false;
                for (const auto &page : project.mapPages(map))
                    if (page.settlement == (MAPS_NewLocation(r.map) ? 0 : (r.map - 1) % 8) &&
                        page.floor == r.floor)
                        found = true;
                require(found, "Sign refers to a floor absent from its location");
            }
        } else
            for (int i = 0; i < 512; i++)
                description(b, i);
        return;
    }
    if (kind == EditorKind::Dungeon) {
        DungeonDocument::validate(project);
        return;
    }
    if (kind == EditorKind::Fonts) {
        FontDocument::validate(name, b);
        return;
    }
    if (kind == EditorKind::Artwork)
        U5::readGraphics(name, b);
    else if (kind == EditorKind::Conversation) {
        auto entries = U5::readDialogue(b);
        auto originals = project.resources[name].original.isEmpty()
                             ? QVector<U5::Conversation>()
                             : U5::readDialogue(project.resources[name].original);
        for (auto e : entries) {
            U5::validateText(e.bytes);
            bool unchanged = false;
            for (auto original : originals)
                if (original.id == e.id && original.bytes == e.bytes)
                    unchanged = true;
            if (!unchanged)
                for (auto issue : Dialogue::parse(e.bytes).issues())
                    require(!issue.error, name + ": " + issue.message);
        }
        U5::writeDialogue(entries);
    } else if (kind == EditorKind::Schedule)
        require(NPCS_Validate(b.constData(), b.size()), "Invalid NPC schedules");
    else if (kind == EditorKind::State)
        require(b.size() == 4192, "INIT.GAM must be 4192 bytes");
    else if (kind == EditorKind::WorldObjects)
        WorldObjects::validate(name, b, project.resources[name].original);
    else if (kind == EditorKind::Story)
        U5::storyPages(b);
    else if (name == "BRIT.DAT" || name == "DATA.OVL") {
        const auto &overlay = project.data("DATA.OVL");
        const uint8_t *index = U5_BritanniaDefaultIndex;
        if (project.resources["DATA.OVL"].original != overlay) {
            require(overlay.size() >= int(U5_WORLD_INDEX_OFFSET + U5_WORLD_INDEX_SIZE),
                    "DATA.OVL is missing its Britannia chunk index");
            index = reinterpret_cast<const uint8_t *>(overlay.constData()) + U5_WORLD_INDEX_OFFSET;
        }
        require(U5_ValidBritanniaIndex(index, project.data("BRIT.DAT").size()),
                "Britannia chunk index references missing/truncated BRIT.DAT chunks");
        U5::worldMap("BRIT.DAT", project.data("BRIT.DAT"), overlay);
    } else if (name == "UNDER.DAT")
        U5::worldMap(name, b, QByteArray());
    else if (kind == EditorKind::Map) {
        U5::mapPages(name, b);
        if (name == "DUNGEON.CBT" && project.resources.contains("DUNGEON.DAT"))
            DungeonDocument::validate(project, true);
    }
}
} // namespace Workshop
