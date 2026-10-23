#include "maps.h"
#include "contents.h"
#include "discoveries.h"
#include "package.h"
#include "types.h"
#include <string.h>
static ContentsDef defs[CONTENTS_MAX], suppressed;
static ContentsState states[CONTENTS_MAX], encounter[CONTENTS_MAX];
static unsigned count, stateCount, battleKind = 255, battleMap;
unsigned CONTENTS_Word(const unsigned char *p) { return p[0] | ((unsigned)p[1] << 8); }
uint32_t CONTENTS_Dword(const unsigned char *p) {
    return CONTENTS_Word(p) | ((uint32_t)CONTENTS_Word(p + 2) << 16);
}
void CONTENTS_PutWord(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
void CONTENTS_PutDword(unsigned char *p, uint32_t v) {
    CONTENTS_PutWord(p, v);
    CONTENTS_PutWord(p + 2, v >> 16);
}
void CONTENTS_DecodeRecord(const unsigned char *p, const unsigned char *space, ContentsDef *d) {
    memset(d, 0, sizeof(*d));
    memcpy(d->space, space, 16);
    d->id = CONTENTS_Dword(p);
    d->map = CONTENTS_Word(p + 4);
    d->floor = (int8_t)p[6];
    d->kind = p[7];
    d->x = p[8];
    d->y = p[9];
    d->action = p[10];
    d->refill = p[11];
    d->original = CONTENTS_Word(p + 12);
    d->slot = p[14];
    d->flags = p[15];
    d->lockDifficulty = p[16];
    d->trapDifficulty = p[17];
    d->trapEffect = p[18];
    d->count = p[19];
    for (unsigned i = 0; i < CONTENTS_ITEMS; i++) {
        d->items[i].type = p[32 + 4 * i];
        d->items[i].variant = p[33 + 4 * i];
        d->items[i].quantity = CONTENTS_Word(p + 34 + 4 * i);
    }
}
void CONTENTS_EncodeRecord(unsigned char *p, const ContentsDef *d) {
    memset(p, 0, CONTENTS_RECORD);
    CONTENTS_PutDword(p, d->id);
    CONTENTS_PutWord(p + 4, d->map);
    p[6] = (unsigned char)d->floor;
    p[7] = d->kind;
    p[8] = d->x;
    p[9] = d->y;
    p[10] = d->action;
    p[11] = d->refill;
    CONTENTS_PutWord(p + 12, d->original);
    p[14] = d->slot;
    p[15] = d->flags;
    p[16] = d->lockDifficulty;
    p[17] = d->trapDifficulty;
    p[18] = d->trapEffect;
    p[19] = d->count;
    for (unsigned i = 0; i < d->count; i++) {
        p[32 + 4 * i] = d->items[i].type;
        p[33 + 4 * i] = d->items[i].variant;
        CONTENTS_PutWord(p + 34 + 4 * i, d->items[i].quantity);
    }
}
static int targetEqual(const ContentsDef *a, const ContentsDef *b) {
    return a->kind == b->kind && a->map == b->map && a->floor == b->floor && a->x == b->x &&
           a->y == b->y;
}
int CONTENTS_Validate(const void *data, size_t size) {
    const unsigned char *p = data;
    if (!p || size < 32 || memcmp(p, "ICONT001", 8))
        return 0;
    unsigned n = CONTENTS_Word(p + 24);
    if (n > CONTENTS_MAX || size != 32 + (size_t)n * 96)
        return 0;
    unsigned nonzero = 0;
    for (int i = 8; i < 24; i++)
        nonzero |= p[i];
    if (!nonzero)
        return 0;
    for (int i = 26; i < 32; i++)
        if (p[i])
            return 0;
    for (unsigned i = 0; i < n; i++) {
        const unsigned char *r = p + 32 + i * 96;
        ContentsDef d;
        CONTENTS_DecodeRecord(r, p + 8, &d);
        if (!d.id || d.kind > 2 || d.action > 1 || d.refill > 2 || d.count > 16 || d.flags > 3 ||
            d.trapEffect || d.lockDifficulty > 30 || d.trapDifficulty > 30)
            return 0;
        if (d.kind == 0) {
            if ((d.map > 40 && !MAPS_NewLocation(d.map)) || d.floor < -1 || d.floor > 7 ||
                (!d.map && d.floor != 0 && d.floor != -1))
                return 0;
            if (d.map >= 33 && d.map <= 40 && (d.x >= 8 || d.y >= 8 || d.floor < 0))
                return 0;

        } else if (d.map >= (d.kind == 1 ? 16 : 112) || d.floor || d.slot != 255 || d.refill != 0)
            return 0;
        if (d.slot != 255 && (d.slot < 1 || d.slot > 31 || d.kind || d.map || d.action != 1))
            return 0;
        if (d.action == 0 && d.flags)
            return 0;
        if (d.original != 65535 && (d.original >= 113 || d.kind))
            return 0;
        if (d.refill == 2 && d.original != 13 && d.original != 14 && d.original != 15)
            return 0;
        for (int k = 20; k < 32; k++)
            if (r[k])
                return 0;
        for (unsigned j = 0; j < 16; j++) {
            ContentsItem t = d.items[j];
            if (j >= d.count) {
                if (t.type || t.variant || t.quantity)
                    return 0;
                continue;
            }
            if (!t.type || t.type > 9 || !t.quantity || t.quantity > (t.type <= 2 ? 9999 : 99))
                return 0;
            if (t.variant >= (t.type == 7 ? 48 : t.type >= 8 ? 8 : 1))
                return 0;
        }
        for (unsigned j = 0; j < i; j++) {
            ContentsDef old;
            CONTENTS_DecodeRecord(p + 32 + j * 96, p + 8, &old);
            if (old.id == d.id || targetEqual(&d, &old) ||
                (d.original != 65535 && old.original == d.original))
                return 0;
        }
    }
    return 1;
}
void CONTENTS_Unmount(void) {
    count = 0;
    CONTENTS_Reset();
}
int CONTENTS_CanMount(const void *data, size_t size) {
    if (!CONTENTS_Validate(data, size))
        return 0;
    const unsigned char *p = data;
    unsigned n = CONTENTS_Word(p + 24);
    if (n > CONTENTS_MAX - count)
        return 0;
    for (unsigned i = 0; i < n; i++) {
        ContentsDef d;
        CONTENTS_DecodeRecord(p + 32 + i * 96, p + 8, &d);
        for (unsigned j = 0; j < count; j++)
            if (targetEqual(&d, &defs[j]) ||
                (!memcmp(d.space, defs[j].space, 16) && d.id == defs[j].id) ||
                (d.original != 65535 && d.original == defs[j].original))
                return 0;
    }
    return 1;
}
void CONTENTS_Mount(const void *data, size_t size) {
    if (!CONTENTS_CanMount(data, size))
        return;
    const unsigned char *p = data;
    unsigned n = CONTENTS_Word(p + 24);
    for (unsigned i = 0; i < n; i++)
        CONTENTS_DecodeRecord(p + 32 + i * 96, p + 8, &defs[count++]);
}
unsigned CONTENTS_Count(void) { return count; }
const ContentsDef *CONTENTS_Def(unsigned i) { return i < count ? &defs[i] : NULL; }
const ContentsDef *CONTENTS_Find(unsigned kind, unsigned map, int floor, int x, int y) {
    for (unsigned i = 0; i < count; i++)
        if (defs[i].kind == kind && defs[i].map == map && defs[i].floor == floor &&
            defs[i].x == x && defs[i].y == y)
            return &defs[i];
    if (!kind)
        for (unsigned i = 0; i < count; i++)
            if (defs[i].original != 65535) {
                const unsigned char *o = U5_Discoveries[defs[i].original];
                if (o[2] == map && (int8_t)o[3] == floor && o[4] == x && o[5] == y) {
                    memset(&suppressed, 0, sizeof(suppressed));
                    suppressed.slot = 255;
                    return &suppressed;
                }
            }
    return NULL;
}
void CONTENTS_Reset(void) {
    memset(states, 0, sizeof(states));
    stateCount = 0;
    CONTENTS_EndEncounter();
}
void CONTENTS_BeginEncounter(unsigned kind, unsigned map) {
    MAPS_Combat(kind, map);
    TYPES_BeginEncounter(kind, map);
    battleKind = kind;
    battleMap = map;
    memset(encounter, 0, sizeof(encounter));
}
void CONTENTS_EndEncounter(void) {
    MAPS_Combat(255, 0);
    battleKind = 255;
    battleMap = 0;
    memset(encounter, 0, sizeof(encounter));
}
unsigned CONTENTS_EncounterKind(void) { return battleKind; }
unsigned CONTENTS_EncounterMap(void) { return battleMap; }
const ContentsState *CONTENTS_Peek(const ContentsDef *d) {
    static const ContentsState empty = {0};
    if (!d || !d->id)
        return &empty;
    if (d->kind) {
        if (d->kind != battleKind || d->map != battleMap)
            return &empty;
        return &encounter[d - defs];
    }
    for (unsigned i = 0; i < stateCount; i++)
        if (states[i].id == d->id && !memcmp(states[i].space, d->space, 16))
            return &states[i];
    return &empty;
}
ContentsState *CONTENTS_State(const ContentsDef *d) {
    if (!d || !d->id)
        return NULL;
    if (d->kind) {
        if (d->kind != battleKind || d->map != battleMap)
            return NULL;
        return &encounter[d - defs];
    }
    for (unsigned i = 0; i < stateCount; i++)
        if (states[i].id == d->id && !memcmp(states[i].space, d->space, 16))
            return &states[i];
    if (stateCount == CONTENTS_MAX)
        return NULL;
    ContentsState *s = &states[stateCount++];
    memset(s, 0, sizeof(*s));
    s->id = d->id;
    memcpy(s->space, d->space, 16);
    return s;
}
uint32_t CONTENTS_GameDay(unsigned year, unsigned month, unsigned day) {
    return (year * 13u + month) * 28u + day;
}
int CONTENTS_Available(const ContentsDef *d, ContentsState *s, uint32_t day, const uint8_t *bits,
                       unsigned lastDay, unsigned keys, unsigned questItem) {
    if (!d || !d->id || !s)
        return 0;
    if (s->flags & 1) {
        for (unsigned i = 0; i < 16; i++)
            if (s->remaining[i])
                return 0;
    }
    if (d->refill == 1) {
        if (!(s->flags & 1) && d->original == 14 && lastDay == (day % 28 ? day % 28 : 28))
            return 0; // Import vanilla's already-collected Minoc reward on the first visit.
        return !(s->flags & 1) || s->day != day;
    }
    if (d->refill == 2) {
        if (d->original == 13)
            return !keys;
        if (d->original == 14)
            return (s->flags & 1) ? s->day != day : lastDay != (day % 28 ? day % 28 : 28);
        if (d->original == 15)
            return !questItem;
        return 0;
    }
    if (s->flags & 8)
        return 0;
    if (!(s->flags & 1) && d->original != 65535 && d->original != 13 && d->original != 14 &&
        d->original != 15 && bits && (bits[d->original / 8] & (1u << (d->original % 8))))
        return 0;
    return 1;
}
int CONTENTS_Reveal(const ContentsDef *d, ContentsState *s, uint32_t day) {
    if (!d || !s)
        return 0;
    for (unsigned i = 0; i < 16; i++)
        if (s->remaining[i])
            return 0;
    for (unsigned i = 0; i < d->count; i++)
        s->remaining[i] = d->items[i].quantity;
    s->flags |= 1 | 8;
    s->day = day;
    return 1;
}
int CONTENTS_PendingShape(unsigned kind, unsigned map, int floor, int x, int y) {
    const ContentsDef *d = CONTENTS_Find(kind, map, floor, x, y);
    if (!d || !d->id)
        return 0;
    const ContentsState *s = CONTENTS_Peek(d);
    static const int shapes[] = {0, 2, 15, 13, 8, 7, 7, 5, 3, 4};
    for (unsigned i = 0; i < d->count; i++)
        if (s->remaining[i])
            return d->items[i].type == 7 ? U5_ContentsEquipmentShapes[d->items[i].variant]
                                         : shapes[d->items[i].type];
    return 0;
}
int CONTENTS_WriteState(FILE *f) {
    if (!stateCount)
        return TYPES_WriteState(f);
    unsigned char data[CONTENTS_SAVE_MAX] = {0};
    memcpy(data, "ICSAVE01", 8);
    CONTENTS_PutDword(data + 8, stateCount);
    for (unsigned i = 0; i < stateCount; i++) {
        unsigned char *p = data + 16 + 64 * i;
        const ContentsState *s = &states[i];
        memcpy(p, s->space, 16);
        CONTENTS_PutDword(p + 16, s->id);
        CONTENTS_PutDword(p + 20, s->day);
        p[24] = s->flags;
        for (unsigned j = 0; j < 16; j++)
            CONTENTS_PutWord(p + 32 + 2 * j, s->remaining[j]);
    }
    size_t len = 16 + 64 * stateCount;
    CONTENTS_PutDword(data + 12, MOD_Crc32(data + 16, len - 16));
    return fwrite(data, 1, len, f) == len && TYPES_WriteState(f);
}
int CONTENTS_ReadState(FILE *f, int apply) {
    unsigned char data[CONTENTS_SAVE_MAX];
    size_t n = fread(data, 1, 16, f);
    if (n == 16 && !memcmp(data, "ITYSAV01", 8)) {
        long pos = ftell(f) - 16;
        if (pos < 0 || fseek(f, pos, SEEK_SET) || !TYPES_ReadState(f, 0))
            return 0;
        if (apply) {
            if (fseek(f, pos, SEEK_SET) || !TYPES_ReadState(f, 1))
                return 0;
            CONTENTS_Reset();
        }
        return 1;
    }
    if (n == 16 && !memcmp(data, "ICSAVE01", 8) && CONTENTS_Dword(data + 8) <= CONTENTS_MAX)
        n += fread(data + 16, 1, 64 * CONTENTS_Dword(data + 8), f);
    if (!n) {
        if (ferror(f))
            return 0;
        if (apply)
            CONTENTS_Reset();
        if (apply)
            TYPES_Reset();
        return 1;
    }
    if (n < 16 || memcmp(data, "ICSAVE01", 8) || CONTENTS_Dword(data + 8) > CONTENTS_MAX ||
        n != 16 + 64 * CONTENTS_Dword(data + 8) ||
        MOD_Crc32(data + 16, n - 16) != CONTENTS_Dword(data + 12))
        return 0;
    unsigned num = CONTENTS_Dword(data + 8);
    ContentsState next[CONTENTS_MAX];
    memset(next, 0, sizeof(next));
    for (unsigned i = 0; i < num; i++) {
        unsigned char *p = data + 16 + 64 * i;
        ContentsState *s = &next[i];
        memcpy(s->space, p, 16);
        s->id = CONTENTS_Dword(p + 16);
        s->day = CONTENTS_Dword(p + 20);
        s->flags = p[24];
        if (!s->id || s->flags > 15)
            return 0;
        for (int k = 25; k < 32; k++)
            if (p[k])
                return 0;
        unsigned nonzero = 0;
        for (unsigned j = 0; j < 16; j++)
            nonzero |= s->space[j];
        if (!nonzero)
            return 0;
        for (unsigned j = 0; j < 16; j++) {
            s->remaining[j] = CONTENTS_Word(p + 32 + j * 2);
            if (s->remaining[j] > 9999 || (s->remaining[j] && !(s->flags & 1)))
                return 0;
        }
        for (unsigned j = 0; j < i; j++)
            if (s->id == next[j].id && !memcmp(s->space, next[j].space, 16))
                return 0;
    }
    long typeOffset = ftell(f);
    if (typeOffset < 0 || !TYPES_ReadState(f, 0))
        return 0;
    if (apply && (fseek(f, typeOffset, SEEK_SET) || !TYPES_ReadState(f, 1)))
        return 0;
    if (apply) {
        memcpy(states, next, sizeof(states));
        stateCount = num;
        CONTENTS_EndEncounter();
    }
    return 1;
}
