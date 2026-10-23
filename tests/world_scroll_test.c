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
#include "macros.h"
#include "funcs.h"
#include "mod/contents.h"
#include "mod/types.h"
#include "tiles.h"
#include <stdlib.h>
byte *ULTIMA_4402_BaseAddr(int, int);

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
    puts("World block scrolling and diagonal boundary tests passed.");
    return 0;
}
