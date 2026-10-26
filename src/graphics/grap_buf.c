#include "common/common.h"
#include "key/key.h"
#include "mod/types.h"
#include "time/time.h"

#include "vars.h"
#include "tiles.h"

#include "grap_buf.h"
#include "animate.h"
#include "reveal.h"
#include "widescreen.h"
#include "tileset.h"
#include "sprite_order.h"
#include "text/text_window.h"
#if defined(TARGET_SDL)
#include "grap_sdl.h"
#include "event/event.h"
#endif

#if defined(ENABLE_GRAP_OVERLAY_DEBUG)
#include "common/dbg_font_data.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_enableDebugOverlay = 0;

// 64k
u8* g_linearEgaBuffer0;

// 64k
u8* g_linearEgaBuffer1;

#if defined(ENABLE_GRAP_OVERLAY)
// 256k
u8* g_linearOverlayBuffer;
#endif

static byte s_grapPenColor = 0;

extern VideoDriverParams D_52ba_vdp;

static u8 s_bitMask[8] = {0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1};

static u8 s_colorTable[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

static byte* s_tileset;
static bool s_transparentSprites;

bool GRAP_BUF_TransparentSprites(void) { return s_transparentSprites; }

void GRAP_BUF_SetTransparentSprites(bool enabled) { s_transparentSprites = enabled; }

static pfGrapFlushFrame* s_pfFlushFrame;

static u32 s_lastPresentTick = 0;
static bool s_dirty = false;

#define TEXT_HISTORY_LINES 512
static byte s_textHistory[TEXT_HISTORY_LINES][8 * 128];
static int s_historyNext, s_historyCount, s_historyOffset;
static int s_historyLeft, s_historyTop, s_historyWidth, s_historyHeight;
void GRAP_BUF_ResetTextHistory(void)
{
    s_historyNext = s_historyCount = s_historyOffset = 0;
    s_historyWidth = 0;
    s_dirty = true;
}
static bool TextHistoryWindow(void)
{
    const TextWindow *w = &D_535e_textWindows[2];
    int left = w->left * 8, top = w->top * 8;
    int width = (w->right - w->left + 1) * 8;
    int height = (w->bottom - w->top + 1) * 8;
    // Only the gameplay message panel, never full-screen text or artwork.
    if (left < 192 || top < 80 || width < 8 || width > 128 ||
        height < 8 || left + width > 320 || top + height > 200)
        return false;
    if (left != s_historyLeft || top != s_historyTop || width != s_historyWidth ||
        height != s_historyHeight) {
        GRAP_BUF_ResetTextHistory();
        s_historyLeft = left; s_historyTop = top;
        s_historyWidth = width; s_historyHeight = height;
    }
    return true;
}
static bool RecordTextHistory(void)
{
#if defined(TARGET_SDL)
    return D_58a4 && !GRAP_SDL_PixelUI() && !EVT_ImmediateExitEnabled() &&
           D_52ba_vdp._52d8_page == 0 && TextHistoryWindow();
#else
    return false;
#endif
}
void GRAP_BUF_EndTextScroll(void)
{
    if (s_historyOffset) { s_historyOffset = 0; s_dirty = true; }
}
void GRAP_BUF_ScrollTextHistory(int lines)
{
    if (!g_linearEgaBuffer0 || !TextHistoryWindow()) return;
    long long offset = (long long)s_historyOffset + lines;
    if (offset < 0) offset = 0;
    if (offset > s_historyCount) offset = s_historyCount;
    if (offset != s_historyOffset) { s_historyOffset = (int)offset; s_dirty = true; }
}
static void ArchiveTextLine(int y)
{
    for (int row = 0; row < 8; ++row)
        memcpy(s_textHistory[s_historyNext] + row * s_historyWidth,
               g_linearEgaBuffer0 + (y + row) * loresWidth + s_historyLeft, s_historyWidth);
    s_historyNext = (s_historyNext + 1) % TEXT_HISTORY_LINES;
    if (s_historyCount < TEXT_HISTORY_LINES) ++s_historyCount;
}
bool GRAP_BUF_TextHistoryPixel(int x, int y, byte *color)
{
    if (!s_historyOffset) return false;
    // A geometry change can reset the offset while validating the window.
    if (!TextHistoryWindow() || !s_historyOffset ||
        x < s_historyLeft || x >= s_historyLeft + s_historyWidth ||
        y < s_historyTop || y >= s_historyTop + s_historyHeight) return false;
    int row = (y - s_historyTop) / 8;
    int line = s_historyCount - s_historyOffset + row;
    if (line < s_historyCount) {
        int slot = (s_historyNext - s_historyCount + line + TEXT_HISTORY_LINES) % TEXT_HISTORY_LINES;
        *color = s_textHistory[slot][((y - s_historyTop) % 8) * s_historyWidth + x - s_historyLeft];
    } else {
        *color = g_linearEgaBuffer0[(s_historyTop + (line - s_historyCount) * 8 +
                                   (y - s_historyTop) % 8) * loresWidth + x];
    }
    return true;
}

// T1K:19d9
static u16 s_tileRevealSeed = 0;

void GRAP_BUF_Initialize(pfGrapFlushFrame* pfFlushFrame)
{
    GRAP_BUF_ResetTextHistory();
    g_linearEgaBuffer0 = malloc(loresWidth * loresHeight);
    memset(g_linearEgaBuffer0, 0, loresWidth * loresHeight);

    g_linearEgaBuffer1 = malloc(loresWidth * loresHeight);
    memset(g_linearEgaBuffer1, 0, loresWidth * loresHeight);
    TILESET_Register(g_linearEgaBuffer0,loresWidth*loresHeight);
    TILESET_Register(g_linearEgaBuffer1,loresWidth*loresHeight);

#if defined(ENABLE_GRAP_OVERLAY)
    g_linearOverlayBuffer = malloc(hiresWidth * hiresHeight);
    memset(g_linearOverlayBuffer, 0, hiresWidth * hiresHeight);
#endif

    s_pfFlushFrame = pfFlushFrame;
}

void GRAP_BUF_Cleanup(void)
{
    if (s_tileset != NULL)
    {
        free(s_tileset);
        s_tileset = NULL;
    }

    TILESET_Unregister(g_linearEgaBuffer0);TILESET_Unregister(g_linearEgaBuffer1);
    free(g_linearEgaBuffer0);
    free(g_linearEgaBuffer1);

    g_linearEgaBuffer0 = NULL;
    g_linearEgaBuffer1 = NULL;

#if defined(ENABLE_GRAP_OVERLAY)
    free(g_linearOverlayBuffer);
    g_linearOverlayBuffer = NULL;
#endif
}

void GRAP_BUF_SetPenColor(byte color)
{
    s_grapPenColor = color;
}

void GRAP_BUF_SetPage(int page)
{
    D_52ba_vdp._52d8_page = page;
}

static inline byte* GetPage(int page)
{
    if (page == 0)
        return g_linearEgaBuffer0;
    else if (page == 1)
        return g_linearEgaBuffer1;
    return 0;
}

#if defined(ENABLE_GRAP_OVERLAY)
static inline void GrPutOverlayPixel(int x, int y, int egaColor) { g_linearOverlayBuffer[y * hiresWidth + x] = egaColor; }
#endif

static inline void GrPutPixel(int page, int x, int y, int egaColor)
{
    byte* target = GetPage(page);
    TILESET_SetSample(0);TILESET_Record(target+y*loresWidth+x,egaColor);
    target[y * loresWidth + x] = egaColor;
}

static inline void GrPutByte(int page, int x, int y, byte egaByte)
{
    byte* target = GetPage(page);
    TILESET_SetSample(0);TILESET_Record(target+y*loresWidth+x,egaByte>>4);TILESET_Record(target+y*loresWidth+x+1,egaByte&15);
    target[y * loresWidth + x] = egaByte >> 4;
    target[y * loresWidth + x + 1] = egaByte & 0xf;
}

// safe but slow
static inline void GrPutClippedPixel(int page, int x, int y, int egaColor)
{
    if (x >= 0 && x < loresWidth && y >= 0 && y < loresHeight)
    {
        GrPutPixel(page, x, y, egaColor);
    }
}

#if defined(ENABLE_GRAP_OVERLAY)
static inline void GrPutOverlayMonoByte(int x, int y, byte b, int egaColor)
{
    g_linearOverlayBuffer[y * hiresWidth + x + 0] = ((b >> 7) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 1] = ((b >> 6) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 2] = ((b >> 5) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 3] = ((b >> 4) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 4] = ((b >> 3) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 5] = ((b >> 2) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 6] = ((b >> 1) & 1) ? egaColor : 0;
    g_linearOverlayBuffer[y * hiresWidth + x + 7] = ((b >> 0) & 1) ? egaColor : 0;
}
#endif

#if defined(ENABLE_GRAP_OVERLAY_DEBUG)
static int _debugPenX;
static int _debugPenY;

static void PrintDebugOverlayChar(int penX, int penY, uint ch)
{
    static u8 mask[8] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80};

    char* p = font8x8_basic[ch];
    for (int y = 0; y < 8; y++)
    {
        byte b = p[y];
        for (int x = 0; x < 8; x++)
        {
            byte col = b & mask[x] ? 15 : 1;

            GrPutOverlayPixel(penX * 8 + x, penY * 8 + y, col);
        }
    }
}

