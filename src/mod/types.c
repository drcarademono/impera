#include "maps.h"
#include "types.h"
#include "types_state.h"
#include "package.h"
#include "type_defaults.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned word(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
uint32_t TYPES_U32(const void *v) {
    const unsigned char *p = v;
    return word(p) | (uint32_t)word(p + 2) << 16;
}
void TYPES_Put32(void *v, uint32_t n) {
    unsigned char *p = v;
    for (int i = 0; i < 4; i++)
        p[i] = (unsigned char)(n >> (i * 8));
}
static void put16(unsigned char *p, unsigned n) {
    p[0] = n;
    p[1] = n >> 8;
}
void TYPES_Decode(const unsigned char *p, TypeDef *d) {
    memset(d, 0, sizeof(*d));
    d->id = TYPES_U32(p + 4);
    d->kind = p[8];
    d->base = p[9];
    d->filters = p[10];
    d->render = p[11];
    d->movement = word(p + 12);
    d->sight = p[14];
    d->projectile = p[15];
    d->travel = p[16];
    d->effects = p[17];
    d->damageMin = p[18];
    d->damageMax = p[19];
    d->ground = TYPES_U32(p + 20);
    for (int i = 0; i < 4; i++)
        d->result[i] = TYPES_U32(p + 24 + i * 4);
    memcpy(d->stats, p + 40, 8);
    d->actorFlags = word(p + 48);
    d->moveProfile = p[50];
    d->range = p[51];
    d->projectileAppearance = p[52];
    d->companion = p[53];
    d->effectModes = word(p + 54);
    memcpy(d->name, p + 56, 64);
    memcpy(d->description, p + 120, 128);
    d->skiffDirections = p[250];
    d->humanoidSeat = p[251];
    d->combatMap = p[252];
    d->frames = word(p + 248);
    d->width = 1 + p[253];
    d->height = 1 + p[254];
    d->animation = p[255];
}
void TYPES_Encode(unsigned char *p, const TypeDef *d) {
    memset(p, 0, TYPES_RECORD);
    TYPES_Put32(p, TYPES_RECORD);
    TYPES_Put32(p + 4, d->id);
    p[8] = d->kind;
    p[9] = d->base;
    p[10] = d->filters;
    p[11] = d->render;
    put16(p + 12, d->movement);
    p[14] = d->sight;
    p[15] = d->projectile;
    p[16] = d->travel;
    p[17] = d->effects;
    p[18] = d->damageMin;
    p[19] = d->damageMax;
    TYPES_Put32(p + 20, d->ground);
    for (int i = 0; i < 4; i++)
        TYPES_Put32(p + 24 + i * 4, d->result[i]);
    memcpy(p + 40, d->stats, 8);
    put16(p + 48, d->actorFlags);
    p[50] = d->moveProfile;
    p[51] = d->range;
    p[52] = d->projectileAppearance;
    p[53] = d->companion;
    put16(p + 54, d->effectModes);
    memcpy(p + 56, d->name, 64);
    memcpy(p + 120, d->description, 128);
    p[250] = d->skiffDirections;
    p[251] = d->humanoidSeat;
    p[252] = d->combatMap;
    p[253] = TYPES_Width(d) - 1;
    p[254] = TYPES_Height(d) - 1;
    p[255] = d->animation;
    put16(p + 248, d->frames);
}
int TYPES_Walkable(unsigned a, unsigned t) {
    if (a > 255 || t > 255)
        return 0;
    int normal = !(vanilla_D_54d4[t >> 3] & (0x80 >> (t & 7)));
    if ((a & 0xfe) != 0x1c && (a & 0xf0) != 0x40 && (t & 0xfc) == 0x90)
        normal = 0;
    int sea = t < 4 || (t & 0xf0) == 0x60;
    switch (vanilla_D_54f4[a >> 2]) {
    case 0:
        return normal;
    case 1:
        return sea;
    case 2:
        return sea || normal;
    case 3:
        return normal && t != 0x8f && t != 4;
    case 4:
        return !sea;
    case 5:
        if ((t & 0xfc) == 0x34)
            return !!((8 >> (a & 3)) & vanilla_D_5544[t - 0x34]);
        return sea && (t < 0x60 || (vanilla_D_5534[t - 0x60] & (8 >> (a & 3))));
    case 6:
        return t <= 2;
    case 7:
        return t == 4;
    case 8:
        return t == 5;
    case 9:
        return t == 1;
    case 10:
        return t == 7;
    default:
        return 0;
    }
}
void TYPES_Vanilla(unsigned kind, unsigned id, TypeDef *d) {
    memset(d, 0, sizeof(*d));
    d->kind = kind;
    d->base = id;
    d->id = kind ? 256 + id : id;
    d->ground = 5;
    d->effectModes = 2047;
    if (kind) {
        unsigned i = id >= 64 ? (id - 64) / 4 : 48;
        d->moveProfile = vanilla_D_54f4[(id & 255) >> 2];
        if (i < 48) {
            memcpy(d->stats, vanillaStats[i], 8);
            d->actorFlags = vanilla_D_153c_enemyFlags[i];
            d->range = vanilla_D_159c[i];
            d->projectileAppearance = vanilla_D_15cc[i];
            d->companion = vanilla_D_16d4[i];
            snprintf(d->name, 64, "%s", vanilla_D_1856[i]);
            snprintf(d->description, 128, "%s", vanilla_D_18b6[i]);
        }
        return;
    }
    const unsigned actors[11] = {0x1c, 0x80, 0x14, 0x10, 0x9c, 0x28, 0x20, 0xf8, 0xf4, 0xec, 0xe0};
    for (unsigned i = 0; i < 11; i++)
        if (TYPES_Walkable(actors[i], id))
            d->movement |= 1u << i;
    for (unsigned direction = 0; direction < 4; direction++)
        if (TYPES_Walkable(0x28 + direction, id))
            d->skiffDirections |= 8u >> direction;
    if (d->skiffDirections)
        d->movement |= 1u << 5;
    d->humanoidSeat = (id & 0xfc) == 0x90;
    d->sight = memchr(vanilla_D_6a86, id, sizeof(vanilla_D_6a86)) ? 1 : 0;
    if (id == 0x4a || id == 0x4b || id == 0xba || id == 0xbb || id == 0x98)
        d->sight = 2;
    d->projectile = !!(vanilla_D_6a14[id >> 3] & (0x80 >> (id & 7)));
    d->travel = id == 5                      ? 0
                : (id == 0x1e || id == 0x1f) ? 1
                : (id < 4 || id >= 0x10)     ? 0
                : id >= 9                    ? 2
                                             : 1;
    d->effects = id == 4 ? TYPES_POISON : (id == 0x8f || id == 0xbc) ? TYPES_BURN : 0;
    if (id == 4)
        d->effectModes = 1;
    d->damageMin = 1;
    d->damageMax = 8;
    if (id == 0x4a || id == 0x4b || (id >= 0x4d && id <= 0x57) || id == 0x5a ||
        (id >= 0x70 && id <= 0x7f) || id == 0x87 || id == 0x97 || id == 0x98 ||
        (id >= 0xb8 && id <= 0xbc) || id == 0xf8 || id == 0xfe)
        d->render |= TYPES_MASONRY;
    if (id == 0xa1 || (id & 0xfc) == 0xd8 || id == 0xb2 || id == 0xb3 || id == 0xde || id == 0x59 ||
        id == 0xfc || id == 0xfd || (id >= 0x80 && id <= 0x84) || id == 0x86 || id == 0x8b ||
        id == 0x8e || id == 0x99 || id == 0xaa || id == 0xc8 || id == 0xc9 || id == 0x88 ||
        id == 0xa3 || (id >= 0xb4 && id <= 0xb7))
        d->render |= TYPES_SPRITE;
    if (id == 0xb8 || id == 0xb9) {
        d->result[0] = 0x44;
        d->result[1] = 0xb8;
        d->result[2] = 0xb9;
    }
    if (id == 0xba || id == 0xbb) {
        d->result[0] = 0x44;
        d->result[1] = 0xba;
        d->result[2] = 0xbb;
    }
    if (id == 0x97) {
        d->result[1] = 0xb8;
        d->result[2] = 0xb9;
    }
    if (id == 0x98) {
        d->result[1] = 0xba;
        d->result[2] = 0xbb;
    }
    d->result[3] = id == 0x9d ? 0x9f : id == 0x2e ? 0x2b : id == 0x2d ? 0x2c : 0;
    snprintf(d->name, 64, "Tile %u", id);
}
static int zeros(const unsigned char *p, size_t n) {
    while (n--)
        if (*p++)
            return 0;
    return 1;
}
const unsigned char *TYPES_Find(const void *v, size_t size, uint32_t id) {
    const unsigned char *data = v;
    if (!data || size < 32)
        return NULL;
    size_t off = 32;
    unsigned n = TYPES_U32(data + 24);
    if (n > TYPES_MAX)
        return NULL;
    for (unsigned i = 0; i < n; i++) {
        if (off > size || size - off < 256)
            return NULL;
        const unsigned char *p = data + off;
        unsigned len = TYPES_U32(p);
        if (len < 256 || len > size - off)
            return NULL;
        if (TYPES_U32(p + 4) == id)
            return p;
        off += len;
    }
    return NULL;
}
int TYPES_Validate(const void *v, size_t size) {
    const unsigned char *b = v;
    if (!b || size < 32 || size > MOD_MAX_RESOURCE || memcmp(b, "ITYPES01", 8) ||
        zeros(b + 8, 16) || !zeros(b + 28, 4))
        return 0;
    unsigned count = TYPES_U32(b + 24), prev = 0;
    if (count > TYPES_MAX)
        return 0;
    size_t off = 32;
    for (unsigned i = 0; i < count; i++) {
        if (size - off < 256)
            return 0;
        const unsigned char *p = b + off;
        unsigned len = TYPES_U32(p);
        if (len < 256 || len > size - off)
            return 0;
        TypeDef d;
        TYPES_Decode(p, &d);
        if (d.id > TYPES_LAST_CUSTOM || (i && d.id <= prev) || d.kind > 2 || d.filters > 3 ||
            d.render > 3 || d.movement > 2047 || d.sight > 2 || d.projectile > 1 || d.travel > 2 ||
            d.effects > 3 || d.damageMin > d.damageMax || d.moveProfile > 10 ||
            d.projectileAppearance > 7 || !memchr(d.name, 0, 64) || !d.name[0] ||
            !memchr(d.description, 0, 128) || d.effectModes > 2047 || d.skiffDirections > 15 ||
            d.humanoidSeat > 1 || d.combatMap > 16 || p[253] > 1 || p[254] > 1 || p[255] > 1 ||
            ((!d.kind || d.id < 512) && p[255]) || ((!d.kind || d.id < 512) && (p[253] || p[254])))
            return 0;
        if (d.id < 512 && ((d.kind == 0 && d.id > 255) || (d.kind && (d.id < 256 || d.id > 511))))
            return 0;
        if (d.kind && d.id < 512 && d.base != d.id - 256)
            return 0;
        if (d.kind && d.id >= 512 &&
            (d.base < 64 || (d.base & 3) || !d.stats[5] || !d.stats[6] || d.stats[6] > 16 ||
             d.stats[1] > 30 || d.companion >= 48 || d.range > 11))
            return 0;
        if (d.frames > 768 || (d.id >= 512 && !d.frames))
            return 0;
        unsigned variants[12] = {0}, last = 0;
        size_t f = 256;
        for (unsigned j = 0; j < d.frames; j++) {
            if (len - f < 4)
                return 0;
            unsigned variant = p[f], side = p[f + 1] & 127, ms = word(p + f + 2);
            if (variant > 11 || (j && variant < last) ||
                (side != 16 && side != 32 && !(variant % 6 == 2 && side == 14)) || ms < 10 ||
                ms > 60000)
                return 0;
            size_t bytes = TYPES_FrameBytes(p, f);
            if (bytes > len - f)
                return 0;
            if (p[f + 1] & 128) {
                size_t pixels =
                    (size_t)TYPES_FrameWidth(p, variant, side) * TYPES_FrameRows(p, variant, side);
                for (size_t k = 0; k < pixels; ++k)
                    if (p[f + 4 + pixels * 4 + k] != 0 && p[f + 4 + pixels * 4 + k] != 255)
                        return 0;
            }
            if (++variants[variant] > 64)
                return 0;
            last = variant;
            f += bytes;
        }
        if (f != len || (d.frames && !variants[0]))
            return 0;
        for (int k = 1; k < 12; k++)
            if (k != 6 && variants[k] && variants[k] != variants[k < 6 ? 0 : 6])
                return 0;
        if ((p[253] || p[254]) && (!variants[6] || !d.kind))
            return 0;
        if (!d.kind)
            for (int k = 6; k < 12; ++k)
                if (variants[k])
                    return 0;
        prev = d.id;
        off += len;
    }
    if (off != size)
        return 0;
    off = 32;
    for (unsigned i = 0; i < count; i++) {
        TypeDef d;
        TYPES_Decode(b + off, &d);
        uint32_t refs[5] = {d.ground, d.result[0], d.result[1], d.result[2], d.result[3]};
        for (unsigned j = 0; j < 5; j++) {
            if (!refs[j] || (j == 0 && refs[j] == TYPES_INFER_GROUND))
                continue;
            if (d.kind && j) {
                if (refs[j] < 320 || (refs[j] < 512 && (refs[j] & 3)))
                    return 0;
                if (refs[j] < 512)
                    continue;
                const unsigned char *r = TYPES_Find(b, size, refs[j]);
                if (!r || !r[8])
                    return 0;
            } else if (refs[j] >= 256) {
                const unsigned char *r = TYPES_Find(b, size, refs[j]);
                if (!r || r[8])
                    return 0;
            }
        }
        /* Ground must be a ground tile, not recursively another sprite. */
        if (d.render & TYPES_SPRITE) {
            const unsigned char *r = TYPES_Find(b, size, d.ground);
            if (r && (r[11] & TYPES_SPRITE))
                return 0;
        }
        off += TYPES_U32(b + off);
    }
    return 1;
}
void TYPES_CellDecode(const unsigned char *p, TypeCell *c) {
    c->kind = p[0];
    c->map = p[1] | (unsigned)p[5] << 8;
    c->floor = (int8_t)p[2];
    c->x = p[3];
    c->y = p[4];
    c->type = TYPES_U32(p + 8);
    c->restoreType = TYPES_U32(p + 12);
}
void TYPES_CellEncode(unsigned char *p, const TypeCell *c) {
    memset(p, 0, 16);
    p[0] = c->kind;
    p[1] = c->map;
    p[5] = c->map >> 8;
    p[2] = (uint8_t)c->floor;
    p[3] = c->x;
    p[4] = c->y;
    TYPES_Put32(p + 8, c->type);
    TYPES_Put32(p + 12, c->restoreType);
}
uint64_t TYPES_CellKey(const TypeCell *c) {
    return (uint64_t)c->kind << 40 | (uint64_t)c->map << 24 | (uint64_t)(uint8_t)c->floor << 16 |
           (uint64_t)c->y << 8 | c->x;
}
static int validCell(const TypeCell *c) {
    if (c->map > 255 && !(c->kind == 1 || c->kind == 2 || c->kind == 5 || c->kind == 6))
        return 0;
    /* Actor placements use stable instance slots, never terrain coordinates. */
    if (c->kind == 3)
        return MAPS_Town(c->map) && c->floor == 0 && (c->x || c->y) && c->y < 128;
    if (c->kind == 4)
        return c->map == 0 && (c->floor == 0 || c->floor == -1) && c->x > 0 && c->x < 32 &&
               (c->floor != -1 || c->x < 28) && !c->y;
    if (c->kind == 5 || c->kind == 6)
        return c->floor == 0 && c->x < 16 && !c->y;

    if (!c->kind && c->map >= 1 && c->map <= 32) {
        static const int8_t low[32] = {0,  0,  0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       -1, -1, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1};
        static const int8_t high[32] = {1, 1, 1, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0,
                                        3, 3, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 2, 2, 1};
        if (c->floor < low[c->map - 1] || c->floor > high[c->map - 1])
            return 0;
    }

    return c->kind <= 2 &&
           (c->kind       ? c->floor == 0
            : c->map == 0 ? (c->floor == 0 || c->floor == -1)
                          : MAPS_Town(c->map) &&
                                (!MAPS_NewLocation(c->map) || (c->floor >= -1 && c->floor <= 3)));
}
int TYPES_ValidateMap(const void *v, size_t size, const void *defs, size_t len) {
    const unsigned char *b = v;
    if (!b || size < 32 || memcmp(b, "ITYMAP01", 8) || !zeros(b + 28, 4) ||
        !TYPES_Validate(defs, len) || memcmp(b + 8, (const unsigned char *)defs + 8, 16))
        return 0;
    unsigned n = TYPES_U32(b + 24);
    if (n > TYPES_CELLS || size != 32 + (size_t)n * 16)
        return 0;
    uint64_t prev = 0;
    for (unsigned i = 0; i < n; i++) {
        const unsigned char *p = b + 32 + i * 16;
        TypeCell c;
        TYPES_CellDecode(p, &c);
        uint64_t key = TYPES_CellKey(&c);
        const unsigned char *r = TYPES_Find(defs, len, c.type);
        if (!validCell(&c) || !zeros(p + 6, 2) || !zeros(p + 12, 4) || (i && key <= prev) ||
            c.type < 512 || !r || (!!r[8] != (c.kind >= 3)))
            return 0;
        prev = key;
    }
    return 1;
}
void TYPES_AdvanceAnimation(unsigned base, unsigned char *tile, unsigned char *state,
                            unsigned (*randomByte)(void)) {
    unsigned delay = *state & 15, step = *state >> 4, shape = base & 252;
    if (delay == 15)
        return;
    if (delay) {
        --*state;
        return;
    }
    if (!*tile || *tile == 0x1d || *tile == 0x1e)
        return;
    if (shape < 0x34 || shape == 0xe8 || shape == 0xb4)
        return;
    if (shape != 0x5c && shape != 0xa8 && randomByte() < 128)
        return;
    const unsigned char *pattern = vanilla_D_1b18 + vanilla_D_1b18[0xb0 + (shape - 0x34) / 4] * 16;
    for (;;) {
        unsigned command = pattern[step];
        if (command >= 1 && command <= 4) {
            *tile = shape + command - 1;
            ++step;
        } else if (command == 5) {
            if (randomByte() < 64) {
                *tile = base;
                if (shape == 0x5c)
                    ++step;
                else
                    delay = 6;
            } else {
                ++step;
                continue;
            }
        } else if (command == 6) {
            step = randomByte() >= 192 ? step + 1 : 0;
            continue;
        } else if (!command) {
            step = 0;
            continue;
        } else if (command == 7) {
            step = 2;
            continue;
        } else {
            delay = (command + 128) & 15;
            ++step;
        }
        *state = step * 16 + delay;
        return;
    }
}
const unsigned char *TYPES_FrameAt(const unsigned char *p, unsigned variant, unsigned index,
                                   unsigned *width, unsigned *height) {
    if (!p || variant > 11)
        return NULL;
    unsigned count = 0;
    for (size_t off = 256; off < TYPES_U32(p); off += TYPES_FrameBytes(p, off))
        count += p[off] == variant;
    if (!count && variant % 6)
        return TYPES_FrameAt(p, variant < 6 ? 0 : 6, index, width, height);
    if (!count && variant >= 6)
        return TYPES_FrameAt(p, variant - 6, index, width, height);
    if (!count)
        return NULL;
    index %= count;
    for (size_t off = 256; off < TYPES_U32(p); off += TYPES_FrameBytes(p, off))
        if (p[off] == variant && !index--) {
            *width = TYPES_FrameWidth(p, variant, p[off + 1]);
            *height = TYPES_FrameRows(p, variant, p[off + 1]);
            return p + off + 4;
        }
    return NULL;
}
const unsigned char *TYPES_FrameImage(const unsigned char *p, unsigned variant, uint64_t ms,
                                      unsigned *width, unsigned *height) {
    if (!p || variant > 11)
        return NULL;
    unsigned count = word(p + 248), total = 0;
    size_t off = 256;
    for (unsigned i = 0; i < count; ++i) {
        if (p[off] == variant)
            total += word(p + off + 2);
        off += TYPES_FrameBytes(p, off);
    }
    if (!total && variant % 6)
        return TYPES_FrameImage(p, variant < 6 ? 0 : 6, ms, width, height);
    if (!total && variant >= 6)
        return TYPES_FrameImage(p, variant - 6, ms, width, height);
    if (!total)
        return NULL;
    ms %= total;
    off = 256;
    for (unsigned i = 0; i < count; ++i) {
        unsigned duration = word(p + off + 2);
        if (p[off] == variant) {
            if (ms < duration) {
                *width = TYPES_FrameWidth(p, variant, p[off + 1]);
                *height = TYPES_FrameRows(p, variant, p[off + 1]);
                return p + off + 4;
            }
            ms -= duration;
        }
        off += TYPES_FrameBytes(p, off);
    }
    return NULL;
}
const unsigned char *TYPES_Frame(const unsigned char *p, unsigned variant, uint64_t ms,
                                 unsigned *side) {
    unsigned height;
    return TYPES_FrameImage(p, variant, ms, side, &height);
}
static const unsigned char *mounted;
static size_t mountedSize;
static TypeCell cells[TYPES_CELLS];
static unsigned cellCount;
static const unsigned char *initial;
static size_t initialSize;
static const unsigned char *artRecords[TYPES_MAX];
static unsigned artCount;
static unsigned char inheritedAnimation[TYPES_MAX];
unsigned TYPES_ArtSlotType(unsigned slot) {
    return slot >= 512 && slot < 512 + artCount ? TYPES_U32(artRecords[slot - 512] + 4) : 65536;
}
static unsigned actorPoseCount(const unsigned char *record);
static uint16_t byId[TYPES_LAST_CUSTOM + 1];
typedef struct FrameCache {
    uint64_t tick;
    const unsigned char *rgba;
    unsigned side, height;
} FrameCache;
static FrameCache frameCache[TYPES_MAX][12];
void TYPES_Reset(void) {
    cellCount = 0;
    if (initial)
        for (unsigned i = 0; i < TYPES_U32(initial + 24); i++)
            TYPES_CellDecode(initial + 32 + i * 16, &cells[cellCount++]);
}
void TYPES_Mount(const void *defs, size_t len, const void *map, size_t mapLen) {
    if ((defs && !TYPES_Validate(defs, len)) ||
        (map && (!defs || !TYPES_ValidateMap(map, mapLen, defs, len)))) {
        defs = NULL;
        len = 0;
        map = NULL;
        mapLen = 0;
    }
    mounted = defs;
    mountedSize = len;
    initial = map;
    initialSize = mapLen;
    artCount = 0;
    memset(byId, 0, sizeof(byId));
    memset(frameCache, 0, sizeof(frameCache));
    if (defs) {
        size_t off = 32;
        artCount = TYPES_U32(mounted + 24);
        for (unsigned i = 0; i < artCount; i++) {
            artRecords[i] = mounted + off;
            inheritedAnimation[i] = mounted[off + 8] && TYPES_U32(mounted + off + 4) >= 512 &&
                                    !mounted[off + 255] && actorPoseCount(mounted + off) <= 4;
            byId[TYPES_U32(mounted + off + 4)] = i + 1;
            off += TYPES_U32(mounted + off);
        }
    }
    TYPES_Reset();
}
int TYPES_Get(uint32_t id, TypeDef *d) {
    const unsigned char *p = id <= TYPES_LAST_CUSTOM && byId[id] ? artRecords[byId[id] - 1] : NULL;
    if (p) {
        TYPES_Decode(p, d);
        return 1;
    }
    if (id < 512) {
        TYPES_Vanilla(id >= 256, id & 255, d);
        return 1;
    }
    return 0;
}
static unsigned cellIndex(TypeCell c) {
    uint64_t k = TYPES_CellKey(&c);
    unsigned lo = 0, hi = cellCount;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (TYPES_CellKey(&cells[mid]) < k)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}
