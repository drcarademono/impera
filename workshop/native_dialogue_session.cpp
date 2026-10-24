#include "native_dialogue_session.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
namespace Dialogue {
namespace {
struct Execution {
    NativeTextWindow window;
    NativeTextWindow vitalsWindow;
    ImperaTalk runtime{};
    ImperaTalkStorage storage{};
    QByteArray conversation;
    ImperaInput input{};
    uint16_t cycle = 0, numpad = 0, buffering = 0;
    uint8_t animate = 0;
    NativeSetup setup;
    NativeResult result;
    QVector<QByteArray> names;
    QVector<int> entryStarts;
    ImperaDialogueGame game{};
    QByteArray extraKilled, extraMet;
    NativeRandom random;
    uint8_t dirty = 0, frameFlag = 0;
    uint64_t elapsed = 0;
    int charset = 0, entry = -1, offset = -1, consumed = 0;
    bool capture = false;
    std::function<bool()> cancelled;
    std::function<int()> poll;
    std::function<void(int)> sleep;
    std::function<void(const QImage &)> present;
    std::function<void(const QString &, int, int)> action;
    static Execution &self(void *host) { return *static_cast<Execution *>(host); }
    bool stopped() const { return runtime.error || (cancelled && cancelled()); }
    void delay(int ms) {
        if (sleep)
            sleep(ms);
        elapsed += ms;
    }
    void publish(const QString &kind = {}, int value = 0) {
        if (capture)
            result.boundaries.append({kind, elapsed, window.cursor().currentX,
                                      window.cursor().currentY, charset, *runtime.pending, entry,
                                      offset, value, window.indexedPixels()});
        if (present && !stopped())
            present(window.image());
    }
    void fail(const char *text) {
        runtime.error = 1;
        runtime.errorText = text;
    }
    Execution(const QByteArray &bytes, const QByteArray &normal, const QByteArray &runes,
              const NativeSetup &s)
        : window(normal, runes), vitalsWindow(normal, runes, 0), setup(s) {
        window.setRecordGlyphs(false);
        U5::require(!bytes.isEmpty(), "Conversation is empty");
        U5::require(s.state.partySize >= 1 && s.state.partySize <= 6 && s.state.gold >= 0 &&
                        s.state.gold <= 9999 && s.state.karma >= 0 && s.state.karma <= 99,
                    "Invalid Playtest starting state");
        auto doc = parse(bytes);
        for (const auto &issue : doc.issues())
            U5::require(!issue.error, issue.message);
        for (const auto &entry : doc.entries)
            entryStarts.append(entry.offset);
        conversation = bytes;
        IMPERA_TalkBind(&runtime, &storage, 0);
        runtime.data = reinterpret_cast<uint8_t *>(conversation.data());
        runtime.size = size_t(conversation.size());
        storage.cursor = runtime.data;
        auto party = s.state.partyNames;
        if (party.isEmpty())
            party.append(s.state.avatar);
        while (party.size() < s.state.partySize)
            party.append(party.size() == 1 ? "Iolo" : "Shamino");
        for (const QString &name : party) {
            QByteArray encoded = name.left(8).toLatin1();
            U5::require(QString::fromLatin1(encoded) == name.left(8),
                        "Party names must use DOS ASCII");
            for (unsigned char ch : encoded)
                U5::require(ch >= 32 && ch < 127, "Invalid party name");
            encoded += QByteArray(9 - encoded.size(), char(0));
            names.append(encoded);
        }
        if (setup.gameState.isEmpty()) {
            setup.gameState = QByteArray(4192, char(0));
            setup.gameState[0x2ed] = 13;
        }
        U5::require(setup.gameState.size() == 4192, "Invalid Playtest game state");
        if (setup.actorRecords.isEmpty())
            setup.actorRecords = QByteArray(256, char(0));
        if (setup.npcRecords.isEmpty())
            setup.npcRecords = QByteArray(512, char(0));
        if (setup.scheduleRecords.isEmpty())
            setup.scheduleRecords = QByteArray(512, char(0));
        if (setup.shapes.isEmpty())
            setup.shapes = QByteArray(32, char(0));
        U5::require(setup.shapes.size() >= 32 && setup.shapes.size() <= 32768 &&
                        setup.actorRecords.size() == setup.shapes.size() * 8 &&
                        setup.npcRecords.size() == setup.shapes.size() * 16 &&
                        setup.scheduleRecords.size() == setup.shapes.size() * 16,
                    "Invalid Playtest NPC context");
        auto *state = reinterpret_cast<uint8_t *>(setup.gameState.data());
        for (int i = 0; i < std::min(names.size(), qsizetype(16)); ++i) {
            U5::require(names[i].size() == 9, "Invalid party name record");
            std::copy(names[i].cbegin(), names[i].cend(), state + 2 + i * 32);
        }
        state[0x2b5] = uint8_t(s.state.partySize);
        state[0x204] = uint8_t(s.state.gold);
        state[0x205] = uint8_t(s.state.gold >> 8);
        state[0x2e2] = uint8_t(s.state.karma);
        game.party = state + 2;
        game.partyCount = state + 0x2b5;
        game.food = state + 0x202;
        game.gold = state + 0x204;
        game.equipment = state + 0x21a;
        const int supplies[] = {0x202, 0x204, 0x206, 0x207, 0x208, 0x209,
                                0x20a, 0x216, 0x214, 0x218, 0x20b};
        for (int i = 0; i < 11; ++i)
            game.supplies[i] = state + supplies[i];
        game.karma = state + 0x2e2;
        game.turn = state + 0x2e5;
        game.map = state + 0x2ed;
        game.killed = state + 0x5b4;
        game.met = state + 0x634;
        game.actors = reinterpret_cast<uint8_t *>(setup.actorRecords.data());
        game.npcs = reinterpret_cast<uint8_t *>(setup.npcRecords.data());
        game.schedules = reinterpret_cast<uint8_t *>(setup.scheduleRecords.data());
        game.shapes = reinterpret_cast<uint8_t *>(setup.shapes.data());
        game.npc = setup.hasNpc ? setup.npc : -1;
        game.npcCount = setup.shapes.size();
        game.actorCount = setup.actorRecords.size() / 8;
        game.mapCount = 128;
        extraKilled = QByteArray(128 * 4096, 0);
        extraMet = extraKilled;
        game.extraKilled = reinterpret_cast<uint8_t *>(extraKilled.data());
        game.extraMet = reinterpret_cast<uint8_t *>(extraMet.data());
        game.dirty = &dirty;
        game.host = this;
        game.vitals = [](void *h) { self(h).updateVitals(); };
        vitalsWindow.setRows(9);
        vitalsWindow.setRecordGlyphs(false);
        vitalsWindow.setSleeper([this](int ms) { delay(ms); });
        animate = state[0x2eb];
        if (setup.hasNpc) {
            U5::require(game.npc >= 1 && game.npc < int(game.npcCount) && *game.map >= 1 &&
                            *game.map < 128,
                        "Invalid Playtest NPC identity");
            auto *flag = game.npc < 32 && *game.map <= 32
                             ? game.met + (*game.map - 1) * 4 + game.npc / 8
                             : game.extraMet + (*game.map - 1) * 4096 + game.npc / 8;
            *flag = uint8_t((*flag & ~(1 << (game.npc % 8))) |
                            (s.state.introduced ? 1 << (game.npc % 8) : 0));
        }
        random.seed(setup.seed);
        game.random = [](void *h, int low, int high) {
            auto &e = self(h);
            ++e.result.randomDraws;
            return e.random.next() % (high - low + 1) + low;
        };
        runtime.host = this;
        runtime.cancelled = [](void *h) {
            auto &e = self(h);
            return int(e.cancelled && e.cancelled());
        };
        runtime.print = [](void *h, uint16_t ch) {
            auto &e=self(h);
            if(!e.stopped()) e.window.print(ch);
        };
        runtime.string = [](void *h, const char *str) {
            auto &e=self(h);
            if(!e.stopped()) e.window.printString(str);
        };
        runtime.column = [](void *h) { return int(self(h).window.cursor().currentX); };
        runtime.font = [](void *h, int index) {
            auto &e = self(h);
            if (index == 1 && !e.hasRunes) {
                e.fail("RUNES.CH is required for this conversation");
                return;
            }
            e.charset = index;
            e.window.selectFont(index);
        };
        runtime.getChar = [](void *h) { return IMPERA_InputChar(&self(h).input); };
        runtime.getString = [](void *h, char *str, int limit) {
            IMPERA_InputString(&self(h).input, str, limit);
        };
        runtime.poll = [](void *h) {
            auto &e = self(h);
            e.numpad = 0;
            int key = e.poll ? e.poll() : 0;
            if (key & 256) {
                e.numpad = 1;
                key &= 255;
            }
            if (key)
                ++e.consumed;
            return key;
        };
        runtime.waitTicks = [](void *h, int ticks) { self(h).delay(ticks * 55); };
        runtime.frame = [](void *h) {
            auto &e = self(h);
            e.frameFlag = 255;
            e.animate = 1;
            ++e.result.frames;
            // Talk disables world animation, wind RNG and movement; clock is not advanced.
        };
        runtime.clearKeys = [](void *) {}; // The current engine's ClearKbdBuffer is a stub.
        runtime.introduced = [](void *h) {
            auto &e = self(h);
            return e.setup.hasNpc ? IMPERA_GameMet(&e.game) : int(e.setup.state.introduced);
        };
        runtime.introduce = [](void *h) {
            auto &e = self(h);
            if (e.setup.hasNpc)
                IMPERA_GameIntroduce(&e.game);
            else
                e.setup.state.introduced = true;
        };
        runtime.partySize = [](void *h) { return int(*self(h).game.partyCount); };
        runtime.name = [](void *h, int index) -> const uint8_t * {
            return self(h).game.party + index * 32;
        };
        runtime.karma = [](void *h) { return int(*self(h).game.karma); };
        runtime.changeKarma = [](void *h, int delta) {
            auto &v = *self(h).game.karma;
            v = uint8_t(std::clamp(int(v) + delta, 0, 99));
        };
        runtime.pay = [](void *h, int amount) {
            auto &e = self(h);
            if (e.setup.requireNpcContext && !e.setup.hasNpc) {
                e.fail("Choose an NPC in Setup to test payments");
                return 1;
            }
            return IMPERA_GamePay(&e.game, amount);
        };
        runtime.give = [](void *h, uint8_t code) {
            auto &e = self(h);
            if (!IMPERA_GameGive(&e.game, code))
                e.fail("Invalid equipment item");
        };
        runtime.join = [](void *h) {
            auto &e = self(h);
            if (!e.setup.hasNpc) {
                e.fail("Choose an NPC in Setup to test recruitment");
                return 1;
            }
            return IMPERA_GameJoin(&e.game, &e.runtime);
        };
        runtime.guards = [](void *h) {
            auto &e = self(h);
            if (!e.setup.hasNpc) {
                e.fail("Choose an NPC in Setup to test guard reactions");
                return;
            }
            IMPERA_GameGuards(&e.game);
        };
        runtime.greetingRoll = [](void *h) {
            auto &e = self(h);
            e.random.seed(e.setup.seed);
            ++e.result.randomDraws;
            return e.random.next() % 2;
        };
        runtime.observe = [](void *h, const uint8_t *cursor) {
            auto &e = self(h);
            const int absolute = int(cursor - e.runtime.data);
            const auto following =
                std::upper_bound(e.entryStarts.cbegin(), e.entryStarts.cend(), absolute);
            e.entry = int(following - e.entryStarts.cbegin()) - 1;
            e.offset = absolute - e.entryStarts[e.entry];
            if (e.capture)
                e.result.opcodes.append({absolute, *cursor});
            if (!*e.runtime.pending && *cursor >= 129 && *cursor <= 159 && *cursor != 141 &&
                *cursor != 144)
                if (e.action)
                    e.action(actionName(*cursor), e.entry, e.offset);
        };
        input.advance = window.advanceFlag();
        input.cycle = &cycle;
        input.numpad = &numpad;
        input.buffering = &buffering;
        input.cursorBase = 5;
        input.cursorCount = 4;
        input.map = *game.map;
        input.driver = 1;
        input.animate = &animate;
        input.host = this;
        input.cancelled = [](void *h) { return int(self(h).stopped()); };
        input.print = runtime.print;
        input.poll = runtime.poll;
        input.waitTicks = runtime.waitTicks;
        input.frame = runtime.frame;
        input.backspace = [](void *h, int n) { self(h).window.backspace(n); };
        hasRunes = runes.size() == 1024;
        window.setSleeper([this](int ms) { delay(ms); });
        window.setObserver([this](const QString &kind, int value) { publish(kind, value); });
    }
    bool hasRunes = false;
    // Run the native non-combat vitals layout offscreen: its wrapping still requests ticks.
    void updateVitals() {
        auto &w = vitalsWindow;
        w.selectFont(charset);
        auto byte = [&](int at) { return uint8_t(setup.gameState[at]); };
        auto number = [&](int n, int width = 1, char fill = ' ') {
            w.printString(
                QString::number(n).rightJustified(width, QChar(fill)).toLatin1().constData());
        };
        for (int i = 0; i < 6; ++i) {
            w.setPosition(0, i);
            if (i >= *game.partyCount) {
                w.printString("               ");
                continue;
            }
            auto *p = game.party + i * 32;
            int length = 0;
            while (length < 9 && p[length])
                w.print(p[length++]);
            for (int x = length; x < 9; ++x)
                w.print(' ');
            bool active = i == byte(0x2d5);
            w.print(active && p[11] != 'D' && p[11] != 'S' ? 0x1a : ' ');
            if (active && (p[11] == 'D' || p[11] == 'S'))
                setup.gameState[0x2d5] = char(255);
            number(int16_t(p[16] | (p[17] << 8)), 4);
            w.print(p[11]);
        }
        w.setPosition(0, 7);
        w.printString("F:");
        number(int16_t(game.food[0] | (game.food[1] << 8)));
        while (w.cursor().currentX < 8)
            w.print(' ');
        if (*game.map < 128 && (byte(0x2d6) & 0xf8) == 0x20) {
            w.printString("Ship:");
            number(game.actors[5]);
            if (game.actors[5] < 10)
                w.print(' ');
        } else {
            w.setPosition(8, 7);
            int gold = int16_t(game.gold[0] | (game.gold[1] << 8));
            if (gold < 1000)
                w.print(' ');
            if (gold < 100)
                w.print(' ');
            if (gold < 10)
                w.print(' ');
            w.printString(" G:");
            number(gold);
            while (w.cursor().currentX < 15)
                w.print(' ');
        }
        w.printString("\n   ");
        if (byte(0x2d7) < 10 && byte(0x2d8) < 10)
            w.print(' ');
        number(byte(0x2d7));
        w.print('-');
        number(byte(0x2d8));
        w.print('-');
        number(byte(0x2ce) | (byte(0x2cf) << 8), 3, '0');
        while (w.cursor().currentX < 15)
            w.print(' ');
    }
    NativeResult run() {
        publish("start");
        IMPERA_TalkRun(&runtime);
        if (!stopped())
            IMPERA_TalkFlush(&runtime);
        result.pixels = window.indexedPixels();
        result.time = elapsed;
        result.keysConsumed = consumed;
        result.x = window.cursor().currentX;
        result.y = window.cursor().currentY;
        result.gold = game.gold[0] | (game.gold[1] << 8);
        result.karma = *game.karma;
        result.keys = *game.supplies[2];
        result.partySize = *game.partyCount;
        result.introduced = setup.hasNpc ? IMPERA_GameMet(&game) != 0 : setup.state.introduced;
        setup.gameState[0x2eb] = char(animate);
        setup.gameState.replace(0x6b4, 256, setup.actorRecords.left(256));
        setup.gameState.replace(0x7b8, 512, setup.scheduleRecords.left(512));
        setup.gameState.replace(0x9b8, 512, setup.npcRecords.left(512));
        setup.gameState.replace(0xff8, 32, setup.shapes.left(32));
        result.gameState = setup.gameState;
        result.npcRecords = setup.npcRecords;
        result.scheduleRecords = setup.scheduleRecords;
        result.actorRecords = setup.actorRecords;
        result.shapes = setup.shapes;
        result.cancelled = cancelled && cancelled();
        if (runtime.error)
            result.error = QString::fromUtf8(runtime.errorText ? runtime.errorText
                                                               : "Conversation execution failed");
        if (present)
            present(window.image());
        return result;
    }
};
} // namespace
NativeResult runNativeVirtual(const QByteArray &bytes, const QByteArray &font,
                              const QByteArray &runes, const NativeSetup &setup,
                              const QVector<NativeKey> &keys, uint64_t initialTime) {
    Execution e(bytes, font, runes, setup);
    e.capture = true;
    e.elapsed = initialTime;
    size_t index = 0;
    bool scheduled = false;
    uint64_t ready = 0;
    e.poll = [&] {
        if (index >= size_t(keys.size())) {
            e.fail("Script exhausted before native input completed");
            return 0;
        }
        if (!scheduled) {
            ready = e.elapsed + keys[int(index)].delay;
            scheduled = true;
        }
        if (e.elapsed < ready)
            return 0;
        scheduled = false;
        return keys[int(index++)].code;
    };
    e.sleep = [&](int) {
        if (e.elapsed > 300000)
            e.fail("Virtual conversation time limit exceeded");
    };
    return e.run();
}
NativeDialogueSession::NativeDialogueSession(QByteArray c, QByteArray n, QByteArray r,
                                             NativeSetup s, QObject *parent)
    : QObject(parent), conversation(std::move(c)), normal(std::move(n)), runes(std::move(r)),
      setup(std::move(s)) {}
NativeDialogueSession::~NativeDialogueSession() { cancel(); }
void NativeDialogueSession::start() {
    cancel();
    cancelled = false;
    active = true;
    {
        std::lock_guard<std::mutex> lock(mutex);
        keys.clear();
    }
    worker = std::thread([this] {
        try {
            Execution e(conversation, normal, runes, setup);
            e.cancelled = [this] { return cancelled.load(); };
            e.poll = [this] {
                std::lock_guard<std::mutex> lock(mutex);
                if (keys.empty())
                    return 0;
                int key = keys.front();
                keys.pop_front();
                return key;
            };
            e.sleep = [this](int ms) {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(ms),
                              [this] { return cancelled.load(); });
            };
            e.present = [this](const QImage &image) {
                if (!cancelled)
                    emit frameReady(image);
            };
            e.action = [this](const QString &name, int entry, int offset) {
                if (!cancelled)
                    emit actionReady(name, entry, offset);
            };
            auto result = e.run();
            active = false;
            if (!cancelled) {
                QStringList changes;
                changes << QString("Party: %1 · Gold: %2 · Karma: %3 · Keys: %4")
                               .arg(result.partySize)
                               .arg(result.gold)
                               .arg(result.karma)
                               .arg(result.keys);
                if (!setup.scheduleRecords.isEmpty()) {
                    int changed = 0;
                    for (int i = 0; i < setup.scheduleRecords.size() / 16; ++i)
                        if (result.scheduleRecords.mid(i * 16, 16) !=
                            setup.scheduleRecords.mid(i * 16, 16))
                            ++changed;
                    if (changed)
                        changes << QString("NPC schedules changed: %1").arg(changed);
                }
                emit stateReady(changes);
                emit finished(result.error.isEmpty() ? "Conversation ended" : result.error);
            }
        } catch (const std::exception &error) {
            active = false;
            if (!cancelled)
                emit finished(QString::fromUtf8(error.what()));
        }
    });
}
void NativeDialogueSession::sendKey(int key) {
    if (key < 1 || key > 511 || !active || cancelled)
        return;
    std::lock_guard<std::mutex> lock(mutex);
    if (keys.size() < 4096)
        keys.push_back(key);
}
void NativeDialogueSession::cancel() {
    cancelled = true;
    wake.notify_all();
    if (worker.joinable())
        worker.join();
    active = false;
}
} // namespace Dialogue