static void PrintDebugOverlayString(char* str)
{
    if (_debugPenY >= 50)
        return;

    int len = strlen(str);
    for (int i = 0; i < len; i++)
    {
        PrintDebugOverlayChar(_debugPenX, _debugPenY, str[i]);
        _debugPenX++;
        if (_debugPenX >= 80)
        {
            _debugPenX = 0;
            _debugPenY++;
        }
    }

    _debugPenX = 0;
    _debugPenY++;
}

static void PrintDebugOverlayStringFmt(char* str, ...)
{
    char debugBuffer[256];

    va_list args;
    va_start(args, str);

    if (vsprintf(debugBuffer, str, args) < 0)
    {
        debugBuffer[0] = 0;
    }

    va_end(args);

    PrintDebugOverlayString(debugBuffer);
}

static void DisplayDebugMessages(void)
{
#if _DEBUG
    if (!g_enableDebugOverlay)
        return;

    memset(g_linearOverlayBuffer, 0, hiresWidth * hiresHeight);

    _debugPenX = 0;
    _debugPenY = 0;

    PrintDebugOverlayStringFmt("P: X%d Y%d", D_5c5a[0]._2_x, D_5c5a[0]._3_y);
    PrintDebugOverlayStringFmt("Map: %d", D_5893_map_id);
    PrintDebugOverlayStringFmt("Time: %02d-%02d %02d:%02d", D_587d, D_587e, D_587f, D_5881);
#endif
    return;

    if (D_5893_map_id > 0)
    {
        PrintDebugOverlayString("NPC:");

        for (int i = 0; i < 32; i++)
        {
            if (i >= 8)
            {
                PrintDebugOverlayString("...");
                break;
            }

            PrintDebugOverlayStringFmt("%2x: %d X%d Y%d %d %d %d %d %d", i, D_5f5e[i]._0, D_5f5e[i]._2_x, D_5f5e[i]._4_y,
                                       D_5f5e[i]._6_z, D_5f5e[i]._8, D_5f5e[i]._a, D_5f5e[i]._c, D_5f5e[i]._e);
        }
    }
    else
    {
        PrintDebugOverlayString("Actor:");

        for (int i = 0; i < 32; i++)
        {
            if (i >= 8)
            {
                PrintDebugOverlayString("...");
                break;
            }
            PrintDebugOverlayStringFmt("%2x: T%02x A%02x X%d Y%d Z%d %d %d %d", i, D_5c5a[i]._0_tile, D_5c5a[i]._1_animTile,
                                       D_5c5a[i]._2_x, D_5c5a[i]._3_y, D_5c5a[i]._4_z, D_5c5a[i]._5, D_5c5a[i]._6,
                                       D_5c5a[i]._7);
        }
    }
}
#endif