uint32_t TYPES_At(TypeCell c, uint32_t fallback) {
    unsigned i = cellIndex(c);
    return i < cellCount && TYPES_CellKey(&cells[i]) == TYPES_CellKey(&c) ? cells[i].type
                                                                          : fallback;
}
void TYPES_Remove(TypeCell c) {
    unsigned i = cellIndex(c);
    if (i < cellCount && TYPES_CellKey(&cells[i]) == TYPES_CellKey(&c)) {
        memmove(cells + i, cells + i + 1, (cellCount - i - 1) * sizeof(*cells));
        cellCount--;
    }
}
int TYPES_Set(TypeCell c, uint32_t id) {
    if (!mounted)
        return id < 256;
    TypeDef d;
    if (!validCell(&c) || !TYPES_Get(id, &d) || (!!d.kind != (c.kind >= 3)))
        return 0;
    unsigned i = cellIndex(c);
    c.type = id;
    if (i < cellCount && TYPES_CellKey(&cells[i]) == TYPES_CellKey(&c)) {
        cells[i] = c;
        return 1;
    }
    if (cellCount == TYPES_CELLS)
        return 0;
    memmove(cells + i + 1, cells + i, (cellCount - i) * sizeof(*cells));
    cells[i] = c;
    cellCount++;
    return 1;
}
const unsigned char *TYPES_Art(uint32_t id, unsigned variant, uint64_t ms, unsigned *side) {
    return TYPES_Frame(TYPES_Find(mounted, mountedSize, id), variant, ms, side);
}
int TYPES_WriteState(FILE *f) {
    if (!mounted)
        return 1;
    size_t n = 32 + (size_t)cellCount * 16;
    unsigned char *b = calloc(1, n);
    if (!b)
        return 0;
    memcpy(b, "ITYSAV01", 8);
    memcpy(b + 8, mounted + 8, 16);
    TYPES_Put32(b + 24, cellCount);
    for (unsigned i = 0; i < cellCount; i++)
        TYPES_CellEncode(b + 32 + i * 16, &cells[i]);
    TYPES_Put32(b + 28, MOD_Crc32(b + 32, n - 32));
    int ok = fwrite(b, 1, n, f) == n;
    free(b);
    return ok;
}
int TYPES_ReadState(FILE *f, int apply) {
    unsigned char h[32];
    size_t got = fread(h, 1, 32, f);
    if (!got && !ferror(f)) {
        if (apply)
            TYPES_Reset();
        return 1;
    }
    if (got != 32 || memcmp(h, "ITYSAV01", 8) || !mounted || memcmp(h + 8, mounted + 8, 16))
        return 0;
    unsigned n = TYPES_U32(h + 24);
    if (n > TYPES_CELLS)
        return 0;
    size_t size = (size_t)n * 16;
    unsigned char *b = malloc(size ? size : 1);
    if (!b)
        return 0;
    int ok =
        fread(b, 1, size, f) == size && fgetc(f) == EOF && MOD_Crc32(b, size) == TYPES_U32(h + 28);
    uint64_t prev = 0;
    for (unsigned i = 0; ok && i < n; i++) {
        TypeCell c;
        TYPES_CellDecode(b + i * 16, &c);
        TypeDef d;
        uint64_t key = TYPES_CellKey(&c);
        ok = validCell(&c) && zeros(b + i * 16 + 6, 2) &&
             (!c.restoreType || (TYPES_Get(c.restoreType, &d) && !d.kind)) && (!i || key > prev) &&
             TYPES_Get(c.type, &d) && (!!d.kind == (c.kind >= 3));
        prev = key;
    }
    if (ok && apply) {
        cellCount = n;
        for (unsigned i = 0; i < n; i++)
            TYPES_CellDecode(b + i * 16, &cells[i]);
    }
    free(b);
    return ok;
}

