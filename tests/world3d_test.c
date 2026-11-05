#undef NDEBUG
#include "common/common.h"
#include "common/engine_settings.h"
#include "common/file.h"
#include "common/lzw.h"
#include "graphics/grap.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "graphics/tileset.h"
#include "graphics/world3d.h"
#include "graphics/widescreen.h"
#include "key/key.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent);
extern void KEY_SDL_ClearInput(void);
extern void KEY_SDL_ReleaseKey(SDL_Keycode);
#include "event/event.h"
#include "macros.h"
#include "vars.h"

#include "synthetic_tileset_mask.h"
static Uint32 palette[16] = {0xff000000, 0xff0000aa, 0xff00aa00, 0xff00aaaa, 0xffaa0000, 0xffaa00aa,
                             0xffaa5500, 0xffaaaaaa, 0xff555555, 0xff5555ff, 0xff55ff55, 0xff55ffff,
                             0xffff5555, 0xffff55ff, 0xffffff55, 0xffffffff};
static Uint32 at(SDL_Surface *s, int x, int y) {
    return ((Uint32 *)((byte *)s->pixels + (y + 16) * s->pitch))[x + 16];
}
int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO));
    syntheticMaskBegin();
    GRAP_Initialize();
    EVT_SetImmediateExit(0);
    byte *tiles = calloc(512, 128);
    assert(tiles);
    memset(tiles + 0x44 * 128, 0x22, 128);
    memset(tiles + 0x4f * 128, 0x44, 128);
    for (int y = 1; y < 15; y++)
        for (int x = 5; x < 11; x++)
            tiles[400 * 128 + y * 8 + x / 2] |= (x & 1) ? 1 : 16;
    GRAP_BUF_LoadTileset(tiles);
    GRAP_BUF_SetTransparentSprites(true);
    for (int i = 0; i < 256; i++)
        D_b11e[i] = i;
    D_5893_map_id = 13;
    D_5895_map_level = 0;
    D_58a4 = 1;
    D_58a5 = 50;
    D_5896_map_x = D_5897_map_y = 16;
    memset(D_6608_map.town, 0x44, 32 * 32);
    memset(D_5c5a, 0, sizeof(D_5c5a));
    memset(D_ab02, 0x44, sizeof(D_ab02));
    memset(D_ac64, 0x16, sizeof(D_ac64));
    D_6608_map.town[14][16] = D_6608_map.town[16][18] = 0x4f;
    D_6608_map.town[18][16] = D_6608_map.town[16][14] = 0x4f;
    assert(!WORLD3D_Enabled());
    ENGINE_Set(ENGINE_WORLD_3D, 1);
    assert(WORLD3D_Active());
    SDL_Surface *image = SDL_CreateSurface(640, 400, SDL_PIXELFORMAT_ARGB8888);
    assert(image);
    SDL_FillSurfaceRect(image, NULL, 0xff123456);
    for (int direction = 0; direction < 4; direction++) {
        WORLD3D_Render(image, palette);
        assert(((at(image, 176, 176) >> 16) & 255) > 100 && (at(image, 176, 176) & 255) == 0);
        int dx, dy;
        assert(WORLD3D_Pick(0.5f, 0.5f, &dx, &dy));
        const int xs[] = {0, 2, 0, -2}, ys[] = {-2, 0, 2, 0};
        assert(dx == xs[direction] && dy == ys[direction]);
        assert(WORLD3D_Move(U5_KEY_RIGHT) == 0);
    }
    assert(WORLD3D_Facing() == 0);
    assert(WORLD3D_Direction(U5_KEY_UP) == U5_KEY_UP);
    assert(WORLD3D_Move(U5_KEY_HOME) == U5_KEY_LEFT);
    assert(WORLD3D_Move(U5_KEY_PGUP) == U5_KEY_RIGHT);
    WORLD3D_Move(U5_KEY_RIGHT);
    assert(WORLD3D_Move(U5_KEY_UP) == U5_KEY_RIGHT && WORLD3D_Move(U5_KEY_DOWN) == U5_KEY_LEFT);
    WORLD3D_Move(U5_KEY_LEFT);
    D_5c5a[1]._0_tile = D_5c5a[1]._1_animTile = 0x90;
    D_5c5a[1]._2_x = 16;
    D_5c5a[1]._3_y = 15;
    D_5c5a[1]._4_z = 0;
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 176) & 255) > 100 && ((at(image, 176, 176) >> 16) & 255) == 0);
    assert(((at(image, 100, 176) >> 16) & 255) > 100); /* Wall visible through sprite black. */
    GRAP_BUF_SetTransparentSprites(false);
    WORLD3D_Render(image, palette);
    assert(!GRAP_BUF_TransparentSprites() && ((at(image, 100, 176) >> 16) & 255) > 100);
    GRAP_BUF_SetTransparentSprites(true);
    int dx, dy;
    assert(WORLD3D_Pick(0.5f, 0.5f, &dx, &dy) && dx == 0 && dy == -1);
    assert(((at(image, 176, 350) >> 8) & 255) > 100); /* Flat floor below the actor. */
    D_5c5a[1]._3_y = 13;
    WORLD3D_Render(image, palette);
    assert(((at(image, 176, 176) >> 16) & 255) > 100); /* Wall occludes rear actor. */
    assert(SDL_SaveBMP(image, "world3d-synthetic.bmp"));
    D_5c5a[1]._3_y = 15;
    SDL_Surface *mask = SDL_LoadPNG("synthetic-tileset/Ultima_5_Mask_DOS.png");
    assert(mask);
    assert(SDL_WriteSurfacePixel(mask, 16 * 16 + 3, 12 * 16 + 8, 255, 255, 255, 255));
    assert(SDL_SavePNG(mask, "synthetic-tileset/Ultima_5_Mask_DOS.png"));
    SDL_DestroySurface(mask);
    FILE_ReloadMods();
    assert(TILESET_Select(TILESET_DOS));
    WORLD3D_Render(image, palette);
    assert((at(image, 100, 176) & 0xffffff) == 0); /* Protected black stays opaque. */
    KEY_SDL_ClearInput();
    KEY_SDL_SetGameplayInput(1);
    SDL_KeyboardEvent key = {0};
    key.key = SDLK_RIGHT;
    KEY_SDL_ProcessKeyDown(key);
    assert(KEY_PollKey() == 0 && WORLD3D_Facing() == 1 && D_5896_map_x == 16 && D_5897_map_y == 16);
    KEY_SDL_ReleaseKey(SDLK_RIGHT);
    key.key = SDLK_UP;
    KEY_SDL_ProcessKeyDown(key);
    assert(KEY_PollKey() == U5_KEY_RIGHT && D_538a == 1); /* Forward now means east. */
    KEY_SDL_ReleaseKey(SDLK_UP);
    WORLD3D_Move(U5_KEY_LEFT);
    KEY_SDL_SetGameplayInput(0);
    assert(((Uint32 *)image->pixels)[0] == 0xff123456); /* No writes to UI outside map. */
    /* Arbitrary rectangular viewport, including geometry beyond eight tiles. */
    memset(D_5c5a, 0, sizeof(D_5c5a));
    memset(D_6608_map.town, 0x44, 32 * 32);
    D_6608_map.town[4][16] = 0x4f;
    SDL_Surface *wide = SDL_CreateSurface(1000, 500, SDL_PIXELFORMAT_ARGB8888);
    assert(wide);
    SDL_FillSurfaceRect(wide, NULL, 0xff123456);
    WORLD3D_RenderRegion(wide, palette, 16, 16, 800, 400, 1.2f);
    assert(WORLD3D_Pick(0.5f, 0.5f, &dx, &dy) && dx == 0 && dy == -12);
    assert(((Uint32 *)wide->pixels)[0] == 0xff123456);
    assert((((Uint32 *)((byte *)wide->pixels + 415 * wide->pitch))[815] >> 8 & 255) > 0);
    D_58a5 = 4;
    WORLD3D_RenderRegion(wide, palette, 16, 16, 800, 400, 1.2f);
    assert(!WORLD3D_Pick(0.5f, 0.5f, &dx, &dy));
    D_58a5 = 50;
    SDL_DestroySurface(wide);
    GRAP_SDL_SetVideoMode(GRAP_VIDEO_FULLSCREEN);
    SDL_Surface *fullscreen = GRAP_SDL_CaptureFrame();
    assert(fullscreen);
    WideLayout layout = WIDE_Layout(fullscreen->w, fullscreen->h);
    assert(layout.columns >= 11 && layout.rows >= 11);
    assert(WORLD3D_Pick(0.5f, 0.5f, &dx, &dy) && dx == 0 && dy == -12);
    assert(SDL_SaveBMP(fullscreen, "world3d-fullscreen.bmp"));
    SDL_DestroySurface(fullscreen);
    GRAP_SDL_SetVideoMode(GRAP_VIDEO_WINDOWED);

    /* Camera turns and steps interpolate, while teleports and disabling smoothness snap. */
    GRAP_SDL_SetSmoothMovement(true);
    WORLD3D_Render(image, palette);
    float px, py, angle;
    WORLD3D_Move(U5_KEY_RIGHT);
    WORLD3D_Render(image, palette);
    WORLD3D_CameraPose(&px, &py, &angle);
    float startAngle = angle;
    assert(WORLD3D_Animating());
    SDL_Delay(GRAP_SDL_MovementInterval() / 2);
    WORLD3D_Render(image, palette);
    WORLD3D_CameraPose(&px, &py, &angle);
    assert(angle > startAngle && angle < 1.5708f);
    SDL_Delay(GRAP_SDL_MovementInterval() + 10);
    GRAP_BUF_FlushPendingPresent();
    assert(!WORLD3D_Animating());
    D_5896_map_x++;
    WORLD3D_Render(image, palette);
    WORLD3D_CameraPose(&px, &py, &angle);
    assert(px < 0 && WORLD3D_Animating());
    SDL_Delay(GRAP_SDL_MovementInterval() / 2);
    WORLD3D_Render(image, palette);
    WORLD3D_CameraPose(&px, &py, &angle);
    assert(px > -0.5f && px < 0.5f);
    GRAP_SDL_SetSmoothMovement(false);
    WORLD3D_Render(image, palette);
    WORLD3D_CameraPose(&px, &py, &angle);
    assert(px == 0.5f && !WORLD3D_Animating());
    WORLD3D_Move(U5_KEY_LEFT);
    D_5896_map_x = 16;
    assert(ENGINE_Save());
    ENGINE_Set(ENGINE_WORLD_3D, 0);
    ENGINE_Load();
    assert(WORLD3D_Enabled());
    WORLD3D_SceneView();
    assert(!WORLD3D_Active());
    WORLD3D_MapDrawn();
    assert(WORLD3D_Active());
    GRAP_SDL_SetPixelUI(true);
    assert(!WORLD3D_Active());
    GRAP_SDL_SetPixelUI(false);
    D_5893_map_id = 128;
    assert(!WORLD3D_Active());
    SDL_FillSurfaceRect(image, NULL, 0xff123456);
    WORLD3D_Render(image, palette);
    assert(at(image, 176, 176) == 0xff123456); /* Combat stays untouched. */
    D_5893_map_id = 33;
    assert(!WORLD3D_Active()); /* Existing dungeon renderer. */
    D_5893_map_id = 0;
    assert(WORLD3D_Active()); /* Both world layers share map zero. */
    ENGINE_Set(ENGINE_WORLD_3D, 0);
    assert(!WORLD3D_Active());
    const char *art = SDL_getenv("IMPERA_3D_ART");
    if (art) {
        FILE *file = fopen(art, "rb");
        assert(file);
        byte *original = NULL;
        u32 length = 0;
        assert(!LzwDecompressFile(file, &original, &length));
        fclose(file);
        assert(length >= 512 * 128);
        GRAP_BUF_UnloadTileset();
        syntheticMaskEnd();
        FILE_ReloadMods();
        GRAP_BUF_LoadTileset(original);
        D_5893_map_id = 13;
        D_58a4 = 1;
        D_58a5 = 50;
        WORLD3D_MapDrawn();
        ENGINE_Set(ENGINE_WORLD_3D, 1);
        memset(D_6608_map.town, 0x44, 32 * 32);
        memset(D_5c5a, 0, sizeof(D_5c5a));
        for (int i = 11; i <= 21; i++) {
            D_6608_map.town[11][i] = D_6608_map.town[21][i] = 0x4f;
            D_6608_map.town[i][11] = D_6608_map.town[i][21] = 0x4f;
        }
        D_6608_map.town[11][16] = 0xb8;
        D_6608_map.town[14][14] = D_6608_map.town[14][18] = 0x4d;
        D_5c5a[1]._0_tile = D_5c5a[1]._1_animTile = 0x90;
        D_5c5a[1]._2_x = 16;
        D_5c5a[1]._3_y = 14;
        D_5c5a[1]._4_z = 0;
        WORLD3D_Render(image, palette);
        assert(SDL_SaveBMP(image, "world3d-original-art.bmp"));
        Uint64 start = SDL_GetTicksNS();
        for (int i = 0; i < 50; i++)
            WORLD3D_Render(image, palette);
        printf("3D render average: %.2f ms\n", (SDL_GetTicksNS() - start) / 50000000.0);
    }
    SDL_DestroySurface(image);
    GRAP_BUF_UnloadTileset();
    GRAP_Cleanup();
    if (!art)
        syntheticMaskEnd();
    SDL_Quit();
    remove("ENGINE.CFG");
    puts("First-person geometry, billboards, depth, controls, settings, and combat isolation "
         "passed");
    return 0;
}