void GRAP_BUF_Present(void)
{
#if defined(ENABLE_GRAP_OVERLAY_DEBUG)
    DisplayDebugMessages();
#endif

    (*s_pfFlushFrame)();

    s_dirty = false;
}

extern u32 TIME_CurrentFrame(void);

void GRAP_BUF_FlushPendingPresent(void)
{
    if (!s_dirty)
        return;

    u32 tick = TIME_CurrentFrame();
    if (s_lastPresentTick != tick)
    {
        s_lastPresentTick = tick;

        GRAP_BUF_Present();
    }
}

void GRAP_BUF_MarkDirty(void)
{
    s_dirty = true;
}

static int s_textPixelOffset;
void GRAP_BUF_SetTextPixelOffset(int y)
{
    s_textPixelOffset = y;
}

static void textPixel(void *host, int x, int y, byte color) {
    (void)host;
    GrPutPixel(D_52ba_vdp._52d8_page, x, y, color);
}
void GRAP_BUF_PrintChar(byte* ptr, int offset, byte fgColor, byte bgColor, int penX, int penY)
{
    // Input cursors are redrawn continuously with advance disabled; they
    // must not snap scrollback to the bottom while the player is reading.
    if (D_538e && RecordTextHistory() && penX * 8 >= s_historyLeft && penX * 8 < s_historyLeft + s_historyWidth &&
        penY * 8 >= s_historyTop && penY * 8 < s_historyTop + s_historyHeight)
        GRAP_BUF_EndTextScroll();
    IMPERA_TextGlyph(ptr, offset, s_colorTable[fgColor], s_colorTable[bgColor], penX * 8,
                     penY * 8 + s_textPixelOffset, NULL, textPixel);
    s_dirty = true;
}
static void textCopy(void *host, byte *dst, const byte *src, size_t size) {
    (void)host;
    TILESET_Copy(dst, src, (int)size);
}
void GRAP_BUF_ScrollWindow(int left, int top, int right, int bottom, int amount)
{
    if (amount == -8 && RecordTextHistory() && left == s_historyLeft && top == s_historyTop &&
        right == left + s_historyWidth - 1 && bottom == top + s_historyHeight - 1) {
        ArchiveTextLine(top);
        GRAP_BUF_EndTextScroll();
    }
    IMPERA_TextScroll(g_linearEgaBuffer0, loresWidth, left, top, right, bottom, amount, NULL,
                      textCopy);
    s_dirty = true;
}

