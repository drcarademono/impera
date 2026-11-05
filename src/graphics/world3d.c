#include "world3d.h"
#include "common/file.h"
#include "event/event.h"
#include "funcs.h"
#include "grap_buf.h"
#include "grap_sdl.h"
#include "macros.h"
#include "mod/actors.h"
#include "mod/contents_game.h"
#include "mod/floor_coverage.h"
#include "mod/package.h"
#include "mod/runtime.h"
#include "mod/types_game.h"
#include "tiles.h"
#include "tileset.h"
#include "vars.h"
#include "widescreen.h"
#include <math.h>
#include <string.h>

#define HALF_PI 1.57079632679489661923
#define BUCKETS 1024
#define STORY_HEIGHT 2.0f
#define EYE_HEIGHT (13.0f / 16.0f)
static bool enabled, sceneView, pickValid;
static int facing;
static const int compass[] = {U5_KEY_UP, U5_KEY_RIGHT, U5_KEY_DOWN, U5_KEY_LEFT};
static unsigned pickMap;
static int pickLevel, pickWorldX, pickWorldY, pickFacing;
static int viewX, viewY, viewW, viewH;
static float *depth;
static byte *floorDepth;
static int *pickX, *pickY, *pickFloor;
static size_t pixelCapacity;
typedef struct {
    unsigned tile;
    int mode;
    unsigned w, h, pad;
    int inkTop, inkBottom;
    int next;
    Uint32 pixels[68 * 68];
    Uint32 *largePixels;
} Texture;
static Texture *textures;
static int textureCount, textureCapacity, textureBuckets[BUCKETS];
typedef struct {
    unsigned stamp;
    int x, y, level;
    bool visible, wall, opaque, empty, opening, arch, grated;
    int surface, upperSurface, ground;
} Cell;
static Cell *cells;
static size_t cellCapacity, cellCount;
static unsigned generation;
typedef struct {
    float x, y;
    int texture, dx, dy, level;
    bool flat, topOnly;
    float offset, lift;
} Sprite;
static Sprite *sprites;
static int spriteCount, spriteCapacity;
static byte *wallMaterials[5];
static size_t wallMaterialSizes[5];
static unsigned wallMaterialStamps[5];
static unsigned *wallQueue, *wallSources;
static size_t wallQueueSize;
typedef struct {
    float x, y, z, ux, uy, uz, vx, vy, vz, crop;
    int texture, underneath, dx, dy, level;
    bool ground;
} ObjectQuad;
static ObjectQuad *objectQuads;
static int objectQuadCount, objectQuadCapacity;
typedef struct {
    int x, y, level;
} SignKey;
static SignKey *drawnSigns;
static int drawnSignCount, drawnSignCapacity;
static const Uint32 *colors;
/* Unwrapped world coordinates keep seam crossings adjacent on wrapped maps. */
static struct {
    bool valid, moving;
    unsigned map;
    int level, x, y, heading;
    Uint64 started;
    double fromX, fromY, fromAngle, targetX, targetY, targetAngle, xNow, yNow, angleNow;
} camera;
static unsigned duration;
/* Actor slots retain their own unwrapped paths, independent of camera travel. */
typedef struct {
    bool valid, moving, sighted;
    unsigned kind;
    int x, y, level;
    Uint64 started;
    unsigned duration;
    double fromX, fromY, targetX, targetY, xNow, yNow;
} ActorMotion;
static ActorMotion *actors;
static int actorCapacity, trackedActorCount;
static bool actorsValid, actorsMoving;
static bool nativeValid;
static byte *coverageResource, *suggestedCoverage[5];
static size_t coverageSize;
static unsigned coverageRevision = ~0u, suggestedHash[5], suggestedMap[5];
static int suggestedWidth[5], suggestedHeight[5];
static unsigned actorsMap;
static int actorsLevel;
static void resetMotion(void) {
    camera.valid = camera.moving = false;
    actorsValid = actorsMoving = false;
}

bool WORLD3D_Enabled(void) { return enabled; }
void WORLD3D_SetEnabled(bool value) {
    enabled = value;
    pickValid = false;
    resetMotion();
    GRAP_BUF_MarkDirty();
}
bool WORLD3D_Active(void) {
    return enabled && !sceneView && D_58a4 && D_5893_map_id < 128 && MAPS_Outside(D_5893_map_id) &&
           !GRAP_SDL_PixelUI() && !EVT_ImmediateExitEnabled() && GRAP_BUF_HasTileset();
}
void WORLD3D_SceneView(void) {
    sceneView = true;
    pickValid = false;
    resetMotion();
}
void WORLD3D_MapDrawn(void) {
    sceneView = false;
    if (!D_58a4 || D_5893_map_id >= 128 || !MAPS_Outside(D_5893_map_id))
        resetMotion();
}
int WORLD3D_Facing(void) { return facing; }
int WORLD3D_Direction(int key) {
    const int directions[] = {U5_KEY_UP,   U5_KEY_PGUP, U5_KEY_RIGHT, U5_KEY_PGDN,
                              U5_KEY_DOWN, U5_KEY_END,  U5_KEY_LEFT,  U5_KEY_HOME};
    for (int i = 0; i < 8; i++)
        if (key == directions[i])
            return directions[(i + facing * 2) & 7];
    return key;
}
int WORLD3D_Move(int key) {
    if (key == U5_KEY_LEFT || key == U5_KEY_RIGHT) {
        facing = (facing + (key == U5_KEY_LEFT ? 3 : 1)) & 3;
        pickValid = false;
        GRAP_BUF_MarkDirty();
        GRAP_BUF_Present();
        return 0;
    }
    if (key == U5_KEY_HOME)
        return compass[(facing + 3) & 3];
    if (key == U5_KEY_PGUP)
        return compass[(facing + 1) & 3];
    if (key == U5_KEY_END || key == U5_KEY_PGDN)
        return 0;
    return WORLD3D_Direction(key);
}
static void sampleCamera(Uint64 now) {
    double t = camera.moving ? SDL_min(1.0, (double)(now - camera.started) / duration) : 1;
    camera.xNow = camera.fromX + (camera.targetX - camera.fromX) * t;
    camera.yNow = camera.fromY + (camera.targetY - camera.fromY) * t;
    camera.angleNow = camera.fromAngle + (camera.targetAngle - camera.fromAngle) * t;
    if (t >= 1)
        camera.moving = false;
}
static void updateCamera(void) {
    Uint64 now = SDL_GetTicks();
    int dx = D_5896_map_x - camera.x, dy = D_5897_map_y - camera.y;
    if (!D_5893_map_id) {
        dx = MAPS_Wrap(dx + MAP_WIDTH / 2, MAP_WIDTH) - MAP_WIDTH / 2;
        dy = MAPS_Wrap(dy + MAP_HEIGHT / 2, MAP_HEIGHT) - MAP_HEIGHT / 2;
    }
    bool same = camera.valid && camera.map == D_5893_map_id && camera.level == D_5895_map_level;
    if (!same || !GRAP_SDL_SmoothMovementEnabled() || abs(dx) > 1 || abs(dy) > 1) {
        camera.targetX = camera.fromX = D_5896_map_x + 0.5;
        camera.targetY = camera.fromY = D_5897_map_y + 0.5;
        camera.targetAngle = camera.fromAngle = facing * HALF_PI;
        camera.moving = false;
    } else {
        sampleCamera(now);
        if (dx || dy || camera.heading != facing) {
            camera.fromX = camera.xNow;
            camera.fromY = camera.yNow;
            camera.fromAngle = camera.angleNow;
            camera.targetX += dx;
            camera.targetY += dy;
            int turn = (facing - camera.heading + 4) & 3;
            if (turn == 3)
                turn = -1;
            camera.targetAngle += turn * HALF_PI;
            duration = SDL_max(16, GRAP_SDL_MovementInterval());
            camera.started = now;
            camera.moving = true;
        }
    }
    camera.valid = true;
    camera.map = D_5893_map_id;
    camera.level = D_5895_map_level;
    camera.x = D_5896_map_x;
    camera.y = D_5897_map_y;
    camera.heading = facing;
    sampleCamera(now);
}
bool WORLD3D_Animating(void) {
    return WORLD3D_Active() && GRAP_SDL_SmoothMovementEnabled() && (camera.moving || actorsMoving);
}
void WORLD3D_CameraPose(float *x, float *y, float *angle) {
    *x = 0.5f + (float)(camera.xNow - camera.targetX);
    *y = 0.5f + (float)(camera.yNow - camera.targetY);
    *angle = (float)camera.angleNow;
}
void WORLD3D_Cleanup(void) {
    if (textures)
        for (int i = 0; i < textureCount; i++)
            SDL_free(textures[i].largePixels);
    textureCount = 0;
    SDL_free(objectQuads);
    objectQuads = NULL;
    objectQuadCount = objectQuadCapacity = 0;
    SDL_free(drawnSigns);
    drawnSigns = NULL;
    drawnSignCount = drawnSignCapacity = 0;
    for (int i = 0; i < 5; i++) {
        SDL_free(wallMaterials[i]);
        wallMaterials[i] = NULL;
        wallMaterialSizes[i] = 0;
        wallMaterialStamps[i] = 0;
    }
    SDL_free(wallQueue);
    SDL_free(wallSources);
    wallQueue = wallSources = NULL;
    wallQueueSize = 0;
    SDL_free(depth);
    SDL_free(floorDepth);
    SDL_free(pickX);
    SDL_free(pickY);
    SDL_free(pickFloor);
    SDL_free(textures);
    SDL_free(sprites);
    SDL_free(cells);
    SDL_free(coverageResource);
    coverageResource = NULL;
    coverageSize = 0;
    coverageRevision = ~0u;
    for (int i = 0; i < 5; i++) {
        SDL_free(suggestedCoverage[i]);
        suggestedCoverage[i] = NULL;
        suggestedWidth[i] = 0;
    }
    SDL_free(actors);
    actors = NULL;
    actorCapacity = trackedActorCount = 0;
    depth = NULL;
    floorDepth = NULL;
    pickX = pickY = pickFloor = NULL;
    textures = NULL;
    sprites = NULL;
    cells = NULL;
    pixelCapacity = cellCapacity = cellCount = 0;
    textureCapacity = spriteCapacity = 0;
    pickValid = false;
    resetMotion();
    nativeValid = false;
}
static bool reservePixels(int width, int height) {
    size_t count = (size_t)width * height;
    if (count <= pixelCapacity)
        return true;
    if (count > SIZE_MAX / sizeof(float) || count > SIZE_MAX / sizeof(int))
        return false;
    float *z = SDL_malloc(count * sizeof(float));
    byte *floor = SDL_malloc(count);
    int *x = SDL_malloc(count * sizeof(int)), *y = SDL_malloc(count * sizeof(int)),
        *levels = SDL_malloc(count * sizeof(int));
    if (!z || !x || !y || !floor || !levels) {
        SDL_free(z);
        SDL_free(floor);
        SDL_free(x);
        SDL_free(y);
        SDL_free(levels);
        return false;
    }
    SDL_free(depth);
    SDL_free(floorDepth);
    SDL_free(pickX);
    SDL_free(pickY);
    SDL_free(pickFloor);
    depth = z;
    floorDepth = floor;
    pickX = x;
    pickY = y;
    pickFloor = levels;
    pixelCapacity = count;
    return true;
}
static void textureInk(Texture *t, int y, Uint32 color) {
    if (!(color >> 24))
        return;
    int row = y - (int)t->pad;
    t->inkTop = SDL_min(t->inkTop, row);
    t->inkBottom = SDL_max(t->inkBottom, row + 1);
}
static unsigned placementTile(unsigned tile) {
    unsigned id = TYPES_ArtworkType(tile);
    TypeDef d;
    return id >= 512 && TYPES_Get(id, &d) && !d.kind ? d.base : id;
}
static float spriteLift(unsigned tile, const Texture *t) {
    unsigned id = placementTile(tile);
    if (id == 176 || id == 177)
        return 0;
    if (id == 200 || id == 279)
        return STORY_HEIGHT - ((float)t->h - t->inkTop) / 32;
    return -((float)t->h - t->inkBottom) / 32;
}
/* Furniture/moongates made upright in 3D need cutouts even when their 2D
 * drawing path deliberately keeps an opaque rectangular backdrop. */