unsigned TYPES_ArtSlot(uint32_t id) {
    id = TYPES_ArtworkType(id);
    if (id > TYPES_LAST_CUSTOM || !byId[id])
        return 0;
    unsigned i = byId[id] - 1;
    return word(artRecords[i] + 248) ? 512 + i : 0;
}
static unsigned actorPoseCount(const unsigned char *record) {
    unsigned local = 0, world = 0;
    for (size_t off = 256; off < TYPES_U32(record); off += TYPES_FrameBytes(record, off)) {
        local += record[off] == 0;
        world += record[off] == 6;
    }
    return local > world ? local : world;
}
unsigned TYPES_ActorArt(uint32_t type, unsigned nativeTile) {
    if (type >= 256 && type < 512 && byId[type]) {
        const unsigned char *r = artRecords[byId[type] - 1];
        unsigned defaults = 0;
        size_t off = 256;
        for (unsigned i = 0; i < word(r + 248); ++i) {
            defaults += r[off] == 0;
            off += TYPES_FrameBytes(r, off);
        }
        if (defaults <= 1)
            return nativeTile; /* Native animation selects among four independently authored slots. */
    }
    if (type >= 512 && type <= TYPES_LAST_CUSTOM && byId[type]) {
        const unsigned char *r = artRecords[byId[type] - 1];
        if (TYPES_ArtworkType(nativeTile) == type)
            return nativeTile;
        if (inheritedAnimation[byId[type] - 1])
            return type | (((nativeTile - 256 - r[9]) & 3) << 16);
    }
    return TYPES_ArtSlot(type) ? type : nativeTile;
}
void TYPES_ArtSize(uint32_t id, int world, unsigned *width, unsigned *height) {
    TypeDef d;
    *width = *height = 1;
    if (!world && TYPES_Get(TYPES_ArtworkType(id), &d) && d.kind) {
        *width = TYPES_Width(&d);
        *height = TYPES_Height(&d);
    }
}
const unsigned char *TYPES_ArtImage(uint32_t id, unsigned variant, uint64_t ms, unsigned *w,
                                    unsigned *h) {
    return TYPES_FrameImage(TYPES_Find(mounted, mountedSize, id), variant, ms, w, h);
}
void TYPES_ArtSlotSize(unsigned slot, int world, unsigned *w, unsigned *h) {
    *w = *h = 1;
    if (!world && slot >= 512 && slot < 512 + artCount) {
        const unsigned char *r = artRecords[slot - 512];
        if (r[8]) {
            *w = 1 + r[253];
            *h = 1 + r[254];
        }
    }
}
const unsigned char *TYPES_SampleArtImage(unsigned slot, unsigned variant, uint64_t ms, unsigned *w,
                                          unsigned *h) {
    unsigned phase = slot >> 16;
    slot &= 65535;
    if (slot < 512 || slot >= 512 + artCount || variant > 11)
        return NULL;
    const unsigned char *record = artRecords[slot - 512];
    int inherited = inheritedAnimation[slot - 512];
    uint64_t key = inherited ? UINT64_MAX - phase : ms;
    FrameCache *cache = &frameCache[slot - 512][variant];
    if (cache->tick != key || !cache->rgba) {
        cache->rgba = inherited
                          ? TYPES_FrameAt(record, variant, phase, &cache->side, &cache->height)
                          : TYPES_FrameImage(record, variant, ms, &cache->side, &cache->height);
        cache->tick = key;
    }
    *w = cache->side;
    *h = cache->height;
    return cache->rgba;
}
const unsigned char *TYPES_SampleArt(unsigned slot, unsigned variant, uint64_t ms, unsigned *side) {
    unsigned height;
    return TYPES_SampleArtImage(slot, variant, ms, side, &height);
}

