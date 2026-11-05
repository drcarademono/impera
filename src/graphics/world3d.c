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

#define SIDE 352
#define RADIUS 8
#define GRID (RADIUS * 2 + 1)
#define MAX_TEXTURES 256
#define MAX_SPRITES 1024
static bool pickValid;
static bool enabled, sceneView;
void WORLD3D_SceneView(void) {
    sceneView = true;
    pickValid = false;
}
void WORLD3D_MapDrawn(void) { sceneView = false; }
static int facing;
static const int vx[] = {0, 1, 0, -1}, vy[] = {-1, 0, 1, 0};
static const int compass[] = {U5_KEY_UP, U5_KEY_RIGHT, U5_KEY_DOWN, U5_KEY_LEFT};
static float depth[SIDE * SIDE];
static signed char pickX[SIDE * SIDE], pickY[SIDE * SIDE];
static unsigned pickMap;
static int pickLevel, pickWorldX, pickWorldY, pickFacing;
typedef struct {
    unsigned tile;
    bool sprite;
    unsigned w, h;
    Uint32 pixels[64 * 64];
} Texture;
static Texture textures[MAX_TEXTURES];
static int textureCount;
typedef struct {
    bool visible, wall;
    int surface, ground;
} Cell;
static Cell cells[GRID][GRID];
typedef struct {
    float x, y;
    int texture, dx, dy;
} Sprite;
static Sprite sprites[MAX_SPRITES];
static int spriteCount;
static const Uint32 *colors;

