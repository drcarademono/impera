#include "extended_maps.h"
#include "mod/npcs.h"
#include "cutscene_editor.h"
#include "cutscene_composition.h"
#include "map_positions.h"
#include "content_navigation.h"
#include "shop_editor.h"
#include "mod/tileset_resources.h"
#include "contents_editor.h"
#include "gates_editor.h"
#include "dialogue.h"
#include "map_editor.h"
#include "mod/discoveries.h"
#include "narrative_editor.h"
#include "resource_document.h"
#include "scene_editor.h"
#include "world_objects.h"
#include <algorithm>
namespace Workshop {
QString artworkTitle(const QString &name) {
    if (auto sheet = MOD_TilesetSheet(name.toLatin1().constData()))
        return QString::fromUtf8(sheet->title);
    static const QMap<QString, QString> titles = {{"TILES.16", "DOS"},
                                                  {"ITEMS.16", "Dungeon item illustrations"},
                                                  {"CREATE.16", "Character creation artwork"},
                                                  {"TEXT.16", "Story text framing"},
                                                  {"ULTIMA.16", "Title artwork"},
                                                  {"STARTSC.16", "Title scene artwork"},
                                                  {"ENDSC.16", "Final rescue artwork"}};
    if (titles.contains(name)) return titles[name];
    const auto stem = name.section('.', 0, 0);
    for (const auto prefix : {QString("STORY"), QString("END"), QString("DNG"), QString("MON")}) {
        bool ok = false;
        int number = stem.mid(prefix.size()).toInt(&ok);
        if (stem.startsWith(prefix) && ok) {
            QString purpose = prefix == "STORY" ? "Introduction illustrations"
                            : prefix == "END" ? "Ending illustrations"
                            : prefix == "DNG" ? "Dungeon scenery" : "Dungeon creature illustrations";
            return purpose + QString(" — set %1").arg(prefix == "MON" ? number + 1 : number);
        }
    }
    return "Artwork collection — " + stem;
}
bool matchesSelection(const ContentDestination &d, const QMap<QString, int> &state) {
    for (auto it = d.selection.begin(); it != d.selection.end(); ++it)
        if (state.value(it.key(), 0) != it.value()) return false;
    return true;
}
QStringList contentWorkspaces() {
    return {"World", "Dialogue", "Cutscenes", "Graphics", "Game Rules", "Mod Overview"};
}
QVector<ContentDestination> characterDestinations(const Project &project) {
    QVector<ContentDestination> result;
    QStringList stems{"TOWNE", "CASTLE", "KEEP", "DWELLING"};
    for (const auto &m : extendedMaps(project))
        if (m.kind == 1 && MAPS_NewLocation(m.id))
            stems.append(m.resource.section('.', 0, 0));
    for (const QString &stem : stems) {
        const QString talk = stem + ".TLK", schedule = stem + ".NPC", map = stem + ".DAT";
        QVector<U5::Conversation> conversations;
        if (project.resources.contains(talk)) {
            try {
                conversations = U5::readDialogue(project.data(talk));
            } catch (...) {
            }
        }
        QStringList names;
        for (const auto &conversation : conversations) {
            QString name;
            try {
                auto doc = Dialogue::parse(conversation.bytes);
                if (!doc.entries.isEmpty()) {
                    name = Dialogue::displayName(doc.entries[0].bytes);
                }
            } catch (...) {
            }
            names.append(name.isEmpty() ? "Unnamed NPC" : name);
        }
        QMap<int, QString> locations;
        if (project.resources.contains(map)) {
            try {
                for (const auto &page : project.mapPages(map))
                    if (page.settlement >= 0)
                        locations[page.settlement] = page.name.section(" — ", 0, 0);
            } catch (...) {
            }
        }
        const QByteArray data =
            project.resources.contains(schedule) ? project.data(schedule) : QByteArray();
        QMap<int, QStringList> users;
        if (data.size() >= 4608) {
            for (int town = 0; town < 8; ++town)
                for (int slot = 0; slot < int(NPCS_Count(data.constData(), data.size(), town));
                     ++slot) {
                    if (U5::byte(data,
                                 NPCS_ShapeOffset(data.constData(), data.size(), town, slot)) == 0)
                        continue;
                    if (NPCS_ObjectKind(
                            data.constData() +
                                NPCS_Offset(data.constData(), data.size(), town, slot),
                            U5::byte(data, NPCS_ShapeOffset(data.constData(), data.size(), town,
                                                            slot))) >= 0)
                        continue;
                    int id = U5::byte(
                            data, NPCS_DialogueOffset(data.constData(), data.size(), town, slot)),
                        conversation = -1;
                    for (int i = 0; i < conversations.size(); ++i)
                        if (int(conversations[i].id) == id) {
                            conversation = i;
                            break;
                        }
                    const QString location =
                        locations.value(town, QString("%1 location %2").arg(stem).arg(town + 1));
                    QString name = conversation >= 0 ? names[conversation]
                                                     : QString("NPC %1").arg(slot + 1);
                    ContentDestination d{
                        QString("character/%1/%2/%3").arg(stem).arg(town).arg(slot), "Dialogue",
                        location, name, conversation >= 0 ? talk : schedule};
                    d.character = npcCharacter(town, slot);
                    d.conversation = conversation;
                    d.settlement = town;
                    d.scheduleResource = schedule;
                    d.provenance = QString("%1 · NPC %2 · conversation number %3")
                                       .arg(location)
                                       .arg(slot + 1)
                                       .arg(id);
                    result.append(d);
                    if (conversation >= 0)
                        users[conversation].append(d.provenance);
                }
        }
        for (int i = 0; i < conversations.size(); ++i) {
            if (!users.contains(i)) {
                ContentDestination d{
                    QString("conversation/%1/%2").arg(stem).arg(conversations[i].id), "Dialogue",
                    data.size() >= 4608 ? "Unassigned conversations" : "Unverified assignments",
                    names[i], talk};
                d.conversation = i;
                d.provenance = data.size() >= 4608 ? "No scheduled NPC uses this conversation. It "
                                                     "may be referenced by special game logic."
                                                   : "Schedule data is missing or damaged; NPC "
                                                     "assignments cannot be verified.";
                result.append(d);
            }
        }
        for (auto &d : result)
            if (d.resource == talk && d.conversation >= 0 && users.contains(d.conversation))
                d.provenance +=
                    QString("\n%1%2 scheduled NPC(s) use this conversation:\n%3")
                        .arg(users[d.conversation].size() > 1 ? "Shared dialogue: " : "")
                        .arg(users[d.conversation].size())
                        .arg(users[d.conversation].join("\n"));
    }
    return result;
}
QVector<ContentDestination> contentDestinations(const Project &project) {
    QVector<ContentDestination> result;
    // Schedule-only NPCs belong on maps, not in the dialogue browser. Keep the
    // complete identity index for map navigation, relinking and undo.
    for (const auto &destination : characterDestinations(project))
        if (destination.conversation >= 0)
            result.append(destination);
    result.append({"overview", "Mod Overview", "Project", "Mod overview", "@overview"});
    result.append({"interface", "Graphics", "Interface", "Frame color", "INTERFACE.OVR"});
    bool usableScenes = false;
    if (project.resources.contains("MISCMAPS.DAT")) {
        try {
            SceneDocument::validate(project.data("MISCMAPS.DAT"));
            usableScenes = true;
        } catch (...) {
            // Keep independent message editors available when their scene bank is damaged.
        }
    }
    for (auto it = project.resources.begin(); it != project.resources.end(); ++it) {
        const auto name = it.key();
        if (name == "TYPES.OVR") {
            ContentDestination tiles{"custom-tiles", "Graphics", "Custom tiles", "Tiles", name};
            tiles.selection["customTiles/kind"] = 0;
            result.append(tiles);
            for (int kind = 1; kind <= 2; kind++) {
                ContentDestination types{kind == 1 ? "npc-types" : "monster-types", "Graphics",
                                         "Custom tiles",
                                         kind == 1 ? "NPC types" : "Monster types", name};
                types.selection["customTiles/kind"] = kind;
                result.append(types);
            }
            continue;
        }
        if (name == "LOCATIONS.OVR")
            continue;
        if (name == "TYPEMAP.OVR")
            continue;
        if (name == "MODINFO.DAT" || name == "INTERFACE.OVR") continue;
        if (name == "CONTENTS.OVR" || name == "GATES.OVR")
            continue; // Edited through the map inspector.
        if (MAPS_LocationResource(name.toLatin1().constData())) {
            bool exists = false;
            for (const auto &m : extendedMaps(project))
                exists |= m.resource.section('.', 0, 0) == name.section('.', 0, 0);
            if (!exists)
                continue;
        }
        const auto c = capability(name);
        if (MapDocument::supported(name)) {
            const auto pages = project.mapPages(name);
            for (int i = 0; i < pages.size(); ++i) {
                const auto &p = pages[i];
                QString group = p.settlement >= 0       ? "Locations"
                                : name.endsWith(".CBT") ? "Combat maps"
                                                        : "World maps";
                QString title = p.name;
                // Settlement floors belong in the editor's floor selector, not another tree.
                if (p.settlement >= 0) {
                    int main = i;
                    for (int j = 0; j < pages.size(); ++j)
                        if (pages[j].settlement == p.settlement &&
                            qAbs(pages[j].floor) < qAbs(pages[main].floor))
                            main = j;
                    if (main != i)
                        continue;
                    title = title.section(" — ", 0, 0);
                } else if (name.endsWith(".CBT")) {
                    title = QString("%1 encounter %2")
                                .arg(name == "BRIT.CBT" ? "Outdoor" : "Dungeon")
                                .arg(i + 1);
                }
                ContentDestination destination{
                    QString("world/%1/%2").arg(name).arg(p.settlement >= 0 ? p.settlement : i),
                    "World", group, title, name, i};
                destination.settlement = p.settlement;
                result.append(destination);
            }
            continue;
        }
        QString workspace, group = c.group, title = c.title;
        switch (c.editor) {
        case EditorKind::Dungeon:
            for (int id : dungeonIds(project)) {
                const int dungeon = id - 33;
                ContentDestination d{QString("world/dungeon/%1").arg(dungeon), "World", "Dungeons",
                                     project.locationName(33 + dungeon), name, -1};
                d.selection["DUNGEON.DAT/dungeon"] = dungeon;
                d.sources = {name};
                result.append(d);
            }
            continue;
        case EditorKind::LocationText:
            if (name == "SIGNS.DAT") {
                bool mapAvailable = false;
                for (const QString &map :
                     {QString("BRIT.DAT"), QString("UNDER.DAT"), QString("TOWNE.DAT"),
                      QString("DWELLING.DAT"), QString("CASTLE.DAT"), QString("KEEP.DAT")})
                    mapAvailable |= project.resources.contains(map);
                if (mapAvailable)
                    continue;
                workspace = "Mod Overview";
                group = "Compatibility details";
            } else {
                workspace = "Game Rules";
                group = "Descriptions";
            }
            break;
        case EditorKind::Conversation:
        case EditorKind::Schedule:
            // Healthy schedule banks remain editable on maps even when none of
            // their NPCs has dialogue; do not add a bank to the dialogue list.
            if (c.editor == EditorKind::Schedule &&
                NPCS_Validate(project.data(name).constData(), project.data(name).size()))
                continue;
            // Damaged or empty banks remain accessible for compatibility.
            if (std::any_of(result.cbegin(), result.cend(), [&](const auto &d) {
                    return d.resource == name || d.scheduleResource == name;
                }))
                continue;
            workspace = "Mod Overview";
            group = "Unavailable NPC data";
            break;
        case EditorKind::Artwork:
            workspace = "Graphics";
            title = artworkTitle(name);
            group = MOD_IsTilesetMask(name.toLatin1().constData()) ? "Transparency masks"
                    : name == "TILES.16" || MOD_TilesetSheet(name.toLatin1().constData())
                        ? "Tilesets"
                        : "Illustrations";
            break;
        case EditorKind::Fonts:
            workspace = "Graphics";
            if (name.endsWith(".HCS"))
                title += " (Hercules)";
            break;
        case EditorKind::State:
            workspace = "Game Rules";
            group = "Starting game";
            title = "Starting party and world state";
            break;
        case EditorKind::WorldObjects:
            if (name == "INIT.OOL" && project.resources.contains("UNDER.OOL"))
                continue;
            if (project.resources.contains(name == "BRIT.OOL" ? "BRIT.DAT" : "UNDER.DAT") &&
                project.resources.contains("INIT.OOL") && project.resources.contains("BRIT.OOL") &&
                project.resources.contains("UNDER.OOL"))
                continue; // Select / Move opens the shared world-object inspector on maps.
            workspace = "Mod Overview";
            group = "Compatibility details";
            title = name == "BRIT.OOL" ? "Britannia objects" : "Underworld objects";
            break;
        case EditorKind::Tables:
            if (name == "DATA.OVL")
                continue; // Edited through the Britannia map.
            workspace = "Game Rules";
            group = "Shops";
            title = "Prices and stock";
            break;
        case EditorKind::Scenes:
            for (int scene = 0; scene < SceneDocument::names().size(); ++scene) {
                ContentDestination d{"scene/" + QString::number(scene),
                    scene == 4 ? "Game Rules" : "Cutscenes", scene == 4 ? "Starting game" : "Scenes",
                    SceneDocument::names()[scene], name};
                d.selection["scene/current"] = scene;
                d.sources = {name, scene == 4 ? "QUESTION.DAT" : scene == 3 ? "ENDMSG.DAT" : "MISCMSG.DAT"};
                result.append(d);
            }
            continue;
        case EditorKind::Story:
        case EditorKind::Narrative:
            if (name == "SHOPPE.DAT") {
                const auto services = shopServices();
                for (int index = 0; index < services.size(); ++index) {
                    ContentDestination d{"shop/" + services[index], "Dialogue", "Shops & Services",
                                         services[index], name};
                    d.selection["SHOPPE.DAT/service"] = index;
                    d.sources = {name, "TEXT.OVR"};
                    result.append(d);
                }
                continue;
            }
            if (usableScenes &&
                QStringList{"QUESTION.DAT", "MISCMSG.DAT", "ENDMSG.DAT"}.contains(name)) continue;
            workspace = QStringList{"QUESTION.DAT", "SHOPPE.DAT"}.contains(name) ? "Game Rules" : "Cutscenes";
            group = name == "QUESTION.DAT" ? "Starting game"
                    : name == "SHOPPE.DAT" ? "Shops"
                                           : "Stories and messages";
            if (name == "SHOPPE.DAT") title = "Shop and service dialogue";
            if (name == "KARMA.DAT") title = "Dreams and virtue speeches";
            break;
        default:
            workspace = "Mod Overview";
            group = "Compatibility details";
            break;
        }
        if (name == "CUTSCENE.OVR" || name == "MAPS.OVR")
            continue;
        ContentDestination d{"content/" + name, workspace, group, title, name, -1};
        d.sources = {name};
        if (name == "STORY.DAT" || name == "END.DAT") d.sources.append("CUTSCENE.OVR");
        if (name == "UNDER.OOL" && project.resources.contains("INIT.OOL")) d.sources.append("INIT.OOL");
        result.append(d);
    }
    return result;
}
// Derived comparisons only: originals and overrides remain owned by Project.
QMap<QString, QStringList> contentChanges(const Project &project,
                                          const QVector<ContentDestination> &destinations) {
    QMap<QString, QStringList> result;
    if (project.changed().isEmpty())
        return result;
    Project original = project;
    for (auto it = original.resources.begin(); it != original.resources.end(); ++it) {
        it->edited = it->original;
        it->included = false;
    }
    auto changed = [&](const QString &name) {
        if (!project.resources.contains(name))
            return false;
        const auto &r = project.resources[name];
        return r.edited != r.original || r.included;
    };
    QMap<QString, QVector<U5::Conversation>> beforeTalk, afterTalk;
    auto dialogueChanged = [&](const ContentDestination &d) {
        if (!changed(d.resource) || !d.resource.endsWith(".TLK"))
            return false;
        if (!afterTalk.contains(d.resource)) {
            afterTalk[d.resource] = U5::readDialogue(project.data(d.resource));
            beforeTalk[d.resource] = U5::readDialogue(original.data(d.resource));
        }
        const auto &after = afterTalk[d.resource];
        if (d.conversation < 0 || d.conversation >= after.size())
            return true;
        const auto &entry = after[d.conversation];
        for (const auto &old : beforeTalk[d.resource])
            if (old.id == entry.id)
                return old.bytes != entry.bytes;
        return true;
    };
    for (const auto &d : destinations) {
        if (d.resource == "@overview")
            continue;
        QStringList parts;
        try {
            if (d.workspace == "Dialogue" && d.character >= 0) {
                if (changed(d.scheduleResource)) {
                    const auto &now = project.data(d.scheduleResource);
                    const auto &old = original.data(d.scheduleResource);
                    U5::require(now.size() >= 4608 && old.size() >= 4608, "Damaged schedules");
                    int town = Workshop::npcTown(d.character),
                        slot = Workshop::npcSlot(d.character), base = town * 576;
                    auto npcBytes = [&](const QByteArray &b) {
                        const int off = NPCS_Offset(b.constData(), b.size(), town, slot);
                        if (off >= b.size())
                            return QByteArray();
                        return b.mid(off, 16) +
                               b.mid(NPCS_ShapeOffset(b.constData(), b.size(), town, slot), 1) +
                               b.mid(NPCS_DialogueOffset(b.constData(), b.size(), town, slot), 1);
                    };
                    if (npcBytes(now) != npcBytes(old))
                        parts.append("Schedule / appearance");
                }
                if (dialogueChanged(d))
                    parts.append("Conversation");
            } else if (d.workspace == "Dialogue" && d.conversation >= 0) {
                if (dialogueChanged(d))
                    parts.append("Conversation");
            } else if (d.page >= 0 && MapDocument::supported(d.resource)) {
                MapDocument map(&original, d.resource);
                Project live = project;
                MapDocument edited(&live, d.resource);
                if (d.page >= map.pages.size()) {
                    result[d.id] = {"New map"};
                    continue;
                }
                if (!d.resource.endsWith(".CBT")) {
                    int id =
                        d.resource == "UNDER.DAT" ? 41 : positionMap(d.resource, map.pages[d.page]);
                    if (project.locationName(id) != original.locationName(id))
                        parts.append("Location name");
                }

                if (changed(d.resource) || (d.resource == "BRIT.DAT" && changed("DATA.OVL")))
                    for (int i = 0; i < map.pages.size(); ++i) {
                        if (d.settlement >= 0 ? map.pages[i].settlement != d.settlement
                                              : i != d.page)
                            continue;
                        if (map.terrain(i) != edited.terrain(i)) {
                            parts.append("Terrain");
                            break;
                        }
                    }
                if (!d.resource.endsWith(".CBT") && (changed("INIT.GAM") || changed("RULES.TBL"))) {
                    auto nowPositions = mapPositions(project),
                         oldPositions = mapPositions(original);
                    const int mapId = positionMap(d.resource, map.pages[d.page]);
                    const int floor = d.resource == "UNDER.DAT" ? -1 : map.pages[d.page].floor;
                    auto belongs = [&](const MapPosition &p) {
                        return !p.carried && p.map == mapId &&
                               (d.settlement >= 0 || p.floor == floor);
                    };
                    for (auto pos : nowPositions) {
                        auto old = std::find_if(oldPositions.begin(), oldPositions.end(),
                                                [&](auto p) { return p.id == pos.id; });
                        if (old != oldPositions.end() && (belongs(pos) || belongs(*old)) &&
                            (pos.cell != old->cell || pos.map != old->map ||
                             pos.floor != old->floor || pos.carried != old->carried)) {
                            parts.append("Map positions");
                            break;
                        }
                    }
                }
                if (changed("GATES.OVR") && !d.resource.endsWith(".CBT")) {
                    int mapId = positionMap(d.resource, map.pages[d.page]);
                    auto gateRows = [&](const Project &p) {
                        QList<QByteArray> rows;
                        for (const auto &r : readGates(p.resources.value("GATES.OVR").edited))
                            if (U5::byte(r, 0) == unsigned(mapId)) rows.append(r);
                        return rows;
                    };
                    if (gateRows(project) != gateRows(original)) parts.append("Gates");
                }
                if (changed("CONTENTS.OVR")) {
                    auto target = contentsTarget(d.resource, map.pages[d.page], d.page);
                    for (const auto &entry :
                         readContents(project.resources.value("CONTENTS.OVR").edited)) {
                        bool hit = entry.kind == target.kind && entry.map == target.map &&
                                   (d.settlement >= 0 || entry.floor == target.floor);
                        if (entry.original != 65535) {
                            const auto *source = U5_Discoveries[entry.original];
                            hit |= !target.kind && source[2] == target.map &&
                                   (d.settlement >= 0 || (int8_t)source[3] == target.floor);
                        }
                        if (hit) {
                            parts.append("Contents");
                            break;
                        }
                    }
                }
                // World destinations created in earlier phases encode settlement in their page.
                int settlement = map.pages[d.page].settlement;
                if (settlement >= 0) {
                    QString schedules = d.resource.section('.', 0, 0) + ".NPC";
                    auto scheduleTown = [&](const QByteArray &bytes) {
                        QByteArray result;
                        for (unsigned slot = 0;
                             slot < NPCS_Count(bytes.constData(), bytes.size(), settlement);
                             ++slot) {
                            result += bytes.mid(
                                NPCS_Offset(bytes.constData(), bytes.size(), settlement, slot), 16);
                            result += bytes.mid(
                                NPCS_ShapeOffset(bytes.constData(), bytes.size(), settlement, slot),
                                1);
                            result += bytes.mid(NPCS_DialogueOffset(bytes.constData(), bytes.size(),
                                                                    settlement, slot),
                                                1);
                        }
                        return result;
                    };
                    if (changed(schedules) && scheduleTown(project.data(schedules)) !=
                                                  scheduleTown(original.data(schedules)))
                        parts.append("NPCs / schedules");
                    for (const auto &character : destinations)
                        if (character.settlement == settlement &&
                            character.scheduleResource == schedules && dialogueChanged(character)) {
                            parts.append("Conversations");
                            break;
                        }
                    if (changed("SIGNS.DAT")) {
                        const QStringList banks = {"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT",
                                                   "KEEP.DAT"};
                        int location = banks.indexOf(d.resource) * 8 + settlement + 1;
                        auto signsAt = [&](const Project &p) {
                            QVector<SignRecord> records;
                            for (auto sign : readSigns(p.data("SIGNS.DAT")))
                                if (sign.map == location)
                                    records.append(sign);
                            return records;
                        };
                        if (signsAt(project) != signsAt(original))
                            parts.append("Signs");
                    }
                } else if (d.resource == "BRIT.DAT" || d.resource == "UNDER.DAT") {
                    const QString objects = d.resource == "BRIT.DAT" ? "BRIT.OOL" : "UNDER.OOL";
                    int world = objects == "BRIT.OOL" ? 0 : 1;
                    if (changed(objects) || (world == 1 && changed("INIT.OOL") &&
                                             !WorldObjects::overridden(project, world)))
                        parts.append("Starting objects");
                    if (changed("SIGNS.DAT")) {
                        auto worldSigns = [world](const Project &p) {
                            QVector<SignRecord> signs;
                            for (auto sign : readSigns(p.data("SIGNS.DAT")))
                                if (sign.map == 0 && sign.floor == (world ? -1 : 0))
                                    signs.append(sign);
                            return signs;
                        };
                        if (worldSigns(project) != worldSigns(original))
                            parts.append("Signs");
                    }
                }
                if (d.resource.endsWith(".CBT") && parts.isEmpty() &&
                    project.data(d.resource).mid(map.pages[d.page].offset, 352) !=
                        original.data(d.resource).mid(map.pages[d.page].offset, 352))
                    parts.append("Encounter details");
            } else if (d.resource == "DUNGEON.DAT") {
                const int dungeon = d.selection.value("DUNGEON.DAT/dungeon");
                const int id = 33 + dungeon;
                if (project.locationName(id) != original.locationName(id))
                    parts.append("Location names");
                if (changed("MAPS.OVR"))
                    for (const auto &m : extendedMaps(project))
                        if (m.kind == 4 && m.id == unsigned(id)) {
                            parts.append("Dungeon dimensions / terrain");
                            break;
                        }
                if (project.data(d.resource).mid(dungeon * 512, 512) !=
                    original.data(d.resource).mid(dungeon * 512, 512))
                    parts.append("Dungeon terrain");
                if (changed("CONTENTS.OVR"))
                    for (const auto &entry : readContents(project.data("CONTENTS.OVR")))
                        if (!entry.kind && entry.map == unsigned(id)) {
                            parts.append("Contents");
                            break;
                        }
            } else if (d.resource == "MISCMAPS.DAT" && d.selection.contains("scene/current")) {
                int scene = d.selection["scene/current"];
                if (changed(d.resource))
                    for (int stage = 0; stage < (scene == 4 ? 4 : 1); ++stage)
                        if (SceneDocument::map(project.data(d.resource), scene, stage) !=
                            SceneDocument::map(original.data(d.resource), scene, stage)) {
                            parts.append("Scene map");
                            break;
                        }
                if (scene == 4 &&
                    project.data(d.resource).mid(1216) != original.data(d.resource).mid(1216))
                    parts.append("Creation sequence");
                QString messages = scene == 4   ? "QUESTION.DAT"
                                   : scene == 3 ? "ENDMSG.DAT"
                                                : "MISCMSG.DAT";
                if (project.resources.contains(messages) &&
                    (changed(messages) || changed("TEXT.OVR")))
                    for (auto entry : narrativeEntries(messages)) {
                        if (scene < 3 && !QString::fromLatin1(entry->label)
                                              .startsWith(scene == 0   ? "Blackthorn"
                                                          : scene == 1 ? "Shrine"
                                                                       : "Codex"))
                            continue;
                        if (narrativeText(project, *entry) != narrativeText(original, *entry)) {
                            parts.append("Messages");
                            break;
                        }
                    }
            } else if (d.resource == "SHOPPE.DAT") {
                const auto service = shopServices().value(d.selection.value("SHOPPE.DAT/service"));
                for (auto entry : narrativeEntries("SHOPPE.DAT"))
                    if (shopEntryServices(*entry).contains(service) &&
                        narrativeText(project, *entry) != narrativeText(original, *entry)) {
                        parts.append("Dialogue");
                        break;
                    }
            } else if (d.resource == "STORY.DAT" || d.resource == "END.DAT") {
                int scene = d.resource == "END.DAT" ? 1 : 0;
                bool different=false;
                if(project.resources.value("CUTSCENE.OVR").edited.startsWith("ICUT0003")) {
                    auto overridden=compositionPages(project,scene,false);
                    different=!overridden.isEmpty();
                } else {
                    auto before = cutscenePages(original, scene);
                    auto after = cutscenePages(project, scene);
                    different = before.size() != after.size();
                    for (int i = 0; !different && i < before.size(); ++i)
                        different = !(before[i] == after[i]);
                }
                if (different) parts.append("Pages");
            } else {
                for (const auto &source : d.sources.isEmpty() ? QStringList{d.resource} : d.sources)
                    if (changed(source))
                        parts.append("Content");
                if (changed("TEXT.OVR") && capability(d.resource).editor == EditorKind::Narrative)
                    for (auto entry : narrativeEntries(d.resource))
                        if (narrativeText(project, *entry) != narrativeText(original, *entry)) {
                            parts.append("Text");
                            break;
                        }
            }
        } catch (...) {
            if (changed(d.resource) || changed(d.scheduleResource))
                parts.append("Changed resource — detailed comparison unavailable");
            for (const auto &source : d.sources)
                if (changed(source))
                    parts.append("Changed companion — detailed comparison unavailable");
        }
        parts.removeDuplicates();
        result[d.id] = parts;
    }
    return result;
}
} // namespace Workshop
