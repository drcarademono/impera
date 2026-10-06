#include "common/common.h"
#include "common/file.h"
#include "vars.h"
#include "funcs.h"
#include "macros.h"
#include "tiles.h"
#include "sprites.h"
#include "grap_buf.h"
#include "widescreen.h"
#include <string.h>

WideLayout WIDE_Layout(int width, int height)
{
    WideLayout l;
    /* Reserve room for extra rows instead of scaling the old 200px canvas
     * all the way up to the display height. */
    l.scale = height / 240;
    if (width / 320 < l.scale) l.scale = width / 320;
    if (l.scale < 1) l.scale = 1;
    l.width = width / l.scale;
    l.height = height / l.scale;
    l.sidebarX = l.width - 128;
    l.columns = (l.sidebarX - 16) / 16;
    l.rows = (l.height - 16) / 16;
    l.mapX = 8 + ((l.sidebarX - 16) - l.columns * 16) / 2;
    l.mapY = 8 + ((l.height - 16) - l.rows * 16) / 2;
    return l;
}

/* Additional world blocks are read separately: looking beyond the resident
 * 32x32 map must not wrap back into the four blocks used by gameplay. */
static byte WorldTile(int x, int y)
{
    static byte blocks[256][256];
    static byte loaded[256];
    static int level = -1;
    x &= 255;
    y &= 255;
    int block = (y / 16) * 16 + x / 16;
    if (level != D_5895_map_level) {
        memset(loaded, 0, sizeof(loaded));
        level = D_5895_map_level;
    }
    if (((x - D_589b) & 255) < 32 && ((y - D_589c) & 255) < 32)
        return *ULTIMA_4402_GetTileAddr(x, y);
    if (!loaded[block]) {
        if (!level && D_3876[block] == 255)
            memset(blocks[block], 1, 256);
        else {
            FILE* file = FILE_Open(level ? "UNDER.DAT" : "BRIT.DAT", "rb");
            if (!file || fseek(file, (level ? block : D_3876[block]) * 256, SEEK_SET) != 0 ||
                fread(blocks[block], 1, 256, file) != 256)
                memset(blocks[block], 255, 256);
            if (file) fclose(file);
        }
        loaded[block] = 1;
    }
    byte tile = blocks[block][(y & 15) * 16 + (x & 15)];
    if (tile >= 0x16 && tile <= 0x18) {
        bool open = true;
        for (int i = 0; i < 8; i++)
            if (D_3866[i] == block) open = D_58d0[i] == 0;
        if (open) tile = 0xdf;
    }
    if (tile == 0x19) {
        bool ruined = false;
        for (int i = 0; i < 8; i++)
            if (D_386e[i] == block) ruined = D_58d8[i] > 127;
        if (ruined) tile = 0x1a;
    }
    return tile;
}

byte WIDE_MapTile(int dx, int dy)
{
    int x = D_5896_map_x + dx, y = D_5897_map_y + dy;
    if (D_5893_map_id == 0) return WorldTile(x, y);
    if (x < 0 || y < 0 || x >= 32 || y >= 32) return 255;
    return *ULTIMA_4402_GetTileAddr(x, y);
}

static bool Transparent(byte tile, int distance)
{
    if (tile == 0x4b || tile == 0x4a || tile == 0xba || tile == 0xbb || tile == 0x98)
        return distance != 1;
    return memchr(D_6a86, tile, sizeof(D_6a86)) == NULL;
}

bool WIDE_Visible(int dx, int dy)
{
    if (D_58a5 == 0) return dx == 0 && dy == 0;
    /* Daylight illuminates the larger viewport; torch/night light keeps the
     * original squared-distance limit. Opaque intervening tiles block sight. */
    if (D_58a5 < 50 && dx * dx + dy * dy > D_58a5) return false;
    int x = 0, y = 0, ax = abs(dx), ay = abs(dy);
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, error = ax - ay;
    while (x != dx || y != dy) {
        int twice = error * 2;
        if (twice > -ay) { error -= ay; x += sx; }
        if (twice < ax) { error += ax; y += sy; }
        if (x == dx && y == dy) break;
        if (!Transparent(WIDE_MapTile(x, y), x * x + y * y)) return false;
    }
    return true;
}

static void Copy(byte* dst, int stride, int dx, int dy, int sx, int sy, int w, int h)
{
    for (int y = 0; y < h; y++)
        memcpy(dst + (dy + y) * stride + dx,
               g_linearEgaBuffer0 + (sy + y) * 320 + sx, w);
}