// 0x3f
void GRAP_BUF_FillWindow(int x1, int y1, int x2, int y2, int xorMode)
{
    if (y1 > y2)
        return;
    if (x1 > x2)
        return;

    byte* target = GetPage(D_52ba_vdp._52d8_page);
    if (!xorMode && target == g_linearEgaBuffer0 && RecordTextHistory()) {
        if (x1 == s_historyLeft && y1 == s_historyTop &&
            x2 == x1 + s_historyWidth - 1 && y2 == y1 + s_historyHeight - 1) {
            int last = -1;
            for (int y = y1; y <= y2; ++y)
                for (int x = x1; x <= x2; ++x)
                    if (target[y * loresWidth + x] != s_grapPenColor) last = (y - y1) / 8;
            for (int line = 0; line <= last; ++line) ArchiveTextLine(y1 + line * 8);
            GRAP_BUF_EndTextScroll();
        } else if (x1 <= s_historyLeft && y1 <= s_historyTop &&
                   x2 >= s_historyLeft + s_historyWidth - 1 &&
                   y2 >= s_historyTop + s_historyHeight - 1) {
            GRAP_BUF_ResetTextHistory();
        }
    }

    for (int y = y1; y <= y2; y++)
    {
        if (!xorMode)
        {
            TILESET_Copy(&target[y * loresWidth + x1], NULL, x2-x1+1);
            memset(&target[y * loresWidth + x1], s_grapPenColor, x2 - x1 + 1);
        }
        else
        {
            for (int x = x1; x <= x2; x++)
            {
                target[y * loresWidth + x] ^= s_grapPenColor;
                target[y * loresWidth + x] &= 0xf;
            }
        }
    }

    if (D_52ba_vdp._52d8_page == 0)
    {
        s_dirty = true;
    }
}

void GRAP_BUF_LoadTileset(byte* tileset)
{
    s_tileset = tileset;
    /* Settings load before mod mounting. Refresh artwork and masks once the
     * game's tile bank is ready, using the new mount revision. */
    TILESET_Select(TILESET_Selected());
}

bool GRAP_BUF_HasTileset(void) { return s_tileset != NULL; }

byte GRAP_BUF_TilePixel(int tile, int x, int y)
{
    if (tile >= 0 && x >= 0 && y >= 0 && TYPES_ArtSlot(tile))
        return (byte)TILESET_Pixel(tile, x, y);
    if (!s_tileset || tile < 0 || tile >= 512 || x < 0 || y < 0 || x >= 16 || y >= 16) {TILESET_SetSample(0);return 0;}
    int alternate=TILESET_Pixel(tile,x,y);
    if(alternate>=0) return (byte)alternate;
    TILESET_SetSample((unsigned)(tile*256+y*16+x+1));
    byte packed = s_tileset[tile * 128 + y * 8 + x / 2];
    return x & 1 ? packed & 15 : packed >> 4;
}

/* -1 is transparent. A one-pixel, eight-connected dilation supplies the
 * black outline, including the one-pixel margin outside the source tile. */
bool GRAP_BUF_SpriteIsTransparent(int tile)
{
    if (TYPES_ArtSlot(tile)) {
        TypeDef definition;
        return s_transparentSprites && TYPES_Get(TYPES_ArtworkType(tile), &definition) &&
               (definition.render & TYPES_SPRITE);
    }
    /* Furniture and seated poses include their scenery; keep the original
     * pixels and do not generate an outline outside their tile. */
    if ((tile >= TILE_MAP_CHAIR_90 && tile <= TILE_MAP_TABLE_96) ||
        (tile >= TILE_MAP_TABLE_9A && tile <= TILE_MAP_TABLE_9C) ||
        tile == TILE_MAP_TABLE_BE ||
        (tile >= 256 + 0x30 && tile <= 256 + 0x3b))
        return false;
    return s_transparentSprites && tile != TILE_MAP_85 &&
        tile != 256 + TILE_ACTOR_SLEEPING_IN_BED &&
        !(tile >= 256 + 0x64 && tile <= 256 + 0x67); /* occupied manacles */
}

/* Inspect all native subpixels: Sharp artwork can mix black and color inside
 * a single logical pixel. Raw tile/editor sampling must retain opaque black. */
static int CustomSpritePixel(int tile, int x, int y) {
    int color = TILESET_Pixel(tile, x, y);
    if (color < 0)
        return 0;
    for (int sy = 0; sy < 2; sy++)
        for (int sx = 0; sx < 2; sx++)
            if (TILESET_LastSpriteColor(color, sx, sy, IMPERA_TextPalette, true) >> 24)
                return color;
    return 0;
}
static int NativeSpritePixel(int tile, int x, int y) {
    int color = GRAP_BUF_TilePixel(tile, x, y);
    return !color && TILESET_ProtectedBlack(tile, x, y) ? 16 : color;
}
int GRAP_BUF_SpritePixel(int tile, int x, int y)
{
    bool authored = TYPES_ArtSlot(tile) != 0;
    unsigned w = 1, h = 1;
    if (authored)
        TYPES_ArtSize(tile, D_5893_map_id == 0, &w, &h);
    if (!GRAP_BUF_SpriteIsTransparent(tile))
        return x < 0 || y < 0 || x >= (int)(16 * w) || y >= (int)(16 * h)
                   ? -1
                   : GRAP_BUF_TilePixel(tile, x, y);
    int color = authored ? CustomSpritePixel(tile, x, y) : NativeSpritePixel(tile, x, y);
    if (color) return color;
    if (!TILESET_SpriteOutline()) return -1;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (authored ? CustomSpritePixel(tile, x + dx, y + dy)
                         : NativeSpritePixel(tile, x + dx, y + dy)) {
                TILESET_SetSample(0);
                return 0;
            }
    return -1;
}

