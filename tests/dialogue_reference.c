#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_filesystem.h>
#include "common/common.h"
#include "graphics/grap_buf.h"
#include "mod/package.h"
#include "time/time.h"
#include "vars.h"
#include "reference_provenance.h"

static void REFERENCE_TraceOpcode(const byte *cursor);
static void REFERENCE_CheckCursor(const byte *cursor);
/* Whole engine source; the generated copy adds only opcode/bounds observation hooks. */
#ifndef DIALOGUE_REFERENCE_TALK
#define DIALOGUE_REFERENCE_TALK "reference_talk.c"
#endif
#include DIALOGUE_REFERENCE_TALK

enum { TEXT_X = 192, TEXT_Y = 88, TEXT_W = 128, TEXT_H = 104, MAX_KEYS = 4096 };
typedef struct {
    u32 delay;
    int key;
} ScriptKey;
typedef struct {
    const char *name;
    byte bytes[1024];
    size_t size;
    ScriptKey keys[MAX_KEYS];
    int key_count, introduced;
} Scenario;
typedef struct {
    u32 time, trace_hash, pixel_hash, intro;
    unsigned events, scrolls, glyphs, rune_glyphs, sleep55, clears, snapshots;
    int keys, x, y, charset, karma, gold, supplies;
    byte pixels[TEXT_W * TEXT_H];
} Result;
static Scenario scenario;
static ScriptKey *script;
static int key_count, key_index;
static bool allow_unused_keys;
static u32 now, ready_at, seed = 42, trace_hash;
static bool scheduled, recording;
static unsigned event_count, scroll_count, glyph_count, sleep55_count, clear_count;
static unsigned rune_glyph_count;
static unsigned pause_ticks[32], pause_count, pause_tick_start;
static bool pausing;
static int opcode_entry = -1, opcode_offset = -1;
static FILE *trace;
static FILE *snapshots;
static unsigned snapshot_count;
static byte normal_font[1024], rune_font[1024], normal_original[1024], rune_original[1024];
static char input_strings[64][16];
static int input_count;
static int start_x = 0, start_y = 12, conversation_id = 1;

