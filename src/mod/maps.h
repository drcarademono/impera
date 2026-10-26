#ifndef IMPERA_MAPS_H
#define IMPERA_MAPS_H
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define MAPS_HEADER 16u
#define MAPS_RECORD 128u
#define MAPS_MAX 4096u
#define MAPS_MAX_BYTES (8u * 1024u * 1024u)
static inline unsigned MAPS_Word(const void *v) {
    const unsigned char *p = (const unsigned char *)v;
    return p[0] | (unsigned)p[1] << 8;
}
static inline uint32_t MAPS_U32(const void *v) {
    const unsigned char *p = (const unsigned char *)v;
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline int MAPS_NewLocation(unsigned id) {
    return id >= 42 && id < 128 && (id < 64 || id > 66);
}
static inline int MAPS_Town(unsigned id) { return (id >= 1 && id <= 32) || MAPS_NewLocation(id); }
#ifdef __cplusplus
extern "C" {
#endif
int MAPS_IsDungeon(unsigned id);
int MAPS_IsTown(unsigned id);
int MAPS_DungeonRoom(unsigned id, unsigned room);
int MAPS_Outside(unsigned id);
void MAPS_Entered(unsigned map, int world, int x, int y);
int MAPS_ReturnPoint(unsigned map, int *world, int *x, int *y);
#ifdef __cplusplus
}
#endif
/* New maps use independent resources, never a slot in a native town bank. */
static inline unsigned MAPS_LocationResource(const char *name) {
    if (!name || strlen(name) != 10 || strncmp(name, "LOC", 3) || name[3] < '0' || name[3] > '9' ||
        name[4] < '0' || name[4] > '9' || name[5] < '0' || name[5] > '9' ||
        (strcmp(name + 6, ".DAT") && strcmp(name + 6, ".NPC") && strcmp(name + 6, ".TLK")))
        return 0;
    unsigned id = (name[3] - '0') * 100 + (name[4] - '0') * 10 + name[5] - '0';
    return MAPS_NewLocation(id) ? id : 0;
}
/* Combat maps use the previously reserved byte 23 for the high identity byte. */
static inline unsigned MAPS_Id(const unsigned char *r) {
    return r[5] | (r[4] == 2 || r[4] == 3 ? (unsigned)r[23] << 8 : 0);
}
static inline uint32_t MAPS_Key(const unsigned char *r) {
    return (uint32_t)r[4] << 24 | (uint32_t)MAPS_Id(r) << 8 | (uint8_t)(r[8] + 128);
}
static inline int MAPS_Validate(const void *v, size_t n) {
    const unsigned char *b = (const unsigned char *)v;
    if (!b || n > MAPS_MAX_BYTES || n < MAPS_HEADER || memcmp(b, "IMAPS001", 8) ||
        MAPS_U32(b + 8) > MAPS_MAX || MAPS_U32(b + 12))
        return 0;
    size_t off = MAPS_HEADER;
    uint32_t prev = 0;
    for (unsigned i = 0; i < MAPS_U32(b + 8); ++i) {
        if (n - off < MAPS_RECORD)
            return 0;
        const unsigned char *r = b + off;
        unsigned w = MAPS_Word(r + 10), h = MAPS_Word(r + 12), length = MAPS_U32(r);
        if (r[4] > 4 || !w || !h || w > 256 || h > 256 || length != MAPS_RECORD + w * h ||
            length > n - off || (i && MAPS_Key(r) <= prev) || !memchr(r + 24, 0, 64) || !r[24] ||
            !memchr(r + 88, 0, 32) || !r[88])
            return 0;
        if ((!r[4] && (r[5] > 1 || r[8])) ||
            (r[4] == 1 && (!MAPS_Town(r[5]) ||
                           (MAPS_NewLocation(r[5]) && ((int8_t)r[8] < -1 || (int8_t)r[8] > 3)))) ||
            ((r[4] == 2 || r[4] == 3) && r[8]) ||
            (r[4] == 4 && (!((r[5] >= 33 && r[5] <= 40) || MAPS_NewLocation(r[5])) || r[8] > 7)) ||
            (int8_t)r[14] < -2 || (int8_t)r[14] > 0 ||
            (MAPS_NewLocation(r[5]) && r[4] == 1 && !r[8] &&
             ((int8_t)r[14] == -2 || r[17] >= w || r[18] >= h)))
            return 0;
        for (unsigned j = 21; j < 24; ++j)
            if (r[j] && !(r[4] == 4 && MAPS_NewLocation(r[5]) && j < 23) &&
                !(j == 23 && (r[4] == 2 || r[4] == 3)))
                return 0;
        if (r[20] > 1 || (r[20] && (r[4] < 2 || r[4] == 4)))
            return 0;
        for (unsigned j = 120; j < 128; ++j)
            if (r[j] && !r[20])
                return 0;
        for (unsigned j = 24; r[j] && j < 88; ++j)
            if (r[j] < 32 || r[j] > 126)
                return 0;
        for (unsigned j = 88; r[j] && j < 120; ++j)
            if (!((r[j] >= 'A' && r[j] <= 'Z') || (r[j] >= '0' && r[j] <= '9') || r[j] == '.' ||
                  r[j] == '_'))
                return 0;
        const char *resource = (const char *)r + 88;
        unsigned page = MAPS_Word(r + 6);
        if (r[4] == 0) {
            if (page || strcmp(resource, r[5] ? "UNDER.DAT" : "BRIT.DAT"))
                return 0;
        } else if (r[4] == 1) {
            if (MAPS_NewLocation(r[5])) {
                if (page > 4 || MAPS_LocationResource(resource) != r[5] ||
                    strcmp(resource + 6, ".DAT"))
                    return 0;
            } else {
                static const char *banks[] = {"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT",
                                              "KEEP.DAT"};
                if ((int8_t)r[8] < -1 || (int8_t)r[8] > 3 ||
                    strcmp(resource, banks[(r[5] - 1) / 8]))
                    return 0;
            }
        } else if (r[4] == 4) {
            if (strcmp(resource, "DUNGEON.DAT") ||
                (r[5] <= 40 ? page != (r[5] - 33) * 8 + r[8]
                            : page < 64 || MAPS_Word(r + 21) < 112 || MAPS_Word(r + 21) > 65520 ||
                                  MAPS_Word(r + 21) % 16))
                return 0;
        } else if (page != MAPS_Id(r) || strcmp(resource, r[4] == 2 ? "BRIT.CBT" : "DUNGEON.CBT"))
            return 0;
        if (!((r[4] == 1 || r[4] == 4) && !r[8]) && ((int8_t)r[14] != -2 || r[19]))
            return 0;
        if (r[4] == 4 && MAPS_NewLocation(r[5]) && !r[8] &&
            ((int8_t)r[14] == -2 || r[17] >= w || r[18] >= h))
            return 0;
        if ((int8_t)r[14] != -2 && (r[17] >= w || r[18] >= h))
            return 0;
        /* Disallow page aliases and duplicate entrance coordinates. */
        for (size_t earlier = MAPS_HEADER; earlier < off; earlier += MAPS_U32(b + earlier)) {
            const unsigned char *other = b + earlier;
            if (MAPS_Word(other + 6) == page && !strcmp((const char *)other + 88, resource))
                return 0;
            if (MAPS_NewLocation(r[5]) && (r[4] == 1 || r[4] == 4) &&
                (other[4] == 1 || other[4] == 4) && r[5] == other[5] && r[4] != other[4])
                return 0;
            if (r[4] == 4 && MAPS_NewLocation(r[5]) && other[4] == 4 &&
                MAPS_NewLocation(other[5]) &&
                (r[5] == other[5] ? MAPS_Word(r + 21) != MAPS_Word(other + 21) ||
                                        page - r[8] != MAPS_Word(other + 6) - other[8]
                                  : MAPS_Word(r + 21) == MAPS_Word(other + 21)))
                return 0;
            if ((int8_t)r[14] != -2 && r[14] == other[14] && r[15] == other[15] &&
                r[16] == other[16])
                return 0;
        }
        prev = MAPS_Key(r);
        off += length;
    }
    return off == n;
}
static inline const unsigned char *MAPS_Find(const void *v, size_t n, unsigned kind, unsigned id,
                                             int floor) {
    const unsigned char *b = (const unsigned char *)v;
    if (!b || n < MAPS_HEADER)
        return NULL;
    size_t off = MAPS_HEADER;
    for (unsigned i = 0; i < MAPS_U32(b + 8) && off + MAPS_RECORD <= n; ++i) {
        const unsigned char *r = b + off;
        unsigned length = MAPS_U32(r);
        if (length < MAPS_RECORD || length > n - off)
            return NULL;
        if (r[4] == kind && MAPS_Id(r) == id && (int8_t)r[8] == floor)
            return r;
        off += length;
    }
    return NULL;
}
static inline int MAPS_Contains(const void *catalog, size_t size, unsigned kind, unsigned id,
                                int floor, int x, int y) {
    const unsigned char *r = MAPS_Find(catalog, size, kind, id, floor);
    int w = r ? MAPS_Word(r + 10) : kind == 0 ? 256 : kind == 1 ? 32 : kind == 4 ? 8 : 11;
    int h = r ? MAPS_Word(r + 12) : w;
    if (kind == 1 && MAPS_NewLocation(id) && !r)
        return 0;
    return x >= 0 && y >= 0 && x < w && y < h;
}
static inline int MAPS_MarkerActive(const unsigned char *r, unsigned bit, int x, int y) {
    if (bit >= 64)
        return 0;
    return r && r[20] ? !!(r[120 + bit / 8] & (1u << (bit % 8)))
                      : x >= 0 && y >= 0 && x < 11 && y < 11;
}
#ifdef __cplusplus
extern "C" {
#endif
int MAPS_Mount(const void *, size_t);
void MAPS_Reset(void);
const unsigned char *MAPS_OriginalRecord(unsigned kind, unsigned id, int floor);
const unsigned char *MAPS_Record(unsigned kind, unsigned id, int floor);
const unsigned char *MAPS_Entrance(int world, int x, int y);
void MAPS_Combat(unsigned kind, unsigned id);
int MAPS_CombatMarkerActive(unsigned bit, int x, int y);
void MAPS_ConsumeTrigger(unsigned slot);
int MAPS_ActiveWidth(unsigned map, int level);
int MAPS_ActiveHeight(unsigned map, int level);
int MAPS_Wrap(int value, int size);
unsigned char *MAPS_ActiveCell(unsigned map, int level, int x, int y);
unsigned char *MAPS_LocationCell(unsigned map, int level, int x, int y, unsigned char *native,
                                 unsigned char *background);
unsigned char *MAPS_DungeonCell(unsigned map, int level, int x, int y, unsigned char *native);
unsigned char *MAPS_CombatCell(int x, int y, unsigned char *native);
int MAPS_WriteState(FILE *);
int MAPS_ReadState(FILE *, int apply);
#ifdef __cplusplus
}
#endif
#endif