static Uint32 uprightPixel(unsigned tile, int x, int y, int sx, int sy) {
    if (x < 0 || y < 0 || x >= 16 || y >= 16)
        return 0;
    if (TYPES_ArtSlot(tile)) {
        int c = GRAP_BUF_SpritePixel(tile, x, y);
        return TILESET_LastSpriteColor(c, sx, sy, colors, GRAP_BUF_SpriteIsTransparent(tile));
    }
    int c = GRAP_BUF_TilePixel(tile, x, y);
    /* Keep normal native protection masks here; only split sheets bypass them. */
    if (!c && !TILESET_ProtectedBlack(tile, x, y))
        return 0;
    return TILESET_LastSpriteColor(c, sx, sy, colors, true);
}
/* 0: opaque tile; 1: normal sprite; 2: split ground; 3: forced upright terrain. */
static int texture(unsigned tile, int mode) {
    bool sprite = mode == 1 || mode == 3 || mode == 5;
    bool outline = mode != 5;
    bool split = TILESET_3DSplit(tile) && (sprite || mode == 2);
    unsigned bucket = (tile * 2654435761u + (unsigned)mode) & (BUCKETS - 1);
    for (int i = textureBuckets[bucket]; i >= 0; i = textures[i].next)
        if (textures[i].tile == tile && textures[i].mode == mode)
            return i;
    if (textureCount == textureCapacity) {
        int capacity = textureCapacity ? textureCapacity * 2 : 32;
        Texture *next = SDL_realloc(textures, (size_t)capacity * sizeof(*textures));
        if (!next)
            return -1;
        textures = next;
        textureCapacity = capacity;
    }
    Texture *t = &textures[textureCount];
    t->largePixels = NULL;
    t->tile = tile;
    t->mode = mode;
    t->w = t->h = 1;
    if (sprite && TYPES_ArtSlot(tile))
        TYPES_ArtSize(tile, D_5893_map_id == 0, &t->w, &t->h);
    t->w = SDL_clamp(t->w, 1, 2) * 32;
    t->h = SDL_clamp(t->h, 1, 2) * 32;
    bool transparency = GRAP_BUF_TransparentSprites();
    if (sprite)
        GRAP_BUF_SetTransparentSprites(true);
    /* Preserve the shared one-logical-pixel outline outside every artwork edge. */
    t->pad = sprite && outline ? 2 : 0;
    t->inkTop = t->h;
    t->inkBottom = 0;
    for (unsigned y = 0; y < t->h + 2 * t->pad; y++)
        for (unsigned x = 0; x < t->w + 2 * t->pad; x++) {
            int px = (int)(x / 2) - (int)(t->pad / 2);
            int py = (int)(y / 2) - (int)(t->pad / 2);
            if (split) {
                Uint32 color = TILESET_3DPixel(tile, px, py, sprite);
                textureInk(t, y, color);
                if (sprite && outline && !(color >> 24) && TILESET_SpriteOutline()) {
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                            if (TILESET_3DPixel(tile, px + dx, py + dy, true) >> 24)
                                color = 0xff000000;
                }
                t->pixels[y * 68 + x] = color;
                continue;
            }
            if (mode == 3 || mode == 5) {
                Uint32 color = uprightPixel(tile, px, py, x & 1, y & 1);
                textureInk(t, y, color);
                if (outline && !(color >> 24) && TILESET_SpriteOutline()) {
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                            if (uprightPixel(tile, px + dx, py + dy, x & 1, y & 1) >> 24)
                                color = 0xff000000;
                }
                t->pixels[y * 68 + x] = color;
                continue;
            }
            int c = sprite ? GRAP_BUF_SpritePixel(tile, px, py) : GRAP_BUF_TilePixel(tile, px, py);
            t->pixels[y * 68 + x] =
                c < 0    ? 0
                : sprite ? TILESET_LastSpriteColor(c, x & 1, y & 1, colors,
                                                   GRAP_BUF_SpriteIsTransparent(tile))
                         : TILESET_LastColor(c, x & 1, y & 1, colors);
            if (!sprite || c > 0)
                textureInk(t, y, t->pixels[y * 68 + x]);
        }
    if (t->inkBottom <= t->inkTop) {
        t->inkTop = 0;
        t->inkBottom = t->h;
    }
    GRAP_BUF_SetTransparentSprites(transparency);
    t->next = textureBuckets[bucket];
    textureBuckets[bucket] = textureCount;
    return textureCount++;
}
static Uint32 sample(int id, float u, float v) {
    if (id < 0)
        return 0xff000000;
    Texture *t = &textures[id];
    int w = t->w + 2 * t->pad, h = t->h + 2 * t->pad;
    int x = SDL_clamp((int)(u * w), 0, w - 1), y = SDL_clamp((int)(v * h), 0, h - 1);
    return t->largePixels ? t->largePixels[y * w + x] : t->pixels[y * 68 + x];
}
static Uint32 shade(Uint32 color, float distance, bool side) {
    float light = 1 / (1 + distance * 0.09f);
    if (side)
        light *= 0.78f;
    return 0xff000000 | ((unsigned)(((color >> 16) & 255) * light) << 16) |
           ((unsigned)(((color >> 8) & 255) * light) << 8) | (unsigned)((color & 255) * light);
}
static void mapPoint(int dx, int dy, int *x, int *y) {
    *x = D_5896_map_x + dx;
    *y = D_5897_map_y + dy;
    if (!D_5893_map_id) {
        *x = MAPS_Wrap(*x, MAP_WIDTH);
        *y = MAPS_Wrap(*y, MAP_HEIGHT);
    }
}
/* Only inactive native pages are cached. Active terrain, extended map records,
 * and sparse type state are read live; drawing never swaps the engine's floor. */
