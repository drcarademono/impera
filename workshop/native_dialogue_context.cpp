#include "mod/maps.h"
#include "mod/npcs.h"
#include "native_dialogue_session.h"
#include "content_navigation.h"
#include "engine_tables.h"
#include "native_npc_properties.h"
#include <cstring>
namespace Dialogue {
void NativeRandom::seed(uint16_t value) {
    front = 3;
    rear = 0;
#if defined(_WIN32)
    state[0] = value;
#elif defined(__APPLE__)
    state[0] = value;
#else
    state[0] = value ? value : 1;
    for (int i = 1; i < 31; ++i) {
        int64_t next = 16807LL * state[i - 1] % 2147483647;
        state[i] = uint32_t(next);
    }
    for (int i = 0; i < 310; ++i)
        next();
#endif
}
int NativeRandom::next() {
#if defined(_WIN32)
    state[0] = state[0] * 214013u + 2531011u;
    return int((state[0] >> 16) & 32767);
#elif defined(__APPLE__)
    if (!state[0])
        state[0] = 123459876;
    state[0] = uint32_t(16807ULL * state[0] % 2147483647);
    return int(state[0]);
#else
    state[front] += state[rear];
    int result = int(state[front] >> 1);
    front = (front + 1) % 31;
    rear = (rear + 1) % 31;
    return result;
#endif
}
NativeSetup nativeSetup(const Project &project, const QString &resource, int conversation,
                        int character) {
    NativeSetup result;
    result.requireNpcContext = true;
    if (project.resources.contains("RULES.TBL") && !project.data("RULES.TBL").isEmpty())
        Workshop::TableDocument::validate(project.data("RULES.TBL"));
    U5::require(project.resources.contains("INIT.GAM"),
                "Starting game state is required for Playtest");
    result.gameState = project.data("INIT.GAM");
    U5::require(result.gameState.size() == 4192, "Invalid starting game state");
    auto byte = [&](int offset) { return uint8_t(result.gameState[offset]); };
    result.state.partySize = byte(0x2b5);
    U5::require(result.state.partySize >= 1 && result.state.partySize <= 6,
                "Invalid starting party");
    result.state.partyNames.clear();
    for (int i = 0; i < result.state.partySize; ++i) {
        auto name = result.gameState.mid(2 + i * 32, 9);
        result.state.partyNames.append(QString::fromLatin1(
            name.constData(), name.indexOf(char(0)) < 0 ? 9 : name.indexOf(char(0))));
    }
    result.state.avatar = result.state.partyNames.first();
    result.state.gold = byte(0x204) | (byte(0x205) << 8);
    result.state.karma = byte(0x2e2);
    result.actorRecords = result.gameState.mid(0x6b4, 256);
    result.scheduleRecords = result.gameState.mid(0x7b8, 512);
    result.npcRecords = result.gameState.mid(0x9b8, 512);
    result.shapes = result.gameState.mid(0xff8, 32);
    QVector<Workshop::ContentDestination> matches;
    for (const auto &npc : Workshop::characterDestinations(project))
        if (npc.character > 0 && Workshop::npcSlot(npc.character) != 0 &&
            npc.resource == resource && npc.conversation == conversation &&
            (character == -1 || npc.character == character))
            matches.append(npc);
    /* Shared or unassigned conversations require an explicit choice, never a guessed identity. */
    if (matches.size() != 1)
        return result;
    const auto &selected = matches.first();
    int bank = selected.scheduleResource == "TOWNE.NPC"      ? 0
               : selected.scheduleResource == "DWELLING.NPC" ? 1
               : selected.scheduleResource == "CASTLE.NPC"   ? 2
                                                             : 3;
    const auto &records = project.data(selected.scheduleResource);
    U5::require(records.size() >= 4608, "Invalid NPC schedules");
    int start = selected.settlement * 576;
    int count = NPCS_Count(records.constData(), records.size(), selected.settlement);
    result.scheduleRecords = QByteArray(count * 16, 0);
    result.shapes = QByteArray(count, 0);
    for (int i = 0; i < count; i++) {
        int off = NPCS_Offset(records.constData(), records.size(), selected.settlement, i);
        result.scheduleRecords.replace(i * 16, 16, records.mid(off, 16));
        result.shapes[i] =
            records[NPCS_ShapeOffset(records.constData(), records.size(), selected.settlement, i)];
    }
    result.npcRecords = QByteArray(count * 16, char(0));
    result.actorRecords = result.gameState.mid(0x6b4, 8) + QByteArray(count * 8 - 8, char(0));
    result.npc = Workshop::npcSlot(selected.character);
    result.hasNpc = true;
    result.gameState[0x2ed] = char(MAPS_LocationResource(resource.toLatin1().constData())
                                       ? MAPS_LocationResource(resource.toLatin1().constData())
                                       : bank * 8 + selected.settlement + 1);
    int hour = byte(0x2d9);
    int destination = IMPERA_GameSchedule(
        reinterpret_cast<const uint8_t *>(result.scheduleRecords.constData()) + result.npc * 16,
        uint8_t(hour));
    int floor = uint8_t(result.scheduleRecords[result.npc * 16 + 9 + destination]);
    result.gameState[0x2ef] = char(floor);
    auto word = [](QByteArray &bytes, int offset, int value) {
        bytes[offset] = char(value);
        bytes[offset + 1] = char(value >> 8);
    };
    int actor = 1;
    for (int i = 1; i < count; ++i) {
        int shape = uint8_t(result.shapes[i]);
        if (!shape)
            continue;
        int map = byte(0x2ed);
        const auto *schedule =
            reinterpret_cast<const uint8_t *>(result.scheduleRecords.constData()) + i * 16;
        int index = IMPERA_GameSchedule(schedule, uint8_t(hour));
        word(result.npcRecords, i * 16, 1);
        word(result.npcRecords, i * 16 + 2, schedule[3 + index]);
        word(result.npcRecords, i * 16 + 4, schedule[6 + index]);
        word(result.npcRecords, i * 16 + 6, schedule[9 + index]);
        word(result.npcRecords, i * 16 + 8, shape);
        word(result.npcRecords, i * 16 + 10,
             uint8_t(records[NPCS_DialogueOffset(records.constData(), records.size(),
                                                 selected.settlement, i)]));
        word(result.npcRecords, i * 16 + 14, index);
        if (schedule[9 + index] == floor) {
            word(result.npcRecords, i * 16 + 12, actor);
            result.actorRecords[actor * 8] = result.actorRecords[actor * 8 + 1] = char(shape);
            result.actorRecords[actor * 8 + 2] = char(schedule[3 + index]);
            result.actorRecords[actor * 8 + 3] = char(schedule[6 + index]);
            result.actorRecords[actor * 8 + 4] = char(floor);
            result.actorRecords[actor * 8 + 5] =
                char(shape == 1 ? 0x1e
                     : (map <= 32 && i < 32 && (nativeNpcProperties[map - 1] & (uint32_t(1) << i)))
                         ? 0xff
                         : 0);
            ++actor;
        }
    }
    int map = byte(0x2ed);
    bool shadowlord = map <= 8 && byte(0x2f1) != 4 &&
                      (byte(0x322) == map || byte(0x323) == map || byte(0x324) == map);
    bool presentShadowlord = false;
    for (int i = 0; i < count; ++i)
        presentShadowlord |= uint8_t(result.actorRecords[i * 8]) == 0xfc;
    if (shadowlord && !presentShadowlord) {
        int slot = -1;
        for (int i = count - 1; i > 0; --i)
            if (!result.shapes[i]) {
                slot = i;
                break;
            }
        if (slot < 0) {
            U5::require(count < 32768, "No room for the visiting Shadowlord");
            slot = count++;
            result.shapes.append(char(0));
            result.scheduleRecords.append(QByteArray(16, 0));
            result.npcRecords.append(QByteArray(16, 0));
            result.actorRecords.append(QByteArray(8, 0));
        }
        int index = actor < count ? actor : slot;
        int y = nativeShadowlordRows[map - 1];
        word(result.npcRecords, slot * 16, 1);
        word(result.npcRecords, slot * 16 + 2, 15);
        word(result.npcRecords, slot * 16 + 4, y);
        word(result.npcRecords, slot * 16 + 6, 0);
        word(result.npcRecords, slot * 16 + 12, index);
        result.actorRecords.replace(index * 8, 8, QByteArray(8, char(0)));
        result.actorRecords[index * 8] = result.actorRecords[index * 8 + 1] = char(0xfc);
        result.actorRecords[index * 8 + 2] = 15;
        result.actorRecords[index * 8 + 3] = char(y);
        result.shapes[slot] = char(0xfc);
        for (int i = 0; i < 3; ++i) {
            result.scheduleRecords[slot * 16 + i] = 6;
            result.scheduleRecords[slot * 16 + 3 + i] = 15;
            result.scheduleRecords[slot * 16 + 6 + i] = char(y);
            result.scheduleRecords[slot * 16 + 9 + i] = 0;
        }
        for (int i = 0; i < 4; ++i)
            result.scheduleRecords[slot * 16 + 12 + i] = 0;
    }
    ImperaDialogueGame game{};
    uint8_t dirty = 0;
    game.npcCount = count;
    game.actorCount = count;
    game.actors = reinterpret_cast<uint8_t *>(result.actorRecords.data());
    game.npcs = reinterpret_cast<uint8_t *>(result.npcRecords.data());
    game.schedules = reinterpret_cast<uint8_t *>(result.scheduleRecords.data());
    game.shapes = reinterpret_cast<uint8_t *>(result.shapes.data());
    game.dirty = &dirty;
    for (int i = 0; i < count; ++i) {
        int shape = game.shapes[i];
        bool killed = map <= 32 && i < 32 && byte(0x5b4 + (map - 1) * 4 + i / 8) & (1 << (i % 8));
        if (killed && (shape == 0x0e || shape >= 0x40))
            IMPERA_GameDespawn(&game, i);
    }
    if (map == 29 && byte(0x20f))
        IMPERA_GameDespawn(&game, 9);
    U5::require(uint8_t(result.shapes[result.npc]) != 0,
                "This NPC is absent in the starting game state");
    result.state.introduced =
        map <= 32 && result.npc < 32 &&
        byte(0x634 + (byte(0x2ed) - 1) * 4 + result.npc / 8) & (1 << (result.npc % 8));
    return result;
}
} // namespace Dialogue
