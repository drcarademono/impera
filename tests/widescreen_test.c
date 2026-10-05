#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common/common.h"
#include "vars.h"
#include "macros.h"
#include "graphics/grap_buf.h"
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
    GRAP_BUF_Initialize(NULL);
    byte* tiles = malloc(512 * 128);
    memset(tiles, 0x11, 512 * 128);
    memset(tiles + 257 * 128, 0x22, 128);
    GRAP_BUF_LoadTileset(tiles);
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
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, 0, 0) == 3); /* central effects preserved */
    assert(pixel(pixels, broad, 10, 0) == 1); /* genuinely additional terrain */
    assert(pixels[broad.sidebarX] == 4 && pixels[broad.width - 1] == 4);
    D_5c5a[1]._0_tile = D_5c5a[1]._1_animTile = 1;
    D_5c5a[1]._2_x = 23;
    D_5c5a[1]._3_y = 16;
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, 7, 0) == 2);
    GetMap(22, 16) = 0x09; /* opaque tree hides actors and terrain beyond */
    assert(WIDE_Compose(pixels, broad));
    assert(pixel(pixels, broad, 6, 0) == 1);
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
    GRAP_BUF_Cleanup();
    puts("Expanded map, sidebar, visibility, actors, and layout tests passed.");
    return 0;
}