void GRAP_BUF_DrawSprite(byte* pixels, int stride, int x, int y, int tile,
                         int left, int top, int right, int bottom, int dx, int dy)
{
    unsigned w = 1, h = 1;
    if (TYPES_ArtSlot(tile))
        TYPES_ArtSize(tile, D_5893_map_id == 0, &w, &h);
    bool visible[4] = {true, true, true, true};
    if ((w > 1 || h > 1) && D_5893_map_id > 0 && D_5893_map_id < 128)
        for (unsigned cy = 0; cy < h; ++cy)
            for (unsigned cx = 0; cx < w; ++cx)
                visible[cy * w + cx] = WIDE_FootprintVisible(dx + cx, dy + cy, 1, 1);
    for (int sy = -1; sy <= (int)(16 * h); sy++)
        for (int sx = -1; sx <= (int)(16 * w); sx++) {
            int px = x + sx, py = y + sy;
            if (sx >= 0 && sy >= 0 && sx < (int)(16 * w) && sy < (int)(16 * h) &&
                !visible[(sy / 16) * w + sx / 16])
                continue;
            if (px < left || py < top || px >= right || py >= bottom) continue;
            int color = WIDE_SpritePixel(tile, dx, dy, sx, sy);
            if (color >= 0) { TILESET_RecordSprite(pixels+py*stride+px,color,GRAP_BUF_SpriteIsTransparent(tile));pixels[py * stride + px] = (byte)color; }
        }
}

void GRAP_BUF_PutMapSprite(int x, int y, int tile)
{
    byte* pixels = D_52ba_vdp._52d8_page ? g_linearEgaBuffer1 : g_linearEgaBuffer0;
    GRAP_BUF_DrawSprite(pixels, 320, 8 + x * 16, 8 + y * 16, tile, 8, 8, 184, 184,
        x-(D_5893_map_id>=128?D_5896_map_x:5),
        y-(D_5893_map_id>=128?D_5897_map_y:5));
    s_dirty = true;
}

#if defined(TARGET_SDL)
/* Scripted vignettes have a fixed camera and their own terrain. Do not consult
 * the party's map, LOS, or combat state when compositing their actors. */