typedef struct {
    int level, w, h;
    byte background;
    const byte *record, *native, *coverage;
    const Floor3DDefault *profile;
    const byte *suggestion;
} Floor;
static Floor floors[5];
static int floorCount;
static byte nativeFloors[5][1024];
static bool nativeAvailable[5];
static unsigned nativeMap, nativeRevision;
static int nativeLevel;
static int currentLevel(void) { return (int8_t)D_5895_map_level; }
static Floor *findFloor(int level) {
    for (int i = 0; i < floorCount; i++)
        if (floors[i].level == level)
            return &floors[i];
    return NULL;
}
static const byte *floorTerrain(Floor *f) {
    return f->record ? f->record + MAPS_RECORD : f->level == currentLevel() ? D_6608 : f->native;
}
static void prepareCoverage(void) {
    if (coverageRevision != MOD_Revision()) {
        SDL_free(coverageResource);
        coverageResource = NULL;
        coverageSize = 0;
        FILE *file = FILE_Open("FLOORS3D.OVR", "rb");
        if (file) {
            if (!fseek(file, 0, SEEK_END)) {
                long size = ftell(file);
                rewind(file);
                if (size >= 16 && size <= MAPS_MAX_BYTES) {
                    byte *next = SDL_malloc((size_t)size);
                    if (next && fread(next, 1, (size_t)size, file) == (size_t)size &&
                        FLOOR3D_Validate(next, size)) {
                        coverageResource = next;
                        coverageSize = size;
                    } else
                        SDL_free(next);
                }
            }
            fclose(file);
        }
        coverageRevision = MOD_Revision();
    }
    for (int i = 0; i < floorCount; i++) {
        Floor *f = &floors[i];
        f->coverage = FLOOR3D_Find(coverageResource, coverageSize, D_5893_map_id, f->level);
        f->profile = NULL;
        f->suggestion = NULL;
        if (f->coverage)
            continue;
        for (unsigned j = 0; j < sizeof(floor3dDefaults) / sizeof(floor3dDefaults[0]); j++)
            if (floor3dDefaults[j].map == D_5893_map_id && floor3dDefaults[j].level == f->level) {
                f->profile = &floor3dDefaults[j];
                break;
            }
        if (f->profile || !D_5893_map_id)
            continue;
        const byte *terrain = floorTerrain(f);
        if (!terrain)
            continue;
        Floor *lower = findFloor(f->level - 1);
        const byte *below =
            lower && lower->w == f->w && lower->h == f->h ? floorTerrain(lower) : NULL;
        unsigned hash = MOD_Crc32(terrain, (size_t)f->w * f->h);
        if (below)
            hash = hash * 16777619u ^ MOD_Crc32(below, (size_t)f->w * f->h);
        int slot = f->level + 1;
        if (!suggestedCoverage[slot] || suggestedWidth[slot] != f->w ||
            suggestedHeight[slot] != f->h || suggestedMap[slot] != D_5893_map_id ||
            suggestedHash[slot] != hash) {
            byte *next = SDL_malloc((size_t)f->w * f->h);
            if (next && FLOOR3D_Suggest(terrain, below, f->w, f->h, f->level, next)) {
                SDL_free(suggestedCoverage[slot]);
                suggestedCoverage[slot] = next;
                suggestedWidth[slot] = f->w;
                suggestedHeight[slot] = f->h;
                suggestedMap[slot] = D_5893_map_id;
                suggestedHash[slot] = hash;
            } else {
                SDL_free(next);
                continue;
            }
        }
        f->suggestion = suggestedCoverage[slot];
    }
}
static unsigned coverageAt(Floor *f, int dx, int dy) {
    int x, y;
    mapPoint(dx, dy, &x, &y);
    if (!f)
        return 0;
    if (!D_5893_map_id)
        return 1;
    if (f->coverage)
        return FLOOR3D_Cell(f->coverage, x, y);
    if (x < 0 || y < 0 || x >= f->w || y >= f->h)
        return 0;
    if (f->profile) {
        if (x >= 32 || y >= 32)
            return f->level <= 0 ? 1 : 0;
        unsigned n = y * 32 + x;
        return (f->profile->cells[n / 4] >> ((n % 4) * 2)) & 3;
    }
    return f->suggestion ? f->suggestion[y * f->w + x] : 1;
}
static void prepareFloors(void) {
    floorCount = 0;
    unsigned map = D_5893_map_id, revision = MOD_Revision();
    if (!map) {
        floors[floorCount++] = (Floor){currentLevel(), MAP_WIDTH, MAP_HEIGHT, 0, NULL, NULL};
        return;
    }
    if (!nativeValid || nativeMap != map || nativeRevision != revision ||
        nativeLevel != currentLevel()) {
        memset(nativeAvailable, 0, sizeof(nativeAvailable));
        nativeValid = true;
        nativeMap = map;
        nativeRevision = revision;
        nativeLevel = currentLevel();
        if (map <= 32) {
            static const int8_t low[32] = {0,  0,  0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           -1, -1, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1};
            static const int8_t high[32] = {1, 1, 1, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0,
                                            3, 3, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 2, 2, 1};
            FILE *file = NULL;
            for (int level = low[map - 1]; level <= high[map - 1]; level++) {
                if (level == currentLevel() || MAPS_Record(1, map, level))
                    continue;
                if (!file)
                    file = FILE_Open(D_2652[(map - 1) / 8], "rb");
                if (!file)
                    break;
                if (!fseek(file, (D_1e1a[map - 1] + level) * 1024, SEEK_SET) &&
                    fread(nativeFloors[level + 1], 1, 1024, file) == 1024)
                    nativeAvailable[level + 1] = true;
            }
            if (file)
                fclose(file);
        }
    }
    for (int level = -1; level <= 3; level++) {
        const byte *r = MAPS_Record(1, map, level);
        bool active = level == currentLevel();
        if (!r && !active && !nativeAvailable[level + 1])
            continue;
        const byte *n = nativeAvailable[level + 1] ? nativeFloors[level + 1] : NULL;
        floors[floorCount++] = (Floor){level,
                                       r        ? MAPS_Word(r + 10)
                                       : active ? MAP_WIDTH
                                                : 32,
                                       r        ? MAPS_Word(r + 12)
                                       : active ? MAP_HEIGHT
                                                : 32,
                                       r        ? r[9]
                                       : active ? D_6a07
                                                : n[1023],
                                       r,
                                       n};
    }
}
static unsigned typeAtLevel(int dx, int dy, int level) {
    int x, y;
    mapPoint(dx, dy, &x, &y);
    if (level == currentLevel())
        return TYPES_GAME_At(x, y);
    Floor *f = findFloor(level);
    if (!f || x < 0 || y < 0 || x >= f->w || y >= f->h)
        return 255;
    unsigned base = f->record ? f->record[MAPS_RECORD + y * f->w + x] : f->native[y * 32 + x];
    TypeCell key = {0, D_5893_map_id, level, x, y, 0, 0};
    return TYPES_At(key, base);
}
static unsigned typeAt(int dx, int dy, int level) { return typeAtLevel(dx, dy, level); }
static unsigned art(unsigned id, byte base) {
    return TYPES_ArtSlot(id) ? TYPES_ActorArt(id, D_b11e[base]) : D_b11e[base];
}
static bool uprightTerrain(const TypeDef *d) {
    return (d->render & TYPES_SPRITE) || d->base == 190 || d->base == 220 ||
           (d->base >= 224 && d->base <= 227) || (d->base >= 236 && d->base <= 247) ||
           d->base == 249;
}
static unsigned floorFor(int dx, int dy, int level, const TypeDef *d) {
    if (TYPES_Defined(d->id) && d->ground != TYPES_INFER_GROUND)
        return d->ground;
    unsigned candidates[4];
    int best = -1, votes = 0;
    const int nx[] = {0, 0, 1, -1}, ny[] = {1, -1, 0, 0};
    for (int i = 0; i < 4; i++)
        candidates[i] = typeAt(dx + nx[i], dy + ny[i], level);
    for (int i = 0; i < 4; i++) {
        TypeDef n;
        if (!TYPES_Get(candidates[i], &n) || uprightTerrain(&n) || (n.render & TYPES_MASONRY))
            continue;
        int count = 0;
        for (int j = 0; j < 4; j++)
            count += candidates[i] == candidates[j];
        if (count > votes) {
            best = i;
            votes = count;
        }
    }
    return best < 0 ? (D_5893_map_id ? 0x44 : 5) : candidates[best];
}
static void objectQuad(float, float, float, float, float, float, float, float, float, int, float,
                       int, int, int, int);
static void addSprite(unsigned tile, float x, float y, int dx, int dy, int level, bool flat,
                      bool topOnly) {
    unsigned placement = placementTile(tile);
    flat |= placement == 286 || placement == 287 || (placement >= 296 && placement <= 299);
    if (dx == 0 && dy == 0 && level == currentLevel() && !flat)
        return;
    if (spriteCount == spriteCapacity) {
        int capacity = spriteCapacity ? spriteCapacity * 2 : 32;
        Sprite *next = SDL_realloc(sprites, (size_t)capacity * sizeof(*sprites));
        if (!next)
            return;
        sprites = next;
        spriteCapacity = capacity;
    }
    int index = texture(tile, tile == 190 || tile == 220 ? 3 : 1);
    if (index >= 0) {
        sprites[spriteCount++] =
            (Sprite){x,     y,    index,   dx, dy,
                     level, flat, topOnly, 0,  flat ? 0 : spriteLift(tile, &textures[index])};
        if (placement == 200 || placement == 279) {
            objectQuad(x - .5f, y - .5f, STORY_HEIGHT * (level + 1), 1, 0, 0, 0, 1, 0,
                       texture(art(72, 72), 0), 1, -1, dx, dy, level);
        } else if (placement == 201 || placement == 280) {
            TypeDef d, ground;
            unsigned id = typeAt(dx, dy, level), floor = 5;
            if (TYPES_Get(id, &d))
                floor = floorFor(dx, dy, level, &d);
            if (!TYPES_Get(floor, &ground))
                TYPES_Get(5, &ground);
            int previous = objectQuadCount;
            objectQuad(x - .5f, y - .5f, STORY_HEIGHT * level + .001f, 1, 0, 0, 0, 1, 0,
                       texture(art(floor, ground.base), 0), 1, -1, dx, dy, level);
            if (objectQuadCount > previous)
                objectQuads[objectQuadCount - 1].ground = true;
        }
    }
}
static size_t hashCell(int x, int y, int level) {
    return (unsigned)x * 2654435761u ^ (unsigned)y * 2246822519u ^ (unsigned)level * 3266489917u;
}
static bool growCells(void) {
    size_t capacity = cellCapacity ? cellCapacity * 2 : 4096;
    Cell *next = SDL_calloc(capacity, sizeof(*next));
    if (!next)
        return false;
    for (size_t i = 0; i < cellCapacity; i++)
        if (cells[i].stamp == generation) {
            size_t p = hashCell(cells[i].x, cells[i].y, cells[i].level) & (capacity - 1);
            while (next[p].stamp == generation)
                p = (p + 1) & (capacity - 1);
            next[p] = cells[i];
        }
    SDL_free(cells);
    cells = next;
    cellCapacity = capacity;
    return true;
}
/* Resolve decorated walls with a multi-source flood along the actual masonry.
 * Plain 77/79 tiles seed the material; doors, windows and arches carry it through
 * a connected wall. Results are shared by the whole component for this frame. */