void TYPES_BeginEncounter(unsigned kind, unsigned map) {
    unsigned n = 0;
    for (unsigned i = 0; i < cellCount; i++)
        if (!cells[i].kind || cells[i].kind == 3 || cells[i].kind == 4)
            cells[n++] = cells[i];
    cellCount = n;
    if (initial)
        for (unsigned i = 0; i < TYPES_U32(initial + 24); i++) {
            TypeCell c;
            TYPES_CellDecode(initial + 32 + i * 16, &c);
            if ((c.kind == kind || c.kind == kind + 4) && c.map == map)
                TYPES_Set(c, c.type);
        }
}

int TYPES_State(TypeCell c, TypeCell *out) {
    unsigned i = cellIndex(c);
    if (i >= cellCount || TYPES_CellKey(&cells[i]) != TYPES_CellKey(&c))
        return 0;
    *out = cells[i];
    return 1;
}
int TYPES_Defined(uint32_t id) { return id <= TYPES_LAST_CUSTOM && byId[id] != 0; }

int TYPES_DefWalkable(unsigned actor, const TypeDef *d) {
    if (actor > 255 || !d)
        return 0;
    unsigned profile = vanilla_D_54f4[actor >> 2];
    if (profile > 10 || !(d->movement & (1u << profile)))
        return 0;
    if (profile == 5)
        return !!(d->skiffDirections & (8u >> (actor & 3)));
    if (profile == 0 && d->humanoidSeat && (actor & 0xfe) != 0x1c && (actor & 0xf0) != 0x40)
        return 0;
    return 1;
}

int TYPES_SwapCells(TypeCell a, TypeCell b, uint32_t fallbackA, uint32_t fallbackB) {
    if (!mounted || !validCell(&a) || !validCell(&b) || a.kind != b.kind || a.map != b.map ||
        a.floor != b.floor)
        return 0;
    if (TYPES_CellKey(&a) == TYPES_CellKey(&b))
        return 1;
    TypeCell oldA = a, oldB = b;
    int hasA = TYPES_State(a, &oldA), hasB = TYPES_State(b, &oldB);
    uint32_t source = hasA ? oldA.type : fallbackA, dest = hasB ? oldB.type : fallbackB;
    TypeDef def;
    if ((unsigned)(!hasA + !hasB) > TYPES_CELLS - cellCount || !TYPES_Get(source, &def) ||
        def.kind || !TYPES_Get(dest, &def) || def.kind)
        return 0;
    a.restoreType = oldB.restoreType;
    b.restoreType = oldA.restoreType;
    return TYPES_Set(a, dest) && TYPES_Set(b, source);
}
