#include "world3d.h"
#include "event/event.h"
#include "grap_buf.h"
#include "grap_sdl.h"
#include "macros.h"
#include "mod/actors.h"
#include "mod/contents_game.h"
#include "mod/types_game.h"
#include "tileset.h"
#include "vars.h"
#include "widescreen.h"
#include <math.h>
#include <string.h>

#define HALF_PI 1.57079632679489661923
#define BUCKETS 1024
static bool enabled, sceneView, pickValid;
static int facing;
static const int compass[] = {U5_KEY_UP, U5_KEY_RIGHT, U5_KEY_DOWN, U5_KEY_LEFT};
static unsigned pickMap;
static int pickLevel, pickWorldX, pickWorldY, pickFacing;
static int viewX, viewY, viewW, viewH;
static float *depth;
static int *pickX, *pickY;
static size_t pixelCapacity;
typedef struct {
    unsigned tile;
    bool sprite;
    unsigned w, h;
    int next;
    Uint32 pixels[64 * 64];
} Texture;
static Texture *textures;
static int textureCount, textureCapacity, textureBuckets[BUCKETS];
typedef struct {
    unsigned stamp;
    int x, y;
    bool visible, wall, opaque;
    int surface, ground;
} Cell;
static Cell *cells;
static size_t cellCapacity, cellCount;
static unsigned generation;
typedef struct {
    float x, y;
    int texture, dx, dy;
} Sprite;
static Sprite *sprites;
static int spriteCount, spriteCapacity;
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
    SDL_free(depth);
    SDL_free(pickX);
    SDL_free(pickY);
    SDL_free(textures);
    SDL_free(sprites);
    SDL_free(cells);
    SDL_free(actors);
    actors = NULL;
    actorCapacity = trackedActorCount = 0;
    depth = NULL;
    pickX = pickY = NULL;
    textures = NULL;
    sprites = NULL;
    cells = NULL;
    pixelCapacity = cellCapacity = cellCount = 0;
    textureCapacity = spriteCapacity = 0;
    pickValid = false;
    resetMotion();
}
static bool reservePixels(int width, int height) {
    size_t count = (size_t)width * height;
    if (count <= pixelCapacity)
        return true;
    if (count > SIZE_MAX / sizeof(float) || count > SIZE_MAX / sizeof(int))
        return false;
    float *z = SDL_malloc(count * sizeof(float));
    int *x = SDL_malloc(count * sizeof(int)), *y = SDL_malloc(count * sizeof(int));
    if (!z || !x || !y) {
        SDL_free(z);
        SDL_free(x);
        SDL_free(y);
        return false;
    }
    SDL_free(depth);
    SDL_free(pickX);
    SDL_free(pickY);
    depth = z;
    pickX = x;
    pickY = y;
    pixelCapacity = count;
    return true;
}
static int texture(unsigned tile, bool sprite) {
    unsigned bucket = (tile * 2654435761u + (unsigned)sprite) & (BUCKETS - 1);
    for (int i = textureBuckets[bucket]; i >= 0; i = textures[i].next)
        if (textures[i].tile == tile && textures[i].sprite == sprite)
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
    t->tile = tile;
    t->sprite = sprite;
    t->w = t->h = 1;
    if (sprite && TYPES_ArtSlot(tile))
        TYPES_ArtSize(tile, D_5893_map_id == 0, &t->w, &t->h);
    t->w = SDL_clamp(t->w, 1, 2) * 32;
    t->h = SDL_clamp(t->h, 1, 2) * 32;
    bool transparency = GRAP_BUF_TransparentSprites();
    if (sprite)
        GRAP_BUF_SetTransparentSprites(true);
    for (unsigned y = 0; y < t->h; y++)
        for (unsigned x = 0; x < t->w; x++) {
            int c = sprite ? GRAP_BUF_SpritePixel(tile, x / 2, y / 2)
                           : GRAP_BUF_TilePixel(tile, x / 2, y / 2);
            t->pixels[y * 64 + x] =
                c < 0    ? 0
                : sprite ? TILESET_LastSpriteColor(c, x & 1, y & 1, colors,
                                                   GRAP_BUF_SpriteIsTransparent(tile))
                         : TILESET_LastColor(c, x & 1, y & 1, colors);
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
    int x = SDL_clamp((int)(u * t->w), 0, (int)t->w - 1),
        y = SDL_clamp((int)(v * t->h), 0, (int)t->h - 1);
    return t->pixels[y * 64 + x];
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
static unsigned typeAt(int dx, int dy) {
    int x, y;
    mapPoint(dx, dy, &x, &y);
    return TYPES_GAME_At(x, y);
}
static unsigned art(unsigned id, byte base) {
    return TYPES_ArtSlot(id) ? TYPES_ActorArt(id, D_b11e[base]) : D_b11e[base];
}
static unsigned floorFor(int dx, int dy, const TypeDef *d) {
    if (TYPES_Defined(d->id) && d->ground != TYPES_INFER_GROUND)
        return d->ground;
    unsigned candidates[4];
    int best = -1, votes = 0;
    const int nx[] = {0, 0, 1, -1}, ny[] = {1, -1, 0, 0};
    for (int i = 0; i < 4; i++)
        candidates[i] = typeAt(dx + nx[i], dy + ny[i]);
    for (int i = 0; i < 4; i++) {
        TypeDef n;
        if (!TYPES_Get(candidates[i], &n) || (n.render & (TYPES_SPRITE | TYPES_MASONRY)))
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
static void addSprite(unsigned tile, float x, float y, int dx, int dy) {
    if (dx == 0 && dy == 0)
        return;
    if (spriteCount == spriteCapacity) {
        int capacity = spriteCapacity ? spriteCapacity * 2 : 32;
        Sprite *next = SDL_realloc(sprites, (size_t)capacity * sizeof(*sprites));
        if (!next)
            return;
        sprites = next;
        spriteCapacity = capacity;
    }
    int index = texture(tile, true);
    if (index >= 0)
        sprites[spriteCount++] = (Sprite){x, y, index, dx, dy};
}
static size_t hashCell(int x, int y) {
    return (unsigned)x * 2654435761u ^ (unsigned)y * 2246822519u;
}
static bool growCells(void) {
    size_t capacity = cellCapacity ? cellCapacity * 2 : 4096;
    Cell *next = SDL_calloc(capacity, sizeof(*next));
    if (!next)
        return false;
    for (size_t i = 0; i < cellCapacity; i++)
        if (cells[i].stamp == generation) {
            size_t p = hashCell(cells[i].x, cells[i].y) & (capacity - 1);
            while (next[p].stamp == generation)
                p = (p + 1) & (capacity - 1);
            next[p] = cells[i];
        }
    SDL_free(cells);
    cells = next;
    cellCapacity = capacity;
    return true;
}
static Cell *cell(int dx, int dy) {
    if (!cellCapacity || (cellCount + 1) * 2 >= cellCapacity)
        if (!growCells())
            return NULL;
    size_t p = hashCell(dx, dy) & (cellCapacity - 1);
    while (cells[p].stamp == generation) {
        if (cells[p].x == dx && cells[p].y == dy)
            return &cells[p];
        p = (p + 1) & (cellCapacity - 1);
    }
    Cell *c = &cells[p];
    memset(c, 0, sizeof(*c));
    c->stamp = generation;
    c->x = dx;
    c->y = dy;
    c->surface = c->ground = -1;
    cellCount++;
    double distance = (double)dx * dx + (double)dy * dy;
    c->visible = D_58a5 >= 50 || distance <= D_58a5;
    if (!D_58a5)
        c->visible = dx == 0 && dy == 0;
    if (!c->visible)
        return c;
    unsigned id = typeAt(dx, dy);
    TypeDef d;
    if (!TYPES_Get(id, &d) || d.base == 255) {
        c->visible = false;
        return c;
    }
    c->wall = !!(d.render & TYPES_MASONRY);
    c->opaque = d.sight == 1 || (d.sight == 2 && distance == 1);
    unsigned tile = art(id, d.base);
    c->surface = texture(tile, false);
    unsigned ground = (c->wall || (d.render & TYPES_SPRITE)) ? floorFor(dx, dy, &d) : id;
    TypeDef floor;
    if (TYPES_Get(ground, &floor))
        c->ground = texture(art(ground, floor.base), false);
    if (!c->wall && (d.render & TYPES_SPRITE))
        addSprite(tile, dx + 0.5f, dy + 0.5f, dx, dy);
    int wx, wy;
    mapPoint(dx, dy, &wx, &wy);
    int loot = CONTENTS_GAME_Shape(wx, wy);
    if (loot)
        addSprite(256 + loot, dx + 0.5f, dy + 0.5f, dx, dy);
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
        if (!a->_0_tile || !a->_1_animTile || a->_4_z != D_5895_map_level) {
            if (motion)
                motion->valid = motion->moving = false;
            continue;
        }
        int dx = a->_2_x - D_5896_map_x, dy = a->_3_y - D_5897_map_y;
        if (!D_5893_map_id) {
            dx = MAPS_Wrap(dx + MAP_WIDTH / 2, MAP_WIDTH) - MAP_WIDTH / 2;
            dy = MAPS_Wrap(dy + MAP_HEIGHT / 2, MAP_HEIGHT) - MAP_HEIGHT / 2;
        }
        bool sighted = WIDE_Visible(dx, dy);
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
        unsigned tile = ACTORS_Sprite(i, 256 + a->_1_animTile), w = 1, h = 1;
        if (TYPES_ArtSlot(tile))
            TYPES_ArtSize(tile, D_5893_map_id == 0, &w, &h);
        addSprite(tile, dx + offsetX + w * 0.5f, dy + offsetY + h * 0.5f, dx, dy);
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
    textureCount = spriteCount = 0;
    cellCount = 0;
    memset(textureBuckets, 255, sizeof(textureBuckets));
    if (++generation == 0) {
        if (cells)
            memset(cells, 0, cellCapacity * sizeof(*cells));
        generation = 1;
    }
    prepareActors();
}

/* Sky colors are palette indices throughout: no RGB interpolation or shading.
 * The original light cycle has dawn at 05:00-06:00 and dusk at 19:00-20:00. */
typedef struct {
    int minute;
    byte zenith, middle, horizon;
} SkyKey;
static const SkyKey skyKeys[] = {{0, 0, 0, 1},     {300, 0, 0, 1},   {315, 0, 1, 5},
                                 {330, 1, 5, 12},  {345, 1, 9, 14},  {360, 1, 9, 11},
                                 {1140, 1, 9, 11}, {1155, 1, 5, 14}, {1170, 1, 5, 12},
                                 {1185, 0, 1, 5},  {1200, 0, 0, 1},  {1440, 0, 0, 1}};
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
static void pixel(SDL_Surface *out, int x, int y, Uint32 color, float z, int dx, int dy) {
    int i = y * viewW + x;
    if (z >= depth[i])
        return;
    depth[i] = z;
    pickX[i] = dx;
    pickY[i] = dy;
    ((Uint32 *)((byte *)out->pixels + (y + viewY) * out->pitch))[x + viewX] = color;
}
void WORLD3D_Render(SDL_Surface *out, const Uint32 *palette) {
    WORLD3D_RenderRegion(out, palette, 16, 16, 352, 352, 1);
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
            pickX[i] = pickY[i] = 0;
            ((Uint32 *)((byte *)out->pixels + (y + top) * out->pitch))[x + left] =
                background[(x / 2) & 7];
        }
    }
    /* Pixel-centre sampling reaches the horizon without a fixed distance cutoff. */
    for (int y = viewH / 2; y < viewH; y++) {
        float denominator = y + 0.5f - viewH * 0.5f;
        if (denominator <= 0)
            continue;
        float z = 0.5f * focalY / denominator;
        for (int x = 0; x < viewW; x++) {
            float offset = (x + 0.5f - viewW * 0.5f) / focalX, wx = ox + z * (fx + rx * offset),
                  wy = oy + z * (fy + ry * offset);
            int cx = (int)floorf(wx), cy = (int)floorf(wy);
            Cell *c = cell(cx, cy);
            if (!c || !c->visible || c->wall)
                continue;
            pixel(out, x, y, shade(sample(c->ground, wx - floorf(wx), wy - floorf(wy)), z, false),
                  z, cx, cy);
        }
    }
    /* Beyond focalY distance a unit wall is smaller than the horizon's pixel gap.
     * Trace all geometry that can cover a sample, not an arbitrary tile radius. */
    for (int x = 0; x < viewW; x++) {
        float offset = (x + 0.5f - viewW * 0.5f) / focalX, dx = fx + rx * offset,
              dy = fy + ry * offset;
        float sx = fabsf(dx) < 1e-8f ? INFINITY : fabsf(1 / dx),
              sy = fabsf(dy) < 1e-8f ? INFINITY : fabsf(1 / dy);
        int cx = (int)floorf(ox), cy = (int)floorf(oy), stepX = dx < 0 ? -1 : 1,
            stepY = dy < 0 ? -1 : 1;
        float tx = isfinite(sx) ? (dx < 0 ? ox - cx : cx + 1 - ox) * sx : INFINITY,
              ty = isfinite(sy) ? (dy < 0 ? oy - cy : cy + 1 - oy) * sy : INFINITY, z = 0;
        bool side = false;
        while (z <= focalY) {
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
            if (z > focalY)
                break;
            Cell *c = cell(cx, cy);
            if (!c)
                break;
            if (!c->visible || c->opaque || c->wall) {
                if (!c->visible || !c->wall) {
                    for (int y = c->visible ? 0 : viewH / 2; y < viewH; y++) {
                        int i = y * viewW + x;
                        if (c->visible && pickX[i] == cx && pickY[i] == cy)
                            continue;
                        if (z < depth[i]) {
                            Uint32 background = y < viewH / 2
                                                    ? ((Uint32 *)((byte *)out->pixels +
                                                                  (y + top) * out->pitch))[x + left]
                                                    : colors[0];
                            /* Leave the occluder's own billboard in front of its backdrop. */
                            float curtain = c->visible ? SDL_min(tx, ty) : z;
                            pixel(out, x, y, background, curtain, cx, cy);
                        }
                    }
                    break;
                }
                float topY = viewH * 0.5f - focalY * 0.5f / z,
                      bottom = viewH * 0.5f + focalY * 0.5f / z;
                float hit = side ? ox + dx * z : oy + dy * z, u = hit - floorf(hit);
                if ((side && dy > 0) || (!side && dx < 0))
                    u = 1 - u;
                for (int y = SDL_max(0, (int)ceilf(topY - 0.5f));
                     y < SDL_min(viewH, (int)ceilf(bottom - 0.5f)); y++)
                    pixel(
                        out, x, y,
                        shade(sample(c->surface, u, (y + 0.5f - topY) / (bottom - topY)), z, side),
                        z, cx, cy);
                break;
            }
        }
    }
    for (int i = 0; i < spriteCount; i++) {
        Sprite *s = &sprites[i];
        Texture *t = &textures[s->texture];
        float dx = s->x - ox, dy = s->y - oy, z = dx * fx + dy * fy, across = dx * rx + dy * ry;
        if (z < 0.05f)
            continue;
        float w = focalX * (t->w / 32.0f) / z, h = focalY * (t->h / 32.0f) / z,
              l = viewW * 0.5f + focalX * across / z - w * 0.5f,
              bottom = viewH * 0.5f + focalY * 0.5f / z, topY = bottom - h;
        for (int y = SDL_max(0, (int)ceilf(topY - 0.5f));
             y < SDL_min(viewH, (int)ceilf(bottom - 0.5f)); y++)
            for (int x = SDL_max(0, (int)ceilf(l - 0.5f));
                 x < SDL_min(viewW, (int)ceilf(l + w - 0.5f)); x++) {
                Uint32 color = sample(s->texture, (x + 0.5f - l) / w, (y + 0.5f - topY) / h);
                if (color >> 24)
                    pixel(out, x, y, shade(color, z, false), z, s->dx, s->dy);
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
    Cell *c = cell(*dx, *dy);
    return c && c->visible;
}