static GrapSceneSprite previousSceneSprites[32];
static int previousSceneKey, previousSceneCount;
static bool previousSceneValid;
void GRAP_BUF_ResetScene(void) { previousSceneValid = false; }
static byte SceneGround(const byte *ground, int stride, int columns, int rows, int x, int y) {
    byte tile = ground[y * stride + x];
    byte neighbors[] = {
        y + 1 < rows ? ground[(y + 1) * stride + x] : tile, y ? ground[(y - 1) * stride + x] : tile,
        x + 1 < columns ? ground[y * stride + x + 1] : tile, x ? ground[y * stride + x - 1] : tile};
    return WIDE_GroundFromNeighbors(tile, neighbors);
}
static void DrawSceneSprite(byte *pixels, unsigned tile, int originX, int originY, int left,
                            int top, int right, int bottom) {
    unsigned w = 1, h = 1;
    if (TYPES_ArtSlot(tile))
        TYPES_ArtSize(tile, 0, &w, &h);
    for (int y = -1; y <= (int)h * 16; ++y)
        for (int x = -1; x <= (int)w * 16; ++x) {
            int tx = originX + x, ty = originY + y;
            if (tx < left || tx >= right || ty < top || ty >= bottom)
                continue;
            int color = GRAP_BUF_SpritePixel(tile, x, y);
            if (color < 0)
                continue;
            TILESET_RecordSprite(pixels + ty * 320 + tx, color, GRAP_BUF_SpriteIsTransparent(tile));
            pixels[ty * 320 + tx] = (byte)color;
        }
}
void GRAP_BUF_DrawScene(int key, const byte *ground, int stride, int columns, int rows, int left,
                        int right, int originX, int originY, const GrapSceneSprite *sprites,
                        int count) {
    if (!ground || columns < 1 || rows < 1 || count < 0 || count > 32 || stride < columns ||
        (count && !sprites))
        return;
    left = SDL_max(0, left);
    right = SDL_min(columns - 1, right);
    int clipLeft = SDL_max(0, originX + left * 16),
        clipRight = SDL_min(320, originX + (right + 1) * 16);
    int clipTop = SDL_max(0, originY), clipBottom = SDL_min(200, originY + rows * 16);
    bool compatible = previousSceneValid && previousSceneKey == key && previousSceneCount == count;
    bool moving[32] = {false}, animate = false;
    if (compatible && GRAP_SDL_SmoothMovementEnabled())
        for (int i = 0; i < count; ++i) {
            const GrapSceneSprite *old = &previousSceneSprites[i], *next = &sprites[i];
            int dx = next->x - old->x, dy = next->y - old->y;
            unsigned w = 1, h = 1;
            if (TYPES_ArtSlot(next->tile))
                TYPES_ArtSize(next->tile, 0, &w, &h);
            bool visible =
                (originX + next->x + (int)w * 16 > clipLeft && originX + next->x < clipRight &&
                 originY + next->y + (int)h * 16 > clipTop && originY + next->y < clipBottom) ||
                (originX + old->x + (int)w * 16 > clipLeft && originX + old->x < clipRight &&
                 originY + old->y + (int)h * 16 > clipTop && originY + old->y < clipBottom);
            moving[i] = visible && old->tile && next->tile && old->identity == next->identity &&
                        abs(dx) <= 16 && abs(dy) <= 16 && (dx || dy);
            animate |= moving[i];
        }
    int frames = animate ? 8 : 1;
    byte *pixels = GetPage(D_52ba_vdp._52d8_page);
    for (int frame = 1; frame <= frames; ++frame) {
        Uint64 started = SDL_GetTicks();
        for (int y = 0; y < rows; ++y)
            for (int x = left; x <= right; ++x) {
                unsigned tile = ground[y * stride + x];
                if (tile == TILE_MAP_FE)
                    continue; /* Script owns active reveal/gate effects. */
                byte floor = SceneGround(ground, stride, columns, rows, x, y);
                for (int py = 0; py < 16; ++py)
                    for (int px = 0; px < 16; ++px) {
                        int tx = originX + x * 16 + px, ty = originY + y * 16 + py;
                        if (tx < clipLeft || tx >= clipRight || ty < clipTop || ty >= clipBottom)
                            continue;
                        int color = GRAP_BUF_TilePixel(D_b11e[floor], px, py);
                        TILESET_Record(pixels + ty * 320 + tx, color);
                        pixels[ty * 320 + tx] = (byte)color;
                    }
            }
        for (int y = 0; y < rows; ++y)
            for (int x = left; x <= right; ++x) {
                byte tile = ground[y * stride + x];
                if (tile != TILE_MAP_FE && SceneGround(ground, stride, columns, rows, x, y) != tile)
                    DrawSceneSprite(pixels, D_b11e[tile], originX + x * 16, originY + y * 16,
                                    clipLeft, clipTop, clipRight, clipBottom);
            }
        typedef struct {
            SpriteOrder order;
            int index, x, y;
        } OrderedSceneActor;
        OrderedSceneActor order[32];
        int n = 0;
        for (int i = 0; i < count; ++i) {
            if (!sprites[i].tile)
                continue;
            unsigned w = 1, h = 1;
            if (TYPES_ArtSlot(sprites[i].tile))
                TYPES_ArtSize(sprites[i].tile, 0, &w, &h);
            int x = sprites[i].x, y = sprites[i].y;
            if (moving[i]) {
                x = previousSceneSprites[i].x + (x - previousSceneSprites[i].x) * frame / frames;
                y = previousSceneSprites[i].y + (y - previousSceneSprites[i].y) * frame / frames;
            }
            order[n++] = (OrderedSceneActor){
                {SPRITES_Layer(sprites[i].tile), y + (int)(h - 1) * 16, x, -i}, i, x, y};
        }
        qsort(order, n, sizeof(order[0]), SPRITES_Compare);
        for (int i = 0; i < n; ++i) {
            const GrapSceneSprite *actor = &sprites[order[i].index];
            DrawSceneSprite(pixels, actor->tile, originX + order[i].x, originY + order[i].y,
                            clipLeft, clipTop, clipRight, clipBottom);
        }
        s_dirty = true;
        if (frame < frames) {
            GRAP_BUF_Present();
            if (EVT_ImmediateExitEnabled())
                EVT_PollMessages();
            else
                SDL_PumpEvents();
            Uint32 interval = (Uint32)SDL_roundf(16.0f / GRAP_SDL_MovementSpeed());
            Uint64 elapsed = SDL_GetTicks() - started;
            if (elapsed < interval)
                SDL_Delay(interval - (Uint32)elapsed);
        }
    }
    if (count)
        memcpy(previousSceneSprites, sprites, (size_t)count * sizeof(*sprites));
    previousSceneKey = key;
    previousSceneCount = count;
    previousSceneValid = true;
}
#endif

void GRAP_BUF_UnloadTileset(void)
{
    if (s_tileset != NULL)
    {
        free(s_tileset);
        s_tileset = NULL;
    }
}

void GRAP_BUF_AnimateTileset(void)
{
    AnimateTileset(s_tileset);
    TILESET_Animate();
}

void GRAP_BUF_UpdateTimeTileset(int mode, byte hour, byte minute)
{
    AnimateTimeTileset(s_tileset, mode, hour, minute);
}

void GRAP_BUF_PutTile(int tileX, int tileY, int tileIdx, int xOffset, int yOffset)
{
    int dstX=tileX*16+xOffset,dstY=tileY*16+yOffset;
    byte* pixels=GetPage(D_52ba_vdp._52d8_page);
    for(int y=0;y<16;y++) for(int x=0;x<16;x++) {
        int color=GRAP_BUF_TilePixel(tileIdx,x,y);
        TILESET_Record(pixels+(dstY+y)*320+dstX+x,color);
        pixels[(dstY+y)*320+dstX+x]=(byte)color;
    }

    s_dirty = true;
}

