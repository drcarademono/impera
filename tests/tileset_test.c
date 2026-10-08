#undef NDEBUG
#include "common/engine_settings.h"
#include "graphics/grap.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "graphics/tileset.h"
#include "graphics/widescreen.h"
#include "macros.h"
#include "vars.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Uint32 ega[16];
static SDL_Surface *sheet(const char *name)
{
    char path[4096];
    SDL_snprintf(path, sizeof(path), "%s/%s", SDL_getenv("IMPERA_TILESETS"), name);
    SDL_Surface *image = SDL_LoadPNG(path);
    assert(image);
    return image;
}
static Uint32 rgb(SDL_Surface *image, int x, int y, bool npc)
{
    Uint8 r, g, b, a;
    assert(SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, &a));
    if (npc && r == 128 && g == 128 && b == 128)
        return 0;
    return 0xff000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
}
static void verifySheet(SDL_Surface *image, int choice, int first)
{
    int size = choice == TILESET_SHARP ? 32 : 16;
    int columns = choice == TILESET_APPLE ? 28 : choice == TILESET_SHARP ? 16 : 32;
    int checked = 0, missing = 0, detail = 0;
    for (int tile = first; tile < first + (choice == TILESET_SHARP ? 256 : 512); tile++)
    {
        int local = tile - first, col = local % (choice == TILESET_SHARP ? 16 : 32),
            row = local / (choice == TILESET_SHARP ? 16 : 32);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
            {
                int color = TILESET_Pixel(tile, x, y);
                if (col >= columns)
                {
                    assert(color == -1);
                    missing++;
                    continue;
                }
                if (color < 0)
                {
                    assert(choice != TILESET_SHARP); /* No DOS effect substitutions. */
                    assert(GRAP_BUF_TilePixel(tile, x, y) == 5);
                    continue;
                }
                for (int sy = 0; sy < 2; sy++)
                    for (int sx = 0; sx < 2; sx++)
                    {
                        Uint32 expected = rgb(image, col * size + (x * 2 + sx) * size / 32,
                                              row * size + (y * 2 + sy) * size / 32, first == 256);
                        assert(TILESET_LastColor(color, sx, sy, ega) == (expected ? expected : ega[0]));
                        if (expected != TILESET_LastColor(color, 0, 0, ega))
                            detail++;
                        checked++;
                    }
            }
    }
    assert(checked > 100000);
    if (choice == TILESET_APPLE)
        assert(missing == 64 * 256);
    if (choice == TILESET_SHARP)
        assert(detail > 0);
}
#ifdef TEST_TILESET_PRESENT
static SDL_Surface *expectedActor;
static int checkedFrames;
bool __real_SDL_RenderPresent(SDL_Renderer *renderer);
bool __wrap_SDL_RenderPresent(SDL_Renderer *renderer)
{
    if (expectedActor)
    {
        SDL_Surface *frame = SDL_RenderReadPixels(renderer, NULL);
        assert(frame);
        int mode = GRAP_SDL_VideoMode(), mx = 88, my = 88;
        float sx = frame->w / 320.0f, sy = frame->h / 200.0f, left = 0, top = 0;
        if (mode == GRAP_VIDEO_FULLSCREEN)
        {
            WideLayout layout = WIDE_Layout(frame->w, frame->h);
            mx = layout.mapX + layout.columns / 2 * 16;
            my = layout.mapY + layout.rows / 2 * 16;
            sx = layout.scale;
            sy = layout.scaleY;
            left = (frame->w - layout.width * sx) / 2;
            top = (frame->h - layout.height * sy) / 2;
        }
        else if (mode == GRAP_VIDEO_FULLSCREEN_43)
        {
            sx = SDL_min(frame->w / 320.0f, frame->h / 240.0f);
            sy = sx * 1.2f;
            left = (frame->w - 320 * sx) / 2;
            top = (frame->h - 200 * sy) / 2 + 4 * sy;
        }
        int checked = 0;
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++)
            {
                Uint32 expected = rgb(expectedActor, 18 % 16 * 32 + x, 18 / 16 * 32 + y, true);
                if (!expected)
                    continue; /* transparent gray background */
                /* Fractional scaling changes the edge phase of a separately
                 * rasterized sprite. Compare color interiors; the native
                 * subpixels are checked against the PNG above. */
                if (x == 0 || y == 0 || x == 31 || y == 31 ||
                    rgb(expectedActor, 64 + x - 1, 32 + y, true) != expected ||
                    rgb(expectedActor, 64 + x + 1, 32 + y, true) != expected ||
                    rgb(expectedActor, 64 + x, 32 + y - 1, true) != expected ||
                    rgb(expectedActor, 64 + x, 32 + y + 1, true) != expected)
                    continue;
                Uint8 r, g, b, a;
                int px = (int)(left + (mx + (x + 0.5f) / 2) * sx),
                    py = (int)(top + (my + (y + 0.5f) / 2) * sy);
                assert(SDL_ReadSurfacePixel(frame, px, py, &r, &g, &b, &a));
                Uint32 actual = 0xff000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
                if (actual != expected)
                    fprintf(stderr, "frame %d mode %d source=%d,%d screen=%d,%d expected=%08x actual=%08x\n",
                            checkedFrames, mode, x, y, px, py, expected, actual);
                if (actual != expected)
                    SDL_SavePNG(frame, "failed-sharp-frame.png");
                assert(actual == expected);
                checked++;
            }
        assert(checked > 30);
        checkedFrames++;
        if (checkedFrames == 9)
        {
            char path[64];
            SDL_snprintf(path, sizeof(path), "sharp-mode-%d.png", mode);
            assert(SDL_SavePNG(frame, path));
        }
        SDL_DestroySurface(frame);
    }
    return __real_SDL_RenderPresent(renderer);
}
static void rendering(void)
{
    D_5893_map_id = 13;
    D_5895_map_level = 0;
    D_5896_map_x = D_5897_map_y = 16;
    D_58a4 = 1;
    D_58a5 = 255;
    memset(D_6608_map.town, 7, sizeof(D_6608_map.town));
    memset(D_ab02, 7, sizeof(D_ab02));
    memset(D_ac64, 0x16, sizeof(D_ac64));
    memset(D_5c5a, 0, sizeof(D_5c5a));
    for (int t = 0; t < 256; t++)
        D_b11e[t] = (byte)t;
    GetMapViewport(5, 5) = 0;
    GetActorMap(5, 5) = 18;
    GRAP_BUF_SetTransparentSprites(true);
    for (int mode = 0; mode < 3; mode++)
        for (int dither = 0; dither < 2; dither++)
        {
            GRAP_SDL_SetVideoMode(mode);
            WIDE_SetDitheredDarkness(dither != 0);
            GRAP_SDL_SetSmoothMovement(true);
            checkedFrames = 0;
            for (int move = 0; move < 2; move++)
            {
                if (move)
                {
                    D_5896_map_x++;
                    D_5897_map_y++;
                }
                memset(g_linearEgaBuffer0, 0, 320 * 200);
                TILESET_Copy(g_linearEgaBuffer0, NULL, 320 * 200);
                for (int row = 0; row < 11; row++)
                    for (int col = 0; col < 11; col++)
                        GRAP_BUF_PutTile(col, row, 7, 8, 8);
                GRAP_BUF_PutMapSprite(5, 5, 274);
                expectedActor = sheet("Ultima_5_Tiles_SharpX68000_NPC.png");
                GRAP_SDL_MapDrawn();
                GRAP_BUF_MarkDirty();
                GRAP_BUF_Present();
                SDL_DestroySurface(expectedActor);
                expectedActor = NULL;
            }
            if (checkedFrames != 10)
                fprintf(stderr, "mode=%d dither=%d frames=%d\n", mode, dither, checkedFrames);
            assert(checkedFrames == 10); /* two completed frames + eight smooth frames */
        }
    GRAP_SDL_SetSmoothMovement(false);
}
#endif
static Uint32 chooseTileset(void *data, SDL_TimerID id, Uint32 interval)
{
    (void)data;
    (void)id;
    (void)interval;
    SDL_Event event = {0};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_DOWN;
    for (int i = 0; i < ENGINE_TILESET; i++)
        assert(SDL_PushEvent(&event));
    event.key.key = SDLK_RETURN;
    assert(SDL_PushEvent(&event));
    event.key.key = SDLK_DOWN;
    assert(SDL_PushEvent(&event)); /* DOS -> Amiga */
    event.key.key = SDLK_RETURN;
    assert(SDL_PushEvent(&event));
    event.key.key = SDLK_ESCAPE;
    assert(SDL_PushEvent(&event));
    return 0;
}
int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO));
    GRAP_Initialize();
    byte *bank = malloc(512 * 128);
    assert(bank);
    memset(bank, 0x55, 512 * 128);
    GRAP_BUF_LoadTileset(bank);
    for (int i = 0; i < 16; i++)
        ega[i] = 0xff000000u | i * 0x111111u;
    const char *files[] = {NULL, "Ultima_5_Tiles_Amiga.png", "Ultima_5_Tiles_AppleII.png",
                           "Ultima_5_Tiles_Grayscale.png", "Ultima_5_Tiles_SharpX68000_World.png"};
    for (int choice = 1; choice < TILESET_COUNT; choice++)
    {
        assert(TILESET_Select(choice));
        SDL_Surface *image = sheet(files[choice]);
        verifySheet(image, choice, 0);
        SDL_DestroySurface(image);
        if (choice == TILESET_SHARP)
        {
            image = sheet("Ultima_5_Tiles_SharpX68000_NPC.png");
            verifySheet(image, choice, 256);
            SDL_DestroySurface(image);
        }
    }
    /* Native gray cutout subpixels reveal the terrain, while black sprite
     * details remain opaque. Exercise actual composited framebuffer reads. */
    SDL_Surface *npc = sheet("Ultima_5_Tiles_SharpX68000_NPC.png");
    bool found = false;
    byte *dest = g_linearEgaBuffer0;
    for (int tile = 256; tile < 512 && !found; tile++)
        for (int y = 0; y < 16 && !found; y++)
            for (int x = 0; x < 16 && !found; x++)
            {
                int color = TILESET_Pixel(tile, x, y);
                if (color < 16)
                    continue;
                bool hole = false, ink = false;
                for (int sy = 0; sy < 2; sy++)
                    for (int sx = 0; sx < 2; sx++)
                    {
                        Uint32 value = rgb(npc, (tile - 256) % 16 * 32 + x * 2 + sx,
                                           (tile - 256) / 16 * 32 + y * 2 + sy, true);
                        hole |= value == 0;
                        ink |= value != 0;
                    }
                if (!hole || !ink)
                    continue;
                int ground = GRAP_BUF_TilePixel(17, 4, 4);
                TILESET_Record(dest, ground);
                *dest = (byte)ground;
                Uint32 background[4];
                for (int sy = 0; sy < 2; sy++)
                    for (int sx = 0; sx < 2; sx++)
                        background[sy * 2 + sx] = TILESET_BufferColor(dest, sx, sy, ega);
                color = GRAP_BUF_TilePixel(tile, x, y);
                TILESET_RecordSprite(dest, color, true);
                *dest = (byte)color;
                for (int sy = 0; sy < 2; sy++)
                    for (int sx = 0; sx < 2; sx++)
                    {
                        Uint32 expected = rgb(npc, (tile - 256) % 16 * 32 + x * 2 + sx,
                                              (tile - 256) / 16 * 32 + y * 2 + sy, true);
                        assert(TILESET_BufferColor(dest, sx, sy, ega) ==
                               (!expected ? background[sy * 2 + sx] : expected));
                    }
                found = true;
            }
    assert(found);
    SDL_DestroySurface(npc);
    /* Pack changes repaint semantic tile pixels but preserve ordinary UI. */
    assert(TILESET_Select(TILESET_DOS));
    GRAP_BUF_PutTile(1, 1, 17, 0, 0);
    g_linearEgaBuffer0[0] = 15;
    assert(TILESET_Select(TILESET_AMIGA));
    assert(g_linearEgaBuffer0[0] == 15);
    byte color = g_linearEgaBuffer0[16 * 320 + 16];
    assert(color == GRAP_BUF_TilePixel(17, 0, 0));
    assert(TILESET_Select(TILESET_DOS));
    assert(g_linearEgaBuffer0[16 * 320 + 16] == 5);
    byte font[1024] = {0};
    D_539c[0] = font;
    assert(SDL_AddTimer(30, chooseTileset, NULL));
    ENGINE_ShowOptions(false);
    assert(TILESET_Selected() == TILESET_AMIGA);
    assert(g_linearEgaBuffer0[16 * 320 + 16] == GRAP_BUF_TilePixel(17, 0, 0));
    assert(ENGINE_Save());
    assert(TILESET_Select(TILESET_DOS));
    ENGINE_Load();
    assert(TILESET_Selected() == TILESET_AMIGA);
    remove("ENGINE.CFG");
    /* Bad/missing files leave the working pack untouched. */
    char *original = SDL_strdup(SDL_getenv("IMPERA_TILESETS"));
    assert(original);
    assert(SDL_CreateDirectory("bad-pack"));
    assert(SDL_setenv_unsafe("IMPERA_TILESETS", "bad-pack", 1) == 0);
    SDL_Surface *invalid = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_ARGB8888);
    assert(invalid);
    assert(SDL_SavePNG(invalid, "bad-pack/Ultima_5_Tiles_AppleII.png"));
    SDL_DestroySurface(invalid);
    assert(!TILESET_Select(TILESET_APPLE));
    assert(TILESET_Selected() == TILESET_AMIGA);
    assert(!TILESET_Select(TILESET_SHARP));
    assert(TILESET_Selected() == TILESET_AMIGA);
    assert(!TILESET_Select(-1));
    assert(!TILESET_Select(TILESET_COUNT));
    assert(SDL_setenv_unsafe("IMPERA_TILESETS", original, 1) == 0);
    SDL_free(original);
    remove("bad-pack/Ultima_5_Tiles_AppleII.png");
    assert(SDL_RemovePath("bad-pack"));
    /* Amiga water still scrolls. Sharp's supplied bitmaps stay static even
     * while the original DOS animation code mutates its private tile bank. */
    const int animationChoices[] = {TILESET_AMIGA, TILESET_SHARP};
    for (int choice = 0; choice < 2; choice++)
    {
        assert(TILESET_Select(animationChoices[choice]));
        Uint32 before[32];
        for (int y = 0; y < 16; y++)
            for (int sy = 0; sy < 2; sy++)
            {
                int c = GRAP_BUF_TilePixel(3, 8, y);
                before[y * 2 + sy] = TILESET_LastColor(c, 0, sy, ega);
            }
        GRAP_BUF_AnimateTileset();
        for (int y = 0; y < 16; y++)
            for (int sy = 0; sy < 2; sy++)
            {
                int c = GRAP_BUF_TilePixel(3, 8, y);
                int source = animationChoices[choice] == TILESET_SHARP ? y * 2 + sy : (y * 2 + sy + 30) % 32;
                assert(TILESET_LastColor(c, 0, sy, ega) == before[source]);
            }
    }
    SDL_Surface *world = sheet("Ultima_5_Tiles_SharpX68000_World.png");
    verifySheet(world, TILESET_SHARP, 0);
    SDL_DestroySurface(world);
    npc = sheet("Ultima_5_Tiles_SharpX68000_NPC.png");
    verifySheet(npc, TILESET_SHARP, 256);
    SDL_DestroySurface(npc);
    GRAP_BUF_PutTile(4, 4, 0xdc, 0, 0);
    byte gate[16 * 16];
    for (int y = 0; y < 16; y++)
        memcpy(gate + y * 16, g_linearEgaBuffer0 + (64 + y) * 320 + 64, 16);
    for (int stage = 1; stage <= 15; stage++)
    {
        GRAP_BUF_PutAnimatedMoongateTile(4, 4, stage, 7, 0, 0);
        for (int y = 0; y < 16; y++)
            assert(!memcmp(gate + y * 16, g_linearEgaBuffer0 + (64 + y) * 320 + 64, 16));
    }
#ifdef TEST_TILESET_PRESENT
    rendering();
#endif
    GRAP_Cleanup();
    SDL_Quit();
    puts("Alternate tileset mappings, palettes, native detail, transparency, persistence and fallbacks "
         "passed.");
    return 0;
}