static int compareWallIndex(const void *a, const void *b) {
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return (x > y) - (x < y);
}
static unsigned upperWallMaterial(int dx, int dy, int level, const TypeDef *definition) {
    if (definition->base == 77 || definition->base == 78 || definition->base == 79)
        return definition->base == 78 ? 79 : definition->base;
    Floor *f = findFloor(level);
    if (!f || level < -1 || level > 3)
        return 77;
    unsigned slot = level + 1, count = (unsigned)f->w * f->h;
    int wx, wy;
    mapPoint(dx, dy, &wx, &wy);
    if (wx < 0 || wy < 0 || wx >= f->w || wy >= f->h)
        return 77;
    if (wallMaterialSizes[slot] < count) {
        byte *next = SDL_realloc(wallMaterials[slot], count);
        if (!next)
            return 77;
        wallMaterials[slot] = next;
        wallMaterialSizes[slot] = count;
        wallMaterialStamps[slot] = 0;
    }
    if (wallMaterialStamps[slot] != generation) {
        memset(wallMaterials[slot], 0, count);
        wallMaterialStamps[slot] = generation;
    }
    byte *materials = wallMaterials[slot];
    unsigned start = wy * f->w + wx;
    if (materials[start] >= 77)
        return materials[start];
    if (wallQueueSize < count) {
        unsigned *queue = SDL_malloc(count * sizeof(unsigned)),
                 *sources = SDL_malloc(count * sizeof(unsigned));
        if (!queue || !sources) {
            SDL_free(queue);
            SDL_free(sources);
            return 77;
        }
        SDL_free(wallQueue);
        SDL_free(wallSources);
        wallQueue = queue;
        wallSources = sources;
        wallQueueSize = count;
    }
    unsigned head = 0, tail = 1, seeds = 0;
    wallQueue[0] = start;
    materials[start] = 2;
    while (head < tail) {
        unsigned index = wallQueue[head++];
        int x = index % f->w, y = index / f->w;
        TypeDef d;
        unsigned id = typeAtLevel(x - D_5896_map_x, y - D_5897_map_y, level);
        if (TYPES_Get(id, &d) && (d.base == 77 || d.base == 78 || d.base == 79)) {
            materials[index] = d.base == 78 ? 79 : d.base;
            wallSources[seeds++] = index;
        }
        for (int dir = 0; dir < 4; dir++) {
            int nx = x + (dir == 0) - (dir == 1), ny = y + (dir == 2) - (dir == 3);
            if (!D_5893_map_id) {
                nx = MAPS_Wrap(nx, f->w);
                ny = MAPS_Wrap(ny, f->h);
            }
            if (nx < 0 || ny < 0 || nx >= f->w || ny >= f->h)
                continue;
            unsigned n = ny * f->w + nx;
            if (materials[n])
                continue;
            TypeDef neighbor;
            unsigned type = typeAtLevel(nx - D_5896_map_x, ny - D_5897_map_y, level);
            bool wall = TYPES_Get(type, &neighbor) &&
                        ((neighbor.render & TYPES_MASONRY) || neighbor.base == 134 ||
                         neighbor.base == 135) &&
                        !TILESET_3DSplit(art(type, neighbor.base)) &&
                        (coverageAt(f, nx - D_5896_map_x, ny - D_5897_map_y) & FLOOR3D_GEOMETRY);
            materials[n] = wall ? 2 : 1;
            if (wall)
                wallQueue[tail++] = n;
        }
    }
    if (!seeds) {
        for (unsigned i = 0; i < tail; i++)
            materials[wallQueue[i]] = 77;
    } else {
        qsort(wallSources, seeds, sizeof(unsigned), compareWallIndex);
        head = 0;
        unsigned end = seeds;
        while (head < end) {
            unsigned n = wallSources[head++];
            int x = n % f->w, y = n / f->w;
            for (int dir = 0; dir < 4; dir++) {
                int nx = x + (dir == 0) - (dir == 1), ny = y + (dir == 2) - (dir == 3);
                if (!D_5893_map_id) {
                    nx = MAPS_Wrap(nx, f->w);
                    ny = MAPS_Wrap(ny, f->h);
                }
                if (nx < 0 || ny < 0 || nx >= f->w || ny >= f->h)
                    continue;
                unsigned next = ny * f->w + nx;
                if (materials[next] == 2) {
                    materials[next] = materials[n];
                    wallSources[end++] = next;
                }
            }
        }
    }
    return materials[start] >= 77 ? materials[start] : 77;
}
/* These outdoor fixtures are copied into upper 2D views, never extra stories. */
static bool groundOnly(unsigned tile) {
    switch (tile) {
    case 137:
    case 160:
    case 162:
    case 164:
    case 189:
    case 202:
    case 203:
    case 224:
    case 225:
    case 226:
    case 227:
        return true;
    default:
        return false;
    }
}
static bool signFace(unsigned tile) { return (tile >= 236 && tile <= 247) || tile == 249; }
static unsigned objectType(int x, int y, int level) {
    Floor *f = findFloor(level);
    int wx, wy;
    mapPoint(x, y, &wx, &wy);
    if (!f || wx < 0 || wy < 0 || wx >= f->w || wy >= f->h)
        return 255;
    TypeDef d;
    return TYPES_Get(typeAtLevel(x, y, level), &d) ? d.base : 255;
}
typedef struct {
    int x, y, level, height, side;
    unsigned pole;
} SignLayout;
static bool signLayout(int x, int y, int level, SignLayout *out) {
    Floor *f = findFloor(level);
    if (!f)
        return false;
    unsigned tile = objectType(x, y, level);
    if (signFace(tile)) {
        if (objectType(x - 1, y, level) == 225 || objectType(x - 1, y, level) == 227)
            x--;
        else if (objectType(x + 1, y, level) == 226 || objectType(x + 1, y, level) == 227)
            x++;
        else if (objectType(x, y + 1, level) != 224 && objectType(x, y + 1, level) != 227)
            return false;
        else
            y++;
    } else if (tile != 224 && tile != 225 && tile != 226 && tile != 227)
        return false;
    unsigned pole = objectType(x, y, level) == 227 ? 227 : 224;
    int steps = 0;
    while (steps++ < f->h &&
           (objectType(x, y + 1, level) == 224 || objectType(x, y + 1, level) == 227))
        y++;
    if (steps > f->h || (objectType(x, y, level) != 224 && objectType(x, y, level) != 227))
        return false;
    pole = objectType(x, y, level);
    for (int k = 1; k < f->h; k++) {
        unsigned t = objectType(x, y - k, level);
        if (signFace(t)) {
            *out = (SignLayout){x, y, level, k + 1, 0, pole};
            return true;
        }
        int side = t == 225 ? 1 : t == 226 ? -1 : 0;
        if (t == 227) {
            if (signFace(objectType(x + 1, y - k, level)))
                side = 1;
            else if (signFace(objectType(x - 1, y - k, level)))
                side = -1;
        }
        if (side && signFace(objectType(x + side, y - k, level))) {
            *out = (SignLayout){x, y, level, k + 1, side, pole};
            return true;
        }
        if (t != 224 && t != 227)
            return false;
    }
    return false;
}
#define SIGN_STEM_ONLY 0x80000000u
static Uint32 signPiecePixel(const unsigned *pieces, int columns, int rows, int x, int y) {
    if (x < 0 || y < 0 || x >= columns * 16 || y >= rows * 16)
        return 0;
    unsigned tile = pieces[(y / 16) * columns + x / 16];
    if (tile == ~0u)
        return 0;
    x %= 16;
    y %= 16;
    if (tile & SIGN_STEM_ONLY) {
        tile &= ~SIGN_STEM_ONLY;
        y = SDL_min(y, 11);
    }
    return TILESET_3DSplit(tile) ? TILESET_3DPixel(tile, x, y, true)
                                 : uprightPixel(tile, x, y, 0, 0);
}
static int signTexture(const SignLayout *s) {
    int columns = s->side ? 2 : 1, rows = s->height;
    unsigned *pieces = SDL_malloc((size_t)columns * rows * sizeof(unsigned));
    if (!pieces)
        return -1;
    for (int i = 0; i < columns * rows; i++)
        pieces[i] = ~0u;
    int poleColumn = s->side < 0 ? 1 : 0;
    for (int k = 0; k < rows; k++) {
        unsigned id = typeAtLevel(s->x, s->y - k, s->level);
        TypeDef d;
        if (!TYPES_Get(id, &d))
            continue;
        unsigned tile = art(id, d.base);
        if (d.base == 227 && !TILESET_3DSplit(tile) && !TYPES_ArtSlot(tile)) {
            tile = art(224, 224);
            if (k)
                tile |= SIGN_STEM_ONLY;
        } else if (d.base == 224 && k && !TYPES_ArtSlot(tile))
            tile |= SIGN_STEM_ONLY;
        pieces[(rows - 1 - k) * columns + poleColumn] = tile;
    }
    if (s->side) {
        unsigned id = typeAtLevel(s->x + s->side, s->y - rows + 1, s->level);
        TypeDef d;
        if (TYPES_Get(id, &d))
            pieces[s->side > 0 ? 1 : 0] = art(id, d.base);
    }
    int width = columns * 32 + 4, height = rows * 32 + 4;
    Uint32 *pixels = SDL_calloc((size_t)width * height, sizeof(Uint32));
    if (!pixels) {
        SDL_free(pieces);
        return -1;
    }
    int inkTop = rows * 32, inkBottom = 0;
    bool transparency = GRAP_BUF_TransparentSprites();
    GRAP_BUF_SetTransparentSprites(true);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            int px = x / 2 - 1, py = y / 2 - 1;
            Uint32 color = signPiecePixel(pieces, columns, rows, px, py);
            if (color >> 24) {
                inkTop = SDL_min(inkTop, y - 2);
                inkBottom = SDL_max(inkBottom, y - 1);
            }
            if (!(color >> 24) && TILESET_SpriteOutline())
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if (signPiecePixel(pieces, columns, rows, px + dx, py + dy) >> 24)
                            color = 0xff000000;
            pixels[y * width + x] = color;
        }
    GRAP_BUF_SetTransparentSprites(transparency);
    SDL_free(pieces);
    if (textureCount == textureCapacity) {
        int cap = textureCapacity ? textureCapacity * 2 : 32;
        Texture *next = SDL_realloc(textures, (size_t)cap * sizeof(*textures));
        if (!next) {
            SDL_free(pixels);
            return -1;
        }
        textures = next;
        textureCapacity = cap;
    }
    int index = textureCount++;
    textures[index] = (Texture){.mode = 4,
                                .w = columns * 32,
                                .h = rows * 32,
                                .pad = 2,
                                .inkTop = inkBottom > inkTop ? inkTop : 0,
                                .inkBottom = inkBottom > inkTop ? inkBottom : rows * 32,
                                .next = -1,
                                .largePixels = pixels};
    return index;
}
static void addSign(const SignLayout *s) {
    int x, y;
    mapPoint(s->x, s->y, &x, &y);
    for (int i = 0; i < drawnSignCount; i++)
        if (drawnSigns[i].x == x && drawnSigns[i].y == y && drawnSigns[i].level == s->level)
            return;
    Floor *f = findFloor(s->level);
    if (!(coverageAt(f, s->x, s->y) & FLOOR3D_GEOMETRY))
        return;
    if (drawnSignCount == drawnSignCapacity) {
        int cap = drawnSignCapacity ? drawnSignCapacity * 2 : 16;
        SignKey *next = SDL_realloc(drawnSigns, (size_t)cap * sizeof(*next));
        if (!next)
            return;
        drawnSigns = next;
        drawnSignCapacity = cap;
    }
    drawnSigns[drawnSignCount++] = (SignKey){x, y, s->level};
    int previous = spriteCount;
    addSprite(art(224, 224), s->x + .5f, s->y + .5f, s->x, s->y, s->level, false, false);
    if (spriteCount == previous)
        return;
    int index = signTexture(s);
    if (index < 0) {
        spriteCount--;
        return;
    }
    sprites[spriteCount - 1].texture = index;
    sprites[spriteCount - 1].lift = spriteLift(224, &textures[index]);
    sprites[spriteCount - 1].offset = s->side * .5f;
}
static bool bookshelf(unsigned tile) { return tile == 92 || tile == 93; }
static bool table(unsigned tile) {
    return (tile >= 148 && tile <= 150) || (tile >= 154 && tile <= 156) || tile == 190;
}
static void objectQuad(float x, float y, float z, float ux, float uy, float uz, float vx, float vy,
                       float vz, int tex, float crop, int under, int dx, int dy, int level) {
    if (objectQuadCount == objectQuadCapacity) {
        int cap = objectQuadCapacity ? objectQuadCapacity * 2 : 64;
        ObjectQuad *next = SDL_realloc(objectQuads, (size_t)cap * sizeof(*next));
        if (!next)
            return;
        objectQuads = next;
        objectQuadCapacity = cap;
    }
    objectQuads[objectQuadCount++] =
        (ObjectQuad){x, y, z, ux, uy, uz, vx, vy, vz, crop, tex, under, dx, dy, level};
}
static void furnitureLeg(float x, float y, float z, int wood, int dx, int dy, int level) {
    objectQuad(x, y, z + .58f, .07f, 0, 0, 0, 0, -.58f, wood, 1, -1, dx, dy, level);
    objectQuad(x, y + .07f, z + .58f, .07f, 0, 0, 0, 0, -.58f, wood, 1, -1, dx, dy, level);
    objectQuad(x, y, z + .58f, 0, .07f, 0, 0, 0, -.58f, wood, 1, -1, dx, dy, level);
    objectQuad(x + .07f, y, z + .58f, 0, .07f, 0, 0, 0, -.58f, wood, 1, -1, dx, dy, level);
}
static void addFurniture(int x, int y, int level, unsigned tile, unsigned type) {
    bool shelf = bookshelf(type);
    int face = texture(tile, 5), wood = texture(art(64, 64), 0);
    float z = STORY_HEIGHT * level, back = y + (shelf ? .33f : .1f), depth = shelf ? .34f : .8f;
    float height = shelf ? 1 : .68f, top = shelf ? 1 : .68f;
    bool joinLeft = shelf ? type == 93 && objectType(x - 1, y, level) == 92
                          : table(objectType(x - 1, y, level));
    bool joinRight = shelf ? type == 92 && objectType(x + 1, y, level) == 93
                           : table(objectType(x + 1, y, level));
    objectQuad(x, back + depth, z + height, 1, 0, 0, 0, 0, -height, face, 1, -1, x, y, level);
    objectQuad(x, back, z + height, 1, 0, 0, 0, 0, -height, face, 1, -1, x, y, level);
    objectQuad(x, back, z + top, 1, 0, 0, 0, depth, 0, shelf ? wood : face, shelf ? 1 : .625f,
               shelf ? -1 : wood, x, y, level);
    float sideHeight = shelf ? height : .1f;
    if (!joinLeft) {
        objectQuad(x, back, z + top, 0, depth, 0, 0, 0, -sideHeight, wood, 1, -1, x, y, level);
        if (!shelf) {
            furnitureLeg(x + .06f, back + .06f, z, wood, x, y, level);
            furnitureLeg(x + .06f, back + depth - .13f, z, wood, x, y, level);
        }
    }
    if (!joinRight) {
        objectQuad(x + 1, back, z + top, 0, depth, 0, 0, 0, -sideHeight, wood, 1, -1, x, y, level);
        if (!shelf) {
            furnitureLeg(x + .87f, back + .06f, z, wood, x, y, level);
            furnitureLeg(x + .87f, back + depth - .13f, z, wood, x, y, level);
        }
    }
}
static Cell *cellAt(int dx, int dy, int level) {
    if (!cellCapacity || (cellCount + 1) * 2 >= cellCapacity)
        if (!growCells())
            return NULL;
    size_t p = hashCell(dx, dy, level) & (cellCapacity - 1);
    while (cells[p].stamp == generation) {
        if (cells[p].x == dx && cells[p].y == dy && cells[p].level == level)
            return &cells[p];
        p = (p + 1) & (cellCapacity - 1);
    }
    Cell *c = &cells[p];
    memset(c, 0, sizeof(*c));
    c->stamp = generation;
    c->x = dx;
    c->y = dy;
    c->level = level;
    c->surface = c->ground = -1;
    cellCount++;
    double distance = (double)dx * dx + (double)dy * dy;
    c->visible = D_58a5 >= 50 || distance <= D_58a5;
    if (!D_58a5)
        c->visible = dx == 0 && dy == 0;
    if (!c->visible)
        return c;
    unsigned id = typeAt(dx, dy, level);
    TypeDef d;
    if (!TYPES_Get(id, &d) || d.base == 255) {
        c->visible = false;
        return c;
    }
    Floor *f = findFloor(level);
    unsigned coverage = coverageAt(f, dx, dy);
    SignLayout sign;
    bool groupedSign = signLayout(dx, dy, level, &sign);
    c->empty = !(coverage & FLOOR3D_GEOMETRY) || (level > 0 && groundOnly(d.base) && !groupedSign);
    c->opening = !!(coverage & FLOOR3D_OPENING);
    if (!c->empty) {
        c->arch = d.base == 135 || d.base == 134;
        c->grated = c->arch && (d.base == 134 || D_587f < 5 || D_587f >= 20);
        unsigned tile = art(id, d.base);
        if (d.base == 227 && !TILESET_3DSplit(tile) && !TYPES_ArtSlot(tile))
            tile = art(224, 224);
        bool split = TILESET_3DSplit(tile);
        c->wall = !split && (c->arch || !!(d.render & TYPES_MASONRY));
        c->opaque = d.sight == 1 || (d.sight == 2 && distance == 1);
        c->surface =
            texture(c->arch ? art(c->grated ? 134 : 135, c->grated ? 134 : 135) : tile, false);
        unsigned upper = c->arch ? 135 : c->wall ? upperWallMaterial(dx, dy, level, &d) : 77;
        c->upperSurface = texture(art(upper, upper), false);
        unsigned ground = (c->wall || uprightTerrain(&d)) ? floorFor(dx, dy, level, &d) : id;
        TypeDef floor;
        if (TYPES_Get(ground, &floor))
            c->ground = texture(art(ground, floor.base), false);
        if (split)
            c->ground = texture(tile, 2);
        if (groupedSign) {
            c->wall = c->opaque = c->opening = false;
            if (dx != sign.x || dy != sign.y)
                c->ground = texture(art(5, 5), 0);
            addSign(&sign);
        } else if (bookshelf(d.base) || table(d.base)) {
            c->wall = c->opaque = false;
            if (!split) {
                TypeDef underneath;
                unsigned ground = floorFor(dx, dy, level, &d);
                if (TYPES_Get(ground, &underneath))
                    c->ground = texture(art(ground, underneath.base), 0);
            }
            addFurniture(dx, dy, level, tile, d.base);
        } else if (!c->wall && (uprightTerrain(&d) || split))
            addSprite(tile, dx + 0.5f, dy + 0.5f, dx, dy, level, d.base == 0xaa || d.base == 0x86,
                      c->opening && d.base == 0x86);
        if (c->opening && !uprightTerrain(&d) && !c->wall)
            addSprite(tile, dx + 0.5f, dy + 0.5f, dx, dy, level, true, true);
    }
    int wx, wy;
    mapPoint(dx, dy, &wx, &wy);
    int loot = level == currentLevel() ? CONTENTS_GAME_Shape(wx, wy)
                                       : CONTENTS_GAME_LocationShape(D_5893_map_id, level, wx, wy);
    if (loot)
        addSprite(256 + loot, dx + 0.5f, dy + 0.5f, dx, dy, level, false, false);
    return c;
}
static void sampleActor(ActorMotion *a, Uint64 now) {
    double t = a->moving ? SDL_min(1.0, (double)(now - a->started) / a->duration) : 1;
    a->xNow = a->fromX + (a->targetX - a->fromX) * t;
    a->yNow = a->fromY + (a->targetY - a->fromY) * t;
    if (t >= 1)
        a->moving = false;
}
bool WORLD3D_ActorPose(unsigned actor, float *x, float *y) {
    if (!actorsValid || actor >= (unsigned)actorCapacity || !actors[actor].valid)
        return false;
    *x = (float)actors[actor].xNow;
    *y = (float)actors[actor].yNow;
    return true;
}
static bool reserveActors(int count) {
    if (count <= actorCapacity)
        return true;
    ActorMotion *next = SDL_realloc(actors, (size_t)count * sizeof(*actors));
    if (!next)
        return false;
    memset(next + actorCapacity, 0, (count - actorCapacity) * sizeof(*actors));
    actors = next;
    actorCapacity = count;
    return true;
}
static unsigned spriteOnFloor(unsigned actor, int level) {
    if (level == currentLevel())
        return ACTORS_Sprite(actor, 256 + D_5c5a[actor]._1_animTile);
    ActorFmt *a = &D_5c5a[actor];
    if (a->_0_tile >= 64 && a->_1_animTile < 32)
        return 256 + a->_1_animTile;
    int dx = a->_2_x - D_5896_map_x, dy = a->_3_y - D_5897_map_y;
    TypeDef d;
    unsigned center = typeAtLevel(dx, dy, level), north = typeAtLevel(dx, dy - 1, level),
             south = typeAtLevel(dx, dy + 1, level);
    if (TYPES_Get(center, &d))
        center = d.base;
    if (TYPES_Get(north, &d))
        north = d.base;
    if (TYPES_Get(south, &d))
        south = d.base;
    bool reflection;
    int pose = ULTIMA_ResolveActorTile(a->_1_animTile, center, north, south, &reflection);
    if (pose < 0)
        return 0;
    unsigned type = ACTORS_Instance(actor);
    return type && ((pose & 252) == (a->_1_animTile & 252)) ? TYPES_ActorArt(type, 256 + pose)
                                                            : 256 + pose;
}
static void prepareActors(void) {
    Uint64 now = SDL_GetTicks();
    int count = MAP_ACTOR_COUNT;
    bool tracked = reserveActors(count);
    bool same =
        tracked && actorsValid && actorsMap == D_5893_map_id && actorsLevel == D_5895_map_level;
    actorsMoving = false;
    for (int i = 1; i < count; i++) {
        ActorFmt *a = &D_5c5a[i];
        ActorMotion *motion = tracked ? &actors[i] : NULL;
        if (!a->_0_tile || !a->_1_animTile || !findFloor((int8_t)a->_4_z)) {
            if (motion)
                motion->valid = motion->moving = false;
            continue;
        }
        int dx = a->_2_x - D_5896_map_x, dy = a->_3_y - D_5897_map_y;
        if (!D_5893_map_id) {
            dx = MAPS_Wrap(dx + MAP_WIDTH / 2, MAP_WIDTH) - MAP_WIDTH / 2;
            dy = MAPS_Wrap(dy + MAP_HEIGHT / 2, MAP_HEIGHT) - MAP_HEIGHT / 2;
        }
        bool sighted = a->_4_z == D_5895_map_level ? WIDE_Visible(dx, dy)
                                                   : (D_58a5 >= 50 || dx * dx + dy * dy <= D_58a5);
        float offsetX = 0, offsetY = 0;
        if (motion) {
            unsigned kind = ACTORS_Instance(i);
            if (!kind)
                kind = 256u + a->_0_tile;
            int ax = a->_2_x - motion->x, ay = a->_3_y - motion->y;
            if (!D_5893_map_id) {
                ax = MAPS_Wrap(ax + MAP_WIDTH / 2, MAP_WIDTH) - MAP_WIDTH / 2;
                ay = MAPS_Wrap(ay + MAP_HEIGHT / 2, MAP_HEIGHT) - MAP_HEIGHT / 2;
            }
            if (!same || !motion->valid || !GRAP_SDL_SmoothMovementEnabled() ||
                kind != motion->kind || motion->level != a->_4_z || !sighted || !motion->sighted ||
                abs(ax) > 1 || abs(ay) > 1) {
                motion->targetX = motion->fromX = a->_2_x;
                motion->targetY = motion->fromY = a->_3_y;
                motion->moving = false;
            } else {
                sampleActor(motion, now);
                if (ax || ay) {
                    motion->fromX = motion->xNow;
                    motion->fromY = motion->yNow;
                    motion->targetX += ax;
                    motion->targetY += ay;
                    motion->started = now;
                    motion->duration = SDL_max(16, GRAP_SDL_MovementInterval());
                    motion->moving = true;
                }
            }
            motion->valid = true;
            motion->kind = kind;
            motion->level = a->_4_z;
            motion->x = a->_2_x;
            motion->y = a->_3_y;
            motion->sighted = sighted;
            sampleActor(motion, now);
            actorsMoving |= motion->moving;
            offsetX = (float)(motion->xNow - motion->targetX);
            offsetY = (float)(motion->yNow - motion->targetY);
        }
        if (!sighted)
            continue;
        unsigned tile = spriteOnFloor(i, (int8_t)a->_4_z), w = 1, h = 1;
        if (!tile)
            continue;
        if (TYPES_ArtSlot(tile))
            TYPES_ArtSize(tile, D_5893_map_id == 0, &w, &h);
        addSprite(tile, dx + offsetX + w * 0.5f, dy + offsetY + h * 0.5f, dx, dy, (int8_t)a->_4_z,
                  a->_0_tile == TILE_ACTOR_CARPET || a->_0_tile == TILE_ACTOR_FLYING_CARPET ||
                      a->_0_tile == TILE_ACTOR_FLYING_CARPET + 1,
                  false);
    }
    /* Removed slots must not leave a tween alive or be reused as an old actor. */
    if (tracked && count < trackedActorCount)
        memset(actors + count, 0, (trackedActorCount - count) * sizeof(*actors));
    trackedActorCount = tracked ? count : 0;
    actorsValid = tracked;
    actorsMap = D_5893_map_id;
    actorsLevel = D_5895_map_level;
}
static void prepare(void) {
    for (int i = 0; i < textureCount; i++) {
        SDL_free(textures[i].largePixels);
        textures[i].largePixels = NULL;
    }
    objectQuadCount = drawnSignCount = 0;
    textureCount = spriteCount = 0;
    cellCount = 0;
    memset(textureBuckets, 255, sizeof(textureBuckets));
    if (++generation == 0) {
        if (cells)
            memset(cells, 0, cellCapacity * sizeof(*cells));
        generation = 1;
    }
    prepareFloors();
    prepareCoverage();
    prepareActors();
}

