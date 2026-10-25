#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common/common.h"
#include "vars.h"
#include "outsubs.h"
#include "mainout.h"
#include "town.h"
#include "npc.h"
#include "sjog.h"
#include "cmds.h"
#include "dnglook.h"
#include "dungeon.h"
#include "cast2.h"
#include "macros.h"
#include "funcs.h"
#include "mod/contents.h"
#include "mod/types.h"
#include "tiles.h"
#include <stdlib.h>
#include <SDL3/SDL.h>
byte *ULTIMA_4402_BaseAddr(int, int);

static int directionSelections;
int __wrap_ULTIMA_35ec_SelectDirection(void) {
    directionSelections++;
    D_5876 = 1;
    D_5878 = 0;
    return 1;
}
void __wrap_ULTIMA_1a3e_PrintNumber(int a, int b, int c) {
    (void)a;
    (void)b;
    (void)c;
}
void __wrap_ULTIMA_16ba_PrintChar(int c) { (void)c; }
void __wrap_ULTIMA_1c9e_SelectCharset(int c) { (void)c; }
void __wrap_AUDIO_PlayBgmPerMap(void) {}
static int loadX,loadY,loads,visibility;
void __wrap_OUTSUBS_01b4(int dx,int dy) {loadX=dx;loadY=dy;loads++;}
void __wrap_ULTIMA_5e4a(void) {visibility++;}

static void testShift(int dx,int dy)
{
    byte original[1024];
    for(int block=0;block<4;block++)
        for(int i=0;i<256;i++) original[block*256+i]=(byte)(block*43+i);
    memcpy(D_6608,original,sizeof(original));
    OUTSUBS_02c8(dx,dy);
    for(int y=0;y<2;y++) for(int x=0;x<2;x++) {
        int sx=x+dx,sy=y+dy;
        int source=sx>=0 && sx<2 && sy>=0 && sy<2?sx+sy*2:x+y*2;
        assert(memcmp(D_6608+(x+y*2)*256,original+source*256,256)==0);
    }
}

static void testMove(int x,int y,int dx,int dy,int shiftX,int shiftY)
{
    D_589b=D_589c=0x40;
    D_5896_map_x=(byte)(0x40+x);D_5897_map_y=(byte)(0x40+y);
    loads=visibility=0;
    MAINOUT_0354(dx,dy);
    assert(D_589b==((0x40+shiftX*16)&255));
    assert(D_589c==((0x40+shiftY*16)&255));
    assert(D_5896_map_x==0x40+x+dx && D_5897_map_y==0x40+y+dy);
    assert(loads==((shiftX||shiftY)?1:0) && visibility==loads);
    if(loads) assert(loadX==shiftX && loadY==shiftY);
}

