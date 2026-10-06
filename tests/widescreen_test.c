#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common/common.h"
#include "vars.h"
#include "macros.h"
#include "tiles.h"
#include "funcs.h"
#include "graphics/grap_buf.h"
#include "graphics/grap.h"
#include "graphics/widescreen.h"

static byte pixel(byte* pixels, WideLayout l, int dx, int dy)
{
    return pixels[(l.mapY + (l.rows / 2 + dy) * 16) * l.width + l.mapX + (l.columns / 2 + dx) * 16];
}
int main(void)
{
    WideLayout l = WIDE_Layout(1920, 1080);
    assert(l.scale == 4 && l.columns == 21 && l.rows == 15);
    WideLayout tall = WIDE_Layout(1920, 1200);
    assert(tall.columns == 15 && tall.rows == 14);
    WideLayout broad = WIDE_Layout(2560, 1080);
    assert(broad.columns == 31 && broad.rows == 15);
    assert(WIDE_Layout(320, 200).columns == 11);
    GRAP_Initialize();
    byte* tiles = malloc(512 * 128);
    memset(tiles, 0x11, 512 * 128);
    memset(tiles + 257 * 128, 0x22, 128);
    GRAP_BUF_LoadTileset(tiles);
    /* Sparse sprite: black interior/background becomes ground; outline reaches
     * all eight neighboring pixels and crosses the source tile boundary. */
    memset(tiles + 300 * 128, 0, 128);
    tiles[300 * 128 + 8 * 8] = 0x20;
    assert(GRAP_BUF_SpritePixel(300, 0, 8) == 2);
    assert(GRAP_BUF_SpritePixel(300, 2, 8) == 0); /* opaque by default */
    assert(GRAP_BUF_SpritePixel(300, -1, 7) == -1); /* no outline spill */
    memset(g_linearEgaBuffer0, 5, 320 * 200);
    GRAP_BUF_PutMapSprite(1, 1, 300);
    assert(g_linearEgaBuffer0[32 * 320 + 26] == 0);
    assert(g_linearEgaBuffer0[31 * 320 + 23] == 5);
    GRAP_BUF_SetTransparentSprites(true);
    assert(GRAP_BUF_SpritePixel(300, 0, 8) == 2);
    assert(GRAP_BUF_SpritePixel(300, -1, 7) == 0);
    assert(GRAP_BUF_SpritePixel(300, 2, 8) == -1);
    assert(GRAP_BUF_SpritePixel(300, 15, 8) == -1);
    memset(g_linearEgaBuffer0, 5, 320 * 200);
    GRAP_BUF_PutMapSprite(1, 1, 300);
    assert(g_linearEgaBuffer0[32 * 320 + 24] == 2);
    assert(g_linearEgaBuffer0[31 * 320 + 23] == 0);
    assert(g_linearEgaBuffer0[32 * 320 + 26] == 5);
    GRAP_BUF_PutMapSprite(0, 0, 300);
    assert(g_linearEgaBuffer0[15 * 320 + 7] == 5); /* frame is clipped */
    byte* pixels = malloc((size_t)broad.width * broad.height);
    memset(g_linearEgaBuffer0, 3, 320 * 200);
    for (int y = 0; y < 200; y++) memset(g_linearEgaBuffer0 + y * 320 + 192, 4, 128);
    memset(D_6608_map.town, 1, 32 * 32);
    memset(D_5c5a, 0, sizeof(D_5c5a));
    memset(D_b11e, 1, sizeof(D_b11e));
    D_58a4 = 1;
    D_58a5 = 50;
    D_5893_map_id = 13;
    D_5896_map_x = D_5897_map_y = 16;
    D_5895_map_level = 0;
    memset(D_ab02,1,sizeof(D_ab02));
    memset(D_ac64,0x16,sizeof(D_ac64));
    GetMapViewport(5,5)=0;
    GetActorMap(5,5)=44; /* sparse test sprite at index 300 */
    ULTIMA_56ac_DrawMap();
    assert(g_linearEgaBuffer0[96*320+88]==2);
    assert(g_linearEgaBuffer0[96*320+90]==1); /* fresh terrain under black */
    assert(g_linearEgaBuffer0[95*320+87]==0); /* outline on neighbor */
    GetMapViewport(5,5)=1;
    GetActorMap(5,5)=0x16;
    ULTIMA_56ac_DrawMap();
    assert(g_linearEgaBuffer0[95*320+87]==1); /* no stale outline after move */
    memset(g_linearEgaBuffer0,3,320*200);
    for(int y=0;y<200;y++) memset(g_linearEgaBuffer0+y*320+192,4,128);
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, 0, 0) == 3); /* central effects preserved */
    assert(pixel(pixels, broad, 10, 0) == 1); /* genuinely additional terrain */
    assert(pixels[broad.sidebarX] == 4 && pixels[broad.width - 1] == 4);
    D_5c5a[1]._0_tile = D_5c5a[1]._1_animTile = 1;
    D_5c5a[1]._2_x = 23;
    D_5c5a[1]._3_y = 16;
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, 7, 0) == 2);
    /* Static objects have a single map layer: infer floor by majority without
     * mutating it. Test full fountain animation range and the original viewport. */
    memset(D_5c5a,0,sizeof(D_5c5a));
    memset(tiles+0x44*128,0x33,128);
    D_b11e[0x44]=0x44;
    D_b11e[TILE_MAP_WELL]=TILE_MAP_WELL;
    memset(tiles+TILE_MAP_WELL*128,0,128);
    tiles[TILE_MAP_WELL*128+8*8]=0x20;
    GetMap(23,16)=TILE_MAP_WELL;
    GetMap(22,16)=GetMap(24,16)=GetMap(23,17)=0x44;
    GetMap(23,15)=TILE_MAP_GRASS;
    assert(WIDE_GroundTile(7,0)==0x44);
    assert(WIDE_TerrainPixel(7,0,2,8)==3);
    assert(WIDE_TerrainPixel(7,0,0,8)==2);
    assert(WIDE_TerrainPixel(6,0,15,7)==0); /* neighboring outline */
    assert(WIDE_Compose(pixels,broad));
    int objectX=broad.mapX+(broad.columns/2+7)*16;
    int objectY=broad.mapY+broad.rows/2*16;
    assert(pixels[(objectY+8)*broad.width+objectX+2]==3);
    GRAP_BUF_SetTransparentSprites(false);
    assert(WIDE_GroundTile(7,0)==TILE_MAP_WELL);
    assert(WIDE_Compose(pixels,broad));
    assert(pixels[(objectY+8)*broad.width+objectX+2]==0);
    GRAP_BUF_SetTransparentSprites(true);
    for(int frame=0;frame<4;frame++) {
        GetMap(23,16)=TILE_MAP_FOUNTAIN+frame;
        assert(WIDE_GroundTile(7,0)==0x44);
    }
    /* The audited object list shares the same inference and opt-in behavior. */
    const byte cutouts[]={TILE_MAP_BRAZIER,TILE_MAP_59,0x88,0xa3,TILE_MAP_84,0x8e,
        0x80,0x81,0x82,0x83,TILE_MAP_85,TILE_MAP_86,0x8b,0x99,0xaa,
        TILE_MAP_LADDER_UP,TILE_MAP_LADDER_DOWN,
        TILE_MAP_CANNON_B4,TILE_MAP_CANNON_B5,TILE_MAP_CANNON_B6,TILE_MAP_CANNON_B7};
    for(size_t i=0;i<sizeof(cutouts);i++) {
        byte tile=cutouts[i];
        GetMap(23,16)=tile;
        D_b11e[tile]=tile;
        memset(tiles+tile*128,0,128);
        tiles[tile*128+8*8]=0x20;
        assert(WIDE_GroundTile(7,0)==0x44);
        assert(WIDE_Compose(pixels,broad));
        assert(pixels[(objectY+8)*broad.width+objectX]==2);
        assert(pixels[(objectY+8)*broad.width+objectX+1]==0); /* outline */
        assert(pixels[(objectY+8)*broad.width+objectX+2]==3); /* floor */
        D_5896_map_x=18;
        memset(D_ab02,1,sizeof(D_ab02));
        GetMapViewport(10,5)=tile;
        ULTIMA_56ac_DrawMap();
        assert(g_linearEgaBuffer0[96*320+170]==3);
        D_5896_map_x=16;
        GRAP_BUF_SetTransparentSprites(false);
        assert(WIDE_GroundTile(7,0)==tile);
        assert(WIDE_Compose(pixels,broad));
        assert(pixels[(objectY+8)*broad.width+objectX+2]==0);
        GRAP_BUF_SetTransparentSprites(true);
    }
    /* Empty and occupied stocks use the same grass backdrop; all occupied
     * poses remain transparent through the existing actor sprite path. */
    GetMap(22,16)=GetMap(24,16)=GetMap(23,17)=GetMap(23,15)=TILE_MAP_GRASS;
    D_b11e[TILE_MAP_GRASS]=TILE_MAP_GRASS;
    memset(tiles+TILE_MAP_GRASS*128,0x55,128);
    for(int object=0;object<2;object++) {
        GetMap(23,16)=object?0x8e:TILE_MAP_84;
        assert(WIDE_GroundTile(7,0)==TILE_MAP_GRASS);
        assert(WIDE_Compose(pixels,broad));
        assert(pixels[(objectY+8)*broad.width+objectX+2]==5);
    }
    GetMap(23,16)=TILE_MAP_84;
    D_5c5a[1]._0_tile=D_5c5a[1]._1_animTile=0x44;
    D_5c5a[1]._2_x=23; D_5c5a[1]._3_y=16;
    for(int frame=0;frame<4;frame++) {
        memset(tiles+(256+0x60+frame)*128,0,128);
        tiles[(256+0x60+frame)*128+8*8]=0x20;
    }
    bool stocksReflection;
    int pose=ULTIMA_ResolveActorTile(0x44,TILE_MAP_84,5,5,&stocksReflection);
    assert(pose>=0x60 && pose<=0x63);
    assert(WIDE_Compose(pixels,broad));
    assert(pixels[(objectY+8)*broad.width+objectX+2]==5);
    memset(D_5c5a,0,sizeof(D_5c5a));
    GetMap(22,16)=GetMap(24,16)=GetMap(23,17)=0x44;
    /* All occupied-manacles animation frames are opaque exceptions. Empty
     * manacles still use inferred ground under the static-object rule. */
    GetMap(23,16)=TILE_MAP_85;
    GetMap(22,16)=GetMap(24,16)=GetMap(23,17)=0x44;
    assert(WIDE_GroundTile(7,0)==0x44);
    for(int frame=0x64;frame<=0x67;frame++) {
        int sprite=256+frame;
        memset(tiles+sprite*128,0,128);
        tiles[sprite*128+8*8]=0x20;
        assert(GRAP_BUF_SpritePixel(sprite,2,8)==0);
        assert(GRAP_BUF_SpritePixel(sprite,-1,8)==-1);
        assert(GRAP_BUF_SpritePixel(sprite,0,16)==-1);
        GRAP_BUF_PutMapSprite(1,1,sprite);
        assert(g_linearEgaBuffer0[32*320+26]==0);
    }
    D_5c5a[1]._0_tile=D_5c5a[1]._1_animTile=0x44;
    D_5c5a[1]._2_x=23;D_5c5a[1]._3_y=16;
    assert(WIDE_Compose(pixels,broad));
    assert(pixels[(objectY+8)*broad.width+objectX+2]==0);
    memset(D_5c5a,0,sizeof(D_5c5a));
    const byte opaqueObjects[]={TILE_MAP_CHAIR_90,TILE_MAP_TABLE_94,TILE_MAP_BARREL,
        TILE_MAP_BED,TILE_MAP_DRESSER,TILE_MAP_DOOR_B8,TILE_MAP_FIREPLACE,0xbd};
    for(size_t i=0;i<sizeof(opaqueObjects);i++) {
        GetMap(23,16)=opaqueObjects[i];
        assert(WIDE_GroundTile(7,0)==opaqueObjects[i]);
    }
    GetMap(23,16)=TILE_MAP_BRAZIER;
    D_5896_map_x=18;
    memset(D_ab02,1,sizeof(D_ab02));
    GetMapViewport(10,5)=TILE_MAP_BRAZIER;
    ULTIMA_56ac_DrawMap();
    assert(g_linearEgaBuffer0[96*320+170]==3);
    assert(GetMap(23,16)==TILE_MAP_BRAZIER);
    D_5896_map_x=16;
    GetMap(23,16)=TILE_MAP_WELL;
    D_5896_map_x=18;
    memset(D_ab02,1,sizeof(D_ab02));
    GetMapViewport(10,5)=TILE_MAP_WELL;
    ULTIMA_56ac_DrawMap();
    assert(g_linearEgaBuffer0[96*320+170]==3);
    assert(GetMap(23,16)==TILE_MAP_WELL);
    GetMap(22,16)=GetMap(24,16)=GetMap(23,17)=GetMap(23,15)=TILE_MAP_WALL;
    assert(WIDE_GroundTile(5,0)==TILE_MAP_WELL); /* no ground: opaque fallback */
    GetMap(22,16)=GetMap(24,16)=GetMap(23,17)=GetMap(23,15)=1;
    D_5896_map_x=16;
    GetMap(23,16)=TILE_MAP_BRAZIER;
    GetMap(22,16)=GetMap(24,16)=GetMap(23,17)=GetMap(23,15)=0x40;
    assert(WIDE_GroundTile(7,0)==0x40); /* wooden floors on ship maps */
    GetMap(22,16)=GetMap(24,16)=GetMap(23,17)=GetMap(23,15)=1;
    /* A southern wall stays in front of a sprite's bottom outline. Open
     * ground still receives it, and opaque mode has no outline margin. */
    tiles[300*128+15*8+4]=0x20;
    D_b11e[TILE_MAP_WALL]=TILE_MAP_WALL;
    memset(tiles+TILE_MAP_WALL*128,0x77,128);
    GetMap(16,17)=TILE_MAP_WALL;
    memset(D_ab02,1,sizeof(D_ab02));
    memset(D_ac64,0x16,sizeof(D_ac64));
    GetMapViewport(5,5)=0; GetActorMap(5,5)=44;
    GetMapViewport(5,6)=TILE_MAP_WALL;
    ULTIMA_56ac_DrawMap();
    assert(g_linearEgaBuffer0[103*320+96]==2);
    assert(g_linearEgaBuffer0[104*320+96]==7);
    GetMap(16,17)=GetMapViewport(5,6)=1;
    ULTIMA_56ac_DrawMap();
    assert(g_linearEgaBuffer0[104*320+96]==0);
    GRAP_BUF_SetTransparentSprites(false);
    ULTIMA_56ac_DrawMap();
    assert(g_linearEgaBuffer0[104*320+96]==1);
    GRAP_BUF_SetTransparentSprites(true);
    D_5c5a[1]._0_tile=D_5c5a[1]._1_animTile=44;
    D_5c5a[1]._2_x=23; D_5c5a[1]._3_y=16;
    GetMap(23,16)=1; GetMap(23,17)=TILE_MAP_WALL;
    assert(WIDE_Compose(pixels,broad));
    assert(pixels[(objectY+16)*broad.width+objectX+8]==7);
    GetMap(23,17)=1;
    assert(WIDE_Compose(pixels,broad));
    assert(pixels[(objectY+16)*broad.width+objectX+8]==0);
    /* A bed-bound NPC must keep the same pose across the original viewport
     * boundary. Compare the actual original actor-map result with fullscreen. */
    D_5c5a[1]._2_x=23;
    D_5c5a[1]._3_y=16;
    D_5c5a[1]._0_tile = D_5c5a[1]._1_animTile = 0x44;
    GetMap(23, 16) = TILE_MAP_BED;
    memset(tiles + (256 + 0x1a) * 128, 0x77, 128);
    for(int y=6;y<=10;y++) for(int x=6;x<=10;x++) {
        byte* packed=&tiles[(256+TILE_ACTOR_SLEEPING_IN_BED)*128+y*8+x/2];
        *packed &= x&1?0xf0:0x0f;
    }
    assert(GRAP_BUF_SpritePixel(256+TILE_ACTOR_SLEEPING_IN_BED,8,8)==0);
    assert(GRAP_BUF_SpritePixel(256+TILE_ACTOR_SLEEPING_IN_BED,-1,8)==-1);
    assert(GRAP_BUF_SpritePixel(256+TILE_ACTOR_SLEEPING_IN_BED,8,16)==-1);
    D_5896_map_x = 18;
    memset(D_ab02, 1, sizeof(D_ab02));
    ULTIMA_5394();
    assert(GetActorMap(10, 5) == TILE_ACTOR_SLEEPING_IN_BED);
    ULTIMA_56ac_DrawMap();
    assert(g_linearEgaBuffer0[96*320+176]==0); /* original opaque bed pixels */
    D_5896_map_x = 16;
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, 7, 0) == 7);
    assert(pixels[(objectY+8)*broad.width+objectX+8]==0);
    bool reflection;
    assert(ULTIMA_ResolveActorTile(0x44, TILE_MAP_CHAIR_91, 1, 1, &reflection) == 0x31);
    assert(ULTIMA_ResolveActorTile(0x44, TILE_MAP_LADDER_UP, 1, 1, &reflection) == 0x17);
    GetMap(23, 16) = 1;
    GetMap(22, 16) = 0x09; /* opaque tree hides actors and terrain beyond */
    assert(WIDE_Compose(pixels, broad));
    /* A visible central sprite now outlines the first pixel of this cell. */
    assert(pixels[(broad.mapY + broad.rows / 2 * 16) * broad.width + broad.mapX + (broad.columns / 2 + 6) * 16 + 1] == 1);
    assert(pixel(pixels, broad, 7, 0) == 0);
    GetMap(22, 16) = 1;
    D_58a5 = 2;
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, 7, 0) == 0);
    D_58a5 = 50;
    D_5896_map_x = 31;
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, 7, 0) == 0); /* no reading past town edges */
    /* Read beyond the four resident outdoor blocks without modulo-32 aliasing,
     * including the wrapping seam at coordinate zero. */
    FILE* world = fopen("UNDER.DAT", "wb");
    assert(world);
    byte block[256];
    memset(block, 2, sizeof(block));
    for (int i = 0; i < 256; i++) assert(fwrite(block, 1, 256, world) == 256);
    assert(fclose(world) == 0);
    memset(tiles + 2 * 128, 0x66, 128);
    D_b11e[2] = 2;
    D_5893_map_id = 0;
    D_5895_map_level = 1;
    D_589b = D_589c = 0;
    D_5896_map_x = 26;
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, 7, 0) == 6);
    D_5896_map_x = 0;
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, -7, 0) == 6);
    assert(remove("UNDER.DAT") == 0);
    D_5893_map_id = 0x40;
    assert(!WIDE_Compose(pixels, broad)); /* title uses centered original layout */
    D_5893_map_id = 0xff;
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, 7, 0) == 0); /* combat never fabricates terrain */
    /* Border strips must reach the final row, with one centered opening and
     * no orphaned arrow tips copied near the outer corners. */
    for (int y = 0; y < 8; y++) {
        memset(g_linearEgaBuffer0 + y * 320, 2, 192);
        memset(g_linearEgaBuffer0 + (184 + y) * 320, 2, 192);
        memset(g_linearEgaBuffer0 + y * 320 + 40, 5, 112);
        memset(g_linearEgaBuffer0 + (184 + y) * 320 + 40, 6, 112);
    }
    assert(WIDE_Compose(pixels, tall));
    int middle = tall.sidebarX / 2;
    assert(pixels[40] == 2 && pixels[tall.sidebarX - 40] == 2);
    assert(pixels[middle - 56] == 5 && pixels[middle + 55] == 5);
    assert(pixels[middle - 57] == 2 && pixels[middle + 56] == 2);
    int lastRow = (tall.height - 1) * tall.width;
    assert(pixels[lastRow + 40] == 2);
    assert(pixels[lastRow + middle] == 6);
    assert(tall.mapY + tall.rows * 16 <= tall.height - 8);
    free(pixels);
    GRAP_Cleanup();
    puts("Expanded map, sidebar, visibility, actors, and layout tests passed.");
    return 0;
}