/* Sky colors are palette indices throughout: no RGB interpolation or shading.
 * The original light cycle has dawn at 05:00-06:00 and dusk at 19:00-20:00. */
typedef struct {
    int minute;
    byte zenith, middle, horizon;
} SkyKey;
/* Dawn and dusk stay within black, dark blue and daytime blue. */
static const SkyKey skyKeys[] = {{0, 0, 0, 0}, {300, 0, 0, 0}, {330, 1, 1, 1},
                                 {360, 9, 9, 9}, {1140, 9, 9, 9}, {1170, 1, 1, 1},
                                 {1200, 0, 0, 0}, {1440, 0, 0, 0}};
static const byte bayer[8][8] = {{0, 48, 12, 60, 3, 51, 15, 63}, {32, 16, 44, 28, 35, 19, 47, 31},
                                 {8, 56, 4, 52, 11, 59, 7, 55},  {40, 24, 36, 20, 43, 27, 39, 23},
                                 {2, 50, 14, 62, 1, 49, 13, 61}, {34, 18, 46, 30, 33, 17, 45, 29},
                                 {10, 58, 6, 54, 9, 57, 5, 53},  {42, 26, 38, 22, 41, 25, 37, 21}};
typedef struct {
    const SkyKey *a, *b;
    float blend;
    bool surface;
} Sky;
static Sky currentSky(void) {
    int minute = (D_587f % 24) * 60 + SDL_min(D_5881, 59);
    unsigned i = 0;
    while (minute >= skyKeys[i + 1].minute)
        i++;
    Sky sky = {&skyKeys[i], &skyKeys[i + 1],
               (minute - skyKeys[i].minute) / (float)(skyKeys[i + 1].minute - skyKeys[i].minute),
               D_5895_map_level <= 127 && D_5893_map_id != 0x19};
    if (sky.a->zenith == sky.b->zenith && sky.a->middle == sky.b->middle &&
        sky.a->horizon == sky.b->horizon)
        sky.blend = 0;
    return sky;
}
static void skyRow(const Sky *sky, int y, Uint32 row[8]) {
    if (!sky->surface || y >= viewH / 2) {
        for (int x = 0; x < 8; x++)
            row[x] = colors[0];
        return;
    }
    float vertical = (float)y / SDL_max(1, viewH / 2 - 1);
    bool upper = vertical < 0.65f;
    float blend = upper ? vertical / 0.65f : (vertical - 0.65f) / 0.35f;
    byte indices[4] = {
        upper ? sky->a->zenith : sky->a->middle, upper ? sky->a->middle : sky->a->horizon,
        upper ? sky->b->zenith : sky->b->middle, upper ? sky->b->middle : sky->b->horizon};
    float weights[3] = {(1 - sky->blend) * (1 - blend), (1 - sky->blend) * blend,
                        sky->blend * (1 - blend)};
    for (int x = 0; x < 8; x++) {
        float threshold = (bayer[(y / 2) & 7][x] + 0.5f) / 64;
        int color = 0;
        while (color < 3 && threshold >= weights[color])
            threshold -= weights[color++];
        row[x] = colors[indices[color]];
    }
}
/* Stable celestial directions: turning rotates the stars; walking never makes
 * them swim across the sky. Sparse palette dots stay small at every viewport size. */
