#undef NDEBUG
#include "common/engine_settings.h"
#include "common/file_picker.h"
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
    int tileWidth = choice == TILESET_APPLE ? 14 : choice == TILESET_SHARP ? 32 : 16;
    int tileHeight = choice == TILESET_SHARP ? 32 : 16;
    int checked = 0, detail = 0;
    for (int tile = first; tile < first + (choice == TILESET_SHARP ? 256 : 512); tile++)
    {
        int local = tile - first, col = local % (choice == TILESET_SHARP ? 16 : 32),
            row = local / (choice == TILESET_SHARP ? 16 : 32);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
            {
                int color = TILESET_Pixel(tile, x, y);
                assert(color >= 0); /* Every supplied tile uses the selected artwork. */
                for (int sy = 0; sy < 2; sy++)
                    for (int sx = 0; sx < 2; sx++)
                    {
                        Uint32 expected =
                            rgb(image, col * tileWidth + (x * 2 + sx) * tileWidth / 32,
                                row * tileHeight + (y * 2 + sy) * tileHeight / 32, first == 256);
                        assert(TILESET_LastColor(color, sx, sy, ega) == (expected ? expected : ega[0]));
                        if (expected != TILESET_LastColor(color, 0, 0, ega))
                            detail++;
                        checked++;
                    }
            }
    }
    assert(checked > 100000);
    if (choice == TILESET_APPLE)
    {
        /* Last columns of both banks must be available, not treated as omissions.
         */
        assert(TILESET_Pixel(0x1f, 0, 0) >= 0);
        assert(TILESET_Pixel(0x1ff, 0, 0) >= 0);
    }
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
                    fprintf(stderr,
                            "frame %d mode %d source=%d,%d screen=%d,%d expected=%08x "
                            "actual=%08x\n",
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
static Uint32 chooseCustom(void *user, SDL_TimerID id, Uint32 interval)
{
    (void)id;
    (void)interval;
    SDL_Event e = {0};
    e.type = SDL_EVENT_KEY_DOWN;
    if (user)
    {
        e.key.key = SDLK_DOWN;
        assert(SDL_PushEvent(&e));
        e.key.key = SDLK_RETURN;
        assert(SDL_PushEvent(&e));
    }
    else
    {
        e.key.key = SDLK_ESCAPE;
        assert(SDL_PushEvent(&e));
    }
    return 0;
}
static Uint32 chooseCustomOption(void *user, SDL_TimerID id, Uint32 interval)
{
    (void)id;
    int *stage = user;
    SDL_Event e = {0};
    e.type = SDL_EVENT_KEY_DOWN;
    if (*stage == 0)
    {
        e.key.key = SDLK_DOWN;
        for (int i = 0; i < ENGINE_TILESET; i++)
            assert(SDL_PushEvent(&e)); /* Video -> Tileset */
        e.key.key = SDLK_RETURN;
        assert(SDL_PushEvent(&e));
        e.key.key = SDLK_DOWN;
        for (int i = 0; i < TILESET_CUSTOM; i++)
            assert(SDL_PushEvent(&e));
        e.key.key = SDLK_RETURN;
        assert(SDL_PushEvent(&e));
    }
    else if (*stage == 1)
    {
        e.key.key = SDLK_DOWN;
        assert(SDL_PushEvent(&e)); /* First PNG */
        e.key.key = SDLK_RETURN;
        assert(SDL_PushEvent(&e));
    }
    else
    {
        e.key.key = SDLK_ESCAPE;
        assert(SDL_PushEvent(&e));
        return 0;
    }
    (*stage)++;
    return interval;
}
static bool acceptCustom(const char *path, void *user)
{
    (void)user;
    return TILESET_SelectCustom(path);
}
static void customTileset(void)
{
    assert(SDL_CreateDirectory("custom-picker"));
    SDL_Surface *image = SDL_CreateSurface(1000, 500, SDL_PIXELFORMAT_RGBA32);
    assert(image);
    for (int y = 0; y < image->h; y++)
        for (int x = 0; x < image->w; x++)
            assert(SDL_WriteSurfacePixel(image, x, y, x % 256, y % 256, (x + y) % 256, 255));
    assert(SDL_SavePNG(image, "custom-picker/Custom atlas.PNG"));
    assert(TILESET_SelectCustom("custom-picker/Custom atlas.PNG"));
    assert(TILESET_Selected() == TILESET_CUSTOM);
    /* A non-integral source cell size and a full RGB gradient must both work. */
    for (int tile = 0; tile < 512; tile++)
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++)
            {
                int color = TILESET_Pixel(tile, x / 2, y / 2);
                assert(color >= 0);
                Uint32 expected =
                    rgb(image, (tile % 32 * 32 + x) * 1000 / 1024, (tile / 32 * 32 + y) * 500 / 512, false);
                if (expected == 0xff000000u)
                    expected = ega[0];
                assert(TILESET_LastColor(color, x % 2, y % 2, ega) == expected);
                if (x % 16 == 0 && y % 16 == 0)
                {
                    TILESET_Record(g_linearEgaBuffer0, color);
                    g_linearEgaBuffer0[0] = (byte)color;
                    assert(TILESET_BufferColor(g_linearEgaBuffer0, x % 2, y % 2, ega) == expected);
                }
            }
    SDL_DestroySurface(image);
    image = SDL_CreateSurface(2, 1, SDL_PIXELFORMAT_RGBA32);
    assert(image);
    assert(SDL_WriteSurfacePixel(image, 0, 0, 123, 45, 67, 255));
    assert(SDL_WriteSurfacePixel(image, 1, 0, 1, 2, 3, 0));
    assert(SDL_SavePNG(image, "custom-picker/Replacement.png"));
    SDL_DestroySurface(image);
    /* Replacing Custom while already selected must reload its artwork. */
    assert(TILESET_SelectCustom("custom-picker/Replacement.png"));
    int color = TILESET_Pixel(0, 0, 0);
    assert(TILESET_LastColor(color, 0, 0, ega) == 0xff7b2d43u);
    assert(TILESET_Pixel(31, 0, 0) == 0); /* Alpha-zero source is background. */
    image = SDL_CreateSurface(3, 2, SDL_PIXELFORMAT_RGBA32);
    assert(image);
    assert(SDL_SavePNG(image, "invalid-custom.png"));
    SDL_DestroySurface(image);
    assert(!TILESET_SelectCustom("invalid-custom.png"));
    assert(!TILESET_SelectCustom("missing-custom.png"));
    assert(!strcmp(TILESET_CustomPath(), "custom-picker/Replacement.png"));
    assert(TILESET_Selected() == TILESET_CUSTOM);
    color = TILESET_Pixel(0, 0, 0);
    assert(TILESET_LastColor(color, 0, 0, ega) == 0xff7b2d43u);
    assert(!TILESET_SetCustomPath("bad\npath"));
    assert(ENGINE_Save());
    assert(TILESET_Select(TILESET_DOS));
    assert(TILESET_SetCustomPath(""));
    ENGINE_Load();
    assert(TILESET_Selected() == TILESET_CUSTOM);
    assert(!strcmp(TILESET_CustomPath(), "custom-picker/Replacement.png"));
    assert(TILESET_Select(TILESET_DOS));
    assert(SDL_AddTimer(50, chooseCustom, NULL));
    assert(!FILEPICKER_Select("Custom Tileset", "Choose a 2:1 PNG tileset", NULL, ".png", "custom-picker",
                              "Return to Engine Options", "Invalid PNG", acceptCustom, NULL));
    assert(TILESET_Selected() == TILESET_DOS);
    /* Case-insensitive PNG filtering, a filename with spaces, and real acceptance. */
    assert(SDL_AddTimer(50, chooseCustom, (void *)1));
    assert(FILEPICKER_Select("Custom Tileset", "Choose a 2:1 PNG tileset", NULL, ".png", "custom-picker",
                             "Return to Engine Options", "Invalid PNG", acceptCustom, NULL));
    assert(TILESET_Selected() == TILESET_CUSTOM);
    assert(strstr(TILESET_CustomPath(), "Custom atlas.PNG"));
    assert(TILESET_Select(TILESET_DOS));
    int stage = 0;
    assert(SDL_AddTimer(50, chooseCustomOption, &stage));
    ENGINE_ShowOptions(false);
    assert(stage == 2);
    assert(TILESET_Selected() == TILESET_CUSTOM);
    assert(strstr(TILESET_CustomPath(), "Custom atlas.PNG"));
    assert(!GRAP_SDL_PixelUI());
    assert(ENGINE_Save());
    assert(TILESET_Select(TILESET_DOS));
    assert(TILESET_SetCustomPath(""));
    ENGINE_Load();
    assert(TILESET_Selected() == TILESET_CUSTOM);
    assert(strstr(TILESET_CustomPath(), "Custom atlas.PNG"));
    assert(TILESET_Select(TILESET_DOS));
    remove("custom-picker/Custom atlas.PNG");
    remove("custom-picker/Replacement.png");
    remove("invalid-custom.png");
    remove("ENGINE.CFG");
    assert(SDL_RemovePath("custom-picker"));
    /* A missing configured PNG at startup leaves DOS active. */
    assert(!TILESET_Select(TILESET_CUSTOM));
    assert(TILESET_Selected() == TILESET_DOS);
    assert(TILESET_SetCustomPath(""));
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
    for (int choice = 1; choice <= TILESET_SHARP; choice++)
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
    /* Sharp cutout pixels, including those touching artwork, stay transparent.
     * Native black ink is an opaque palette entry and must still be drawn. */
    bool transparent = GRAP_BUF_TransparentSprites();
    GRAP_BUF_SetTransparentSprites(true);
    assert(GRAP_BUF_SpriteIsTransparent(256 + 18));
    for (int y = -1; y <= 16; y++)
        for (int x = -1; x <= 16; x++)
        {
            int pixel = GRAP_BUF_TilePixel(256 + 18, x, y);
            assert(GRAP_BUF_SpritePixel(256 + 18, x, y) == (pixel ? pixel : -1));
        }
    GRAP_BUF_SetTransparentSprites(transparent);
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
    for (int choice = TILESET_AMIGA; choice <= TILESET_SHARP; choice++)
    {
        assert(TILESET_Select(choice));
        /* DOS procedural updates must never replace alternate effect artwork. */
        const int effects[] = {0x34, 0x60, 0xe4, 0xfa, 0x116, 0x108, 0x1b4, 0xb0,  0xbc,
                               0xde, 18,   20,   21,   62,    0x121, 0x123, 0x12d, 0x12f};
        Uint32 before[sizeof(effects) / sizeof(effects[0])][1024];
        for (int pass = 0; pass < 2; pass++)
        {
            for (size_t i = 0; i < sizeof(effects) / sizeof(effects[0]); i++)
                for (int y = 0; y < 32; y++)
                    for (int x = 0; x < 32; x++)
                    {
                        int c = GRAP_BUF_TilePixel(effects[i], x / 2, y / 2);
                        Uint32 value = TILESET_LastColor(c, x % 2, y % 2, ega);
                        if (!pass)
                            before[i][y * 32 + x] = value;
                        else
                            assert(value == before[i][y * 32 + x]);
                    }
            if (!pass)
                GRAP_BUF_AnimateTileset();
        }
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
    }
    customTileset();
    assert(TILESET_Select(TILESET_SHARP));
#ifdef TEST_TILESET_PRESENT
    rendering();
#endif
    GRAP_Cleanup();
    SDL_Quit();
    puts("Alternate tileset mappings, palettes, native detail, transparency, "
         "persistence and fallbacks "
         "passed.");
    return 0;
}
