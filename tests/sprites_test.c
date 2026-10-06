#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "common/common.h"
#include "vars.h"
#include "macros.h"
#include "graphics/grap_buf.h"
#include "graphics/sprites.h"
#include "graphics/widescreen.h"
int main(void)
{
    GRAP_BUF_Initialize(NULL);
    byte* tiles=malloc(512*128);
    memset(tiles,0x44,512*128);
    GRAP_BUF_LoadTileset(tiles);
    assert(!SPRITES_HasOverride(0x144));
    SPRITES_SetEnabled(true);
    SPRITES_Load();
    assert(SPRITES_HasOverride(0x144));
    assert(!SPRITES_HasOverride(0x145)); /* wrong dimensions */
    assert(!SPRITES_HasOverride(0x146)); /* missing */
    assert(SPRITES_Composite(0x144,0,0,12,4)==12);
    assert(SPRITES_Composite(0x144,8,0,12,4)==10);
    assert(SPRITES_RGBA(0x144,0,0,0xff000000)>>24==0);
    assert(SPRITES_RGBA(0x144,8,0,0)==0xff55ff55);
    D_52ba_vdp._52d8_page=0;
    memset(g_linearEgaBuffer0,12,320*200);
    GRAP_BUF_PutTile(0,0,0x144,0,0);
    assert(g_linearEgaBuffer0[0]==12 && g_linearEgaBuffer0[8]==10);
    WideLayout l=WIDE_Layout(2560,1080);
    byte* pixels=malloc(l.width*l.height);
    memset(D_6608_map.town,1,32*32);
    memset(D_b11e,1,sizeof(D_b11e));
    memset(D_5c5a,0,sizeof(D_5c5a));
    D_58a4=1; D_58a5=50; D_5893_map_id=13;
    D_5896_map_x=D_5897_map_y=16; D_5895_map_level=0;
    D_5c5a[1]._0_tile=D_5c5a[1]._1_animTile=0x44;
    D_5c5a[1]._2_x=23; D_5c5a[1]._3_y=16;
    assert(WIDE_Compose(pixels,l));
    int pos=(l.mapY+l.rows/2*16)*l.width+l.mapX+(l.columns/2+7)*16;
    assert(pixels[pos]==4 && pixels[pos+8]==10);
    SPRITES_SetEnabled(false); SPRITES_Load();
    assert(!SPRITES_HasOverride(0x144));
    free(pixels); GRAP_BUF_Cleanup();
    return 0;
}