static size_t appendMap(unsigned char *b, size_t off, unsigned kind, unsigned id, int world, int w,
                        int h, const char *name, const char *resource) {
    unsigned char *r = b + off;
    unsigned length = MAPS_RECORD + w * h;
    TYPES_Put32(r, length);
    r[4] = kind;
    r[5] = id;
    r[9] = 5;
    r[10] = w;
    r[11] = w >> 8;
    r[12] = h;
    r[13] = h >> 8;
    r[14] = world;
    if (kind == 1) {
        r[15] = world ? 10 : 18;
        r[16] = world ? 8 : 12;
        r[17] = w - 1;
        r[18] = h - 1;
    }
    strcpy((char *)r + 24, name);
    strcpy((char *)r + 88, resource);
    memset(r + MAPS_RECORD, 5, w * h);
    return off + length;
}
void __wrap_ULTIMA_1850_PrintString(char *text) { (void)text; }
static void testExtendedMaps(void) {
    unsigned char *catalog = calloc(1, 200000);
    assert(catalog);
    memcpy(catalog, "IMAPS001", 8);
    TYPES_Put32(catalog + 8, 5);
    size_t size = MAPS_HEADER;
    size = appendMap(catalog, size, 0, 0, -2, 19, 13, "Britannia", "BRIT.DAT");
    size = appendMap(catalog, size, 0, 1, -2, 23, 17, "Underworld", "UNDER.DAT");
    size = appendMap(catalog, size, 1, 42, 0, 256, 48, "New island", "LOC042.DAT");
    size = appendMap(catalog, size, 1, 43, -1, 40, 80, "Deep cavern", "LOC043.DAT");
    size = appendMap(catalog, size, 2, 0, -2, 65, 24, "Combat map 1", "BRIT.CBT");
    assert(MAPS_Mount(catalog, size));
    D_5893_map_id = 0;
    D_5895_map_level = 0;
    D_5896_map_x = D_5897_map_y = 0;
    MAINOUT_0354(-1, -1);
    assert(D_5896_map_x == 18 && D_5897_map_y == 12);
    assert(MAINOUT_08de_EnterCmd() == 0);
    assert(D_5893_map_id == 42 && D_5896_map_x == 255 && D_5897_map_y == 47);
    assert(MAP_WIDTH == 256 && MAP_HEIGHT == 48);
    unsigned char npcs[4608] = {0};
    for (int d = 0; d < 3; d++) {
        npcs[16 + 3 + d] = 255;
        npcs[16 + 6 + d] = 47;
    }
    npcs[513] = 80;
    npcs[545] = 1;
    FILE *npcFile = fopen("LOC042.NPC", "wb");
    assert(npcFile && fwrite(npcs, 1, sizeof(npcs), npcFile) == sizeof(npcs));
    fclose(npcFile);
    D_587f = 6;
    assert(NPC_InitializeNewGame());
    assert(D_659e[1] == 80 && D_5f5e[1]._2_x == 255 && D_5f5e[1]._4_y == 47);
    remove("LOC042.NPC");

    D_5896_map_x = 0;
    D_5897_map_y = 20;
    D_587c_partyTile = TILE_ACTOR_AVATAR;
    D_5c5a[1]._0_tile = D_5c5a[1]._1_animTile = TILE_ACTOR_WIZARD;
    D_5c5a[1]._2_x = 255;
    D_5c5a[1]._3_y = 20;
    D_5c5a[1]._4_z = 0;
    memset(D_ab02, 5, sizeof(D_ab02));
    memset(D_ac64, 0, sizeof(D_ac64));
    ULTIMA_5394();
    assert(GetActorMap(4, 5) != TILE_ACTOR_WIZARD);
    D_5c5a[1]._2_x = 1;
    memset(D_ab02, 5, sizeof(D_ab02));
    ULTIMA_5394();
    assert(GetActorMap(6, 5) == TILE_ACTOR_WIZARD);
    *ULTIMA_4402_BaseAddr(0, 20) = TILE_MAP_LADDER_UP;
    assert(TOWN_0b82_KlimbCmd() == 1 && D_5895_map_level == 0);
    *ULTIMA_4402_BaseAddr(255, 47) = 68;
    assert(GetMap(255, 47) == 68);
    assert(TOWN_ExtendedExit());
    assert(!D_5893_map_id && !D_5895_map_level && D_5896_map_x == 18 && D_5897_map_y == 12);
    D_5895_map_level = 255;
    D_5896_map_x = 10;
    D_5897_map_y = 8;
    assert(MAINOUT_08de_EnterCmd() == 0 && D_5893_map_id == 43 && D_5896_map_x == 39 &&
           D_5897_map_y == 79);
    assert(TOWN_ExtendedExit());
    assert(!D_5893_map_id && D_5895_map_level == 255 && D_5896_map_x == 10 && D_5897_map_y == 8);
    D_5893_map_id = 128;
    CONTENTS_BeginEncounter(1, 0);
    assert(MAP_WIDTH == 65 && MAP_HEIGHT == 24);
    memset(D_ad14, 0xcc, 352);
    GetCombatMap(64, 23) = 68;
    assert(*ULTIMA_4402_BaseAddr(64, 23) == 68 && D_ad14[0] == 0xcc);
    assert(GetCombatMapSpecial(1, 11) == 0xcc && GetCombatMap(11, 1) == 5);
    D_5896_map_x = 64;
    D_5897_map_y = 23;
    assert(MAP_VIEW_X == 54 && MAP_VIEW_Y == 13);
    FILE *state = tmpfile();
    assert(state && MAPS_WriteState(state));
    MAPS_Reset();
    assert(GetCombatMap(64, 23) == 5);
    rewind(state);
    assert(MAPS_ReadState(state, 0) && GetCombatMap(64, 23) == 5);
    rewind(state);
    assert(MAPS_ReadState(state, 1) && GetCombatMap(64, 23) == 68);
    assert(MAPS_Record(1, 42, 0)[MAPS_RECORD + 47 * 256 + 255] == 68);
    fclose(state);
    D_5893_map_id = 43;
    D_imperaNpcCount = 32;
    D_659e[1] = TILE_ACTOR_WIZARD;
    TOWN_0052_SetNpcKilled(1);
    assert(TOWN_0000_CheckNpcKilled(1));
    state = tmpfile();
    assert(state && NPC_WriteExtraState(state));
    NPC_ResetExtra();
    assert(!TOWN_0000_CheckNpcKilled(1));
    rewind(state);
    assert(NPC_ReadExtraState(state, 1));
    D_659e[1] = TILE_ACTOR_WIZARD;
    assert(TOWN_0000_CheckNpcKilled(1));
    fclose(state);
    MAPS_Mount(NULL, 0);
    free(catalog);
    D_5893_map_id = 0;
    D_5895_map_level = 0;
}

