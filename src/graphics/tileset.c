#include "tileset.h"
#include "common/file.h"
#include "grap_buf.h"
#include "grap_sdl.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Keep the DOS byte framebuffer and tile IDs authoritative. Indices 0..15
 * retain the UI palette; alternate artwork uses an independent RGB palette.
 * Every logical pixel can describe four native Sharp subpixels without
 * changing map cells, collision, input coordinates or saved game state. */
#define TILE_TOKEN_MASK 0x7fffffu
#define TILE_SPRITE_FLAG 0x800000u
static int selected;
static Uint32 art[512][32 * 32], palette[256];
static byte available[512];
static unsigned sample;
/* refs: expected palette byte (bits 24..31), sprite cutout flag (bit 23),
 * and tile*256 + logical_y*16 + logical_x + 1 (zero means ordinary UI).
 * backs retains terrain provenance for partially transparent 2x2 cells.
 * Checking the expected byte rejects provenance left by legacy raw writes. */
typedef struct Layer
{
    byte *pixels;
    size_t size;
    unsigned *refs;
    unsigned *backs;
} Layer;
static Layer layers[8];
static const char *names[] = {"DOS", "Amiga", "Apple II", "Grayscale", "Sharp X68000"};
static const char *files[] = {NULL, "Ultima_5_Tiles_Amiga.png", "Ultima_5_Tiles_AppleII.png",
                              "Ultima_5_Tiles_Grayscale.png", "Ultima_5_Tiles_SharpX68000_World.png"};