bool WIDE_Compose(byte* pixels, WideLayout l)
{
    if (!D_58a4 || (D_5893_map_id >= 33 && D_5893_map_id < 128) ||
        l.columns < 11 || l.rows < 11 || !GRAP_BUF_HasTileset()) return false;
    memset(pixels, 0, (size_t)l.width * l.height);
    /* Relocate the original status and command column intact. */
    Copy(pixels, l.width, l.sidebarX, 0, 192, 0, 128, 200);
    for (int y = 8; y < l.height - 8; y++) {
        Copy(pixels, l.width, 0, y, 0, 16, 8, 1);
        Copy(pixels, l.width, l.sidebarX - 8, y, 184, 16, 8, 1);
    }
    /* Extend only the frame's blue strips; copy ornaments and wind text. */
    for (int x = 0; x < l.sidebarX; x++) {
        for (int y = 0; y < 8; y++)
            pixels[y * l.width + x] = g_linearEgaBuffer0[y * 320 + 16];
        for (int y = 0; y < 8; y++)
            pixels[(l.height - 8 + y) * l.width + x] = g_linearEgaBuffer0[(184 + y) * 320 + 16];
    }
    /* Corners stay at the edges. Each central opening travels with both of
     * its arrow tips, leaving uninterrupted blue strips between them. */
    Copy(pixels, l.width, 0, 0, 0, 0, 8, 8);
    Copy(pixels, l.width, l.sidebarX - 8, 0, 184, 0, 8, 8);
    Copy(pixels, l.width, l.sidebarX / 2 - 56, 0, 40, 0, 112, 8);
    Copy(pixels, l.width, 0, l.height - 8, 0, 184, 8, 8);
    Copy(pixels, l.width, l.sidebarX - 8, l.height - 8, 184, 184, 8, 8);
    Copy(pixels, l.width, l.sidebarX / 2 - 56, l.height - 8, 40, 184, 112, 8);

    int cx = l.columns / 2, cy = l.rows / 2;
    if (D_5893_map_id < 33) {
        for (int row = 0; row < l.rows; row++) {
            for (int col = 0; col < l.columns; col++) {
                int dx = col - cx, dy = row - cy;
                if (abs(dx) <= 5 && abs(dy) <= 5) continue;
                if (!WIDE_Visible(dx, dy)) continue;
                byte tile = WIDE_MapTile(dx, dy);
                if (tile == 255) continue;
                int idx = D_b11e[tile];
                for (int actor = 31; actor >= 0; actor--) {
                    ActorFmt* a = &D_5c5a[actor];
                    int ax = a->_2_x - D_5896_map_x, ay = a->_3_y - D_5897_map_y;
                    if (D_5893_map_id == 0) {
                        ax = ((ax + 128) & 255) - 128;
                        ay = ((ay + 128) & 255) - 128;
                    }
                    if (a->_0_tile && a->_1_animTile && a->_4_z == D_5895_map_level && ax == dx && ay == dy) {
                        int sprite = a->_1_animTile;
                        bool reflection = false;
                        if (tile == TILE_MAP_87) continue;
                        if ((a->_0_tile & 0xfc) != TILE_ACTOR_E8 &&
                            a->_0_tile != TILE_ACTOR_SLEEP && a->_0_tile != TILE_ACTOR_DEAD &&
                            sprite != TILE_ACTOR_1D && sprite != TILE_ACTOR_SLEEP &&
                            !(a->_0_tile == TILE_ACTOR_BARD && tile == TILE_MAP_CHAIR_92)) {
                            if (a->_0_tile == TILE_ACTOR_BARD) sprite -= 8;
                            sprite = ULTIMA_ResolveActorTile(sprite, tile,
                                WIDE_MapTile(dx, dy - 1), WIDE_MapTile(dx, dy + 1), &reflection);
                        }
                        if (sprite == -1) continue;
                        idx = sprite == -2 ? D_b11e[TILE_MAP_38] : 256 + sprite;
                        if (reflection && row > 0 && WIDE_Visible(dx, dy - 1)) {
                            for (int y = 0; y < 16; y++)
                                for (int x = 0; x < 16; x++)
                                    pixels[(l.mapY + (row - 1) * 16 + y) * l.width + l.mapX + col * 16 + x] =
                                        GRAP_BUF_TilePixel(D_b11e[TILE_MAP_MIRROR_9E], x, y);
                        }
                    }
                }
                for (int y = 0; y < 16; y++)
                    for (int x = 0; x < 16; x++)
                        pixels[(l.mapY + row * 16 + y) * l.width + l.mapX + col * 16 + x] =
                            SPRITES_Composite(idx,x,y,GRAP_BUF_TilePixel(D_b11e[tile],x,y),GRAP_BUF_TilePixel(idx,x,y));
            }
        }
    }
    /* Keep the engine's central tiles, effects, targeting and modal overlays. */
    Copy(pixels, l.width, l.mapX + (cx - 5) * 16, l.mapY + (cy - 5) * 16,
         8, 8, 176, 176);
    return true;
}