static void testDungeonDimensions(void) {
    unsigned char *catalog = calloc(1, 80000);
    assert(catalog);
    memcpy(catalog, "IMAPS001", 8);
    TYPES_Put32(catalog + 8, 3);
    size_t off = MAPS_HEADER, record = off;
    off = appendMap(catalog, off, 4, 33, -2, 19, 3, "Deceit level 1", "DUNGEON.DAT");
    record = off;
    off = appendMap(catalog, off, 4, 33, -2, 256, 256, "Deceit level 2", "DUNGEON.DAT");
    catalog[record + 6] = 1;
    catalog[record + 8] = 1;
    record = off;
    off = appendMap(catalog, off, 4, 33, -2, 1, 1, "Deceit level 3", "DUNGEON.DAT");
    catalog[record + 6] = 2;
    catalog[record + 8] = 2;
    assert(MAPS_Mount(catalog, off));
    memset(D_595a, 0, sizeof(D_595a));
    D_5893_map_id = 33;
    D_5895_map_level = 0;
    assert(MAP_WIDTH == 19 && MAP_HEIGHT == 3);
    GetDungeonMap(18, 2, 0) = 0x82;
    assert(GetDungeonMap(-1, -1, 0) == 0x82 && GetDungeonMap(37, 5, 0) == 0x82);
    assert(D_595a[18] == 0 && GetDungeonMap(0, 0, 1) == 5);
    D_5896_map_x = D_5897_map_y = 0;
    D_58a0 = DIR_UP;
    DNGLOOK_0fda();
    assert(D_5897_map_y == 2);
    D_58a0 = DIR_LEFT;
    DNGLOOK_0fda();
    assert(D_5896_map_x == 18);
    D_58a0 = DIR_RIGHT;
    DNGLOOK_0fda();
    assert(D_5896_map_x == 0);
    D_58a0 = DIR_DOWN;
    DNGLOOK_0fda();
    assert(D_5897_map_y == 0);
    D_5896_map_x = 18;
    D_5897_map_y = 2;
    D_6603 = 1;
    GetDungeonMap(18, 2, 0) = 0;
    GetDungeonMap(0, 2, 0) = 0x82;
    assert(CAST2_07bc(0) == -1 && GetDungeonMap(0, 2, 0) == 0);
    D_5895_map_level = 1;
    assert(MAP_WIDTH == 256 && MAP_HEIGHT == 256);
    D_5896_map_x = 255;
    D_58a0 = DIR_RIGHT;
    DNGLOOK_0fda();
    assert(D_5896_map_x == 0);
    D_58a0 = DIR_LEFT;
    DNGLOOK_0fda();
    assert(D_5896_map_x == 255);
    D_5c5a[1]._5 = 0xff;
    D_5c5a[1]._2_x = D_5c5a[1]._3_y = 255;
    D_5896_map_x = 254;
    D_5897_map_y = 255;
    D_6603 = 1;
    assert(DUNGEON_1d4a_AttackCmd() == 0); /* 255,255 is a cell, not an inactive monster. */
    GetDungeonMap(255, 255, 1) = 0xf1;
    D_3840 = 0;
    memset(D_58e0, 0, sizeof(D_58e0));
    DNGLOOK_0844(1);
    DNGLOOK_093a();
    assert(GetDungeonMap(255, 255, 1) == 0xa1);
    GetDungeonMap(254, 255, 1) = 0x83;
    FILE *saved = tmpfile();
    assert(saved && MAPS_WriteState(saved));
    MAPS_Reset();
    assert(GetDungeonMap(254, 255, 1) == 5);
    rewind(saved);
    assert(MAPS_ReadState(saved, 1));
    fclose(saved);
    assert(GetDungeonMap(254, 255, 1) == 0x83);
    D_5895_map_level = 2;
    assert(MAP_WIDTH == 1 && MAP_HEIGHT == 1);
    GetDungeonMap(-20, 999, 2) = 0x40;
    assert(GetDungeonMap(0, 0, 2) == 0x40);
    D_5895_map_level = 3;
    assert(MAP_WIDTH == 8 && MAP_HEIGHT == 8);
    GetDungeonMap(9, 8, 3) = 0x50;
    assert(D_595a[3 * 64 + 1] == 0x50);
    assert(GetDungeonMap(0, 0, 8) ==
           0xb0); /* Invalid levels cannot index past the native buffer. */
    MAPS_Mount(NULL, 0);
    free(catalog);
    D_5893_map_id = 0;
    D_5895_map_level = 0;
}