static uint32_t starHash(uint32_t n) {
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    return n ^ (n >> 16);
}
static void drawStars(SDL_Surface *out, const Sky *sky, float focalX, float focalY) {
    if (!sky->surface)
        return;
    int minute = (D_587f % 24) * 60 + SDL_min(D_5881, 59);
    float visibility = minute < 300 || minute >= 1200 ? 1
                       : minute < 360                 ? (360 - minute) / 60.0f
                       : minute >= 1140               ? (minute - 1140) / 60.0f
                                                      : 0;
    if (visibility <= 0)
        return;
    for (unsigned i = 0; i < 192; i++) {
        uint32_t a = starHash(i + 1), b = starHash(a);
        if ((b & 65535) / 65536.0f >= visibility)
            continue;
        double angle = (a / 4294967296.0) * 4 * HALF_PI - camera.angleNow;
        float z = (float)cos(angle);
        if (z <= 0)
            continue;
        float elevation = 0.06f + 0.75f * ((b >> 16) / 65536.0f);
        float projectedX = viewW * 0.5f + focalX * (float)sin(angle) / z;
        float projectedY = viewH * 0.5f - focalY * elevation / z;
        /* Reject offscreen directions before converting near-tangent projections to integers. */
        if (projectedX <= -2 || projectedX >= viewW || projectedY <= -2 || projectedY >= viewH / 2)
            continue;
        int x = (int)floorf(projectedX), y = (int)floorf(projectedY);
        Uint32 color = colors[(a & 3) == 0 ? 15 : 7];
        for (int sy = 0; sy < 2; sy++)
            for (int sx = 0; sx < 2; sx++) {
                int px = x + sx, py = y + sy;
                if (px >= 0 && px < viewW && py >= 0 && py < viewH / 2)
                    ((Uint32 *)((byte *)out->pixels + (py + viewY) * out->pitch))[px + viewX] =
                        color;
            }
    }
}
static void pixel(SDL_Surface *out, int x, int y, Uint32 color, float z, int dx, int dy,
                  int level) {
    int i = y * viewW + x;
    if (z >= depth[i])
        return;
    depth[i] = z;
    floorDepth[i] = 0;
    pickX[i] = dx;
    pickY[i] = dy;
    pickFloor[i] = level;
    ((Uint32 *)((byte *)out->pixels + (y + viewY) * out->pitch))[x + viewX] = color;
}
void WORLD3D_Render(SDL_Surface *out, const Uint32 *palette) {
    WORLD3D_RenderRegion(out, palette, 16, 16, 352, 352, 1);
}
/* Axis-aligned furniture faces keep their map orientation. Their intersections
 * use the same camera and depth buffer as walls, actors and sign billboards. */