static void fail(const char *message) {
    fprintf(stderr, "Dialogue reference: %s\n", message);
    exit(2);
}
static u32 hash_bytes(u32 hash, const void *data, size_t size) {
    const byte *p = data;
    while (size--)
        hash = (hash ^ *p++) * 16777619u;
    return hash;
}
static u32 pixel_hash(void) {
    u32 h = 2166136261u;
    for (int y = 0; y < TEXT_H; ++y)
        h = hash_bytes(h, g_linearEgaBuffer0 + (TEXT_Y + y) * 320 + TEXT_X, TEXT_W);
    return h;
}
static u32 introduction(void) {
    u32 flags;
    memcpy(&flags, D_5bda + (D_5893_map_id - 1) * 4, sizeof(flags));
    return flags;
}
static void event(const char *kind, int value, int a, int b) {
    if (!recording)
        return;
    if (++event_count > 250000 || now > 300000)
        fail("execution budget exceeded");
    TextWindow *w = D_539a_currentTextWindow;
    char line[512];
    int n =
        snprintf(line, sizeof(line),
                 "{\"event\":\"%s\",\"seq\":%u,\"ms\":%u,\"value\":%d,\"a\":%d,\"b\":%d,"
                 "\"entry\":%d,\"offset\":%d,\"operand\":%u,\"x\":%u,\"y\":%u,"
                 "\"font\":%u,\"karma\":%u,\"gold\":%u,\"keys\":%u,\"intro\":%u,\"pixels\":%u}\n",
                 kind, event_count, now, value, a, b, opcode_entry, opcode_offset, D_4aee,
                 w->currentX, w->currentY, D_5388_current_charset_idx, D_5888, D_57aa, D_57ac,
                 introduction(), pixel_hash());
    if (n < 0 || n >= (int)sizeof(line))
        fail("trace record too large");
    trace_hash = hash_bytes(trace_hash, line, (size_t)n);
    if (trace && fwrite(line, 1, (size_t)n, trace) != (size_t)n)
        fail("cannot write trace");
    if (!strcmp(kind, "input_wait") || !strcmp(kind, "scroll") || !strcmp(kind, "start") ||
        !strcmp(kind, "finish") || !strcmp(kind, "keyboard_clear_stub") ||
        (!strcmp(kind, "glyph") && !D_538e)) {
        ++snapshot_count;
        if (snapshots) {
            byte header[8];
            for (int i = 0; i < 4; ++i) {
                header[i] = (byte)(event_count >> (8 * i));
                header[4 + i] = (byte)(now >> (8 * i));
            }
            if (fwrite(header, 1, 8, snapshots) != 8)
                fail("cannot write snapshot header");
            for (int y = 0; y < TEXT_H; ++y)
                if (fwrite(g_linearEgaBuffer0 + (TEXT_Y + y) * 320 + TEXT_X, 1, TEXT_W,
                           snapshots) != TEXT_W)
                    fail("cannot write snapshot pixels");
        }
    }
}
static void REFERENCE_CheckCursor(const byte *cursor) {
    uintptr_t p = (uintptr_t)cursor, base = (uintptr_t)D_b21e;
    if (p < base || p >= base + 1024)
        fail("native dialogue escaped its 1024-byte buffer");
}
static void REFERENCE_TraceOpcode(const byte *cursor) {
    REFERENCE_CheckCursor(cursor);
    int offset = (int)(cursor - D_b21e), start = 0, entry = 0;
    for (int i = 0; i < offset;) {
        int code = D_b21e[i];
        if (code == 0) {
            ++entry;
            start = i + 1;
        }
        i += 1 + (code == 133 ? 3 : code == 134 || code == 140 ? 1 : code == 254 ? 2 : 0);
    }
    opcode_entry = entry;
    opcode_offset = offset - start;
    event("opcode", *cursor, offset, D_4aee != 0);
    if (!D_4aee && *cursor == 131) {
        pausing = true;
        pause_tick_start = sleep55_count;
    }
}
void __wrap_GRAP_SetPenColor(byte color) { GRAP_BUF_SetPenColor(color); }
void __wrap_GRAP_FillWindow(int x1, int y1, int x2, int y2, int mode) {
    GRAP_BUF_FillWindow(x1, y1, x2, y2, mode);
    event("fill", mode, x1, y1);
}
void __wrap_GRAP_Line(int x1, int y1, int x2, int y2) { GRAP_BUF_Line(x1, y1, x2, y2); }
void __wrap_GRAP_Pset(int x, int y) { GRAP_BUF_Pset(x, y); }
void __wrap_GRAP_PrintChar(byte *font, int offset, byte fg, byte bg, int x, int y) {
    GRAP_BUF_PrintChar(font, offset, fg, bg, x, y);
    ++glyph_count;
    if (font == rune_font)
        ++rune_glyph_count;
    assert(offset >= 0 && offset + 8 <= 1024);
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col)
            assert(g_linearEgaBuffer0[(y * 8 + row) * 320 + x * 8 + col] ==
                   ((font[offset + row] & (128 >> col)) ? fg : bg));
    event("glyph", offset / 8, x, y);
}
void __wrap_GRAP_ScrollWindow(int left, int top, int right, int bottom, int amount) {
    byte before[TEXT_W * TEXT_H];
    assert(left == TEXT_X && top == TEXT_Y && right == TEXT_X + TEXT_W - 1 &&
           bottom == TEXT_Y + TEXT_H - 1 && amount == -8);
    for (int y = 0; y < TEXT_H; ++y)
        memcpy(before + y * TEXT_W, g_linearEgaBuffer0 + (top + y) * 320 + left, TEXT_W);
    GRAP_BUF_ScrollWindow(left, top, right, bottom, amount);
    for (int y = 0; y < TEXT_H - 8; ++y)
        assert(!memcmp(before + (y + 8) * TEXT_W, g_linearEgaBuffer0 + (top + y) * 320 + left,
                       TEXT_W));
    for (int y = TEXT_H - 8; y < TEXT_H; ++y)
        for (int x = 0; x < TEXT_W; ++x)
            assert(g_linearEgaBuffer0[(top + y) * 320 + left + x] == 0);
    ++scroll_count;
    event("scroll", amount, left, top);
}
u32 __wrap_TIME_GetTicksMs(void) { return now; }
void __wrap_TIME_SleepMs(int ms) {
    if (ms < 0 || ms > 300000 || now + (u32)ms > 300000)
        fail("invalid virtual sleep");
    if (ms == 55)
        ++sleep55_count;
    event("sleep", ms, 0, 0);
    now += (u32)ms;
}
uint __wrap_ULTIMA_2056_GetTime(void) { return (uint)seed; }
int __wrap_KEY_PollKey(void) {
    D_538a = 0;
    if (key_index >= key_count)
        fail("script exhausted while the native interpreter requested input");
    if (!scheduled) {
        ready_at = now + script[key_index].delay;
        scheduled = true;
    }
    if (now < ready_at)
        return 0;
    int key = script[key_index++].key;
    if (key & 256) {
        D_538a = 1;
        key &= 255;
    }
    scheduled = false;
    event("key", key, key_index, 0);
    return key;
}
int __real_ULTIMA_266c_GetChar(void);
int __wrap_ULTIMA_266c_GetChar(void) {
    event("input_wait", 0, 0, 0);
    int key = __real_ULTIMA_266c_GetChar();
    event("input_return", key, 0, 0);
    return key;
}
void __real_ULTIMA_3b1c_GetString(char *text, int limit);
void __wrap_ULTIMA_3b1c_GetString(char *text, int limit) {
    event("string_begin", limit, 0, 0);
    __real_ULTIMA_3b1c_GetString(text, limit);
    assert(input_count < 64 && strlen(text) < sizeof(input_strings[0]));
    strcpy(input_strings[input_count++], text);
    event("string_end", (int)strlen(text), input_count, 0);
}
void __real_ULTIMA_1b16_ClearKbdBuffer(void);
void __wrap_ULTIMA_1b16_ClearKbdBuffer(void) {
    __real_ULTIMA_1b16_ClearKbdBuffer(); // Currently a stub: deliberately do not invent queue
                                         // clearing.
    ++clear_count;
    if (pausing) {
        assert(pause_count < 32);
        pause_ticks[pause_count++] = sleep55_count - pause_tick_start;
        pausing = false;
    }
    event("keyboard_clear_stub", 0, 0, 0);
}
void __real_ULTIMA_5910_UpdateFrame(void);
void __wrap_ULTIMA_5910_UpdateFrame(void) {
    __real_ULTIMA_5910_UpdateFrame(); // Preserve actual updates; this session disables map
                                      // rendering.
    event("frame_update", 0, 0, 0);
}

