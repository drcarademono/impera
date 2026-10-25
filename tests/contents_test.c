#undef NDEBUG
#include "mod/contents.h"
#include "mod/discoveries.h"
#include "mod/package.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char bytes[32 + 96 * 4];
static ContentsDef definition(unsigned id, int kind, int map, int floor, int x, int y) {
    ContentsDef d = {0};
    d.id = id;
    d.kind = kind;
    d.map = map;
    d.floor = floor;
    d.x = x;
    d.y = y;
    d.slot = 255;
    d.original = 65535;
    d.count = 1;
    d.items[0] = (ContentsItem){6, 0, 5};
    return d;
}
static size_t encode(const ContentsDef *d, unsigned n) {
    memset(bytes, 0, sizeof(bytes));
    memcpy(bytes, "ICONT001", 8);
    bytes[8] = 42;
    CONTENTS_PutWord(bytes + 24, n);
    for (unsigned i = 0; i < n; i++)
        CONTENTS_EncodeRecord(bytes + 32 + i * 96, &d[i]);
    return 32 + 96 * n;
}
int main(void) {
    ContentsDef portable = definition(900, 0, 2, 0, 1, 1);
    ContentsState initial = {0};
    portable.refill = 3;
    assert(CONTENTS_Available(&portable, &initial, 1, NULL, 0, 0, 0));
    assert(!CONTENTS_Available(&portable, &initial, 1, NULL, 0, 1, 0));
    portable.refill = 4;
    assert(CONTENTS_Available(&portable, &initial, 1, NULL, 0, 0, 0));
    assert(!CONTENTS_Available(&portable, &initial, 1, NULL, 0, 0, 1));
    assert(U5_Discoveries[14][0] == 7 && U5_Discoveries[14][1] == 133 &&
           U5_Discoveries[14][2] == 5 && U5_Discoveries[14][4] == 2 && U5_Discoveries[14][5] == 2);
    ContentsDef d[4] = {definition(15, 0, 5, 0, 2, 2), definition(100, 0, 0, -1, 130, 150),
                        definition(101, 1, 4, 0, 1, 1), definition(102, 2, 16, 0, 2, 2)};
    d[0].original = 14;
    d[0].refill = 2;
    d[1].action = 1;
    d[1].flags = 3;
    d[1].items[0] = (ContentsItem){7, 2, 1};
    size_t size = encode(d, 4);
    assert(CONTENTS_Validate(bytes, size));
    unsigned char valid[sizeof(bytes)];
    memcpy(valid, bytes, sizeof(bytes));
    assert(!CONTENTS_Validate(bytes, size - 1));
    bytes[26] = 1;
    assert(!CONTENTS_Validate(bytes, size));
    memcpy(bytes, valid, sizeof(bytes));
    bytes[32 + 19] = 17;
    assert(!CONTENTS_Validate(bytes, size));
    memcpy(bytes, valid, sizeof(bytes));
    bytes[32 + 32] = 10;
    assert(!CONTENTS_Validate(bytes, size));
    memcpy(bytes, valid, sizeof(bytes));
    CONTENTS_PutWord(bytes + 32 + 34, 100);
    assert(!CONTENTS_Validate(bytes, size));
    memcpy(bytes, valid, sizeof(bytes));
    CONTENTS_PutDword(bytes + 32 + 96, 15);
    assert(!CONTENTS_Validate(bytes, size));
    memcpy(bytes, valid, sizeof(bytes));
    CONTENTS_Unmount();
    assert(CONTENTS_CanMount(bytes, size));
    CONTENTS_Mount(bytes, size);
    assert(CONTENTS_Count() == 4);
    assert(!CONTENTS_CanMount(bytes, size));
    const ContentsDef *minoc = CONTENTS_Find(0, 5, 0, 2, 2);
    assert(minoc && minoc->original == 14);
    assert(!CONTENTS_Find(0, 5, 1, 2, 2));
    FILE *f = tmpfile();
    assert(f);
    assert(CONTENTS_PendingShape(0, 5, 0, 2, 2) == 0);
    assert(CONTENTS_WriteState(f));
    assert(ftell(f) == 0);
    fclose(f); /* Rendering is read-only. */
    ContentsState *s = CONTENTS_State(minoc);
    uint8_t bits[24] = {0};
    uint32_t day = CONTENTS_GameDay(139, 5, 7);
    assert(!CONTENTS_Available(minoc, s, day, bits, 7, 0, 0));
    assert(CONTENTS_Available(minoc, s, day, bits, 6, 0, 0));
    assert(CONTENTS_Reveal(minoc, s, day));
    assert(s->remaining[0] == 5 && CONTENTS_PendingShape(0, 5, 0, 2, 2) == 7);
    assert(!CONTENTS_Available(minoc, s, day + 1, bits, 7, 0, 0));
    s->remaining[0] = 0;
    assert(!CONTENTS_Available(minoc, s, day, bits, 7, 0, 0));
    assert(CONTENTS_Available(minoc, s, day + 1, bits, 7, 0, 0));
    assert(CONTENTS_GameDay(140, 1, 1) == CONTENTS_GameDay(139, 13, 28) + 1);
    assert(CONTENTS_Reveal(minoc, s, day + 1));
    s->remaining[0] = 3;
    const ContentsDef *world = CONTENTS_Find(0, 0, -1, 130, 150);
    ContentsState *w = CONTENTS_State(world);
    assert(CONTENTS_Available(world, w, day, bits, 0, 0, 0));
    assert(CONTENTS_Reveal(world, w, day));
    assert(CONTENTS_PendingShape(0, 0, -1, 130, 150) == 9);
    w->remaining[0] = 0;
    assert(!CONTENTS_Available(world, w, day + 1, bits, 0, 0, 0));
    w->flags |= 2 | 4;
    f = tmpfile();
    assert(f);
    assert(CONTENTS_WriteState(f));
    long len = ftell(f);
    assert(len > 16);
    CONTENTS_Reset();
    rewind(f);
    assert(CONTENTS_ReadState(f, 1));
    assert(CONTENTS_State(minoc)->remaining[0] == 3 && CONTENTS_State(world)->flags == 15);
    rewind(f);
    assert(fseek(f, 17, SEEK_SET) == 0);
    assert(fputc(255, f) != EOF);
    rewind(f);
    assert(!CONTENTS_ReadState(f, 1));
    assert(CONTENTS_State(minoc)->remaining[0] == 3);
    fclose(f);
    CONTENTS_BeginEncounter(1, 4);
    const ContentsDef *battle = CONTENTS_Find(1, 4, 0, 1, 1);
    assert(CONTENTS_Reveal(battle, CONTENTS_State(battle), day));
    CONTENTS_State(battle)->remaining[0] = 0;
    assert(!CONTENTS_Available(battle, CONTENTS_State(battle), day, bits, 0, 0, 0));
    CONTENTS_BeginEncounter(1, 4);
    assert(CONTENTS_Available(battle, CONTENTS_State(battle), day, bits, 0, 0, 0));
    CONTENTS_BeginEncounter(2, 16);
    assert(CONTENTS_State(battle) == NULL);
    assert(CONTENTS_Find(2, 16, 0, 2, 2));
    CONTENTS_EndEncounter();
    assert(CONTENTS_State(battle) == NULL);
    /* Move keeps identity, suppresses original site, and restores vanilla after unmount. */
    CONTENTS_Unmount();
    d[0].x = 3;
    d[0].y = 4;
    size = encode(d, 1);
    CONTENTS_Mount(bytes, size);
    assert(CONTENTS_Find(0, 5, 0, 2, 2) && !CONTENTS_Find(0, 5, 0, 2, 2)->id);
    assert(CONTENTS_Find(0, 5, 0, 3, 4)->id == 15);
    CONTENTS_Unmount();
    assert(!CONTENTS_Find(0, 5, 0, 2, 2));
    /* Normal original discoveries import vanilla consumption; explicit empty stays once-only. */
    d[0] = definition(2, 0, 1, 0, 1, 1);
    d[0].original = 18;
    d[0].count = 0;
    memset(d[0].items, 0, sizeof(d[0].items));
    size = encode(d, 1);
    CONTENTS_Mount(bytes, size);
    minoc = CONTENTS_Find(0, 1, 0, 1, 1);
    s = CONTENTS_State(minoc);
    bits[18 / 8] |= 1u << (18 % 8);
    assert(!CONTENTS_Available(minoc, s, day, bits, 0, 0, 0));
    bits[18 / 8] = 0;
    assert(CONTENTS_Available(minoc, s, day, bits, 0, 0, 0));
    assert(CONTENTS_Reveal(minoc, s, day));
    assert(!CONTENTS_Available(minoc, s, day + 1, bits, 0, 0, 0));
    ContentsDef daily = {0};
    daily.id = 1;
    daily.original = 14;
    daily.refill = 1;
    ContentsState fresh = {0};
    assert(!CONTENTS_Available(&daily, &fresh, day, bits, day % 28 ? day % 28 : 28, 0, 0));
    assert(CONTENTS_Available(&daily, &fresh, day + 1, bits, day % 28 ? day % 28 : 28, 0, 0));
    puts("PASS contents schema, original Minoc discovery, inheritance, refill, "
         "capacity-independent pending loot, persistence, encounter instances and relocation");
    return 0;
}
