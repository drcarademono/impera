#undef NDEBUG
#include "common/common.h"
#include "common/engine_settings.h"
#include "common/file.h"
#include "common/lzw.h"
#include "graphics/grap.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "graphics/tileset.h"
#include "graphics/widescreen.h"
#include "graphics/world3d.h"
#include "key/key.h"
#include "mod/actors.h"
#include "mod/types.h"
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
static uint64_t skyHash(SDL_Surface *image) {
    uint64_t hash = 1469598103934665603ull;
    for (int y = 0; y < 176; y++)
        for (int x = 32; x < 352; x++) {
            Uint32 color = at(image, x, y);
            bool allowed = false;
            for (int i = 0; i < 16; i++)
                allowed |= color == palette[i];
            assert(allowed); /* Every sky pixel is an exact original palette entry. */
            hash = (hash ^ color) * 1099511628211ull;
        }
    return hash;
}
static void testSkies(SDL_Surface *image) {
    memset(D_6608_map.town, 0x44, 32 * 32);
    memset(D_5c5a, 0, sizeof(D_5c5a));
    D_587f = 12;
    D_5881 = 0;
    WORLD3D_Render(image, palette);
    uint64_t day = skyHash(image);
    assert(at(image, 64, 0) == palette[1]);
    assert(at(image, 64, 174) == palette[11] || at(image, 64, 174) == palette[9]);
    assert(SDL_SaveBMP(image, "world3d-day.bmp"));
    D_587f = 0;
    WORLD3D_Render(image, palette);
    uint64_t night = skyHash(image);
    assert(day != night && at(image, 64, 0) == palette[0]);
    assert(SDL_SaveBMP(image, "world3d-night.bmp"));
    D_587f = 5;
    D_5881 = 30;
    WORLD3D_Render(image, palette);
    uint64_t dawn = skyHash(image);
    assert(dawn != night && dawn != day);
    assert(SDL_SaveBMP(image, "world3d-dawn.bmp"));
    D_5881 = 31;
    WORLD3D_Render(image, palette);
    assert(dawn != skyHash(image)); /* Minute changes blend using palette dithering. */
    D_587f = 19;
    D_5881 = 30;
    WORLD3D_Render(image, palette);
    assert(skyHash(image) == dawn);
    assert(SDL_SaveBMP(image, "world3d-dusk.bmp"));
    D_587f = 23;
    D_5881 = 59;
    WORLD3D_Render(image, palette);
    assert(skyHash(image) == night); /* No discontinuity at midnight. */
    D_587f = 12;
    D_5881 = 0;
    D_58a5 = 2;
    WORLD3D_Render(image, palette);
    assert(skyHash(image) == day); /* A short visibility radius must not erase the sky. */
    int dx, dy;
    assert(!WORLD3D_Pick(0.2f, 0.1f, &dx, &dy)); /* Sky is not selectable terrain. */
    D_5895_map_level = 255;
    WORLD3D_Render(image, palette);
    for (int y = 0; y < 176; y++)
        for (int x = 32; x < 352; x++)
            assert(at(image, x, y) == palette[0]);
    D_5895_map_level = 0;
    D_5893_map_id = 0x19; /* Dungeon-like local map stays dark above, too. */
    WORLD3D_Render(image, palette);
    assert(at(image, 64, 100) == palette[0]);
    D_5893_map_id = 13;
    D_58a5 = 50;
}
static float blueCenter(SDL_Surface *image) {
    float sum = 0;
    int count = 0;
    for (int x = 0; x < 352; x++) {
        Uint32 c = at(image, x, 220);
        if ((c & 255) > 80 && !(c & 0x00ffff00)) {
            sum += x;
            count++;
        }
    }
    assert(count > 0);
    return sum / count;
}
static void testActorMotion(SDL_Surface *image) {
    /* Leave room for several instrumented renders within one movement interval. */
    GRAP_SDL_SetMovementSpeed(0.25f);
    GRAP_SDL_SetSmoothMovement(true);
    memset(D_6608_map.town, 0x44, 32 * 32);
    D_5c5a[1] = (ActorFmt){0x90, 0x90, 15, 14, 0, 0, 0, 0};
    WORLD3D_Render(image, palette);
    float x, y;
    assert(WORLD3D_ActorPose(1, &x, &y) && x == 15 && y == 14);
    float start = blueCenter(image);
    D_5c5a[1]._2_x = 16;
    WORLD3D_Render(image, palette);
    assert(WORLD3D_Animating() && WORLD3D_ActorPose(1, &x, &y) && x == 15);
    assert(blueCenter(image) == start); /* The raster, as well as tracking state, interpolates. */
    SDL_Delay(GRAP_SDL_MovementInterval() / 2);
    WORLD3D_Render(image, palette);
    assert(WORLD3D_ActorPose(1, &x, &y) && x > 15 && x < 16);
    float middle = blueCenter(image);
    assert(middle > start);
    D_5c5a[1]._1_animTile = 0x91; /* Native animation changes do not restart movement. */
    WORLD3D_Render(image, palette);
    assert(WORLD3D_ActorPose(1, &x, &y) && x > 15 && x < 16);
    D_5c5a[1]._1_animTile = 0x90;
    SDL_Delay(GRAP_SDL_MovementInterval() + 10);
    GRAP_BUF_FlushPendingPresent(); /* NPC-only motion drives event-loop redraws. */
    assert(!WORLD3D_Animating() && WORLD3D_ActorPose(1, &x, &y) && x == 16);
    WORLD3D_Render(image, palette);
    assert(blueCenter(image) > middle);
    D_5c5a[1]._2_x = 15;
    WORLD3D_Render(image, palette);
    SDL_Delay(GRAP_SDL_MovementInterval() / 3);
    WORLD3D_Render(image, palette);
    assert(WORLD3D_ActorPose(1, &x, &y));
    float before = x;
    D_5c5a[1]._2_x = 14; /* Retarget from the displayed pose; never queue stale steps. */
    WORLD3D_Render(image, palette);
    assert(WORLD3D_ActorPose(1, &x, &y) && x <= before && x > 15);
    D_5c5a[1]._2_x = 10; /* Teleport. */
    WORLD3D_Render(image, palette);
    assert(WORLD3D_ActorPose(1, &x, &y) && x == 10 && !WORLD3D_Animating());
    D_5c5a[1]._2_x = 11;
    WORLD3D_Render(image, palette);
    GRAP_SDL_SetSmoothMovement(false);
    WORLD3D_Render(image, palette);
    assert(WORLD3D_ActorPose(1, &x, &y) && x == 11 && !WORLD3D_Animating());
    GRAP_SDL_SetSmoothMovement(true);
    D_5c5a[1]._0_tile = 0;
    WORLD3D_Render(image, palette);
    assert(!WORLD3D_ActorPose(1, &x, &y) && !WORLD3D_Animating());
    D_5c5a[1] = (ActorFmt){0x40, 0x40, 15, 14, 0, 0, 0, 0};
    WORLD3D_Render(image, palette);
    assert(WORLD3D_ActorPose(1, &x, &y) && x == 15); /* New monster in a reused slot snaps. */
    D_5c5a[1]._2_x = 16;
    D_5896_map_x = 17; /* Simultaneous camera and monster travel. */
    WORLD3D_Render(image, palette);
    assert(WORLD3D_Animating() && WORLD3D_ActorPose(1, &x, &y) && x == 15);
    D_5895_map_level = 1;
    D_5c5a[1]._4_z = 1;
    WORLD3D_Render(image, palette);
    assert(WORLD3D_ActorPose(1, &x, &y) && x == 16 && !WORLD3D_Animating());
    D_5895_map_level = 0;
    D_5896_map_x = 16;
    memset(D_5c5a, 0, sizeof(D_5c5a));
    GRAP_SDL_SetSmoothMovement(false);
    GRAP_SDL_SetMovementSpeed(1.0f);
}
static void testCustomActorMotion(SDL_Surface *image) {
    GRAP_SDL_SetMovementSpeed(0.25f);
    const unsigned recordSize = 256 + 4 + 32 * 32 * 4 + 4 + 16 * 16 * 4,
                   length = 32 + 2 * recordSize;
    byte *defs = calloc(1, length);
    assert(defs);
    memcpy(defs, "ITYPES01", 8);
    defs[8] = 1;
    TYPES_Put32(defs + 24, 2);
    for (unsigned i = 0; i < 2; i++) {
        TypeDef d;
        TYPES_Vanilla(i + 1, 0x90, &d);
        d.id = 40000 + i;
        d.kind = i + 1;
        d.width = d.height = 2;
        d.frames = 2;
        d.render = TYPES_SPRITE;
        byte *record = defs + 32 + i * recordSize;
        TYPES_Encode(record, &d);
        TYPES_Put32(record, recordSize);
        record[257] = 16;
        record[258] = 150;
        for (int p = 0; p < 1024; p++) {
            record[260 + p * 4 + 2] = 255;
            record[260 + p * 4 + 3] = 255;
        }
        byte *world = record + 260 + 32 * 32 * 4;
        world[0] = 6;
        world[1] = 16;
        world[2] = 150;
        for (int p = 0; p < 256; p++) {
            world[4 + p * 4 + 2] = 255;
            world[4 + p * 4 + 3] = 255;
        }
    }
    assert(TYPES_Validate(defs, length));
    TYPES_Mount(defs, length, NULL, 0);
    D_imperaNpcCount = 34; /* Also cover actors beyond the original 32 slots. */
    for (unsigned i = 0; i < 2; i++) {
        unsigned actor = 32 + i;
        D_5f5e[actor]._0 = 1;
        D_5f5e[actor]._c = actor;
        TypeCell key = {3, 13, 0, actor, 0, 40000 + i, 0};
        assert(TYPES_Set(key, 40000 + i));
        D_5c5a[actor] = (ActorFmt){0x90, 0x90, 14 + i * 3, 13, 0, 0, 0, 0};
        unsigned w, h;
        assert(ACTORS_Instance(actor) == 40000 + i);
        ACTORS_Size(actor, &w, &h);
        assert(w == 2 && h == 2);
    }
    GRAP_SDL_SetSmoothMovement(true);
    WORLD3D_Render(image, palette);
    D_5c5a[32]._2_x++;
    D_5c5a[33]._2_x--;
    WORLD3D_Render(image, palette);
    float x, y;
    assert(WORLD3D_ActorPose(32, &x, &y) && x == 14);
    assert(WORLD3D_ActorPose(33, &x, &y) && x == 17 && WORLD3D_Animating());
    SDL_Delay(GRAP_SDL_MovementInterval() / 2);
    WORLD3D_Render(image, palette);
    assert(WORLD3D_ActorPose(32, &x, &y) && x > 14 && x < 15);
    assert(WORLD3D_ActorPose(33, &x, &y) && x > 16 && x < 17);
    SDL_Delay(GRAP_SDL_MovementInterval() + 10);
    WORLD3D_Render(image, palette);
    assert(WORLD3D_ActorPose(32, &x, &y) && x == 15);
    assert(WORLD3D_ActorPose(33, &x, &y) && x == 16 && !WORLD3D_Animating());
    D_5c5a[32]._2_x++;
    WORLD3D_Render(image, palette);
    assert(WORLD3D_Animating());
    D_imperaNpcCount = 32;
    WORLD3D_Render(image, palette);
    assert(!WORLD3D_ActorPose(32, &x, &y) && !WORLD3D_Animating());
    memset(D_5c5a, 0, sizeof(D_5c5a));
    memset(D_5f5e, 0, sizeof(D_5f5e));
    TYPES_Mount(NULL, 0, NULL, 0);
    free(defs);
    /* World seam steps must remain one tile long instead of crossing the map. */
    D_5893_map_id = 0;
    D_5896_map_x = 254;
    D_5897_map_y = 10;
    D_589b = 248;
    D_589c = 0;
    memset(&D_6608_map, 0x44, sizeof(D_6608_map));
    D_5c5a[1] = (ActorFmt){0x90, 0x90, 255, 8, 0, 0, 0, 0};
    WORLD3D_RenderRegion(image, palette, 16, 16, 32, 32, 1);
    D_5c5a[1]._2_x = 0;
    WORLD3D_RenderRegion(image, palette, 16, 16, 32, 32, 1);
    assert(WORLD3D_ActorPose(1, &x, &y) && x == 255 && WORLD3D_Animating());
    SDL_Delay(GRAP_SDL_MovementInterval() / 2);
    WORLD3D_RenderRegion(image, palette, 16, 16, 32, 32, 1);
    assert(WORLD3D_ActorPose(1, &x, &y) && x > 255 && x < 256);
    SDL_Delay(GRAP_SDL_MovementInterval() + 10);
    WORLD3D_RenderRegion(image, palette, 16, 16, 32, 32, 1);
    assert(WORLD3D_ActorPose(1, &x, &y) && x == 256 && !WORLD3D_Animating());
    D_5893_map_id = 13;
    D_5896_map_x = D_5897_map_y = 16;
    D_589b = D_589c = 0;
    memset(D_5c5a, 0, sizeof(D_5c5a));
    GRAP_SDL_SetSmoothMovement(false);
    GRAP_SDL_SetMovementSpeed(1.0f);
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
    testSkies(image);
    testActorMotion(image);
    testCustomActorMotion(image);
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
