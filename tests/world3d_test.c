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
#include "mod/floor_coverage.h"
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
    for (int y = 0; y < 176; y++)
        for (int x = 32; x < 352; x++)
            assert(at(image, x, y) == palette[9]); /* Solid daytime sky, no dither pattern. */
    assert(SDL_SaveBMP(image, "world3d-day.bmp"));
    D_587f = 0;
    WORLD3D_Render(image, palette);
    uint64_t night = skyHash(image);
    assert(day != night && at(image, 64, 0) == palette[0]);
    int stars = 0, black = 0;
    for (int y = 0; y < 176; y++)
        for (int x = 32; x < 352; x++) {
            Uint32 c = at(image, x, y);
            assert(c == palette[0] || c == palette[7] || c == palette[15]);
            stars += c != palette[0];
            black += c == palette[0];
        }
    assert(stars > 0 && black > stars * 100); /* Pitch black, with sparse stars and no blue haze. */
    WORLD3D_Render(image, palette);
    assert(skyHash(image) == night); /* Deterministic, with no random flickering. */
    D_5896_map_x++;
    WORLD3D_Render(image, palette);
    assert(skyHash(image) == night); /* Stars are distant, not attached to the ground. */
    D_5896_map_x--;
    WORLD3D_Move(U5_KEY_RIGHT);
    WORLD3D_Render(image, palette);
    assert(skyHash(image) != night);
    for (int i = 0; i < 3; i++)
        WORLD3D_Move(U5_KEY_RIGHT);
    WORLD3D_Render(image, palette);
    assert(skyHash(image) == night);
    assert(SDL_SaveBMP(image, "world3d-night.bmp"));
    D_6608_map.town[15][16] = 0x4f;
    WORLD3D_Render(image, palette);
    for (int y = 75; y < 176; y++)
        for (int x = 32; x < 352; x++)
            assert(at(image, x, y) != palette[7] && at(image, x, y) != palette[15]);
    D_6608_map.town[15][16] = 0x44;
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
static void testEdgeOutline(SDL_Surface *image, byte *tiles) {
    byte original[128];
    memcpy(original, tiles + 400 * 128, 128);
    memset(tiles + 400 * 128, 0x11, 128); /* Artwork touches all four edges and corners. */
    D_5c5a[1] = (ActorFmt){0x90, 0x90, 16, 14, 0, 0, 0, 0};
    WORLD3D_Render(image, palette);
    const int points[][2] = {{103, 220}, {249, 220}, {176, 145}, {176, 292},
                             {103, 145}, {249, 145}, {103, 292}, {249, 292}};
    for (unsigned i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
        assert(at(image, points[i][0], points[i][1]) == palette[0]);
    }
    assert((at(image, 108, 220) & 255) >
           80); /* No artwork shrinking to make room for the outline. */
    assert(at(image, 95, 95) == palette[9]); /* Padding must not become a black rectangle. */
    memcpy(tiles + 400 * 128, original, 128);
    memset(D_5c5a, 0, sizeof(D_5c5a));
    WORLD3D_Render(image, palette);
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
    ActorFmt saved[2] = {D_5c5a[32], D_5c5a[33]};
    for (unsigned i = 0; i < 2; i++) {
        D_5c5a[32]._0_tile = D_5c5a[33]._0_tile = 0;
        D_5c5a[32 + i] = (ActorFmt){0x90, 0x90, 16, 13, 0, 0, 0, 0};
        WORLD3D_Render(image, palette);
        assert(at(image, 118, 100) == palette[0] && at(image, 344, 100) == palette[0]);
        assert(at(image, 200, 42) == palette[0] && at(image, 200, 269) == palette[0]);
        assert(at(image, 118, 42) == palette[0] && (at(image, 125, 100) & 255) > 80);
    }
    D_5c5a[32] = saved[0];
    D_5c5a[33] = saved[1];
    WORLD3D_Render(image, palette);
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
static void testHeightAndCarpets(SDL_Surface *image, byte *tiles) {
    memset(D_6608_map.town, 0x44, 32 * 32);
    D_6608_map.town[14][16] = 0x4f;
    WORLD3D_Render(image, palette);
    assert(((at(image, 176, 140) >> 16) & 255) > 100); /* Extended second cube. */
    assert(((at(image, 176, 143) >> 16) & 255) > 100 && !(at(image, 176, 143) & 255));
    assert(((at(image, 176, 327) >> 8) & 255) > 100); /* 13/16 eye height, not a midpoint camera. */
    /* Doors and windows retain their lower artwork; extensions are plain tile 77. */
    const unsigned decoratedWalls[] = {0xb8, 0x4b};
    for (unsigned i = 0; i < 2; i++) {
        unsigned tile = decoratedWalls[i];
        memset(tiles + tile * 128, 0x55, 128);
        D_6608_map.town[14][16] = tile;
        WORLD3D_Render(image, palette);
        assert((at(image, 176, 60) & 255) > 100 && !(at(image, 176, 60) & 0xff0000));
        assert((at(image, 176, 200) & 255) > 100 && (at(image, 176, 200) & 0xff0000));
    }
    /* Material propagates through decorated masonry, not through floor cells.
     * Editing the source wall is reflected on the next frame. */
    D_6608_map.town[14][16] = 0xb8;
    D_6608_map.town[14][15] = 0x4b;
    D_6608_map.town[14][14] = 0x50;
    D_6608_map.town[14][13] = 79;
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 60) & 0xff0000) && !(at(image, 176, 60) & 255));
    D_6608_map.town[14][13] = 77;
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 60) & 255) > 100 && !(at(image, 176, 60) & 0xff0000));
    D_6608_map.town[14][13] = D_6608_map.town[14][14] = D_6608_map.town[14][15] = 0x44;
    /* Arches move up one cube, leaving a passage or a night-time grate below. */
    memset(tiles + 135 * 128, 0x11, 128);
    memset(tiles + 134 * 128, 0x22, 128);
    D_6608_map.town[14][16] = 135;
    D_6608_map.town[11][16] = 79;
    D_587f = 12;
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 60) & 255) > 80 && !(at(image, 176, 60) & 0xff0000));
    assert((at(image, 176, 200) & 0xff0000) && !(at(image, 176, 200) & 255));
    D_587f = 22;
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 200) & 0xff00) && !(at(image, 176, 200) & 0xff0000));
    memset(tiles + 134 * 128, 0, 128);
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 200) & 0xff0000) && !(at(image, 176, 200) & 255)); /* Grate gaps. */
    memset(tiles + 135 * 128, 0, 128);
    D_587f = 12;
    WORLD3D_Render(image, palette);
    assert(at(image, 176, 60) == palette[9]); /* Arch cutouts show the sky. */
    D_6608_map.town[11][16] = 0x44;
    /* These tiles are vertical only in 3D. Their black backgrounds are cut out,
     * including table 190 whose ordinary 2D sprite path is intentionally opaque. */
    const unsigned uprightTiles[] = {190, 220};
    for (unsigned i = 0; i < 2; i++) {
        unsigned tile = uprightTiles[i];
        memset(tiles + tile * 128, 0, 128);
        for (int y = 4; y < 16; y++)
            for (int x = 6; x < 10; x++)
                tiles[tile * 128 + y * 8 + x / 2] |= x & 1 ? 5 : 0x50;
        D_6608_map.town[14][16] = tile;
        WORLD3D_Render(image, palette);
        if (tile == 190) {
            assert((at(image, 176, 240) & 255) > 80 && (at(image, 176, 240) & 0xff0000));
            assert(TILESET_ProtectedBlack(tile, 2, 5));
            assert(at(image, 117, 239) ==
                   palette[0]); /* The fixed table face retains normal masks. */
            assert((at(image, 205, 240) & 0xff00) && !(at(image, 205, 240) & 0xff0000));
        } else {
            assert((at(image, 176, 200) & 255) > 80 && (at(image, 176, 200) & 0xff0000));
            assert((at(image, 130, 200) & 0xff00) && !(at(image, 130, 200) & 0xff0000));
            assert(at(image, 155, 200) == palette[0]);
        }
    }
    memset(tiles + 0xaa * 128, 0x55, 128);
    memset(tiles + (256 + 0x1b) * 128, 0x55, 128);
    for (int mode = 0; mode < 2; mode++) {
        D_6608_map.town[14][16] = mode ? 0x44 : 0xaa;
        if (mode)
            D_5c5a[1] = (ActorFmt){0x1b, 0x1b, 16, 14, 0, 0, 0, 0};
        WORLD3D_Render(image, palette);
        int carpet = 0;
        for (int y = 0; y < 352; y++)
            for (int x = 32; x < 352; x++) {
                Uint32 color = at(image, x, y);
                if ((color & 255) > 50 && ((color >> 16) & 255) > 50 && !(color & 0xff00)) {
                    assert(y > 176);
                    carpet++;
                }
            }
        assert(carpet > 0); /* Both decorative and transport carpets lie on the ground. */
        assert(at(image, 176, 150) == palette[9]);
        memset(D_5c5a, 0, sizeof(D_5c5a));
    }
    memset(D_6608_map.town, 0x44, 32 * 32);
}
static void testStackedFloors(SDL_Surface *image, byte *tiles) {
    const unsigned recordSize = MAPS_RECORD + 32 * 32, size = MAPS_HEADER + 4 * recordSize;
    byte *catalog = calloc(1, size);
    assert(catalog);
    memcpy(catalog, "IMAPS001", 8);
    TYPES_Put32(catalog + 8, 4);
    for (int i = 0; i < 4; i++) {
        byte *r = catalog + MAPS_HEADER + i * recordSize;
        TYPES_Put32(r, recordSize);
        r[4] = 1;
        r[5] = 42;
        r[6] = i;
        r[8] = i - 1;
        r[9] = 5;
        r[10] = r[12] = 32;
        r[14] = (i == 1) ? 0 : 254;
        strcpy((char *)r + 24, "Stacked test");
        strcpy((char *)r + 88, "LOC042.DAT");
        memset(r + MAPS_RECORD, i <= 1 ? 0x44 : 5, 32 * 32);
        if (i)
            r[MAPS_RECORD + 11 * 32 + 16] = i == 1 ? 0x4f : i == 2 ? 0x50 : 0x51;
    }
    memset(tiles + 0x50 * 128, 0x77, 128);
    memset(tiles + 0x51 * 128, 0x66, 128);
    assert(MAPS_Mount(catalog, size));
    D_5893_map_id = 42;
    D_5895_map_level = 0;
    WORLD3D_Render(image, palette);
    Uint32 ground = at(image, 176, 200), upper = at(image, 176, 80), tower = at(image, 176, 0);
    assert(((ground >> 16) & 255) > 80 && !(ground & 255));
    assert((upper & 255) > 80 && (upper & 255) == ((upper >> 16) & 255));
    assert((tower & 255) > 80 && !(tower & 0xff0000));
    assert(at(image, 300, 40) == palette[9]); /* Background upstairs remains open sky. */
    assert(D_5893_map_id == 42 && D_5895_map_level == 0);
    const unsigned groundOnlyTiles[] = {137, 160, 162, 164, 189, 202, 203, 224, 225, 226, 227};
    for (unsigned i = 0; i < sizeof(groundOnlyTiles) / sizeof(groundOnlyTiles[0]); i++) {
        byte *cell = MAPS_ActiveCell(42, 1, 16, 11);
        *cell = groundOnlyTiles[i];
        WORLD3D_Render(image, palette);
        assert(at(image, 176, 80) == palette[9]);
        assert(*cell == groundOnlyTiles[i]); /* Presentation never edits terrain. */
    }
    *MAPS_ActiveCell(42, 1, 16, 11) = 0x50;
    WORLD3D_Render(image, palette);
    int dx, dy;
    assert(!WORLD3D_Pick(0.5f, 80.0f / 352, &dx, &dy)); /* Don't send actions to another floor. */
    *MAPS_ActiveCell(42, 1, 16, 13) = 0x44;
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 60) & 255) > 100 &&
           (at(image, 176, 60) & 0xff0000)); /* Tile 72 ceiling, not green floor. */
    *MAPS_ActiveCell(42, 1, 16, 13) = 5;
    *MAPS_ActiveCell(42, 1, 16, 12) = 0x44;
    D_5c5a[1] = (ActorFmt){0x90, 0x90, 16, 12, 1, 0, 0, 0};
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 80) & 255) > 80 && !((at(image, 176, 80) >> 16) & 255));
    float ax, ay;
    assert(WORLD3D_ActorPose(1, &ax, &ay)); /* An NPC on another floor is included. */
    memset(D_5c5a, 0, sizeof(D_5c5a));
    *MAPS_ActiveCell(42, 1, 16, 16) = 0x44;
    D_5895_map_level = 1;
    WORLD3D_Render(image, palette);
    upper = at(image, 176, 200);
    assert((upper & 255) > 80 && (upper & 255) == ((upper >> 16) & 255));
    assert(D_5895_map_level == 1);
    assert(SDL_SaveBMP(image, "world3d-stacked-floors.bmp"));
    D_5893_map_id = 13;
    D_5895_map_level = 0;
    assert(MAPS_Mount(NULL, 0));
    free(catalog);
    WORLD3D_Render(image, palette);
}
static void writeCoverage(const byte *coverage, size_t size) {
    assert(FLOOR3D_Validate(coverage, size));
    FILE *file = fopen("world3d-native-fixture/FLOORS3D.OVR", "wb");
    assert(file && fwrite(coverage, 1, size, file) == size);
    fclose(file);
    WORLD3D_Cleanup();
}
static void testNativeFloors(SDL_Surface *image) {
    assert(SDL_CreateDirectory("world3d-native-fixture"));
    FILE *file = fopen("world3d-native-fixture/CASTLE.DAT", "wb");
    assert(file);
    byte page[1024];
    for (int i = 0; i < 5; i++) {
        memset(page, 5, sizeof(page));
        if (i == 2)
            page[11 * 32 + 16] = 0x50;
        if (i == 3)
            page[11 * 32 + 16] = 0x51;
        assert(fwrite(page, 1, sizeof(page), file) == sizeof(page));
    }
    fclose(file);
    byte coverage[16 + 5 * (16 + 256)] = {0};
    memcpy(coverage, "I3DFLR01", 8);
    TYPES_Put32(coverage + 8, 5);
    for (int i = 0; i < 5; i++) {
        byte *r = coverage + 16 + i * 272;
        TYPES_Put32(r, 272);
        r[4] = 17;
        r[6] = i - 1;
        r[8] = r[10] = 32;
        if (i == 1)
            memset(r + 16, 0x55, 256);
        else if (i == 2 || i == 3) {
            unsigned cell = 11 * 32 + 16;
            r[16 + cell / 4] = 1 << (2 * (cell % 4));
        }
    }
    assert(FLOOR3D_Validate(coverage, sizeof(coverage)));
    file = fopen("world3d-native-fixture/FLOORS3D.OVR", "wb");
    assert(file);
    assert(fwrite(coverage, 1, sizeof(coverage), file) == sizeof(coverage));
    fclose(file);
    WORLD3D_Cleanup();
    FILE_SetDataDirectory("world3d-native-fixture");
    D_5893_map_id = 17;
    memset(D_6608_map.town, 0x44, 32 * 32);
    D_6608_map.town[11][16] = 0x4f;
    WORLD3D_Render(image, palette);
    assert(((at(image, 176, 200) >> 16) & 255) > 80 && !(at(image, 176, 200) & 255));
    assert((at(image, 176, 80) & 255) > 80);
    assert((at(image, 176, 0) & 255) > 80 && !(at(image, 176, 0) & 0xff0000));
    D_6608_map.town[11][16] = 0x44;
    WORLD3D_Render(image, palette);
    assert(!(at(image, 176, 200) & 0xff0000)); /* Current floor reads live terrain, not the file. */
    assert((at(image, 176, 80) & 255) > 80);
    /* Excluded masonry really is backdrop, even when the terrain is a wall. */
    byte *upperCoverage = coverage + 16 + 2 * 272;
    memset(upperCoverage + 16, 0, 256);
    writeCoverage(coverage, sizeof(coverage));
    WORLD3D_Render(image, palette);
    assert(at(image, 176, 80) == palette[9]);
    D_5c5a[1] = (ActorFmt){0x90, 0x90, 16, 11, 1, 0, 0, 0};
    WORLD3D_Render(image, palette);
    float ax, ay;
    assert(WORLD3D_ActorPose(1, &ax, &ay)); /* Actors do not inherit the terrain mask. */
    memset(D_5c5a, 0, sizeof(D_5c5a));
    /* A covered floor blocks the view upwards; its Opening flag exposes the sky. */
    file = fopen("world3d-native-fixture/CASTLE.DAT", "r+b");
    assert(file);
    assert(!fseek(file, 2 * 1024 + 13 * 32 + 16, SEEK_SET));
    assert(fputc(0x44, file) != EOF);
    fclose(file);
    memset(coverage + 16 + 3 * 272 + 16, 0, 256);
    unsigned ceilingCell = 13 * 32 + 16;
    upperCoverage[16 + ceilingCell / 4] = 1 << (2 * (ceilingCell % 4));
    writeCoverage(coverage, sizeof(coverage));
    WORLD3D_Render(image, palette);
    Uint32 ceiling = at(image, 176, 60);
    assert((ceiling & 255) > 100 && (ceiling & 0xff0000));
    upperCoverage[16 + ceilingCell / 4] = 3 << (2 * (ceilingCell % 4));
    writeCoverage(coverage, sizeof(coverage));
    WORLD3D_Render(image, palette);
    assert(at(image, 176, 60) == palette[9]);
    if (SDL_getenv("IMPERA_3D_ART")) {
        Uint64 start = SDL_GetTicksNS();
        for (int i = 0; i < 20; i++)
            WORLD3D_Render(image, palette);
        printf("Five-floor render average: %.2f ms\n", (SDL_GetTicksNS() - start) / 20000000.0);
    }
    FILE_SetDataDirectory("");
    remove("world3d-native-fixture/CASTLE.DAT");
    remove("world3d-native-fixture/FLOORS3D.OVR");
    WORLD3D_Cleanup();
    D_5893_map_id = 13;
    WORLD3D_Render(image, palette);
}
static void testSplitSheets(SDL_Surface *image) {
    const char *groundPath = "synthetic-tileset/Ultima_5_Tiles_DOS3d_Ground.png";
    const char *spritePath = "synthetic-tileset/Ultima_5_Tiles_DOS3d_Sprites.png";
    SDL_Surface *ground = SDL_CreateSurface(512, 256, SDL_PIXELFORMAT_RGBA32);
    SDL_Surface *sprites = SDL_CreateSurface(512, 256, SDL_PIXELFORMAT_RGBA32);
    assert(ground && sprites);
    assert(SDL_FillSurfaceRect(ground, NULL, SDL_MapSurfaceRGBA(ground, 0, 170, 0, 255)));
    assert(SDL_FillSurfaceRect(sprites, NULL, SDL_MapSurfaceRGBA(sprites, 0, 170, 0, 255)));
    /* A previously ordinary terrain cell becomes a billboard solely from the sheet difference. */
    unsigned tile = 43;
    SDL_Rect cell = {(tile % 32) * 16, (tile / 32) * 16, 16, 16};
    assert(SDL_FillSurfaceRect(sprites, &cell, SDL_MapSurfaceRGBA(sprites, 0, 0, 0, 255)));
    SDL_Rect stem = {cell.x + 6, cell.y + 4, 4, 12};
    assert(SDL_FillSurfaceRect(sprites, &stem, SDL_MapSurfaceRGBA(sprites, 170, 0, 170, 255)));
    assert(SDL_SavePNG(ground, groundPath) && SDL_SavePNG(sprites, spritePath));
    TILESET_Refresh();
    assert(TILESET_3DSplit(tile) && !TILESET_3DSplit(tile + 1));
    assert(TILESET_3DPixel(tile, 0, 0, false) == 0xff00aa00);
    assert(TILESET_3DPixel(tile, 0, 0, true) == 0 &&
           TILESET_3DPixel(tile, 7, 8, true) == 0xffaa00aa);
    assert(TILESET_Pixel(tile, 0, 0) == -1); /* 2D DOS artwork still uses the original byte bank. */
    memset(D_6608_map.town, 0x44, 32 * 32);
    D_6608_map.town[14][16] = tile;
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 200) & 255) > 80 && (at(image, 176, 200) & 0xff0000));
    assert((at(image, 176, 300) & 0xff00) && !(at(image, 176, 300) & 0xff0000));
    /* Later artwork edits need no C tile list: a new differing cell is detected on reload. */
    SDL_Rect added = {44 % 32 * 16, 44 / 32 * 16, 16, 16};
    assert(SDL_FillSurfaceRect(sprites, &added, SDL_MapSurfaceRGBA(sprites, 170, 0, 0, 255)));
    assert(SDL_SavePNG(sprites, spritePath));
    TILESET_Refresh();
    assert(TILESET_3DSplit(44));
    SDL_DestroySurface(ground);
    SDL_DestroySurface(sprites);
    assert(SDL_RemovePath(groundPath) && SDL_RemovePath(spritePath));
    TILESET_Refresh();
    assert(!TILESET_3DSplit(tile)); /* Missing pair keeps the original renderer. */
    memset(D_6608_map.town, 0x44, 32 * 32);
}
static void setTestPixel(byte *tiles, unsigned tile, int x, int y, unsigned color) {
    byte *p = tiles + tile * 128 + y * 8 + x / 2;
    if (x & 1)
        *p = (*p & 0xf0) | color;
    else
        *p = (*p & 15) | (color << 4);
}
static int colorCountAbove(SDL_Surface *image, unsigned red, unsigned blue) {
    int count = 0;
    for (int y = 22; y < 176; y++)
        for (int x = 22; x < 330; x++) {
            Uint32 c = at(image, x, y);
            if (((c >> 16) & 255) > red && (c & 255) > blue && !(c & 0xff00))
                count++;
        }
    return count;
}
static void testSignsAndFurniture(SDL_Surface *image, byte *tiles) {
    memset(tiles + 5 * 128, 0x33,
           128); /* Cyan grass distinguishes substituted cells from the floor. */
    memset(tiles + 64 * 128, 0x66, 128);
    for (unsigned tile = 224; tile <= 226; tile++) {
        memset(tiles + tile * 128, 0, 128);
        for (int y = 0; y < 16; y++)
            setTestPixel(tiles, tile, 8, y, 7);
    }
    for (int x = 5; x <= 11; x++)
        setTestPixel(tiles, 224, x, 15, 7);
    for (int x = 8; x < 16; x++)
        setTestPixel(tiles, 225, x, 4, 7);
    for (int x = 0; x <= 8; x++)
        setTestPixel(tiles, 226, x, 4, 7);
    memset(tiles + 227 * 128, 0x22, 128); /* Pole fallback while the new art is not yet provided. */
    memset(tiles + 240 * 128, 0x55, 128);
    memset(tiles + 236 * 128, 0x55, 128);
    for (int side = -1; side <= 1; side++) {
        memset(D_6608_map.town, 0x44, 1024);
        D_6608_map.town[13][16] = 224;
        D_6608_map.town[12][16] = side < 0 ? 226 : side > 0 ? 225 : 240;
        if (side)
            D_6608_map.town[12][16 + side] = 240;
        byte unchanged[1024];
        memcpy(unchanged, D_6608_map.town, 1024);
        WORLD3D_Render(image, palette);
        int x = side < 0 ? 85 : side > 0 ? 268 : 176;
        assert((at(image, x, 110) & 255) > 60 && (at(image, x, 110) & 0xff0000));
        int dx, dy;
        assert(WORLD3D_Pick(x / 352.0f, 110 / 352.0f, &dx, &dy) && dx == 0 && dy == -3);
        assert(!memcmp(unchanged, D_6608_map.town, 1024));
        if (side > 0) {
            Uint32 grass = at(image, 245, 232);
            assert((grass & 255) > 60 && (grass & 0xff00) && !(grass & 0xff0000));
        }
    }
    memset(D_6608_map.town, 0x44, 1024);
    D_6608_map.town[13][16] = D_6608_map.town[12][16] = D_6608_map.town[11][16] = 227;
    D_6608_map.town[11][17] = 236;
    WORLD3D_Render(image, palette);
    assert(colorCountAbove(image, 50, 50) >
           50); /* One tall 227 pole carries its flag above the base. */
    assert(SDL_SaveBMP(image, "world3d-tall-sign.bmp"));
    memset(D_6608_map.town, 0x44, 1024);
    memset(tiles + 92 * 128, 0x11, 128);
    memset(tiles + 93 * 128, 0x55, 128);
    D_6608_map.town[13][15] = 92;
    D_6608_map.town[13][16] = 93;
    WORLD3D_Render(image, palette);
    assert((at(image, 80, 190) & 255) > 60 && !(at(image, 80, 190) & 0xff0000));
    assert((at(image, 176, 190) & 255) > 60 && (at(image, 176, 190) & 0xff0000));
    assert(SDL_SaveBMP(image, "world3d-bookcase-front.bmp"));
    D_5896_map_x = 13;
    D_5897_map_y = 13;
    WORLD3D_Move(U5_KEY_RIGHT);
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 180) & 0xff0000) &&
           !(at(image, 176, 180) & 255)); /* Wooden end, not a swivelling book face. */
    assert(SDL_SaveBMP(image, "world3d-bookcase-end.bmp"));
    WORLD3D_Move(U5_KEY_LEFT);
    D_5896_map_x = D_5897_map_y = 16;
    memset(D_6608_map.town, 0x44, 1024);
    const unsigned row[] = {148, 149, 190, 156, 150};
    for (unsigned i = 0; i < 5; i++) {
        memset(tiles + row[i] * 128, 0x55, 128);
        D_6608_map.town[13][13 + i] = row[i];
    }
    WORLD3D_Render(image, palette);
    assert((at(image, 176, 210) & 255) > 60 && (at(image, 176, 210) & 0xff0000));
    assert(SDL_SaveBMP(image, "world3d-table-run.bmp"));
    memset(D_6608_map.town, 0x44, 1024);
}
int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO));
    syntheticMaskBegin();
    /* A protected black pixel in a non-split tile must survive in 3D too. */
    SDL_Surface *protectedMask = SDL_LoadPNG("synthetic-tileset/Ultima_5_Mask_DOS.png");
    assert(protectedMask);
    SDL_Rect protectedCell = {190 % 32 * 16 + 2, 190 / 32 * 16 + 5, 1, 1};
    assert(SDL_FillSurfaceRect(protectedMask, &protectedCell,
                               SDL_MapSurfaceRGBA(protectedMask, 255, 255, 255, 255)));
    assert(SDL_SavePNG(protectedMask, "synthetic-tileset/Ultima_5_Mask_DOS.png"));
    SDL_DestroySurface(protectedMask);
    GRAP_Initialize();
    EVT_SetImmediateExit(0);
    byte *tiles = calloc(512, 128);
    assert(tiles);
    memset(tiles + 0x44 * 128, 0x22, 128);
    memset(tiles + 0x4f * 128, 0x44, 128);
    memset(tiles + 77 * 128, 0x33, 128);
    memset(tiles + 72 * 128, 0x55, 128);
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
    assert(((at(image, 10, 350) >> 8) & 255) > 100); /* Flat floor below the actor. */
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
    assert((at(image, 100, 262) & 0xffffff) == 0); /* Protected black stays opaque. */
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
    testEdgeOutline(image, tiles);
    testActorMotion(image);
    testCustomActorMotion(image);
    testHeightAndCarpets(image, tiles);
    testStackedFloors(image, tiles);
    testNativeFloors(image);
    testSplitSheets(image);
    testSignsAndFurniture(image, tiles);
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
        /* Original-art review scene for the new composed/fixed objects. */
        memset(D_6608_map.town, 5, 1024);
        memset(D_5c5a, 0, sizeof(D_5c5a));
        D_5896_map_x = D_5897_map_y = 16;
        D_6608_map.town[12][16] = 224;
        D_6608_map.town[11][16] = 225;
        D_6608_map.town[11][17] = 240;
        D_6608_map.town[12][13] = 92;
        D_6608_map.town[12][14] = 93;
        D_6608_map.town[13][18] = 148;
        D_6608_map.town[13][19] = 190;
        D_6608_map.town[13][20] = 150;
        WORLD3D_Render(image, palette);
        assert(SDL_SaveBMP(image, "world3d-objects-original.bmp"));
        memset(D_6608_map.town, 5, 1024);
        const unsigned tableReview[] = {148, 149, 190, 156, 150};
        for (unsigned i = 0; i < 5; i++)
            D_6608_map.town[13][14 + i] = tableReview[i];
        WORLD3D_Render(image, palette);
        assert(SDL_SaveBMP(image, "world3d-table-original.bmp"));
        const char *castle = SDL_getenv("IMPERA_3D_CASTLE");
        if (castle) {
            FILE_SetDataDirectory(castle);
            FILE *f = FILE_Open("CASTLE.DAT", "rb");
            assert(f && !fseek(f, 1024, SEEK_SET));
            assert(fread(D_6608_map.raw, 1, 1024, f) == 1024);
            fclose(f);
            D_5893_map_id = 17;
            D_5895_map_level = 0;
            D_5896_map_x = 16;
            D_5897_map_y = 28;
            memset(D_5c5a, 0, sizeof(D_5c5a));
            WORLD3D_Render(image, palette);
            assert(SDL_SaveBMP(image, "world3d-castle.bmp"));
            start = SDL_GetTicksNS();
            for (int i = 0; i < 20; i++)
                WORLD3D_Render(image, palette);
            printf("Original castle render average: %.2f ms\n",
                   (SDL_GetTicksNS() - start) / 20000000.0);
            FILE_SetDataDirectory("");
        }
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
