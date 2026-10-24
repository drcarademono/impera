#include "maps.h"
#include "package.h"
#include "types.h"
#include <stdlib.h>
/* Original catalog belongs to the mounted package; working terrain is private. */
static const unsigned char *source;
static unsigned char *working;
static size_t catalogSize;
static unsigned combatKind = 255, combatMap;
static unsigned consumedTriggers;
static unsigned char outside = 255;
static int cacheValid, cacheFloor;
static unsigned cacheKind, cacheId;
static const unsigned char *cacheRecord;
int MAPS_Mount(const void *bytes, size_t size) {
    unsigned char *next = NULL;
    if (bytes) {
        if (!MAPS_Validate(bytes, size) || !(next = malloc(size)))
            return 0;
        memcpy(next, bytes, size);
    }
    cacheValid = 0;
    free(working);
    working = next;
    source = bytes;
    catalogSize = bytes ? size : 0;
    combatKind = 255;
    combatMap = 0;
    return 1;
}
void MAPS_Reset(void) {
    if (working)
        memcpy(working, source, catalogSize);
}
const unsigned char *MAPS_Record(unsigned kind, unsigned id, int floor) {
    if (!cacheValid || cacheKind != kind || cacheId != id || cacheFloor != floor) {
        cacheRecord = MAPS_Find(working, catalogSize, kind, id, floor);
        cacheKind = kind;
        cacheId = id;
        cacheFloor = floor;
        cacheValid = 1;
    }
    return cacheRecord;
}
void MAPS_Combat(unsigned kind, unsigned id) {
    consumedTriggers = 0;
    combatKind = kind;
    combatMap = id;
}
static const unsigned char *active(unsigned map, int level) {
    return MAPS_Record(map >= 128               ? combatKind + 1
                       : map >= 33 && map <= 40 ? 4
                       : map                    ? 1
                                                : 0,
                       map >= 128 ? combatMap
                       : map      ? map
                                  : !!level,
                       map >= 128 || !map ? 0 : (int8_t)level);
}
int MAPS_CombatMarkerActive(unsigned bit, int x, int y) {
    if (bit >= 40 && bit < 48 && (consumedTriggers & (1u << (bit - 40))))
        return 0;
    return MAPS_MarkerActive(active(128, 0), bit, x, y);
}
void MAPS_ConsumeTrigger(unsigned slot) {
    if (slot < 8)
        consumedTriggers |= 1u << slot;
}
int MAPS_ActiveWidth(unsigned map, int level) {
    const unsigned char *r = active(map, level);
    return r ? MAPS_Word(r + 10) : !map ? 256 : map >= 128 ? 11 : map >= 33 && map <= 40 ? 8 : 32;
}
int MAPS_ActiveHeight(unsigned map, int level) {
    const unsigned char *r = active(map, level);
    return r ? MAPS_Word(r + 12) : !map ? 256 : map >= 128 ? 11 : map >= 33 && map <= 40 ? 8 : 32;
}
int MAPS_Wrap(int value, int size) {
    int n = value % size;
    return n < 0 ? n + size : n;
}
unsigned char *MAPS_ActiveCell(unsigned map, int level, int x, int y) {
    const unsigned char *r = active(map, level);
    if (!r)
        return NULL;
    int w = MAPS_Word(r + 10), h = MAPS_Word(r + 12);
    if (!map) {
        x = MAPS_Wrap(x, w);
        y = MAPS_Wrap(y, h);
    }
    if (x < 0 || y < 0 || x >= w || y >= h) {
        outside = r[9];
        return &outside;
    }
    return (unsigned char *)r + MAPS_RECORD + y * w + x;
}
unsigned char *MAPS_DungeonCell(unsigned map, int level, int x, int y, unsigned char *native) {
    if (level < 0 || level >= 8) {
        outside = 0xb0;
        return &outside;
    }
    const unsigned char *r = MAPS_Record(4, map, level);
    int w = r ? MAPS_Word(r + 10) : 8, h = r ? MAPS_Word(r + 12) : 8;
    x = MAPS_Wrap(x, w);
    y = MAPS_Wrap(y, h);
    return r ? (unsigned char *)r + MAPS_RECORD + y * w + x : native + level * 64 + y * 8 + x;
}
unsigned char *MAPS_LocationCell(unsigned map, int level, int x, int y, unsigned char *native,
                                 unsigned char *background) {
    unsigned char *p = MAPS_ActiveCell(map, level, x, y);
    return p ? p : x < 0 || y < 0 || x >= 32 || y >= 32 ? background : native + y * 32 + x;
}
unsigned char *MAPS_CombatCell(int x, int y, unsigned char *native) {
    unsigned char *p = MAPS_ActiveCell(128, 0, x, y);
    if (p)
        return p;
    if (x < 0 || y < 0 || x >= 11 || y >= 11) {
        outside = 255;
        return &outside;
    }
    return native + y * 32 + x;
}
const unsigned char *MAPS_Entrance(int world, int x, int y) {
    for (size_t off = MAPS_HEADER; working && off < catalogSize; off += MAPS_U32(working + off)) {
        const unsigned char *r = working + off;
        if (r[4] == 1 && MAPS_NewLocation(r[5]) && (int8_t)r[14] == (int8_t)world && r[15] == x &&
            r[16] == y)
            return r;
    }
    return NULL;
}
int MAPS_WriteState(FILE *f) {
    unsigned count = 0;
    size_t size = 0;
    for (size_t off = MAPS_HEADER; working && off < catalogSize; off += MAPS_U32(working + off)) {
        unsigned length = MAPS_U32(working + off);
        if (memcmp(working + off + MAPS_RECORD, source + off + MAPS_RECORD, length - MAPS_RECORD)) {
            count++;
            size += length;
        }
    }
    if (!count)
        return 1;
    unsigned char *b = malloc(size), h[24] = {0};
    if (!b)
        return 0;
    size_t dest = 0;
    for (size_t off = MAPS_HEADER; off < catalogSize; off += MAPS_U32(working + off)) {
        unsigned length = MAPS_U32(working + off);
        if (memcmp(working + off + MAPS_RECORD, source + off + MAPS_RECORD, length - MAPS_RECORD)) {
            memcpy(b + dest, working + off, length);
            dest += length;
        }
    }
    memcpy(h, "IMPSAV01", 8);
    TYPES_Put32(h + 8, size);
    TYPES_Put32(h + 12, count);
    TYPES_Put32(h + 16, MOD_Crc32(b, size));
    int ok = fwrite(h, 1, 24, f) == 24 && fwrite(b, 1, size, f) == size;
    free(b);
    return ok;
}
int MAPS_ReadState(FILE *f, int apply) {
    long start = ftell(f);
    unsigned char h[24] = {0};
    size_t n = fread(h, 1, 24, f);
    if (n < 8 || memcmp(h, "IMPSAV01", 8)) {
        if (ferror(f) || fseek(f, start, SEEK_SET))
            return 0;
        if (apply)
            MAPS_Reset();
        return 1;
    }
    unsigned size = MAPS_U32(h + 8), count = MAPS_U32(h + 12);
    if (n != 24 || size > MAPS_MAX_BYTES || count > MAPS_MAX || !count || MAPS_U32(h + 20))
        return 0;
    unsigned char *b = calloc(1, MAPS_HEADER + size);
    if (!b)
        return 0;
    memcpy(b, "IMAPS001", 8);
    TYPES_Put32(b + 8, count);
    int ok = fread(b + MAPS_HEADER, 1, size, f) == size &&
             MOD_Crc32(b + MAPS_HEADER, size) == MAPS_U32(h + 16) &&
             MAPS_Validate(b, MAPS_HEADER + size);
    for (size_t off = MAPS_HEADER; ok && off < MAPS_HEADER + size; off += MAPS_U32(b + off)) {
        const unsigned char *r = b + off,
                            *orig = MAPS_Find(source, catalogSize, r[4], r[5], (int8_t)r[8]);
        if (!orig || memcmp(orig, r, MAPS_RECORD))
            ok = 0;
    }
    if (ok && apply) {
        MAPS_Reset();
        for (size_t off = MAPS_HEADER; off < MAPS_HEADER + size; off += MAPS_U32(b + off)) {
            const unsigned char *r = b + off;
            unsigned char *target = (unsigned char *)MAPS_Record(r[4], r[5], (int8_t)r[8]);
            memcpy(target + MAPS_RECORD, r + MAPS_RECORD, MAPS_U32(r) - MAPS_RECORD);
        }
    }
    free(b);
    return ok;
}