int TILESET_Selected(void)
{
    return selected;
}
const char *TILESET_Label(int choice)
{
    return choice >= 0 && choice < TILESET_COUNT ? names[choice] : "DOS";
}
unsigned TILESET_Sample(void)
{
    return sample;
}
void TILESET_SetSample(unsigned value)
{
    sample = value;
}
static Layer *layer(const byte *p)
{
    uintptr_t address = (uintptr_t)p;
    for (int i = 0; i < 8; i++)
        if (layers[i].pixels && address >= (uintptr_t)layers[i].pixels &&
            address < (uintptr_t)layers[i].pixels + layers[i].size)
            return &layers[i];
    return NULL;
}
void TILESET_Unregister(byte *pixels)
{
    for (int i = 0; i < 8; i++)
        if (layers[i].pixels == pixels)
        {
            free(layers[i].refs);
            free(layers[i].backs);
            memset(&layers[i], 0, sizeof(Layer));
        }
}
void TILESET_Register(byte *pixels, size_t size)
{
    TILESET_Unregister(pixels);
    for (int i = 0; i < 8; i++)
        if (!layers[i].pixels)
        {
            layers[i] = (Layer){pixels, size, calloc(size, sizeof(unsigned)), calloc(size, sizeof(unsigned))};
            if (!layers[i].refs || !layers[i].backs)
                DEBUG_Error("Cannot allocate tileset pixel provenance (%zu pixels)", size);
            return;
        }
    DEBUG_Error("Too many registered tileset framebuffers");
}
void TILESET_Record(byte *pixel, int color)
{
    Layer *l = layer(pixel);
    if (l && l->refs)
    {
        l->refs[pixel - l->pixels] = sample ? ((unsigned)color << 24) | sample : 0;
        if (l->backs)
            l->backs[pixel - l->pixels] = 0;
    }
}
void TILESET_RecordSprite(byte *pixel, int color, bool transparent)
{
    Layer *l = layer(pixel);
    unsigned back = l && l->refs ? l->refs[pixel - l->pixels] : 0;
    if (back >> 24 != *pixel)
        back = (unsigned)*pixel << 24;
    TILESET_Record(pixel, color);
    if (transparent && sample && l && l->refs && l->backs)
    {
        l->refs[pixel - l->pixels] |= TILE_SPRITE_FLAG;
        l->backs[pixel - l->pixels] = back;
    }
}
void TILESET_Copy(byte *destination, const byte *source, size_t size)
{
    Layer *a = layer(destination);
    Layer *b = layer(source);
    if (a && a->refs && a->backs && destination - a->pixels + size <= a->size)
    {
        if (b && b->refs && b->backs && source - b->pixels + size <= b->size)
        {
            memmove(a->refs + (destination - a->pixels), b->refs + (source - b->pixels),
                    size * sizeof(unsigned));
            memmove(a->backs + (destination - a->pixels), b->backs + (source - b->pixels),
                    size * sizeof(unsigned));
        }
        else
        {
            memset(a->refs + (destination - a->pixels), 0, size * sizeof(unsigned));
            memset(a->backs + (destination - a->pixels), 0, size * sizeof(unsigned));
        }
    }
}
Uint32 TILESET_Color(int color, const Uint32 *ega)
{
    return color < 16 ? ega[color & 15] : palette[color & 255];
}
static Uint32 sampledColor(int color, int subX, int subY, const Uint32 *ega, bool transparent)
{
    if (color < 16 || !sample || !selected)
        return TILESET_Color(color, ega);
    unsigned p = sample - 1, t = p / 256, x = p % 16, y = p % 256 / 16;
    Uint32 value = art[t][(y * 2 + subY) * 32 + x * 2 + subX];
    return value ? value : transparent ? 0 : ega[0];
}
Uint32 TILESET_LastColor(int color, int subX, int subY, const Uint32 *ega)
{
    return sampledColor(color, subX, subY, ega, false);
}
Uint32 TILESET_LastSpriteColor(int color, int subX, int subY, const Uint32 *ega, bool transparent)
{
    return color < 0 ? 0 : sampledColor(color, subX, subY, ega, transparent);
}
Uint32 TILESET_BufferColor(const byte *pixel, int subX, int subY, const Uint32 *ega)
{
    if (!selected)
        return ega[*pixel & 15];
    Layer *l = layer(pixel);
    unsigned ref = l && l->refs ? l->refs[pixel - l->pixels] : 0;
    unsigned saved = sample;
    sample = ref >> 24 == *pixel ? (ref & TILE_TOKEN_MASK) : 0;
    Uint32 color = sampledColor(*pixel, subX, subY, ega, (ref & TILE_SPRITE_FLAG) != 0);
    if (!color && l && l->backs)
    {
        unsigned back = l->backs[pixel - l->pixels];
        sample = back & TILE_TOKEN_MASK;
        int index = back >> 24;
        if (sample)
        {
            unsigned token = sample - 1;
            index = GRAP_BUF_TilePixel(token / 256, token % 16, token % 256 / 16);
        }
        color = TILESET_LastColor(index, subX, subY, ega);
    }
    sample = saved;
    return color;
}
static SDL_Surface *load(const char *filename)
{
    char paths[4][FILE_PATH_SIZE];
    int count = 0;
    const char *custom = SDL_getenv("IMPERA_TILESETS");
    if (custom)
        SDL_snprintf(paths[count++], FILE_PATH_SIZE, "%s/%s", custom, filename);
    const char *appimage = SDL_getenv("APPIMAGE");
    if (appimage)
    {
        SDL_strlcpy(paths[count], appimage, FILE_PATH_SIZE);
        char *slash = strrchr(paths[count], '/');
        if (slash)
            *slash = 0;
        SDL_strlcat(paths[count], "/textures/tilesets/", FILE_PATH_SIZE);
        SDL_strlcat(paths[count++], filename, FILE_PATH_SIZE);
    }
    SDL_snprintf(paths[count++], FILE_PATH_SIZE, "%stextures/tilesets/%s", SDL_GetBasePath(), filename);
    SDL_snprintf(paths[count++], FILE_PATH_SIZE, "textures/tilesets/%s", filename);
    for (int i = 0; i < count; i++)
    {
        SDL_Surface *image = SDL_LoadPNG(paths[i]);
        if (image)
        {
            debug("Loaded tileset sheet %s (%dx%d)", paths[i], image->w, image->h);
            return image;
        }
    }
#ifdef __APPLE__
    SDL_snprintf(paths[0], FILE_PATH_SIZE, "%s../Resources/textures/tilesets/%s", SDL_GetBasePath(),
                 filename);
    return SDL_LoadPNG(paths[0]);
#else
    return NULL;
#endif
}
static int colorIndex(Uint32 value)
{
    for (int i = 16; i < 256; i++)
        if (palette[i] == value)
            return i;
    return 0;
}
static bool readSheet(SDL_Surface *image, int choice, int first)
{
    /* Apple II artwork has narrower source pixels, not missing tile columns. */
    int tileWidth = choice == TILESET_APPLE ? 14 : choice == TILESET_SHARP ? 32 : 16;
    int tileHeight = choice == TILESET_SHARP ? 32 : 16;
    int cols = choice == TILESET_SHARP ? 16 : 32;
    if (image->w != cols * tileWidth || image->h != 16 * tileHeight)
        return false;
    for (int t = first; t < first + (choice == TILESET_SHARP ? 256 : 512); t++)
    {
        int local = t - first, col = local % 32, row = local / 32;
        if (choice == TILESET_SHARP)
        {
            col = local % 16;
            row = local / 16;
        }
        available[t] = 1;
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++)
            {
                Uint8 r, g, b, a;
                if (!SDL_ReadSurfacePixel(image, col * tileWidth + x * tileWidth / 32,
                                          row * tileHeight + y * tileHeight / 32, &r, &g, &b, &a) ||
                    a != 255)
                    return false;
                /* Sharp's NPC sheet uses gray as its cutout/background mask. */
                bool background = choice == TILESET_SHARP && first == 256 ? r == 128 && g == 128 && b == 128
                                                                          : r == 0 && g == 0 && b == 0;
                Uint32 color = background ? 0 : 0xff000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
                art[t][y * 32 + x] = color;
                if (color && !colorIndex(color))
                {
                    int index = 16;
                    while (index < 256 && palette[index])
                        index++;
                    if (index == 256)
                        return false;
                    palette[index] = color;
                }
            }
    }
    return true;
}
void TILESET_Refresh(void)
{
    sample = 0;
    for (int i = 0; i < 8; i++)
        if (layers[i].refs)
            for (size_t p = 0; p < layers[i].size; p++)
            {
                unsigned ref = layers[i].refs[p], token = ref & TILE_TOKEN_MASK;
                if (!token || ref >> 24 != layers[i].pixels[p])
                {
                    layers[i].refs[p] = 0;
                    continue;
                }
                token--;
                int color = GRAP_BUF_TilePixel(token / 256, token % 16, token % 256 / 16);
                layers[i].pixels[p] = (byte)color;
                layers[i].refs[p] = ((unsigned)color << 24) | (token + 1) | (ref & TILE_SPRITE_FLAG);
            }
    sample = 0;
    GRAP_SDL_SetSmoothMovement(GRAP_SDL_SmoothMovementEnabled());
}
bool TILESET_Select(int choice)
{
    if (choice < 0 || choice >= TILESET_COUNT)
        return false;
    if (choice == selected)
        return true;
    if (choice == 0)
    {
        selected = 0;
        TILESET_Refresh();
        return true;
    }
    /* Load and validate every file before replacing the active pack. */
    SDL_Surface *world = load(files[choice]);
    SDL_Surface *actors = choice == TILESET_SHARP ? load("Ultima_5_Tiles_SharpX68000_NPC.png") : NULL;
    if (!world || (choice == TILESET_SHARP && !actors))
    {
        SDL_DestroySurface(world);
        SDL_DestroySurface(actors);
        DEBUG_Error("Tileset %s unavailable: %s", names[choice], SDL_GetError());
        return false;
    }
    Uint32 *oldArt = malloc(sizeof(art));
    Uint32 oldPalette[256];
    byte oldAvailable[512];
    if (!oldArt)
    {
        SDL_DestroySurface(world);
        SDL_DestroySurface(actors);
        DEBUG_Error("Cannot allocate tileset loading buffer");
        return false;
    }
    memcpy(oldArt, art, sizeof(art));
    memcpy(oldPalette, palette, sizeof(palette));
    memcpy(oldAvailable, available, sizeof(available));
    memset(available, 0, sizeof(available));
    memset(palette, 0, sizeof(palette));
    bool ok = readSheet(world, choice, 0) && (choice != TILESET_SHARP || readSheet(actors, choice, 256));
    SDL_DestroySurface(world);
    SDL_DestroySurface(actors);
    if (!ok)
    {
        memcpy(art, oldArt, sizeof(art));
        memcpy(palette, oldPalette, sizeof(palette));
        memcpy(available, oldAvailable, sizeof(available));
        DEBUG_Error("Tileset %s has invalid dimensions or palette", names[choice]);
    }
    else
    {
        selected = choice;
        TILESET_Refresh();
        debug("Selected %s tileset", names[choice]);
    }
    free(oldArt);
    return ok;
}
int TILESET_Pixel(int tile, int x, int y)
{
    sample = 0;
    if (!selected || tile < 0 || tile >= 512 || x < 0 || y < 0 || x >= 16 || y >= 16 || !available[tile])
        return -1;
    sample = (unsigned)(tile * 256 + y * 16 + x + 1);
    for (int sy = 0; sy < 2; sy++)
        for (int sx = 0; sx < 2; sx++)
        {
            Uint32 color = art[tile][(y * 2 + sy) * 32 + x * 2 + sx];
            if (color)
                return colorIndex(color);
        }
    return 0;
}
void TILESET_Animate(void)
{
    /* Sharp supplies static bitmaps for the DOS procedural effects. */
    if (!selected || selected == TILESET_SHARP)
        return;
    const int water[] = {1, 2, 3, 0x8f};
    for (int i = 0; i < 4; i++)
        if (available[water[i]])
        {
            Uint32 tail[64];
            memcpy(tail, art[water[i]] + 30 * 32, sizeof(tail));
            memmove(art[water[i]] + 64, art[water[i]], 30 * 32 * sizeof(Uint32));
            memcpy(art[water[i]], tail, sizeof(tail));
        }
}
