#undef NDEBUG
#include "common/common.h"
#include "macros.h"
#include "mod/contents.h"
#include "mod/contents_game.h"
#include "mod/package.h"
#include "vars.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Real inventory and contents adapter, deterministic command services. */
void ULTIMA_1850_PrintString(char *text) { (void)text; }
void ULTIMA_1a3e_PrintNumber(int number, int width, int fill) {
    (void)number;
    (void)width;
    (void)fill;
}
int ULTIMA_4988(void) { return 0; }
int ULTIMA_2092_RandomRange(int low, int high) {
    (void)low;
    return high;
}
void ULTIMA_2fd0(int actor) { D_55a8_party[actor].hp--; }
static int noBase(void *context, const char *name, unsigned char **data, size_t *size) {
    (void)context;
    (void)name;
    *data = NULL;
    *size = 0;
    return 0;
}
int main(void) {
    unsigned char bytes[32 + 96 * 4] = {0};
    memcpy(bytes, "ICONT001", 8);
    bytes[8] = 9;
    CONTENTS_PutWord(bytes + 24, 4);
    ContentsDef defs[4] = {{0}};
    for (int i = 0; i < 4; i++) {
        defs[i].id = i + 1;
        defs[i].map = i ? 33 : 5;
        defs[i].x = i ? 1 : 2;
        defs[i].y = i ? 1 : 2;
        defs[i].slot = 255;
        defs[i].original = 65535;
        defs[i].count = 2;
        defs[i].items[0] = (ContentsItem){6, 0, 5};
        defs[i].items[1] = (ContentsItem){1, 0, 10};
    }
    defs[0].original = 14;
    defs[0].refill = 2;
    defs[1].action = 1;
    defs[1].flags = 3;
    defs[2].kind = 1;
    defs[2].map = 4;
    defs[2].floor = 0;
    defs[2].x = 2;
    defs[2].y = 3;
    defs[3].map = 5;
    defs[3].floor = 0;
    defs[3].x = 3;
    defs[3].y = 4;
    defs[3].action = 1;
    defs[3].count = 0;
    for (int i = 0; i < 4; i++)
        CONTENTS_EncodeRecord(bytes + 32 + i * 96, &defs[i]);
    assert(CONTENTS_Validate(bytes, sizeof(bytes)));
    CONTENTS_Mount(bytes, sizeof(bytes));
    D_5893_map_id = 5;
    D_5895_map_level = 0;
    D_5874 = 139;
    D_587d = 5;
    D_587e = 10;
    assert(CONTENTS_GAME_Shape(3, 4) == 1); /* An unopened empty chest is still visible. */
    assert(CONTENTS_GAME_Interact(1, 3, 4, 0));
    assert(CONTENTS_GAME_Shape(3, 4) == 0); /* It disappears only when opened. */
    D_57b2 = 9;
    D_57b1 = 97;
    D_57aa = 9998;
    for (int i = 0; i < 42; i++)
        D_5c5a[i]._0_tile = 0x50; /* Full vanilla actor list must not matter. */
    assert(CONTENTS_GAME_Command(2, 2) == 'S');
    assert(CONTENTS_GAME_Interact(0, 2, 2, 0));
    assert(D_57b2 == 10 && CONTENTS_GAME_Shape(2, 2) == 7);
    assert(CONTENTS_GAME_Command(2, 2) == 'G');
    assert(CONTENTS_GAME_Interact(2, 2, 2, 0));
    assert(D_57b1 == 99 && D_57aa == 9999);
    const ContentsDef *d = CONTENTS_Find(0, 5, 0, 2, 2);
    assert(CONTENTS_State(d)->remaining[0] == 3 && CONTENTS_State(d)->remaining[1] == 9);
    D_587e++;
    assert(CONTENTS_GAME_Interact(0, 2, 2, 0));
    assert(CONTENTS_State(d)->remaining[0] == 3); /* no accumulation */
    D_57b1 = 0;
    D_57aa = 0;
    assert(CONTENTS_GAME_Interact(2, 2, 2, 0));
    assert(D_57b1 == 3 && D_57aa == 9);
    assert(CONTENTS_GAME_Interact(0, 2, 2, 0));
    assert(CONTENTS_State(d)->remaining[0] == 5);
    D_5893_map_id = 33;
    D_5895_map_level = 0;
    D_55a8_party[0].hp = 100;
    D_55a8_party[0].dex = 30;
    D_55a8_party[0].status = 'G';
    D_57ac = 1;
    d = CONTENTS_Find(0, 33, 0, 1, 1);
    assert(CONTENTS_GAME_Shape(1, 1) == 1);
    assert(CONTENTS_GAME_DungeonTile(0x08, 1, 1) == 0x48);
    assert(CONTENTS_GAME_DungeonTile(0x20, 2, 2) == 0x20);
    assert(CONTENTS_GAME_Interact(1, 1, 1, 0) && !(CONTENTS_State(d)->flags & 1));
    assert(CONTENTS_GAME_Interact(3, 1, 1, 0) && (CONTENTS_State(d)->flags & 2));
    assert(CONTENTS_GAME_Interact(1, 1, 1, 0));
    assert(D_55a8_party[0].hp == 99 && (CONTENTS_State(d)->flags & 4));
    assert(CONTENTS_GAME_DungeonTile(0x08, 1, 1) == 0x78);
    assert(CONTENTS_GAME_Interact(2, 1, 1, 0));
    assert(CONTENTS_GAME_DungeonTile(0x08, 1, 1) == 0x08);
    CONTENTS_Reset();
    assert(CONTENTS_GAME_Interact(4, 1, 1, 0));
    assert(CONTENTS_GAME_Interact(1, 1, 1, 0) && D_55a8_party[0].hp == 99); /* magic disarmed */
    D_5893_map_id = 128;
    CONTENTS_BeginEncounter(1, 4);
    assert(CONTENTS_GAME_Interact(0, 2, 3, 0));
    assert(CONTENTS_GAME_Interact(2, 2, 3, 0));
    d = CONTENTS_Find(1, 4, 0, 2, 3);
    assert(CONTENTS_State(d)->remaining[0] == 0);
    CONTENTS_BeginEncounter(1, 4);
    assert(CONTENTS_GAME_Interact(0, 2, 3, 0));
    assert(CONTENTS_State(d)->remaining[0] == 5);
    CONTENTS_EndEncounter();
    assert(!CONTENTS_GAME_Interact(0, 2, 3, 0));
    CONTENTS_Unmount();
    assert(!CONTENTS_GAME_Interact(0, 2, 2, 0));
    const char *packagePath = getenv("IMPERA_CONTENTS_TEST_PACKAGE");
    if (packagePath) {
        FILE *f = fopen(packagePath, "rb");
        assert(f);
        assert(!fseek(f, 0, SEEK_END));
        long size = ftell(f);
        assert(size > 0 && size < MOD_MAX_PACKAGE);
        rewind(f);
        void *bytes = malloc(size);
        assert(bytes && fread(bytes, 1, size, f) == (size_t)size);
        fclose(f);
        ModPackage decoded = {0};
        char error[256];
        assert(MOD_Decode(bytes, size, noBase, NULL, &decoded, error, sizeof(error)));
        assert(decoded.count == 1 && !strcmp(decoded.entries[0].name, "CONTENTS.OVR"));
        CONTENTS_Mount(decoded.entries[0].data, decoded.entries[0].size);
        MOD_Free(&decoded);
        free(bytes);
        D_5893_map_id = 5;
        D_5895_map_level = 0;
        D_57b1 = 0;
        D_57b2 = 0;
        D_587e = 12;
        assert(CONTENTS_GAME_Interact(0, 2, 2, 0));
        assert(CONTENTS_GAME_Interact(2, 2, 2, 0));
        assert(D_57b1 == 8);
        CONTENTS_Unmount();
        puts("PASS Workshop-exported package reveals and collects eight skull keys in the engine");
    }
    puts("PASS contents game Search/Open/Get/Jimmy/magic, full actor capacity, partial claims, "
         "refill, traps and encounter lifetime");
    return 0;
}
