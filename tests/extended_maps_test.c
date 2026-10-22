#undef NDEBUG
#include "mod/maps.h"
#include <assert.h>
#include <stdlib.h>

static void put32(unsigned char *p, unsigned value) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = value >> (8 * i);
}
static void map(unsigned char *r, unsigned width, unsigned height) {
    memset(r, 0, MAPS_RECORD + width * height);
    put32(r, MAPS_RECORD + width * height);
    r[10] = width;
    r[11] = width >> 8;
    r[12] = height;
    r[13] = height >> 8;
    r[14] = (unsigned char)-2;
    memcpy(r + 24, "Britannia", 10);
    memcpy(r + 88, "BRIT.DAT", 9);
}
int main(void) {
    size_t size = MAPS_HEADER + MAPS_RECORD + 256 * 256;
    unsigned char *b = calloc(1, size);
    assert(b);
    memcpy(b, "IMAPS001", 8);
    put32(b + 8, 1);
    unsigned char *r = b + MAPS_HEADER;
    map(r, 256, 256);
    assert(MAPS_Validate(b, size));
    assert(MAPS_Find(b, size, 0, 0, 0) == r);
    assert(!MAPS_Find(b, size, 0, 1, 0));
    for (size_t n = 0; n < size; ++n)
        assert(!MAPS_Validate(b, n));
    r[11] = 2;
    assert(!MAPS_Validate(b, size));
    r[11] = 1;
    r[14] = 0;
    assert(!MAPS_Validate(b, size));
    r[14] = (unsigned char)-2;
    r[88] = '/';
    assert(!MAPS_Validate(b, size));
    r[88] = 'B';
    r[120] = 1;
    assert(!MAPS_Validate(b, size));
    r[120] = 0;
    map(r, 1, 256);
    assert(MAPS_Validate(b, MAPS_HEADER + MAPS_RECORD + 256));
    assert(MAPS_LocationResource("LOC042.DAT") == 42);
    assert(MAPS_LocationResource("LOC127.TLK") == 127);
    assert(!MAPS_LocationResource("LOC064.DAT"));
    assert(!MAPS_LocationResource("LOC042.EXE"));
    assert(!MAPS_LocationResource("LOC042.DAT/"));
    assert(!MAPS_LocationResource("LOC999.NPC"));
    assert(!MAPS_LocationResource(NULL));
    free(b);
    return 0;
}
