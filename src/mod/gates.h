#ifndef IMPERA_GATES_H
#define IMPERA_GATES_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Sorted cell overrides; unlisted cells retain native archway/drawbridge rules. */
#define GATES_HEADER 12u
#define GATES_RECORD 16u
#define GATES_MAX 65536u
static inline unsigned GATES_Count(const unsigned char *b) {
    return b[8] | (unsigned)b[9] << 8 | (unsigned)b[10] << 16 | (unsigned)b[11] << 24;
}
static inline uint32_t GATES_Key(const unsigned char *r) {
    return (uint32_t)r[0] << 24 | (uint32_t)(r[1] ^ 128) << 16 | (unsigned)r[3] << 8 | r[2];
}
/* map, signed floor, x, y, kind (1 gate / 2 bridge), mode
 * (0 decorative / 1 scheduled / 2 open / 3 closed), open tile, closed tile, hours. */
static inline int GATES_Validate(const void *v, size_t n) {
    const unsigned char *b = (const unsigned char *)v;
    if (!b || n < GATES_HEADER || memcmp(b, "IGAT0001", 8))
        return 0;
    unsigned count = GATES_Count(b);
    if (count > GATES_MAX || n != GATES_HEADER + count * GATES_RECORD)
        return 0;
    uint32_t previous = 0;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char *r = b + GATES_HEADER + i * GATES_RECORD;
        if (!r[0] || r[0] >= 128 || (int8_t)r[1] < -1 || (int8_t)r[1] > 3 || r[4] < 1 || r[4] > 2 ||
            (r[4] == 1 && !r[3]) || r[5] > 3 || r[8] > 23 || r[9] > 23 || r[10] || r[11] || r[12] ||
            r[13] || r[14] || r[15] || (i && GATES_Key(r) <= previous))
            return 0;
        previous = GATES_Key(r);
    }
    return 1;
}
static inline int GATES_Open(const unsigned char *r, unsigned hour) {
    if (r[5] == 0 || r[5] == 2)
        return 1;
    if (r[5] == 3)
        return 0;
    return r[8] <= r[9] ? hour >= r[8] && hour < r[9] : hour >= r[8] || hour < r[9];
}
#endif