static void bytecode(int value) {
    if (scenario.size >= sizeof(scenario.bytes))
        fail("fixture exceeds native conversation limit");
    scenario.bytes[scenario.size++] = (byte)value;
}
static void literal(const char *text) {
    while (*text) {
        unsigned char ch = (unsigned char)*text++;
        bytecode(ch == '\n' ? 141 : ch | 128);
    }
}
static void entry(const char *text) {
    literal(text);
    bytecode(0);
}
static void keys(const char *text, u32 delay) {
    while (*text) {
        assert(scenario.key_count < MAX_KEYS);
        scenario.keys[scenario.key_count++] = (ScriptKey){delay, (unsigned char)*text++};
        delay = 0;
    }
}
static void base(const char *name, const char *description, const char *greeting) {
    memset(&scenario, 0, sizeof(scenario));
    scenario.name = name;
    scenario.introduced = 1;
    entry("Nora");
    entry(description);
    entry(greeting);
    entry("I study maps.");
    entry("Farewell.");
}
static void tail(void) {
    bytecode(144);
    bytecode(159);
    bytecode(192);
    bytecode(0);
}
static void build(const char *name) {
    if (!strncmp(name, "join_", 5) || !strncmp(name, "guards_", 7) ||
        !strncmp(name, "beggar_", 7) || !strncmp(name, "give_", 5)) {
        memset(&scenario, 0, sizeof(scenario));
        scenario.name = name;
        scenario.introduced = strcmp(name, "guards_first") != 0;
        entry("Nora");
        entry("a traveller.");
        literal("Welcome.");
        if (!strncmp(name, "join_", 5))
            bytecode(132);
        else if (!strncmp(name, "guards_", 7)) {
            bytecode(139);
            bytecode(130);
        } else if (!strncmp(name, "beggar_", 7)) {
            bytecode(133);
            bytecode('1' | 128);
            bytecode('0' | 128);
            bytecode('0' | 128);
            bytecode(130);
        } else {
            for (int i = 0; i < 48; ++i) {
                bytecode(134);
                bytecode(i | 128);
            }
            for (int i = 65; i <= 75; ++i) {
                bytecode(134);
                bytecode(i | 128);
            }
            bytecode(130);
        }
        bytecode(0);
        if (!strcmp(name, "guards_first")) {
            literal("Work.");
            bytecode(139);
            bytecode(130);
            bytecode(0);
        } else
            entry("Work.");
        entry("Bye.");
        tail();
        if (!strcmp(name, "guards_first"))
            keys("job\r", 0);
        if (!strcmp(name, "join_full"))
            keys("xbye\r", 0);
        if (!strcmp(name, "beggar_insufficient"))
            keys("bye\r", 0);
        return;
    }
    if (!strcmp(name, "questions") || !strcmp(name, "fallback")) {
        base(name, "a careful traveller.", "Welcome.");
        entry("help");
        bytecode(135);
        bytecode(0);
        entry("hint");
        literal("Try this.\"\n\n");
        bytecode(145);
        bytecode(0);
        bytecode(144);
        bytecode(145);
        entry("Do you trust me?");
        literal("No match.\"\n\n");
        bytecode(255);
        bytecode(0);
        entry("yes");
        literal("Thank thee.");
        bytecode(137);
        bytecode(134);
        bytecode(195);
        literal("\"\n\n");
        bytecode(255);
        bytecode(0);
        tail();
        keys(!strcmp(name, "questions") ? "hint\ryes\rbye\r" : "help\rwrong\rbye\r", 0);
    } else if (!strcmp(name, "scroll")) {
        char description[600] = "";
        for (int i = 0; i < 22; ++i) {
            char line[24];
            snprintf(line, sizeof(line), "Row %02d marches\n", i);
            strcat(description, line);
        }
        base(name, description, "Welcome.");
        tail();
        keys("bye\r", 55);
    } else if (!strcmp(name, "pause_interrupt") || !strcmp(name, "pause_full")) {
        base(name, "a careful traveller.", "");
        /* Replace greeting; rebuild the remaining fixed entries after its actions. */
        scenario.size -= strlen("I study maps.") + strlen("Farewell.") + 3;
        literal("Stage A.\"\n\n");
        bytecode(131);
        literal("\"Stage B.\"\n\n");
        if (!strcmp(name, "pause_interrupt"))
            bytecode(143);
        literal("\"Stage C.");
        bytecode(0);
        entry("I study maps.");
        entry("Farewell.");
        tail();
        if (!strcmp(name, "pause_interrupt")) {
            keys("p", 110);
            keys(" ", 165);
            keys("bye\r", 0);
        } else
            keys("bye\r", 1800);
    } else if (!strcmp(name, "editing")) {
        base(name, "a careful traveller.", "Welcome.");
        tail();
        keys("jox\bb\rbad\033work\rbye\r", 0);
    } else if (!strcmp(name, "left_editing")) {
        base(name, "a careful traveller.", "Welcome.");
        tail();
        keys("jox", 0);
        scenario.keys[scenario.key_count++] = (ScriptKey){0, 257};
        keys("b\rbye\r", 0);
    } else if (!strcmp(name, "input_limit")) {
        base(name, "a careful traveller.", "Welcome.");
        tail();
        keys("abcdefghijklmnopqr\rbye\r", 0);
    } else if (!strcmp(name, "runes")) {
        base(name, "a careful traveller.", "");
        scenario.size -= strlen("I study maps.") + strlen("Farewell.") + 3;
        literal("Read ");
        bytecode(142);
        literal("INFAMA");
        bytecode(142);
        literal(".");
        bytecode(0);
        entry("I study maps.");
        entry("Farewell.");
        tail();
        keys("bye\r", 0);
    } else if (!strcmp(name, "ask_name")) {
        memset(&scenario, 0, sizeof(scenario));
        scenario.name = name;
        entry("Name");
        literal("a traveller.\"\n\n");
        bytecode(136);
        literal(" Good day.");
        bytecode(0);
        entry("Welcome.");
        entry("I study maps.");
        entry("Farewell.");
        tail();
        keys("Avatar\rbye\r", 0);
    } else if (!strcmp(name, "payment")) {
        base(name, "a traveller.", "Welcome.");
        entry("pay");
        literal("Pay now.\"\n\n");
        bytecode(133);
        literal("999");
        bytecode(0);
        tail();
        keys("pay\rbye\r", 0);
    } else if (!strcmp(name, "nested")) {
        base(name, "a traveller.", "Welcome.");
        entry("hint");
        literal("Choose.\"\n\n");
        bytecode(145);
        bytecode(0);
        bytecode(144);
        bytecode(145);
        entry("First question?");
        literal("No.");
        bytecode(130);
        bytecode(0);
        entry("yes");
        literal("Next.\"\n\n");
        bytecode(254);
        bytecode(50);
        bytecode(146);
        bytecode(0);
        bytecode(144);
        bytecode(146);
        entry("Second question?");
        literal("No.");
        bytecode(130);
        bytecode(0);
        entry("yes");
        literal("Done.");
        bytecode(137);
        bytecode(130);
        bytecode(0);
        tail();
        keys("hint\ryes\ryes\r", 0);
    } else if (!strcmp(name, "introduction")) {
        base(name, "a careful traveller.", "Welcome.");
        scenario.introduced = 0;
        tail();
        keys("bye\r", 0);
    } else
        fail("unknown fixture");
}
static void load_font(const char *path, byte *font) {
    FILE *f = fopen(path, "rb");
    if (!f || fread(font, 1, 1024, f) != 1024)
        fail("cannot read required 1024-byte font");
    fclose(f);
}
static void path_join(char *path, size_t capacity, const char *directory, const char *name) {
    if (snprintf(path, capacity, "%s/%s", directory, name) >= (int)capacity)
        fail("artifact path too long");
}
static unsigned number(const char *text, unsigned maximum) {
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (!*text || *text == '-' || *end || value > maximum)
        fail("invalid numeric option");
    return (unsigned)value;
}
static unsigned le16(const byte *p) { return p[0] | ((unsigned)p[1] << 8); }
static void load_conversation(const char *path, unsigned id) {
    byte file[65536];
    FILE *f = fopen(path, "rb");
    if (!f)
        fail("cannot open TLK resource");
    size_t size = fread(file, 1, sizeof(file), f);
    if (ferror(f) || fgetc(f) != EOF || size < 2)
        fail("invalid or oversized TLK resource");
    fclose(f);
    unsigned count = le16(file), previous = 2 + count * 4, begin = 0, end = 0;
    if (!count || count > 127 || previous >= size)
        fail("invalid native TLK header");
    for (unsigned i = 0; i < count; ++i) {
        unsigned offset = le16(file + 4 + i * 4);
        if (offset < previous || offset >= size)
            fail("invalid TLK conversation offsets");
        previous = offset + 1;
        if (le16(file + 2 + i * 4) == id) {
            if (begin)
                fail("duplicate conversation identity");
            begin = offset;
            end = i + 1 < count ? le16(file + 8 + i * 4) : (unsigned)size;
        }
    }
    if (!begin || end <= begin || end - begin > 1024 || begin > 32767)
        fail("conversation missing or outside native buffer/offset limits");
    scenario.size = end - begin;
    memcpy(scenario.bytes, file + begin, scenario.size);
    unsigned entries = 0;
    bool has_tail = false;
    for (size_t i = 0; i < scenario.size;) {
        int code = scenario.bytes[i], operands = code == 133                  ? 3
                                                 : code == 134 || code == 140 ? 1
                                                 : code == 254                ? 2
                                                                              : 0;
        if (i + operands >= scenario.size)
            fail("truncated dialogue operand");
        for (int n = 1; n <= operands; ++n)
            if (!scenario.bytes[i + n])
                fail("NUL inside dialogue operand");
        if (!code)
            ++entries;
        if (i + 2 < scenario.size && code == 144 && scenario.bytes[i + 1] == 159 &&
            scenario.bytes[i + 2] == 192)
            has_tail = true;
        i += 1 + operands;
    }
    if (entries < 5 || !has_tail)
        fail("reference runner requires the five native fields and structural tail");
}
static void load_script(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        fail("cannot open key script");
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        unsigned delay;
        int key;
        char extra;
        if (sscanf(line, "%u %d %c", &delay, &key, &extra) != 2 || delay > 300000 || key < 1 ||
            key > 511 || scenario.key_count >= MAX_KEYS)
            fail("key script lines must contain delay_ms and a native key code (1..511)");
        scenario.keys[scenario.key_count++] = (ScriptKey){delay, key};
    }
    if (ferror(f) || !scenario.key_count)
        fail("empty or unreadable key script");
    fclose(f);
}
static void save_pixels(const char *directory, const char *name, const byte *pixels, int width,
                        int height) {
    char path[4096];
    path_join(path, sizeof(path), directory, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        fail("cannot create framebuffer artifact");
    if (fwrite(pixels, 1, (size_t)width * height, f) != (size_t)width * height || fclose(f))
        fail("cannot write framebuffer artifact");
}
static void save_game(const char *directory, const char *name) {
    byte state[4192] = {0};
#define COPY(offset, value) memcpy(state + offset, &(value), sizeof(value))
    memcpy(state+2,D_55a8_party,16*32); /* Fixed legacy save prefix. */
    COPY(0x202, D_57a8);
    COPY(0x204, D_57aa);
    COPY(0x206, D_57ac);
    COPY(0x207, D_57ad);
    COPY(0x208, D_57ae);
    COPY(0x209, D_57af);
    COPY(0x20a, D_57b0);
    COPY(0x20b, D_57b1);
    COPY(0x214, D_57ba);
    COPY(0x216, D_57bc);
    COPY(0x218, D_57be);
    COPY(0x21a, D_57c0);
    COPY(0x24a, D_57f0);
    COPY(0x2b5, D_585b);
    COPY(0x2e2, D_5888);
    COPY(0x2e5, D_588b);
    COPY(0x2ed, D_5893_map_id);
    COPY(0x2eb, D_5891);
    COPY(0x2d5, D_587b);
    COPY(0x2d6, D_587c_partyTile);
    COPY(0x2ce, D_5874);
    COPY(0x2d7, D_587d);
    COPY(0x2d8, D_587e);
    /* This oracle artifact is the original DOS record, not the extended runtime capacity. */
    memcpy(state + 0x5b4, D_5b5a, 128);
    memcpy(state + 0x634, D_5bda, 128);
    memcpy(state + 0x6b4, D_5c5a, 256);
    memcpy(state + 0x7b8, D_5d5e, 512);
    memcpy(state + 0x9b8, D_5f5e, 512);
    memcpy(state + 0xff8, D_659e, 32);
#undef COPY
    save_pixels(directory, name, state, sizeof(state), 1);
}
static Result run(const char *directory, bool write_artifacts) {
    memcpy(normal_font, normal_original, 1024);
    memcpy(rune_font, rune_original, 1024);
    memset(D_b21e, 0, sizeof(D_b21e));
    memcpy(D_b21e, scenario.bytes, scenario.size);
    memset(D_5bda, 0, sizeof(D_5bda));
    memset(D_5f5e, 0, sizeof(D_5f5e));
    memset(D_5c5a, 0, sizeof(D_5c5a));
    memset(D_55a8_party, 0, sizeof(D_55a8_party));
    memset(D_5b5a, 0, sizeof(D_5b5a));
    memset(D_5d5e, 0, sizeof(D_5d5e));
    memset(D_659e, 0, sizeof(D_659e));
    memset(D_57c0, 0, sizeof(D_57c0));
    memset(D_57f0, 0, sizeof(D_57f0));
    D_57a8 = 0;
    D_57ad = D_57ae = D_57af = D_57b0 = D_57b1 = 0;
    D_57ba = D_57bc = D_57be = D_588b = 0;
    strcpy(D_55a8_party[0].name, "Avatar");
    strcpy(D_55a8_party[1].name, "Iolo");
    strcpy(D_55a8_party[2].name, "Shamino");
    for (int i = 0; i < 3; ++i) {
        D_55a8_party[i].status = 'G';
        D_55a8_party[i].hp = D_55a8_party[i].maxHp = 30;
    }
    D_585b = 3;
    D_57aa = 100;
    D_57ac = 0;
    D_5888 = 50;
    D_5893_map_id = 13;
    D_bcdc = 5;
    D_58a4 = 0;
    D_5891 = 1;
    D_5f5e[5]._a = (u16)conversation_id;
    D_5f5e[5]._0 = 1;
    D_5f5e[5]._c = 5;
    D_659e[5] = D_5c5a[5]._0_tile = 0x44;
    if (!strncmp(scenario.name, "join_", 5)) {
        if (strcmp(scenario.name, "join_nomatch"))
            strcpy(D_55a8_party[15].name, "Nora");
        D_55a8_party[15].mapId = 13;
        if (!strcmp(scenario.name, "join_full"))
            D_585b = 6;
        D_5c5a[5]._5 = 77;
        D_5d5e[5].type[0] = 1;
    }
    if (!strncmp(scenario.name, "beggar_", 7)) {
        D_659e[5] = D_5c5a[5]._0_tile = 0x6c;
        D_588b = !strcmp(scenario.name, "beggar_early") ? 99 : 100;
        if (!strcmp(scenario.name, "beggar_insufficient"))
            D_57aa = 99;
    }
    if (!strncmp(scenario.name, "guards_", 7)) {
        for (int i = 1; i < 32; ++i) {
            D_5f5e[i]._0 = i % 7 != 0;
            D_5f5e[i]._a = i % 2 ? 0xfe : 2;
            D_659e[i] = i % 5 == 0   ? 0x70
                        : i % 5 == 1 ? 0xd8
                        : i % 5 == 2 ? 0xfc
                        : i % 5 == 3 ? 0x44
                                     : 0x10;
            D_5d5e[i].time[0] = i % 3 == 0 ? 0 : i;
            D_5d5e[i].x[0] = i;
        }
        D_5f5e[5]._a = (u16)conversation_id;
    }
    if (!strcmp(scenario.name, "give_caps")) {
        memset(D_57c0, 99, sizeof(D_57c0));
        D_57a8 = D_57aa = 9999;
        D_57ac = D_57ad = D_57ae = D_57af = D_57b0 = D_57b1 = 99;
    }
    if (scenario.introduced)
        D_5bda[48] = 32;
    D_4aee = D_4aef = D_4af1 = D_4af2 = D_4af3 = D_4af4 = D_4af5 = 0;
    D_4af0 = 255;
    D_540c = 0;
    D_538a = D_538c = 0;
    D_538e = 1;
    D_bcde = D_b21e;
    D_539c[0] = normal_font;
    D_539c[1] = rune_font;
    D_52ba_vdp._52c8_videoDriverSelection = VDP_VIDEO_EGA;
    D_13b0_brightWhiteColor = 15;
    D_13b2_blueColor = 1;
    now = 0;
    scheduled = false;
    recording = false;
    trace_hash = 2166136261u;
    event_count = scroll_count = glyph_count = sleep55_count = clear_count = 0;
    pause_count = 0;
    pausing = false;
    opcode_entry = opcode_offset = -1;
    rune_glyph_count = 0;
    snapshot_count = 0;
    key_index = input_count = 0;
    key_count = scenario.key_count;
    script = scenario.keys;
    ULTIMA_207e_srand((uint)seed);
    GRAP_BUF_Initialize(NULL);
    ULTIMA_1158_InitTimer();
    ULTIMA_1c9e_SelectCharset(0);
    ULTIMA_1c22_SetTextWindowSize(1, 24, 1, 39, 9);
    ULTIMA_1c22_SetTextWindowSize(2, 24, 11, 39, 23);
    ULTIMA_637e_DrawFrame();
    ULTIMA_1b94_SelectTextWindow(2);
    ULTIMA_1bf2_SetTextPosition(start_x, start_y);
    ULTIMA_1cca_SetTextForegroundColor(15);
    ULTIMA_1f26_SetTextBackgroundColor(0);
    glyph_count = 0;
    if (write_artifacts) {
        char path[4096];
        path_join(path, sizeof(path), directory, "events.jsonl");
        trace = fopen(path, "wb");
        if (!trace)
            fail("cannot create trace");
        path_join(path, sizeof(path), directory, "snapshots.bin");
        snapshots = fopen(path, "wb");
        if (!snapshots)
            fail("cannot create snapshots");
        save_pixels(directory, "initial-frame.bin", g_linearEgaBuffer0, 320, 200);
        save_pixels(directory, "conversation.tlk-entry", scenario.bytes, (int)scenario.size, 1);
        save_pixels(directory, "normal.font", normal_original, 1024, 1);
        save_pixels(directory, "runes.font", rune_original, 1024, 1);
        save_game(directory, "initial-game.bin");
        path_join(path, sizeof(path), directory, "keys.tsv");
        FILE *keys_file = fopen(path, "wb");
        if (!keys_file)
            fail("cannot create key script artifact");
        for (int i = 0; i < key_count; ++i)
            fprintf(keys_file, "%u %d\n", script[i].delay, script[i].key);
        if (fclose(keys_file))
            fail("cannot write key script artifact");
    } else
        trace = snapshots = NULL;
    recording = true;
    event("start", 0, 16, 13);
    if (TALK_111c() == 0 && TALK_0b04() == 0)
        TALK_0a3c();
    TALK_04e2_PrintWord();
    event("finish", 0, 0, 0);
    assert(allow_unused_keys || key_index == key_count);
    Result result = {0};
    result.time = now;
    result.trace_hash = trace_hash;
    result.pixel_hash = pixel_hash();
    result.intro = introduction();
    result.events = event_count;
    result.scrolls = scroll_count;
    result.glyphs = glyph_count;
    result.rune_glyphs = rune_glyph_count;
    result.snapshots = snapshot_count;
    result.sleep55 = sleep55_count;
    result.clears = clear_count;
    result.keys = key_index;
    result.x = D_539a_currentTextWindow->currentX;
    result.y = D_539a_currentTextWindow->currentY;
    result.charset = D_5388_current_charset_idx;
    result.karma = D_5888;
    result.gold = D_57aa;
    result.supplies = D_57ac;
    for (int y = 0; y < TEXT_H; ++y)
        memcpy(result.pixels + y * TEXT_W, g_linearEgaBuffer0 + (TEXT_Y + y) * 320 + TEXT_X,
               TEXT_W);
    assert(!memcmp(normal_font, normal_original, 1024) && !memcmp(rune_font, rune_original, 1024));
    assert(result.x < 16 && result.y < 13 && result.charset == 0);
    if (!strcmp(scenario.name, "questions"))
        assert(result.karma == 51 && result.supplies == 1);
    if (!strcmp(scenario.name, "fallback"))
        assert(result.karma == 50 && result.supplies == 0);
    if (!strcmp(scenario.name, "scroll"))
        assert(result.scrolls >= (start_y == 12 ? 22u : 1u));
    if (!strcmp(scenario.name, "runes"))
        assert(result.rune_glyphs == 6);
    if (!strcmp(scenario.name, "pause_interrupt"))
        assert(pause_count == 1 && pause_ticks[0] == 2);
    if (!strcmp(scenario.name, "pause_full"))
        assert(pause_count == 1 && pause_ticks[0] == 28);
    if (!strcmp(scenario.name, "editing"))
        assert(input_count == 3 && !strcmp(input_strings[0], "JOB") &&
               !strcmp(input_strings[1], "WORK"));
    if (!strcmp(scenario.name, "input_limit"))
        assert(input_count == 2 && !strcmp(input_strings[0], "ABCDEFGHIJKLMNO") &&
               !strcmp(input_strings[1], "BYE"));
    if (write_artifacts) {
        save_game(directory, "final-game.bin");
        save_pixels(directory, "frame.bin", g_linearEgaBuffer0, 320, 200);
        save_pixels(directory, "window.bin", result.pixels, TEXT_W, TEXT_H);
        if (fclose(trace) || fclose(snapshots))
            fail("cannot finalize reference artifacts");
        trace = snapshots = NULL;
        char path[4096];
        path_join(path, sizeof(path), directory, "manifest.json");
        FILE *f = fopen(path, "wb");
        if (!f)
            fail("cannot create manifest");
        fprintf(
            f,
            "{\"schema\":1,\"source\":\"%s\",\"case\":\"%s\",\"seed\":%u,\"conversation\":%d,"
            "\"font_crc\":%u,\"rune_crc\":%u,\"window\":[192,88,128,104],\"pixel_aspect\":[5,6],"
            "\"initial_cursor\":[%d,%d],\"cursor\":[%d,%d],\"font\":%d,\"ms\":%u,\"events\":%u,"
            "\"scrolls\":%u,\"snapshots\":%u,"
            "\"trace_hash\":%u,\"pixel_hash\":%u,\"keys_consumed\":%d,\"karma\":%d,"
            "\"gold\":%d,\"keys\":%d,\"introduced_flags\":%u,\"keyboard_clear\":\"native_stub\","
            "\"world_rendering\":false,\"recruitment_guards\":\"native\","
            "\"context\":{\"map\":13,\"npc_slot\":5,\"party\":[\"Avatar\",\"Iolo\",\"Shamino\"],"
            "\"gold\":100,\"karma\":50,\"keys\":0,\"introduced\":%d}}\n",
            REFERENCE_SOURCE_ID, scenario.name, seed, conversation_id,
            MOD_Crc32(normal_original, 1024), MOD_Crc32(rune_original, 1024), start_x, start_y,
            result.x, result.y, result.charset, now, event_count, scroll_count, snapshot_count,
            result.trace_hash, result.pixel_hash, key_index, result.karma, result.gold,
            result.supplies, result.intro, scenario.introduced);
        if (fclose(f))
            fail("cannot write manifest");
    }
    recording = false;
    GRAP_BUF_Cleanup();
    return result;
}
int main(int argc, char **argv) {
    const char *output = ".", *only = NULL, *game = getenv("U5_GAME_DIR");
    const char *tlk = NULL, *script_path = NULL, *font_path = NULL, *rune_path = NULL;
    int introduced = 1;
    if (!game)
        game = TEST_GAME_DIR;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            puts("impera_dialogue_reference --output DIR [--case NAME] [--game DIR]\n"
                 "  [--font IBM.CH] [--runes RUNES.CH] [--seed 0..65535] [--x 0..15] [--y 0..12]\n"
                 "  --tlk FILE --npc ID --script FILE [--introduced 0|1]\n"
                 "Script lines: delay_ms native_key_code. Each delay starts at its first poll.");
            return 0;
        }
        if (i + 1 >= argc)
            fail("expected a value after command option");
        if (!strcmp(argv[i], "--output"))
            output = argv[++i];
        else if (!strcmp(argv[i], "--case"))
            only = argv[++i];
        else if (!strcmp(argv[i], "--game"))
            game = argv[++i];
        else if (!strcmp(argv[i], "--font"))
            font_path = argv[++i];
        else if (!strcmp(argv[i], "--runes"))
            rune_path = argv[++i];
        else if (!strcmp(argv[i], "--tlk"))
            tlk = argv[++i];
        else if (!strcmp(argv[i], "--script"))
            script_path = argv[++i];
        else if (!strcmp(argv[i], "--npc"))
            conversation_id = (int)number(argv[++i], 65535);
        else if (!strcmp(argv[i], "--seed"))
            seed = number(argv[++i], 65535);
        else if (!strcmp(argv[i], "--x"))
            start_x = (int)number(argv[++i], 15);
        else if (!strcmp(argv[i], "--y"))
            start_y = (int)number(argv[++i], 12);
        else if (!strcmp(argv[i], "--introduced"))
            introduced = (int)number(argv[++i], 1);
        else if (!strcmp(argv[i], "--allow-unused-keys"))
            allow_unused_keys = number(argv[++i], 1) != 0;
        else
            fail("unknown command option");
    }
    char path[4096];
    path_join(path, sizeof(path), game, "IBM.CH");
    load_font(font_path ? font_path : path, normal_original);
    path_join(path, sizeof(path), game, "RUNES.CH");
    load_font(rune_path ? rune_path : path, rune_original);
    if (tlk || script_path) {
        if (!tlk || !script_path || only)
            fail("custom runs require both --tlk and --script, without --case");
        memset(&scenario, 0, sizeof(scenario));
        scenario.name = "custom";
        scenario.introduced = introduced;
        load_conversation(tlk, (unsigned)conversation_id);
        load_script(script_path);
        path_join(path, sizeof(path), output, "custom");
        if (!SDL_CreateDirectory(path))
            fail("cannot create artifact directory");
        Result first = run(path, true), repeated = run(path, false);
        assert(!memcmp(&first, &repeated, sizeof(first)));
        printf("PASS native custom conversation %d: %u events, pixels %08x\n", conversation_id,
               first.events, first.pixel_hash);
        return 0;
    }
    if (!only && (start_x != 0 || start_y != 12))
        fail("custom cursor positions require --case or --tlk");
    const char *cases[] = {"questions",       "fallback",     "scroll",
                           "pause_interrupt", "pause_full",   "editing",
                           "input_limit",     "introduction", "runes",
                           "ask_name",        "payment",      "nested",
                           "left_editing",    "join_success", "join_full",
                           "join_nomatch",    "guards_met",   "guards_first",
                           "beggar_reward",   "beggar_early", "beggar_insufficient",
                           "give_caps",       "give_growth"};
    int count = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (only && strcmp(only, cases[i]))
            continue;
        build(cases[i]);
        path_join(path, sizeof(path), output, cases[i]);
        if (!SDL_CreateDirectory(path))
            fail("cannot create artifact directory");
        Result first = run(path, true), repeated = run(path, false);
        assert(!memcmp(&first, &repeated, sizeof(first)));
        printf("PASS native %s: %u events, %u scrolls, %u ms, pixels %08x\n", cases[i],
               first.events, first.scrolls, first.time, first.pixel_hash);
        ++count;
    }
    if (!count)
        fail("unknown case");
    return 0;
}
