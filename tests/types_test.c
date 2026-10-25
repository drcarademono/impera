#include "mod/maps.h"
#include "common/common.h"
#include "mod/contents.h"
#include "mod/package.h"
#include "mod/types.h"
#include "mod/types_state.h"
#include "vars.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x)                                                                                  \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int TYPE_ORACLE_Walkable(int, int);
int main(void) {
    for (unsigned a = 0; a < 256; a++)
        for (unsigned t = 0; t < 256; t++) {
            TypeDef d;
            TYPES_Vanilla(0, t, &d);
            assert(!!TYPES_Walkable(a, t) == !!TYPE_ORACLE_Walkable(a, t));
            assert(!!TYPES_DefWalkable(a, &d) == !!TYPE_ORACLE_Walkable(a, t));
        }
    for (unsigned i = 0; i < 48; i++) {
        TypeDef d;
        TYPES_Vanilla(1, 64 + i * 4, &d);
        assert(!memcmp(d.stats, &D_13bc_enemyStats[i], 8));
        assert(d.actorFlags == D_153c_enemyFlags[i]);
        assert(d.range == D_159c[i]);
        assert(d.projectileAppearance == D_15cc[i]);
        assert(d.companion == D_16d4[i]);
    }
    puts("PASS 65536 vanilla movement pairs and all 48 actor definitions match native tables");
    unsigned len = 32 + 256 + 2 * (4 + 16 * 16 * 4);
    unsigned char *defs = calloc(1, len);
    memcpy(defs, "ITYPES01", 8);
    defs[8] = 1;
    TYPES_Put32(defs + 24, 1);
    TypeDef d;
    TYPES_Vanilla(0, 5, &d);
    d.id = 40000;
    d.frames = 2;
    d.render = TYPES_SPRITE | TYPES_MASONRY;
    strcpy(d.name, "Crystal door");
    TYPES_Encode(defs + 32, &d);
    TYPES_Put32(defs + 32, len - 32);
    unsigned off = 32 + 256;
    for (int i = 0; i < 2; i++) {
        defs[off + 1] = 16;
        defs[off + 2] = 100;
        for (int j = 0; j < 256; j++) {
            defs[off + 4 + j * 4] = i ? 255 : 0;
            defs[off + 4 + j * 4 + 3] = 255;
        }
        off += 4 + 1024;
    }
    assert(TYPES_Validate(defs, len));
    unsigned side;
    assert(TYPES_Frame(defs + 32, 0, 0, &side)[0] == 0 && side == 16);
    assert(TYPES_Frame(defs + 32, 4, 100, &side)[0] == 255);
    assert(TYPES_Frame(defs + 32, 0, 200, &side)[0] == 0);
    /* Rectangular Apple II frames retain row stride and animation offsets. */
    unsigned appleLen = len + 2 * (4 + 14 * 16 * 4);
    unsigned char *apple = calloc(1, appleLen);
    memcpy(apple, defs, len);
    TYPES_Put32(apple + 32, appleLen - 32);
    apple[32 + 248] = 4;
    for (unsigned i = 0, off = len; i < 2; i++, off += 4 + 14 * 16 * 4) {
        apple[off] = 2;
        apple[off + 1] = 14;
        apple[off + 2] = 100;
        apple[off + 4] = 50 + i;
    }
    assert(TYPES_Validate(apple, appleLen));
    assert(TYPES_Frame(apple + 32, 2, 100, &side)[0] == 51);
    assert(side == 14 && TYPES_FrameHeight(side) == 16);
    assert(TYPES_Frame(apple + 32, 4, 100, &side)[0] == 255 && side == 16);
    free(apple);
    unsigned char map[48] = {0};
    memcpy(map, "ITYMAP01", 8);
    map[8] = 1;
    TYPES_Put32(map + 24, 1);
    TypeCell c = {0, 13, 0, 12, 13, 40000};
    TYPES_CellEncode(map + 32, &c);
    assert(TYPES_ValidateMap(map, sizeof(map), defs, len));
    TYPES_Mount(defs, len, map, sizeof(map));
    assert(TYPES_At(c, 5) == 40000);
    TypeCell other = c;
    other.x++;
    assert(TYPES_SwapCells(c, other, 5, 5));
    assert(TYPES_At(c, 5) == 5);
    assert(TYPES_At(other, 5) == 40000);
    TYPES_Reset();
    assert(TYPES_Set(c, 5));
    assert(TYPES_At(c, 99) == 5);
    assert(!TYPES_Set(c, 99999));
    FILE *f = tmpfile();
    assert(f && CONTENTS_WriteState(f));
    TYPES_Reset();
    assert(TYPES_At(c, 5) == 40000);
    rewind(f);
    assert(CONTENTS_ReadState(f, 0));
    assert(TYPES_At(c, 5) == 40000);
    rewind(f);
    assert(CONTENTS_ReadState(f, 1));
    assert(TYPES_At(c, 99) == 5);
    rewind(f);
    fseek(f, 8, SEEK_SET);
    fputc(2, f);
    rewind(f);
    assert(!CONTENTS_ReadState(f, 1));
    assert(TYPES_At(c, 99) == 5);
    fclose(f);
    TYPES_Mount(NULL, 0, NULL, 0);
    f = tmpfile();
    fwrite("ITYSAV01", 1, 8, f);
    rewind(f);
    assert(!TYPES_ReadState(f, 1));
    fclose(f);
    size_t bigSize = 32 + (TYPES_CELLS - 1) * 16;
    unsigned char *big = calloc(1, bigSize);
    assert(big);
    memcpy(big, "ITYMAP01", 8);
    big[8] = 1;
    TYPES_Put32(big + 24, TYPES_CELLS - 1);
    for (unsigned i = 0; i < TYPES_CELLS - 1; i++) {
        TypeCell cell = {0, 0, 0, i & 255, i >> 8, 40000, 0};
        TYPES_CellEncode(big + 32 + i * 16, &cell);
    }
    TYPES_Mount(defs, len, big, bigSize);
    TypeCell a = {0, 13, 0, 1, 1, 0, 0}, b = {0, 13, 0, 2, 1, 0, 0};
    assert(!TYPES_SwapCells(a, b, 5, 5));
    assert(TYPES_At(a, 7) == 7 && TYPES_At(b, 8) == 8);
    TYPES_Mount(NULL, 0, NULL, 0);
    free(big);
    map[34] = 7;
    assert(!TYPES_ValidateMap(map, sizeof(map), defs, len));
    map[34] = 0;
    map[8] = 2;
    assert(!TYPES_ValidateMap(map, sizeof(map), defs, len));
    map[8] = 1;
    map[35] = 32;
    assert(TYPES_ValidateMap(map, sizeof(map), defs, len));
    assert(!MAPS_Contains(NULL, 0, 1, map[33], 0, 32, 0));
    map[35] = 12;
    defs[32 + 8] = 2;
    assert(!TYPES_Validate(defs, len));
    defs[32 + 8] = 0;
    defs[32 + 256 + 1] = 255;
    assert(!TYPES_Validate(defs, len));
    defs[32 + 256 + 1] = 16;
    TYPES_Put32(defs + 32 + 20, 40000);
    assert(!TYPES_Validate(defs, len));
    /* Actor slot references and ability targets are separate from terrain cells. */
    unsigned char *actorDefs = malloc(len);
    memcpy(actorDefs, defs, len);
    TypeDef actorDef;
    TYPES_Vanilla(1, 144, &actorDef);
    actorDef.kind = 2;
    actorDef.id = 40010;
    actorDef.frames = 2;
    actorDef.stats[5] = 40;
    actorDef.stats[6] = 2;
    actorDef.result[1] = 40010;
    actorDef.result[2] = 40010;
    TYPES_Encode(actorDefs + 32, &actorDef);
    TYPES_Put32(actorDefs + 32, len - 32);
    assert(TYPES_Validate(actorDefs, len));
    unsigned char actorMap[48] = {0};
    memcpy(actorMap, "ITYMAP01", 8);
    actorMap[8] = 1;
    TYPES_Put32(actorMap + 24, 1);
    TypeCell actorSlot = {3, 13, 0, 4, 0, 40010, 0};
    TYPES_CellEncode(actorMap + 32, &actorSlot);
    assert(TYPES_ValidateMap(actorMap, sizeof(actorMap), actorDefs, len));
    actorMap[32] = 0;
    assert(!TYPES_ValidateMap(actorMap, sizeof(actorMap), actorDefs, len));
    actorMap[32] = 3;
    TYPES_Mount(actorDefs, len, actorMap, sizeof(actorMap));
    assert(TYPES_At(actorSlot, 0) == 40010);
    TYPES_BeginEncounter(1, 0);
    assert(TYPES_At(actorSlot, 0) == 40010);
    f = tmpfile();
    assert(TYPES_WriteState(f));
    rewind(f);
    assert(TYPES_ReadState(f, 1));
    fclose(f);
    assert(TYPES_At(actorSlot, 0) == 40010);
    TYPES_Mount(NULL, 0, NULL, 0);
    actorSlot.kind = 6;
    actorSlot.map = 300;
    TYPES_CellEncode(actorMap + 32, &actorSlot);
    TypeCell decoded;
    TYPES_CellDecode(actorMap + 32, &decoded);
    assert(decoded.map == 300 && TYPES_ValidateMap(actorMap, sizeof(actorMap), actorDefs, len));
    TYPES_Mount(actorDefs, len, actorMap, sizeof(actorMap));
    TYPES_BeginEncounter(2, 300);
    assert(TYPES_At(actorSlot, 0) == 40010);
    TypeCell low = actorSlot;
    low.map = 44;
    assert(TYPES_At(low, 0) == 0 && TYPES_CellKey(&low) != TYPES_CellKey(&actorSlot));
    f = tmpfile();
    assert(f && TYPES_WriteState(f));
    TYPES_Reset();
    rewind(f);
    assert(TYPES_ReadState(f, 1) && TYPES_At(actorSlot, 0) == 40010);
    fclose(f);
    TYPES_Mount(NULL, 0, NULL, 0);
    actorDef.result[1] = 40011;
    TYPES_Encode(actorDefs + 32, &actorDef);
    TYPES_Put32(actorDefs + 32, len - 32);
    assert(!TYPES_Validate(actorDefs, len));
    free(actorDefs);
    free(defs);
    puts("PASS extended identities, animation, alpha, placement validation and transactional save "
         "state");
    return 0;
}
