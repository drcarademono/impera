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
    assert(MAPS_Mount(b, size));
    assert(MAPS_ActiveWidth(0, 0) == 256 && MAPS_ActiveHeight(0, 0) == 256);
    *MAPS_ActiveCell(0, 0, -1, -1) = 68;
    assert(*MAPS_ActiveCell(0, 0, 255, 255) == 68);
    FILE *save = tmpfile();
    assert(save && MAPS_WriteState(save));
    MAPS_Reset();
    assert(*MAPS_ActiveCell(0, 0, 255, 255) == 0);
    rewind(save);
    assert(MAPS_ReadState(save, 0) && *MAPS_ActiveCell(0, 0, 255, 255) == 0);
    rewind(save);
    assert(MAPS_ReadState(save, 1) && *MAPS_ActiveCell(0, 0, 255, 255) == 68);
    assert(fseek(save, 16, SEEK_SET) == 0);
    int crc = fgetc(save);
    assert(fseek(save, 16, SEEK_SET) == 0);
    fputc(crc ^ 1, save);
    rewind(save);
    assert(!MAPS_ReadState(save, 1) && *MAPS_ActiveCell(0, 0, 255, 255) == 68);
    fclose(save);
    MAPS_Mount(NULL, 0);

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
    map(r, 256, 256);
    r[4] = 2;
    r[20] = 1;
    memset(r + 88, 0, 32);
    memcpy(r + 88, "BRIT.CBT", 9);
    r[123] = 1; /* monster slot zero, bit 24 */
    assert(MAPS_Validate(b, size));
    assert(MAPS_Mount(b, size));
    MAPS_Combat(1, 0);
    assert(MAPS_CombatMarkerActive(24, 255, 255));
    assert(!MAPS_CombatMarkerActive(25, 255, 255));
    MAPS_Mount(NULL, 0);
    free(b);
    return 0;
}