void GRAP_BUF_PutAnimatedMoongateTile(int tileX, int tileY, int visibleRows, byte floorType, int xOffset, int yOffset)
{
    /* The DOS routine borrows an unrelated actor tile as a scratch buffer.
     * Alternate packs draw their own world moongate, not that actor. */
    if (TILESET_Selected() != TILESET_DOS) {
        GRAP_BUF_PutTile(tileX & 0xff, tileY & 0xff, TILE_MAP_MOONGATE, xOffset, yOffset);
        return;
    }
    byte backup[0x80];

    AnimateTile_BuildMoongateTile(s_tileset, visibleRows, floorType, backup);
    GRAP_BUF_PutTile(tileX & 0xff, tileY & 0xff, 0x116, xOffset, yOffset); // magic circle tile is used as scratch tile
    AnimateTile_RestoreMoongateTile(s_tileset, backup);
}

static u16 StepLfsrSeed(u16 seed, u16 mask)
{
    int carry = seed & 1;

    seed >>= 1;
    if (carry)
    {
        seed ^= mask;
    }

    return seed;
}

static void PutTileRevealPixel(int tileX, int tileY, int tileIdx, int relX, int relY, int xOffset, int yOffset)
{
    int dstX;
    int dstY;

    dstX=tileX*16+xOffset+relX;dstY=tileY*16+yOffset+relY;
    int color=GRAP_BUF_TilePixel(tileIdx,relX,relY);
    byte* pixel=GetPage(D_52ba_vdp._52d8_page)+dstY*320+dstX;
    TILESET_Record(pixel,color);*pixel=(byte)color;
    s_dirty = true;
}

// T1K:1b27
void GRAP_BUF_PutTileRevealStep(int tileX, int tileY, int tileIdx, int progress, int xOffset, int yOffset)
{
    u16 seed;

    if (progress == 0)
    {
        PutTileRevealPixel(tileX, tileY, tileIdx, 0, 0, xOffset, yOffset);
        return;
    }

    seed = s_tileRevealSeed;
    if (progress == 1 || seed == 0)
    {
        seed = 1;
    }

    PutTileRevealPixel(tileX, tileY, tileIdx, seed >> 4, seed & 0x0f, xOffset, yOffset);

    // 1b7f
    s_tileRevealSeed = StepLfsrSeed(seed, 0xb8);
}

// https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm#All_cases
void PlotLine(int x1, int y1, int x2, int y2)
{
    int dx = abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int error = dx + dy;
    int e2;

    while (1)
    {
        GrPutPixel(D_52ba_vdp._52d8_page, x1, y1, s_grapPenColor);
        e2 = 2 * error;

        if (e2 >= dy)
        {
            if (x1 == x2)
                break;

            error += dy;
            x1 += sx;
        }
        if (e2 <= dx)
        {
            if (y1 == y2)
                break;

            error += dx;
            y1 += sy;
        }
    }
}

//int ULTIMA_08e6_ClipRectCoord(int* x1, int* y1, int* x2, int* y2);

// 0x27
void GRAP_BUF_Line(int x1, int y1, int x2, int y2)
{
    // ULTIMA_08e6_ClipRectCoord(&x1, &y1, &x2, &y2);

    PlotLine(x1, y1, x2, y2);

    if (D_52ba_vdp._52d8_page == 0)
    {
        s_dirty = true;
    }
}

void GRAP_BUF_LineRectangle(int x1, int y1, int x2, int y2, byte color)
{
    byte x = s_grapPenColor;
    s_grapPenColor = color;

    PlotLine(x1, y1, x2, y1);
    PlotLine(x1, y2, x2, y2);
    PlotLine(x1, y1, x1, y2);
    PlotLine(x2, y1, x2, y2);

    PlotLine(x1, y1, x2, y2);
    PlotLine(x2, y1, x1, y2);

    s_grapPenColor = x;

    s_dirty = true;
}

void GRAP_BUF_Pset(int x, int y)
{
    // ULTIMA_08e6_ClipRectCoord(&x1, &y1, &x2, &y2);

    GrPutClippedPixel(D_52ba_vdp._52d8_page, x, y, s_grapPenColor);

    if (D_52ba_vdp._52d8_page == 0)
    {
        s_dirty = true;
    }
}

static inline byte GetImagePixel(byte packed, int relX)
{
    if ((relX & 1) == 0)
        return packed >> 4;

    return packed & 0x0f;
}

static inline bool MaskHasPixel(byte* maskPtr, int relX)
{
    if (maskPtr == NULL)
        return true;

    return (maskPtr[relX / 8] & (0x80 >> (relX & 7))) == 0;
}

