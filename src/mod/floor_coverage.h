#ifndef IMPERA_FLOOR_COVERAGE_H
#define IMPERA_FLOOR_COVERAGE_H
#include "maps.h"
#include <stdlib.h>
/* I3DFLR01: header[16] (magic, count:u32, zero:u32), sorted records:
 * length:u32,map:u16,floor:i8,zero:u8,width:u16,height:u16,zero:u32,
 * then two bits per cell: 0 backdrop, 1 geometry, 3 floor opening.
 * Coverage affects presentation only, never terrain, collision or schedules. */
#define FLOOR3D_HEADER 16u
#define FLOOR3D_RECORD 16u
#define FLOOR3D_GEOMETRY 1u
#define FLOOR3D_OPENING 2u
static inline unsigned FLOOR3D_Bytes(unsigned w, unsigned h) { return (w * h + 3) / 4; }
static inline unsigned FLOOR3D_Cell(const unsigned char *r, int x, int y) {
    unsigned w = MAPS_Word(r + 8), h = MAPS_Word(r + 10);
    if (x < 0 || y < 0 || (unsigned)x >= w || (unsigned)y >= h)
        return 0;
    unsigned i = (unsigned)y * w + x;
    return (r[FLOOR3D_RECORD + i / 4] >> ((i % 4) * 2)) & 3;
}
static inline int FLOOR3D_Validate(const void *v, size_t n) {
    const unsigned char *b = (const unsigned char *)v;
    if (!b || n < 16 || n > MAPS_MAX_BYTES || memcmp(b, "I3DFLR01", 8) || MAPS_U32(b + 12))
        return 0;
    unsigned count = MAPS_U32(b + 8), previous = 0;
    if (count > 640)
        return 0;
    size_t off = 16;
    for (unsigned i = 0; i < count; i++) {
        if (n - off < 16)
            return 0;
        const unsigned char *r = b + off;
        unsigned map = MAPS_Word(r + 4), w = MAPS_Word(r + 8), h = MAPS_Word(r + 10),
                 len = MAPS_U32(r);
        int level = (int8_t)r[6];
        unsigned key = map * 256 + (unsigned)(level + 128);
        if (!MAPS_Town(map) || level < -1 || level > 3 || r[7] || MAPS_U32(r + 12) || !w || !h ||
            w > 256 || h > 256 || len != 16 + FLOOR3D_Bytes(w, h) || len > n - off ||
            (i && key <= previous))
            return 0;
        for (unsigned cell = 0; cell < w * h; cell++)
            if (((r[16 + cell / 4] >> ((cell % 4) * 2)) & 3) == 2)
                return 0;
        unsigned used = (w * h) % 4;
        if (used && (r[len - 1] >> (used * 2)))
            return 0;
        off += len;
        previous = key;
    }
    return off == n;
}
static inline const unsigned char *FLOOR3D_Find(const void *v, size_t n, unsigned map, int level) {
    const unsigned char *b = (const unsigned char *)v;
    if (!b || n < 16)
        return NULL;
    for (size_t off = 16; off + 16 <= n;) {
        const unsigned char *r = b + off;
        unsigned len = MAPS_U32(r);
        if (len < 16 || len > n - off)
            return NULL;
        if (MAPS_Word(r + 4) == map && (int8_t)r[6] == level)
            return r;
        off += len;
    }
    return NULL;
}
static inline int FLOOR3D_IsOpening(unsigned tile) {
    return (tile >= 0xc4 && tile <= 0xc7) || tile == 0xc9 || tile == 0x86;
}
/* Best-effort starting suggestion for new maps. Saved coverage is authoritative.
 * Natural enclosed regions are included only beside a genuine ladder landing. */
static inline int FLOOR3D_Suggest(const unsigned char *terrain, const unsigned char *below,
                                  unsigned w, unsigned h, int level, unsigned char *flags) {
    if (!terrain || !flags || !w || !h || w > 256 || h > 256)
        return 0;
    unsigned count = w * h;
    unsigned *queue = (unsigned *)malloc(count * sizeof(unsigned));
    unsigned char *seen = (unsigned char *)calloc(count, 1);
    if (!queue || !seen) {
        free(queue);
        free(seen);
        return 0;
    }
    for (unsigned i = 0; i < count; i++) {
        unsigned t = terrain[i];
        int structural = (t >= 0x27 && t <= 0x2a) || (t >= 0x38 && t <= 0x5f) ||
                         (t >= 0x80 && t <= 0xfd && !(t >= 0xe4 && t <= 0xe7));
        flags[i] = t == 255 ? 0 : level <= 0 || structural ? 1 : 0;
        if (flags[i] && FLOOR3D_IsOpening(t))
            flags[i] |= 2;
    }
    if (level > 0)
        for (unsigned seed = 0; seed < count; seed++) {
            if (seen[seed] || flags[seed])
                continue;
            unsigned head = 0, tail = 0;
            int edge = 0, landing = 0;
            seen[seed] = 1;
            queue[tail++] = seed;
            while (head < tail) {
                unsigned cell = queue[head++], x = cell % w, y = cell / w;
                if (!x || !y || x == w - 1 || y == h - 1)
                    edge = 1;
                for (int dir = 0; dir < 4; dir++) {
                    int nx = (int)x + (dir == 0) - (dir == 1),
                        ny = (int)y + (dir == 2) - (dir == 3);
                    if (nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h)
                        continue;
                    unsigned n = (unsigned)ny * w + nx;
                    if (below && terrain[n] == 0xc9 && below[n] == 0xc8)
                        landing = 1;
                    if (!flags[n] && !seen[n]) {
                        seen[n] = 1;
                        queue[tail++] = n;
                    }
                }
            }
            if (!edge && landing)
                for (unsigned i = 0; i < tail; i++)
                    flags[queue[i]] = 1;
        }
    free(queue);
    free(seen);
    return 1;
}
#include "floor_coverage_defaults.h"
static inline unsigned FLOOR3D_Default(unsigned map, int level, int x, int y) {
    for (unsigned i = 0; i < sizeof(floor3dDefaults) / sizeof(floor3dDefaults[0]); i++) {
        const Floor3DDefault *d = &floor3dDefaults[i];
        if (d->map == map && d->level == level) {
            if (x < 0 || y < 0 || x >= 32 || y >= 32)
                return 0;
            unsigned n = (unsigned)y * 32 + x;
            return (d->cells[n / 4] >> ((n % 4) * 2)) & 3;
        }
    }
    return 255; /* No native profile: offer a terrain-based starting suggestion. */
}
#endif