static void testNewDungeon(void) {
    assert(SDL_CreateDirectory("SAVEGAME"));
    unsigned char *catalog = calloc(1, 20000);
    assert(catalog);
    memcpy(catalog, "IMAPS001", 8);
    TYPES_Put32(catalog + 8, 8);
    size_t off = MAPS_HEADER;
    for (unsigned f = 0; f < 8; ++f) {
        size_t record = off;
        off = appendMap(catalog, off, 4, 42, f ? -2 : 0, 19, 3, "Crystal depths", "DUNGEON.DAT");
        catalog[record + 6] = 64 + f;
        catalog[record + 8] = f;
        catalog[record + 21] = 112;
        if (!f) {
            catalog[record + 15] = 18;
            catalog[record + 16] = 12;
            catalog[record + 17] = 9;
            catalog[record + 18] = 1;
        }
    }
    assert(MAPS_Mount(catalog, off));
    assert(MAPS_IsDungeon(42) && !MAPS_Outside(42) && !MAPS_IsTown(42));
    assert(MAPS_DungeonRoom(42, 2) == 114 && MAPS_DungeonRoom(33, 2) == 2);
    D_5893_map_id = D_5895_map_level = 0;
    D_5896_map_x = 18;
    D_5897_map_y = 12;
    D_587c_partyTile = TILE_ACTOR_HORSE;
    assert(MAINOUT_08de_EnterCmd() == 0 && !D_5893_map_id);
    D_587c_partyTile = TILE_ACTOR_AVATAR;
    assert(MAINOUT_08de_EnterCmd() == 0);
    assert(D_5893_map_id == 42 && D_5895_map_level == 0 && D_5896_map_x == 9 && D_5897_map_y == 1);
    assert(MAP_WIDTH == 19 && MAP_HEIGHT == 3);
    GetDungeonMap(18, 2, 0) = 0xf2;
    GetDungeonMap(18, 2, 7) = 0xf2;
    DNGLOOK_0844(2);
    assert(GetDungeonMap(18, 2, 0) == 0xa2 && GetDungeonMap(18, 2, 7) == 0xa2);
    FILE *state = tmpfile();
    assert(state && MAPS_WriteState(state));
    MAPS_Reset();
    assert(GetDungeonMap(18, 2, 0) == 5);
    rewind(state);
    assert(MAPS_ReadState(state, 1));
    fclose(state);
    assert(GetDungeonMap(18, 2, 7) == 0xa2);
    D_5895_map_level = 3;
    DUNGEON_1d08_Exit();
    assert(!D_5893_map_id && !D_5895_map_level && D_5896_map_x == 18 && D_5897_map_y == 12);
    catalog[MAPS_HEADER + 14] = 255;
    assert(MAPS_Mount(catalog, off));
    D_5895_map_level = 255;
    assert(MAINOUT_08de_EnterCmd() == 0 && D_5893_map_id == 42 && !D_5895_map_level);
    DUNGEON_1d08_Exit();
    assert(!D_5893_map_id && D_5895_map_level == 255 && D_5896_map_x == 18 && D_5897_map_y == 12);
    MAPS_Mount(NULL, 0);
    assert(!MAPS_IsDungeon(42) && MAPS_IsTown(42));
    free(catalog);
}