static void drawObjectQuads(SDL_Surface *out, float ox, float oy, float eye, float fx, float fy,
                            float rx, float ry, float focalX, float focalY) {
    for (int i = 0; i < objectQuadCount; i++) {
        ObjectQuad *q = &objectQuads[i];
        float nx = q->uy * q->vz - q->uz * q->vy, ny = q->uz * q->vx - q->ux * q->vz,
              nz = q->ux * q->vy - q->uy * q->vx;
        float numerator = nx * (q->x - ox) + ny * (q->y - oy) + nz * (q->z - eye);
        float uu = q->ux * q->ux + q->uy * q->uy + q->uz * q->uz;
        float vv = q->vx * q->vx + q->vy * q->vy + q->vz * q->vz;
        if (uu < 1e-8f || vv < 1e-8f)
            continue;
        float minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
        bool clipped = false, front = false;
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++) {
                float x = q->x + a * q->ux + b * q->vx - ox, y = q->y + a * q->uy + b * q->vy - oy;
                float z = x * fx + y * fy;
                if (z < .05f) {
                    clipped = true;
                    continue;
                }
                front = true;
                float sx = viewW * .5f + focalX * (x * rx + y * ry) / z;
                float sy = viewH * .5f + focalY * (eye - q->z - a * q->uz - b * q->vz) / z;
                minX = SDL_min(minX, sx);
                maxX = SDL_max(maxX, sx);
                minY = SDL_min(minY, sy);
                maxY = SDL_max(maxY, sy);
            }
        if (!front)
            continue;
        if (clipped) {
            minX = minY = 0;
            maxX = viewW;
            maxY = viewH;
        }
        /* Clamp before converting: near-plane projections can exceed int range. */
        int left = (int)floorf(SDL_clamp(minX, 0, viewW)),
            right = (int)ceilf(SDL_clamp(maxX, 0, viewW));
        int top = (int)floorf(SDL_clamp(minY, 0, viewH)),
            bottom = (int)ceilf(SDL_clamp(maxY, 0, viewH));
        for (int y = top; y < bottom; y++)
            for (int x = left; x < right; x++) {
                float across = (x + .5f - viewW * .5f) / focalX;
                float dx = fx + rx * across, dy = fy + ry * across,
                      dz = (viewH * .5f - y - .5f) / focalY;
                float denominator = nx * dx + ny * dy + nz * dz;
                if (fabsf(denominator) < 1e-8f)
                    continue;
                float z = numerator / denominator;
                if (z < .05f || z >= depth[y * viewW + x])
                    continue;
                float px = ox + dx * z - q->x, py = oy + dy * z - q->y, pz = eye + dz * z - q->z;
                float u = (px * q->ux + py * q->uy + pz * q->uz) / uu,
                      v = (px * q->vx + py * q->vy + pz * q->vz) / vv;
                if (u < 0 || v < 0 || u >= 1 || v >= 1)
                    continue;
                Uint32 color = sample(q->texture, u, v * q->crop);
                if (!(color >> 24) && q->underneath >= 0)
                    color = sample(q->underneath, u, v);
                if (color >> 24) {
                    pixel(out, x, y, shade(color, z, fabsf(nx) > fabsf(ny)), z, q->dx, q->dy,
                          q->level);
                    if (q->ground)
                        floorDepth[y * viewW + x] = 1;
                }
            }
    }
}
void WORLD3D_RenderRegion(SDL_Surface *out, const Uint32 *palette, int left, int top, int width,
                          int height, float aspect) {
    if (!WORLD3D_Active() || width < 1 || height < 1 || left < 0 || top < 0 ||
        left > out->w - width || top > out->h - height || !isfinite(aspect) || aspect <= 0 ||
        !reservePixels(width, height)) {
        pickValid = false;
        return;
    }
    colors = palette;
    viewX = left;
    viewY = top;
    viewW = width;
    viewH = height;
    updateCamera();
    prepare();
    float ox = 0.5f + (float)(camera.xNow - camera.targetX),
          oy = 0.5f + (float)(camera.yNow - camera.targetY);
    float fx = (float)sin(camera.angleNow), fy = -(float)cos(camera.angleNow), rx = -fy, ry = fx,
          focalY = viewH * 0.78f, focalX = focalY * aspect;
    Sky sky = currentSky();
    for (int y = 0; y < viewH; y++) {
        Uint32 background[8];
        skyRow(&sky, y, background);
        for (int x = 0; x < viewW; x++) {
            size_t i = (size_t)y * viewW + x;
            depth[i] = INFINITY;
            floorDepth[i] = 0;
            pickX[i] = pickY[i] = 0;
            pickFloor[i] = currentLevel();
            ((Uint32 *)((byte *)out->pixels + (y + top) * out->pitch))[x + left] =
                background[(x / 2) & 7];
        }
    }
    drawStars(out, &sky, focalX, focalY);
    float eye = STORY_HEIGHT * currentLevel() + EYE_HEIGHT;
    int ceilingSurface = texture(art(72, 72), false);
    /* Horizontal floors, ceilings and cube caps share the same depth buffer.
     * Upper-floor background is open air, not a sheet spanning the whole town. */
    int activeFloor = 0;
    for (int f = 0; f < floorCount; f++)
        if (floors[f].level == currentLevel())
            activeFloor = f;
    for (int step = 0; step < floorCount; step++)
        for (int cap = 0; cap < 2; cap++) {
            int f = (activeFloor + step) % floorCount;
            int level = floors[f].level;
            float vertical = eye - (STORY_HEIGHT * level + cap * STORY_HEIGHT);
            for (int y = 0; y < viewH; y++) {
                float denominator = y + 0.5f - viewH * 0.5f;
                if (fabsf(denominator) < 1e-6f)
                    continue;
                float z = vertical * focalY / denominator;
                if (z < 0.05f)
                    continue;
                for (int x = 0; x < viewW; x++) {
                    if (z >= depth[y * viewW + x])
                        continue;
                    float offset = (x + 0.5f - viewW * 0.5f) / focalX;
                    float wx = ox + z * (fx + rx * offset), wy = oy + z * (fy + ry * offset);
                    int cx = (int)floorf(wx), cy = (int)floorf(wy);
                    if (D_5893_map_id &&
                        (D_5896_map_x + cx < 0 || D_5897_map_y + cy < 0 ||
                         D_5896_map_x + cx >= floors[f].w || D_5897_map_y + cy >= floors[f].h))
                        continue;
                    Cell *c = cellAt(cx, cy, level);
                    if (!c || !c->visible || c->empty || (cap && !c->wall) ||
                        (!cap && !c->wall && c->opening))
                        continue;
                    if (c->arch && cap)
                        continue;
                    int ground =
                        c->wall && !c->arch ? (cap ? c->upperSurface : c->surface) : c->ground;
                    if (vertical < 0)
                        ground = ceilingSurface;
                    pixel(out, x, y,
                          shade(sample(ground, wx - floorf(wx), wy - floorf(wy)), z, false), z, cx,
                          cy, level);
                    if (!c->wall)
                        floorDepth[y * viewW + x] = 1;
                }
            }
        }
    for (int f = 0; f < floorCount; f++)
        for (int x = 0; x < viewW; x++) {
            int level = floors[f].level;
            float offset = (x + 0.5f - viewW * 0.5f) / focalX, dx = fx + rx * offset,
                  dy = fy + ry * offset;
            float sx = fabsf(dx) < 1e-8f ? INFINITY : fabsf(1 / dx),
                  sy = fabsf(dy) < 1e-8f ? INFINITY : fabsf(1 / dy);
            int cx = (int)floorf(ox), cy = (int)floorf(oy), stepX = dx < 0 ? -1 : 1,
                stepY = dy < 0 ? -1 : 1;
            float tx = isfinite(sx) ? (dx < 0 ? ox - cx : cx + 1 - ox) * sx : INFINITY;
            float ty = isfinite(sy) ? (dy < 0 ? oy - cy : cy + 1 - oy) * sy : INFINITY, z = 0;
            /* Stop only beyond the viewport's last possible vertical sample. */
            float limit = focalY * 2 *
                          SDL_max(fabsf(eye - STORY_HEIGHT * level),
                                  fabsf(eye - STORY_HEIGHT * level - STORY_HEIGHT));
            bool side = false;
            while (z <= limit) {
                if (tx < ty) {
                    z = tx;
                    tx += sx;
                    cx += stepX;
                    side = false;
                } else {
                    z = ty;
                    ty += sy;
                    cy += stepY;
                    side = true;
                }
                if (z > limit)
                    break;
                Cell *c = cellAt(cx, cy, level);
                if (!c)
                    break;
                float front = SDL_max(z, 0.001f);
                if (!c->visible || c->empty || (!c->wall && c->opaque)) {
                    if (level == currentLevel() && !c->empty) {
                        int curtainTop =
                            c->visible
                                ? SDL_clamp(
                                      (int)ceilf(viewH * 0.5f +
                                                 focalY *
                                                     (eye - STORY_HEIGHT * level - STORY_HEIGHT) /
                                                     front -
                                                 0.5f),
                                      0, viewH)
                                : viewH / 2;
                        for (int y = curtainTop; y < viewH; y++) {
                            int i = y * viewW + x;
                            if (c->visible && pickFloor[i] == level && pickX[i] == cx &&
                                pickY[i] == cy)
                                continue;
                            if (z < depth[i]) {
                                Uint32 background =
                                    y < viewH / 2 ? ((Uint32 *)((byte *)out->pixels +
                                                                (y + top) * out->pitch))[x + left]
                                                  : colors[0];
                                pixel(out, x, y, background, c->visible ? SDL_min(tx, ty) : z, cx,
                                      cy, level);
                            }
                        }
                    }
                    if (!c->visible || (!c->empty && c->opaque))
                        break;
                    continue;
                }
                if (!c->wall)
                    continue;
                float hit = side ? ox + dx * z : oy + dy * z, u = hit - floorf(hit);
                if ((side && dy > 0) || (!side && dx < 0))
                    u = 1 - u;
                for (int band = 0; band < 2; band++) {
                    if (c->arch && !band && !c->grated)
                        continue;
                    float base = STORY_HEIGHT * level + band;
                    float topY = viewH * 0.5f + focalY * (eye - base - 1) / front;
                    float bottom = viewH * 0.5f + focalY * (eye - base) / front;
                    int surface = band ? c->upperSurface : c->surface;
                    for (int y = SDL_max(0, (int)ceilf(topY - 0.5f));
                         y < SDL_min(viewH, (int)ceilf(bottom - 0.5f)); y++) {
                        Uint32 color = sample(surface, u, (y + 0.5f - topY) / (bottom - topY));
                        if ((color >> 24) && (!c->arch || (color & 0xffffff)))
                            pixel(out, x, y, shade(color, z, side), z, cx, cy, level);
                    }
                }
                /* Arch and grate holes must reveal farther geometry. */
                if (!c->arch && eye >= STORY_HEIGHT * level &&
                    eye <= STORY_HEIGHT * level + STORY_HEIGHT)
                    break;
            }
        }
    drawObjectQuads(out, ox, oy, eye, fx, fy, rx, ry, focalX, focalY);
    for (int i = 0; i < spriteCount; i++) {
        Sprite *s = &sprites[i];
        Texture *t = &textures[s->texture];
        if (s->topOnly && eye <= STORY_HEIGHT * s->level)
            continue;
        if (s->flat) {
            float w = (t->w + 2 * t->pad) / 32.0f, h = (t->h + 2 * t->pad) / 32.0f;
            float left = s->x - w * 0.5f, top = s->y - h * 0.5f;
            for (int y = 0; y < viewH; y++) {
                float denom = y + 0.5f - viewH * 0.5f;
                if (fabsf(denom) < 1e-6f)
                    continue;
                float z = (eye - STORY_HEIGHT * s->level - 0.002f) * focalY / denom;
                if (z < 0.05f)
                    continue;
                for (int x = 0; x < viewW; x++) {
                    if (z >= depth[y * viewW + x])
                        continue;
                    float offset = (x + 0.5f - viewW * 0.5f) / focalX;
                    float wx = ox + z * (fx + rx * offset), wy = oy + z * (fy + ry * offset);
                    if (wx < left || wx >= left + w || wy < top || wy >= top + h)
                        continue;
                    Uint32 color = sample(s->texture, (wx - left) / w, (wy - top) / h);
                    if (color >> 24)
                        pixel(out, x, y, shade(color, z, false), z, s->dx, s->dy, s->level);
                }
            }
            continue;
        }
        float dx = s->x - ox, dy = s->y - oy, z = dx * fx + dy * fy,
              across = dx * rx + dy * ry + s->offset;
        if (z < 0.05f)
            continue;
        float w = focalX * ((t->w + 2 * t->pad) / 32.0f) / z,
              h = focalY * ((t->h + 2 * t->pad) / 32.0f) / z,
              l = viewW * 0.5f + focalX * across / z - w * 0.5f,
              bottom = viewH * 0.5f +
                       focalY * (eye - STORY_HEIGHT * s->level - s->lift + t->pad / 32.0f) / z,
              topY = bottom - h;
        for (int y = SDL_max(0, (int)ceilf(topY - 0.5f));
             y < SDL_min(viewH, (int)ceilf(bottom - 0.5f)); y++)
            for (int x = SDL_max(0, (int)ceilf(l - 0.5f));
                 x < SDL_min(viewW, (int)ceilf(l + w - 0.5f)); x++) {
                Uint32 color = sample(s->texture, (x + 0.5f - l) / w, (y + 0.5f - topY) / h);
                if (color >> 24) {
                    /* The outline below the feet overlays ground, just as in 2D.
                     * Walls and other sprites retain their normal depth priority. */
                    if (t->pad &&
                        y + 0.5f >= bottom - focalY * (t->h + t->pad - t->inkBottom) / 32.0f / z &&
                        floorDepth[y * viewW + x])
                        depth[y * viewW + x] = INFINITY;
                    pixel(out, x, y, shade(color, z, false), z, s->dx, s->dy, s->level);
                }
            }
    }
    const byte *font = D_539c[0] ? D_539c[0] : D_5398_currentCharset;
    if (font)
        for (int y = 0; y < SDL_min(20, viewH - 4); y++)
            for (int x = 0; x < SDL_min(20, viewW - 4); x++) {
                bool ink = x >= 2 && x < 18 && y >= 2 && y < 18 &&
                           (font[(unsigned char)"NESW"[facing] * 8 + (y - 2) / 2] &
                            (0x80 >> ((x - 2) / 2)));
                ((Uint32 *)((byte *)out->pixels + (y + top + 4) * out->pitch))[x + left + 4] =
                    ink ? 0xffffffff : 0xff000000;
            }
    pickValid = true;
    pickMap = D_5893_map_id;
    pickLevel = D_5895_map_level;
    pickWorldX = D_5896_map_x;
    pickWorldY = D_5897_map_y;
    pickFacing = facing;
}
bool WORLD3D_Pick(float u, float v, int *dx, int *dy) {
    if (!pickValid || camera.moving || pickMap != D_5893_map_id || pickLevel != D_5895_map_level ||
        pickWorldX != D_5896_map_x || pickWorldY != D_5897_map_y || pickFacing != facing ||
        !isfinite(u) || !isfinite(v) || u < 0 || v < 0 || u >= 1 || v >= 1)
        return false;
    int i = (int)(v * viewH) * viewW + (int)(u * viewW);
    if (!isfinite(depth[i]))
        return false;
    *dx = pickX[i];
    *dy = pickY[i];
    if (pickFloor[i] != currentLevel())
        return false;
    Cell *c = cellAt(*dx, *dy, currentLevel());
    return c && c->visible;
}