void GRAP_BUF_PutImage(ImageView* view, int x, int y, int flags)
{
    int width = view->width;
    int height = view->height;
    int byteWidth = view->byteWidth;
    int page = D_52ba_vdp._52d8_page;

    if (width <= 0 || height <= 0)
        return;

    bool vflip = (flags & 1) != 0;
    bool hflip = (flags & 2) != 0;

    for (int yy = 0; yy < height; yy++)
    {
        int srcY;
        int dstY;

        if (vflip)
        {
            srcY = height - yy - 1;
            dstY = y + yy + 1;
        }
        else
        {
            srcY = yy;
            dstY = y + yy;
        }

        byte* linePtr = &view->pixels[srcY * view->stride];
        byte* maskPtr = view->hasMask ? &view->maskBits[srcY * view->maskStride] : NULL;
        for (int byteX = 0; byteX < byteWidth; byteX++)
        {
            byte packed = linePtr[byteX];
            int leftRelX = byteX * 2;

            if (hflip)
            {
                int dstX = x + width - 2 - leftRelX;

                if (MaskHasPixel(maskPtr, leftRelX))
                {
                    GrPutClippedPixel(page, dstX, dstY, GetImagePixel(packed, leftRelX));
                }

                leftRelX++;
                if (leftRelX < width && MaskHasPixel(maskPtr, leftRelX))
                {
                    GrPutClippedPixel(page, dstX - 1, dstY, GetImagePixel(packed, leftRelX));
                }
            }
            else
            {
                int dstX = x + leftRelX;

                if (MaskHasPixel(maskPtr, leftRelX))
                {
                    GrPutClippedPixel(page, dstX, dstY, GetImagePixel(packed, leftRelX));
                }

                leftRelX++;
                if (leftRelX < width && MaskHasPixel(maskPtr, leftRelX))
                {
                    GrPutClippedPixel(page, dstX + 1, dstY, GetImagePixel(packed, leftRelX));
                }
            }
        }
    }

    if (page == 0)
    {
        s_dirty = true;
    }
}

void GRAP_BUF_PutBitImage(BitImageView* view, int x, int y)
{
    int w = view->width;
    int h = view->height;
    byte* buf = view->bits;
    int stride = view->stride;
    int page = D_52ba_vdp._52d8_page;

    for (int yy = 0; yy < h; yy++)
    {
        byte* linePtr = &buf[yy * stride];
        for (int xx = 0; xx < w; xx++)
        {
            if (linePtr[xx / 8] & s_bitMask[xx % 8])
            {
                GrPutClippedPixel(page, xx + x, yy + y, 15);
            }
        }
    }

    if (page == 0)
    {
        s_dirty = true;
    }
}

void GRAP_BUF_TransferPage(int srcPage, int dstPage, int x1, int y1, int x2, int y2, int dstX, int dstY)
{
    byte* srcPagePtr = GetPage(srcPage);
    byte* dstPagePtr = GetPage(dstPage);

    // TODO: check bounds
    for (int y = 0; y <= y2 - y1; y++)
    {
        TILESET_Copy(&dstPagePtr[(y+dstY)*loresWidth+dstX],&srcPagePtr[(y+y1)*loresWidth+x1],x2-x1+1);
        memcpy(&dstPagePtr[(y + dstY) * loresWidth + dstX], &srcPagePtr[(y + y1) * loresWidth + x1], x2 - x1 + 1);
    }

    if (dstPage == 0)
    {
        s_dirty = true;
    }
}

void GRAP_BUF_TransferPage_Reveal(int srcPage, int dstPage, int x1, int y1, int x2, int y2, int dstX, int dstY)
{
    GRAP_BUF_FlushPendingPresent();

    RevealState state;

    byte* srcPagePtr = GetPage(srcPage);
    byte* dstPagePtr = GetPage(dstPage);

    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;

    RevealInit(&state, w, h, 1);

    int nextIdx;
    while (RevealNextIndex(&state, &nextIdx))
    {
        int nx = nextIdx % w;
        int ny = nextIdx / w;
        dstPagePtr[(ny + dstY) * loresWidth + nx + dstX] = srcPagePtr[(ny + y1) * loresWidth + nx + x1];

        if (state.produced % 512 == 511)
        {
            GRAP_BUF_Present();

            if (KEY_PollKey())
            {
                GRAP_BUF_TransferPage(srcPage, dstPage, x1, y1, x2, y2, dstX, dstY);
                return;
            }

            TIME_SleepMs(1 * 55);
        }
    }

    GRAP_BUF_Present();
}

void GRAP_BUF_PutRGBA(const unsigned char *rgba,int width,int height,int x,int y) {
    byte *buffer=D_52ba_vdp._52d8_page?g_linearEgaBuffer1:g_linearEgaBuffer0;
    for(int row=0;row<height;row++) for(int col=0;col<width;col++) {
        int dx=x+col,dy=y+row;
        if(dx<0||dx>=320||dy<0||dy>=200) continue;
        const unsigned char *source=rgba+((size_t)row*width+col)*4;
        unsigned alpha=source[3];if(!alpha) continue;
        byte *pixel=buffer+dy*320+dx;
        Uint32 background=TILESET_BufferColor(pixel,0,0,IMPERA_TextPalette),color=0xff000000u;
        for(int channel=0;channel<3;channel++) {
            int shift=16-channel*8;
            unsigned back=(background>>shift)&255;
            color|=((source[channel]*alpha+back*(255-alpha)+127)/255)<<shift;
        }
        TILESET_RecordRGBA(pixel,color);
    }
    if(!D_52ba_vdp._52d8_page) s_dirty=true;
}
