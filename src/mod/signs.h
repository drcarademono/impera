#ifndef IMPERA_MOD_SIGNS_H
#define IMPERA_MOD_SIGNS_H
#include "maps.h"
/* A version 18 sign resource carries the same glyph records with a full map ID. */
static inline int SIGNS_Validate(const void *v, size_t n) {
    const unsigned char *p = (const unsigned char *)v;
    if (!p || n < 12 || n > MAPS_MAX_BYTES || memcmp(p, "ISIG0001", 8) || MAPS_U32(p + 8) > 65535)
        return 0;
    size_t off = 12;
    for (unsigned i = 0; i < MAPS_U32(p + 8); i++) {
        if (n - off < 5 || (p[off] && !MAPS_Town(p[off])) || (int8_t)p[off + 1] < -1 ||
            (int8_t)p[off + 1] > 3)
            return 0;
        const unsigned char *end = (const unsigned char *)memchr(p + off + 4, 0, n - off - 4);
        if (!end || end - (p + off) > 2998)
            return 0;
        off = (size_t)(end - p) + 1;
    }
    return off <= n;
}
static inline size_t SIGNS_Find(const void *v, size_t n, unsigned map, int floor, int x, int y) {
    const unsigned char *p = (const unsigned char *)v;
    if (!SIGNS_Validate(v, n))
        return 0;
    size_t off = 12;
    for (unsigned i = 0; i < MAPS_U32(p + 8); i++) {
        if (p[off] == map && (int8_t)p[off + 1] == floor && p[off + 2] == x && p[off + 3] == y)
            return off;
        off = (size_t)((const unsigned char *)memchr(p + off + 4, 0, n - off - 4) - p) + 1;
    }
    return 0;
}
#endif