bool WORLD3D_Enabled(void) { return enabled; }
void WORLD3D_SetEnabled(bool value) {
    enabled = value;
    pickValid = false;
    GRAP_BUF_MarkDirty();
}
bool WORLD3D_Active(void) {
    return enabled && !sceneView && D_58a4 && D_5893_map_id < 128 && MAPS_Outside(D_5893_map_id) &&
           !GRAP_SDL_PixelUI() && !EVT_ImmediateExitEnabled() && GRAP_BUF_HasTileset();
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
static int texture(unsigned tile, bool sprite) {
    for (int i = 0; i < textureCount; i++)
        if (textures[i].tile == tile && textures[i].sprite == sprite)
            return i;
    if (textureCount == MAX_TEXTURES)
        return -1;
    Texture *t = &textures[textureCount];
    t->tile = tile;
    t->sprite = sprite;
    t->w = t->h = 1;
    if (sprite && TYPES_ArtSlot(tile))
        TYPES_ArtSize(tile, D_5893_map_id == 0, &t->w, &t->h);
    t->w = SDL_min(t->w, 2) * 32;
    t->h = SDL_min(t->h, 2) * 32;
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
    float light = 1.0f / (1.0f + distance * 0.09f);
    if (side)
        light *= 0.78f;
    return 0xff000000 | ((unsigned)(((color >> 16) & 255) * light) << 16) |
           ((unsigned)(((color >> 8) & 255) * light) << 8) | (unsigned)((color & 255) * light);
}
static Cell *cell(int x, int y) {
    if (abs(x) > RADIUS || abs(y) > RADIUS)
        return NULL;
    return &cells[y + RADIUS][x + RADIUS];
}
static unsigned art(unsigned id, byte base) {
    return TYPES_ArtSlot(id) ? TYPES_ActorArt(id, D_b11e[base]) : D_b11e[base];
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
    if (spriteCount == MAX_SPRITES || (dx == 0 && dy == 0) || !WIDE_Visible(dx, dy))
        return;
    int index = texture(tile, true);
    if (index < 0)
        return;
    sprites[spriteCount++] = (Sprite){x, y, index, dx, dy};
}
static void prepare(void) {
    textureCount = spriteCount = 0;
    for (int y = -RADIUS; y <= RADIUS; y++)
        for (int x = -RADIUS; x <= RADIUS; x++) {
            Cell *c = cell(x, y);
            memset(c, 0, sizeof(*c));
            c->surface = c->ground = -1;
            c->visible = WIDE_Visible(x, y);
            if (!c->visible)
                continue;
            unsigned id = typeAt(x, y);
            TypeDef d;
            if (!TYPES_Get(id, &d) || d.base == 255) {
                c->visible = false;
                continue;
            }
            c->wall = !!(d.render & TYPES_MASONRY);
            unsigned tile = art(id, d.base);
            c->surface = texture(tile, false);
            unsigned ground = (c->wall || (d.render & TYPES_SPRITE)) ? floorFor(x, y, &d) : id;
            TypeDef floor;
            if (TYPES_Get(ground, &floor))
                c->ground = texture(art(ground, floor.base), false);
            if (!c->wall && (d.render & TYPES_SPRITE))
                addSprite(tile, x + 0.5f, y + 0.5f, x, y);
            int wx, wy;
            mapPoint(x, y, &wx, &wy);
            int loot = CONTENTS_GAME_Shape(wx, wy);
            if (loot)
                addSprite(256 + loot, x + 0.5f, y + 0.5f, x, y);
        }
    for (int i = 1; i < MAP_ACTOR_COUNT; i++) {
        ActorFmt *a = &D_5c5a[i];
        if (!a->_0_tile || !a->_1_animTile || a->_4_z != D_5895_map_level)
            continue;
        int dx = a->_2_x - D_5896_map_x, dy = a->_3_y - D_5897_map_y;
        if (!D_5893_map_id) {
            dx = MAPS_Wrap(dx + MAP_WIDTH / 2, MAP_WIDTH) - MAP_WIDTH / 2;
            dy = MAPS_Wrap(dy + MAP_HEIGHT / 2, MAP_HEIGHT) - MAP_HEIGHT / 2;
        }
        if (abs(dx) > RADIUS || abs(dy) > RADIUS)
            continue;
        unsigned tile = ACTORS_Sprite(i, 256 + a->_1_animTile), w = 1, h = 1;
        if (TYPES_ArtSlot(tile))
            TYPES_ArtSize(tile, D_5893_map_id == 0, &w, &h);
        addSprite(tile, dx + w * 0.5f, dy + h * 0.5f, dx, dy);
    }
}
static void pixel(SDL_Surface *out, int x, int y, Uint32 color, float z, int dx, int dy) {
    int index = y * SIDE + x;
    if (z >= depth[index])
        return;
    depth[index] = z;
    pickX[index] = dx;
    pickY[index] = dy;
    ((Uint32 *)((byte *)out->pixels + (y + 16) * out->pitch))[x + 16] = color;
}
void WORLD3D_Render(SDL_Surface *out, const Uint32 *palette) {
    if (!WORLD3D_Active() || out->w < 384 || out->h < 384) {
        pickValid = false;
        return;
    }
    colors = palette;
    prepare();
    float fx = vx[facing], fy = vy[facing], rx = -fy, ry = fx, focal = SIDE * 0.78f;
    for (int y = 0; y < SIDE; y++)
        for (int x = 0; x < SIDE; x++) {
            depth[y * SIDE + x] = 1000;
            pickX[y * SIDE + x] = pickY[y * SIDE + x] = 0;
            ((Uint32 *)((byte *)out->pixels + (y + 16) * out->pitch))[x + 16] = 0xff000000;
        }
    /* Flat ground is perspective-correct; no elevation or camera bob. */
    for (int y = SIDE / 2 + 1; y < SIDE; y++) {
        float z = 0.5f * focal / (y - SIDE / 2.0f);
        if (z > RADIUS)
            continue;
        for (int x = 0; x < SIDE; x++) {
            float offset = (x - SIDE / 2.0f) / focal;
            float wx = 0.5f + z * (fx + rx * offset), wy = 0.5f + z * (fy + ry * offset);
            int cx = (int)floorf(wx), cy = (int)floorf(wy);
            Cell *c = cell(cx, cy);
            if (!c || !c->visible || c->wall)
                continue;
            pixel(out, x, y, shade(sample(c->ground, wx - floorf(wx), wy - floorf(wy)), z, false),
                  z, cx, cy);
        }
    }
    /* DDA finds the first cube or unseen cell along each screen column. */
    for (int x = 0; x < SIDE; x++) {
        float offset = (x - SIDE / 2.0f) / focal, dx = fx + rx * offset, dy = fy + ry * offset;
        float sx = fabsf(dx) < 0.00001f ? 1e9f : fabsf(1 / dx),
              sy = fabsf(dy) < 0.00001f ? 1e9f : fabsf(1 / dy);
        float tx = sx * 0.5f, ty = sy * 0.5f, z = 0;
        int cx = 0, cy = 0, stepX = dx < 0 ? -1 : 1, stepY = dy < 0 ? -1 : 1;
        bool side = false;
        for (int n = 0; n < 2 * RADIUS + 2; n++) {
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
            Cell *c = cell(cx, cy);
            if (!c || z > RADIUS)
                break;
            if (!c->visible || c->wall) {
                if (!c->visible) { /* Darkness occludes geometry and sprites behind it. */
                    for (int y = 0; y < SIDE; y++)
                        if (z < depth[y * SIDE + x])
                            pixel(out, x, y, 0xff000000, z, cx, cy);
                    break;
                }
                int top = (int)(SIDE / 2 - focal * 0.5f / z),
                    bottom = (int)(SIDE / 2 + focal * 0.5f / z);
                float hit = side ? 0.5f + dx * z : 0.5f + dy * z, u = hit - floorf(hit);
                if ((side && dy > 0) || (!side && dx < 0))
                    u = 1 - u;
                for (int y = SDL_max(0, top); y < SDL_min(SIDE, bottom); y++)
                    pixel(out, x, y,
                          shade(sample(c->surface, u, (y - top) / (float)(bottom - top)), z, side),
                          z, cx, cy);
                break;
            }
        }
    }
    /* Billboards use per-pixel depth, alpha, and the existing protected-black masks. */
    for (int i = 0; i < spriteCount; i++) {
        Sprite *s = &sprites[i];
        Texture *t = &textures[s->texture];
        float dx = s->x - 0.5f, dy = s->y - 0.5f, z = dx * fx + dy * fy, across = dx * rx + dy * ry;
        if (z < 0.15f || z > RADIUS)
            continue;
        float width = focal * (t->w / 32.0f) / z, height = focal * (t->h / 32.0f) / z;
        float left = SIDE / 2 + focal * across / z - width / 2,
              bottom = SIDE / 2 + focal * 0.5f / z, top = bottom - height;
        for (int y = SDL_max(0, (int)top); y < SDL_min(SIDE, (int)bottom); y++)
            for (int x = SDL_max(0, (int)left); x < SDL_min(SIDE, (int)(left + width)); x++) {
                Uint32 color = sample(s->texture, (x - left) / width, (y - top) / height);
                if (color >> 24)
                    pixel(out, x, y, shade(color, z, false), z, s->dx, s->dy);
            }
    }
    const byte *font = D_539c[0] ? D_539c[0] : D_5398_currentCharset;
    if (font)
        for (int y = 0; y < 20; y++)
            for (int x = 0; x < 20; x++) {
                bool ink = x >= 2 && x < 18 && y >= 2 && y < 18 &&
                           (font[(unsigned char)"NESW"[facing] * 8 + (y - 2) / 2] &
                            (0x80 >> ((x - 2) / 2)));
                ((Uint32 *)((byte *)out->pixels + (y + 20) * out->pitch))[x + 20] =
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
    if (!pickValid || pickMap != D_5893_map_id || pickLevel != D_5895_map_level ||
        pickWorldX != D_5896_map_x || pickWorldY != D_5897_map_y || pickFacing != facing || u < 0 ||
        v < 0 || u >= 1 || v >= 1)
        return false;
    int index = (int)(v * SIDE) * SIDE + (int)(u * SIDE);
    if (depth[index] >= 1000)
        return false;
    *dx = pickX[index];
    *dy = pickY[index];
    Cell *c = cell(*dx, *dy);
    return c && c->visible;
}