static void spawnObject(unsigned slot, unsigned kind, unsigned shape, unsigned value,
                        unsigned aux) {
    D_659e[slot] = shape;
    D_5f5e[slot]._a = value;
    D_5f5e[slot]._c = 0;
    memset(&D_5d5e[slot], 0, sizeof(D_5d5e[slot]));
    for (int j = 0; j < 3; j++) {
        D_5d5e[slot].type[j] = NPCS_OBJECT_MARKER | kind;
        D_5d5e[slot].x[j] = 21;
        D_5d5e[slot].y[j] = 19;
    }
    D_5d5e[slot].time[3] = aux;
    TOWN_1726(slot, 21, 19, 0);
    if (kind == NPCS_OBJECT_TRIGGER)
        return;
    assert(D_5c5a[slot]._0_tile == shape && D_5c5a[slot]._5 == value && D_5c5a[slot]._7 == aux);
}
static void testLocationObjects(void) {
    NPC_ResetExtra();
    D_imperaNpcCount = 48;
    D_5893_map_id = 42;
    D_5895_map_level = 0;
    D_5896_map_x = 20;
    D_5897_map_y = 19;
    D_587c_partyTile = TILE_ACTOR_AVATAR;
    memset(D_5c5a, 0, sizeof(D_5c5a));
    memset(D_659e, 0, sizeof(D_659e));
    memset(D_5f5e, 0, sizeof(D_5f5e));
    memset(D_6608, 5, 1024);
    D_57aa = 0;
    spawnObject(32, 11, 2, 7, 0);
    SJOG_18ce_GetCmd();
    assert(D_57aa == 7 && !D_5c5a[32]._0_tile && !D_659e[32]);
    D_57b1 = 0;
    spawnObject(33, 16, 7, 131, 0);
    SJOG_18ce_GetCmd();
    assert(D_57b1 == 3 && !D_659e[33]);
    D_5828[7] = 0;
    spawnObject(34, 18, 3, 7, 0);
    SJOG_18ce_GetCmd();
    assert(D_5828[7] == 1);
    D_57c0[32] = 0;
    spawnObject(35, 20, 5, 32, 0);
    SJOG_18ce_GetCmd();
    assert(D_57c0[32] == 1);
    D_5840[4] = 0;
    spawnObject(36, 13, 25, 4, 0);
    SJOG_18ce_GetCmd();
    assert(D_5840[4] == 255);
    for (int identity = 0; identity < 3; identity++) {
        D_57b6[identity] = 0;
        spawnObject(40 + identity, 27, 180, identity, 0);
        SJOG_18ce_GetCmd();
        assert(D_57b6[identity] == 255 && !D_659e[40 + identity]);
    }
    D_57b4 = D_57b5 = D_57b3 = 0;
    spawnObject(43, 28, 181, 0, 0);
    SJOG_18ce_GetCmd();
    assert(D_57b4 == 255 && !D_659e[43]);
    spawnObject(44, 29, 182, 0, 0);
    SJOG_18ce_GetCmd();
    assert(D_57b5 == 255 && !D_659e[44]);
    spawnObject(45, 30, 183, 0, 0);
    SJOG_18ce_GetCmd();
    assert(D_57b3 == 255 && !D_659e[45]);
    // Authored Shadow Lords use native hostile NPC behavior and retain their identity.
    spawnObject(46, 26, 252, 0, 2);
    assert(NPC_PlacedShadowLord() == 2);
    D_5c5a[0]._2_x = 20;
    D_5c5a[0]._3_y = 19;
    NPC_0db4(8);
    assert(D_65be == 0x61 && D_65bf == 46 && NPC_ObjectKind(46) == 26);
    D_5c5a[46]._0_tile = D_659e[46] = 0;
    // A whirlpool uses the native alternate-turn delay; off-floor objects stay inactive.
    spawnObject(47, 25, 236, 1, 0);
    D_5c5a[47]._2_x = D_5c5a[47]._3_y = 4;
    D_5c5a[47]._4_z = 1;
    NPC_0db4(8);
    assert(D_5c5a[47]._5 == 1);
    D_5c5a[47]._4_z = 0;
    NPC_0db4(8);
    assert(D_5c5a[47]._5 == 0);
    D_5c5a[47]._0_tile = D_659e[47] = 0;
    spawnObject(47, 24, 44, 100, 0);
    D_5894 = 42;
    D_5893_map_id = 255;
    D_58a1 = 0;
    D_58a0 = 0;
    D_58a3 = 1;
    SJOG_203e(47);
    assert(D_5893_map_id == 42 && D_5c5a[47]._0_tile == 36 && D_5c5a[47]._5 == 99 &&
           D_5c5a[47]._7 == 2);
    D_5c5a[47]._0_tile = D_659e[47] = 0;

    spawnObject(37, 22, 31, 255, 0xb1);
    assert(NPC_BloodAt(21, 19, 0));
    D_5c5a[37]._2_x = D_5c5a[37]._3_y = 0;
    assert(ULTIMA_368e_FindActorTileAtPos(0, 0, 0) == 31);
    assert(ULTIMA_3702(0, 0, 0) == 31);
    D_5c5a[37]._2_x = 21;
    D_5c5a[37]._3_y = 19;

    D_5896_map_x = 21;
    D_5897_map_y = 19;
    D_587c_partyTile = TILE_ACTOR_AVATAR;
    spawnObject(38, 6, 40, 0, 0);
    assert(CMDS_07f6_BoardCmd() && D_587c_partyTile == 40 && !D_659e[38]);
    D_587c_partyTile = TILE_ACTOR_AVATAR;
    spawnObject(38, 7, 36, 99, 1);
    assert(CMDS_07f6_BoardCmd() && D_587c_partyTile == 36 && D_5c5a[0]._5 == 99 &&
           D_5c5a[0]._7 == 1);
    spawnObject(39, 9, 29, 68, 0);
    D_5c5a[39]._0_tile = 0;
    D_5d5e[39].x[1] = 5;
    D_5d5e[39].y[1] = 6;
    D_5d5e[39].x[2] = 7;
    D_5d5e[39].y[2] = 8;
    D_5f5e[39]._0 = 1;
    NPC_ApplyObjectTriggers(21, 19, 0);
    assert(GetMap(5, 6) == 68 && GetMap(7, 8) == 68);
    GetMap(5, 6) = 5;
    NPC_ApplyObjectTriggers(21, 19, 0);
    assert(GetMap(5, 6) == 5);
    FILE *state = tmpfile();
    assert(state && NPC_WriteExtraState(state));
    NPC_ResetExtra();
    rewind(state);
    assert(NPC_ReadExtraState(state, 1));
    fclose(state);
    unsigned char definition[4648] = {0};
    memcpy(definition + 4608, "INPC0001", 8);
    definition[4616] = 1;
    definition[4626] = 32;
    for (int j = 0; j < 3; j++) {
        definition[4628 + j] = NPCS_OBJECT_MARKER | 11;
        definition[4631 + j] = 21;
        definition[4634 + j] = 19;
    }
    definition[4644] = 2;
    definition[4645] = 7;
    FILE *file = fopen("LOC042.NPC", "wb");
    assert(file && fwrite(definition, 1, sizeof(definition), file) == sizeof(definition));
    fclose(file);
    NPC_LoadExtra("LOC042.NPC", 0);
    assert(D_659e[32] == 0);
    remove("LOC042.NPC");
    NPC_ResetExtra();
    D_5893_map_id = 0;
    D_587c_partyTile = TILE_ACTOR_AVATAR;
    D_5896_map_x = 20;
    D_5897_map_y = 19;
    D_57b4 = 0;
    ULTIMA_3a74(181, 181, 21, 19, 0, 0, 1);
    SJOG_18ce_GetCmd();
    assert(D_57b4 == 255 && !D_5c5a[1]._0_tile);
    // Ships in location water retain the same Attack restriction as world ships.
    D_5893_map_id = 42;
    D_imperaNpcCount = 32;
    memset(D_5c5a, 0, sizeof(D_5c5a));
    memset(D_6608, 1, 1024);
    directionSelections = 0;
    D_587c_partyTile = 36;
    TOWN_09e6_AttackCmd();
    assert(directionSelections == 1);
    D_587c_partyTile = 40;
    TOWN_09e6_AttackCmd();
    assert(directionSelections == 1);
    D_5893_map_id = 0;
}

int main(void)
{
    /* Northeast reproduces the original fortified memcpy overflow first. */
    testShift(1,-1);
    for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) testShift(dx,dy);
    for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
        if(!dx&&!dy) continue;
        testMove(dx<0?5:dx>0?26:16,dy<0?5:dy>0?26:16,dx,dy,dx,dy);
        testMove(16,16,dx,dy,0,0);
        if(dx&&dy) {
            testMove(dx<0?5:26,16,dx,dy,dx,0);
            testMove(16,dy<0?5:26,dx,dy,0,dy);
        }
    }
    D_589b=D_589c=0;
    D_5896_map_x=D_5897_map_y=5;
    MAINOUT_0354(-1,-1);
    assert(D_589b==240 && D_589c==240);
    testExtendedMaps();
    testLocationObjects();
    testDungeonDimensions();
    testNewDungeon();
    puts("World block scrolling and diagonal boundary tests passed.");
    return 0;
}
