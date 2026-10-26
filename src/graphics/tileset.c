#include "tileset.h"
#include "common/file.h"
#include "grap_buf.h"
#include "grap_sdl.h"
#include "mod/package.h"
#include "mod/runtime.h"
#include "mod/tileset_resources.h"
#include "mod/types.h"
#include "vars.h"
#include "tileset_assets.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Keep the DOS byte framebuffer and tile IDs authoritative. Indices 0..15
 * retain the UI palette; alternate artwork uses an independent RGB palette.
 * Every logical pixel can describe four native Sharp subpixels without
 * changing map cells, collision, input coordinates or saved game state. */
#define TILE_ART_TOKEN 0x200000u
#define TILE_RGB_TOKEN 0x7fffffu
#define TILE_TOKEN_MASK 0x7fffffu
#define TILE_SPRITE_FLAG 0x800000u
static int selected;
static unsigned revision;
static unsigned mountedRevision = ~0u;
static byte protection[512][1024];
static char customPath[FILE_PATH_SIZE];
enum
{
    CUSTOM_DOS,
    CUSTOM_SHARP,
    CUSTOM_ALPHA
};
static int customMask;
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
static const char *names[] = {"DOS", "Amiga", "Apple II", "Grayscale", "Sharp X68000", "Custom"};
static const char *files[] = {NULL,
                              "Ultima_5_Tiles_Amiga.png",
                              "Ultima_5_Tiles_AppleII.png",
                              "Ultima_5_Tiles_Grayscale.png",
                              "Ultima_5_Tiles_SharpX68000_World.png",
                              NULL};
int TILESET_Selected(void)
{
    return selected;
}
unsigned TILESET_Revision(void)
{
    return revision;
}
const char *TILESET_Label(int choice)
{
    return choice >= 0 && choice < TILESET_COUNT ? names[choice] : "DOS";
}
const char *TILESET_CustomPath(void)
{
    return customPath;
}
bool TILESET_SetCustomPath(const char *path)
{
    if (!path || strlen(path) >= sizeof(customPath) || strpbrk(path, "\r\n"))
        return false;
    SDL_strlcpy(customPath, path, sizeof(customPath));
    return true;
}
bool TILESET_SelectCustom(const char *path)
{
    char previous[FILE_PATH_SIZE];
    SDL_strlcpy(previous, customPath, sizeof(previous));
    if (!TILESET_SetCustomPath(path))
        return false;
    if (TILESET_Select(TILESET_CUSTOM))
        return true;
    SDL_strlcpy(customPath, previous, sizeof(customPath));
    return false;
}
bool TILESET_SpriteOutline(void)
{
    return selected != TILESET_SHARP && (selected != TILESET_CUSTOM || customMask == CUSTOM_DOS);
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
void TILESET_RecordRGBA(byte *pixel, Uint32 rgba) {
    Layer *l=layer(pixel);
    if(!l||!l->refs||!l->backs) return;
    *pixel=16;
    l->refs[pixel-l->pixels]=(16u<<24)|TILE_RGB_TOKEN;
    l->backs[pixel-l->pixels]=rgba|0xff000000u;
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
static Uint32 over(Uint32 foreground, Uint32 background)
{
    unsigned alpha = foreground >> 24;
    if (alpha == 255)
        return foreground;
    if (!alpha)
        return background;
    Uint32 result = 0xff000000u;
    for (int shift = 0; shift <= 16; shift += 8)
    {
        unsigned front = (foreground >> shift) & 255, back = (background >> shift) & 255;
        result |= ((front * alpha + back * (255 - alpha) + 127) / 255) << shift;
    }
    return result;
}
static Uint32 sampledColor(int color, int subX, int subY, const Uint32 *ega, bool transparent)
{
    if (sample >= TILE_ART_TOKEN && sample != TILE_RGB_TOKEN) {
        unsigned token = sample - TILE_ART_TOKEN - 1, width = 0, height = 0;
        unsigned fw, fh;
        TYPES_ArtSlotSize(512 + token / 4096, !D_5893_map_id, &fw, &fh);
        const unsigned char *rgba = TYPES_SampleArtImage(
            (512 + token / 4096) | (((token >> 10) & 3) << 16),
            (unsigned)selected + (!D_5893_map_id ? 6 : 0), SDL_GetTicks(), &width, &height);
        if (rgba) {
            unsigned x = token % 32, y = (token % 1024) / 32;
            unsigned px = (x * 2 + subX) * width / (32 * fw);
            unsigned py = (y * 2 + subY) * height / (32 * fh);
            const unsigned char *p = rgba + (py * width + px) * 4;
            Uint32 value = (Uint32)p[3] << 24 | (Uint32)p[0] << 16 | (Uint32)p[1] << 8 | p[2];
            /* The engine transparency option keys black in sprite artwork, just
             * as in native DOS sprites. Raw tiles and editor pixels keep it. */
            bool keepBlack = TYPES_MaskPixel(rgba, width, height, px, py);
            unsigned native = TYPES_ArtSlotType(512 + token / 4096);
            if (!(rgba[-3] & 128) && native < 512)
                keepBlack = protection[native][(y * 2 + subY) * 32 + x * 2 + subX];
            bool sharpActor = native >= 256 && native < 512 && rgba[-4] % 6 == TILESET_SHARP;
            if (sharpActor && (value & 0xffffffu) == 0x808080u)
                value = 0;
            if (transparent && !sharpActor && !(value & 0xffffffu) && !keepBlack)
                return 0;
            return transparent ? value : over(value, ega[0]);
        }
        return ega[0];
    }
    if (color < 16 || !sample || !selected)
        return TILESET_Color(color, ega);
    unsigned p = sample - 1, t = p / 256, x = p % 16, y = p % 256 / 16;
    Uint32 value = art[t][(y * 2 + subY) * 32 + x * 2 + subX];
    return transparent ? value : over(value, ega[0]);
}
Uint32 TILESET_LastColor(int color, int subX, int subY, const Uint32 *ega)
{
    return sampledColor(color, subX, subY, ega, false);
}
Uint32 TILESET_LastSpriteColor(int color, int subX, int subY, const Uint32 *ega, bool transparent)
{
    return color < 0 ? 0 : sampledColor(color, subX, subY, ega, transparent);
}
Uint32 TILESET_UIBufferColor(const byte *pixel,const Uint32 *ega) {
    if(*pixel==16) {
        Layer *l=layer(pixel);unsigned ref=l&&l->refs?l->refs[pixel-l->pixels]:0;
        if(ref>>24==*pixel && (ref&TILE_TOKEN_MASK)==TILE_RGB_TOKEN && l->backs)
            return l->backs[pixel-l->pixels];
    }
    return TILESET_Color(*pixel,ega);
}
Uint32 TILESET_BufferColor(const byte *pixel, int subX, int subY, const Uint32 *ega)
{
    if(!selected && *pixel!=16) return ega[*pixel&15];
    Layer *l = layer(pixel);
    unsigned ref = l && l->refs ? l->refs[pixel - l->pixels] : 0;
    if(ref>>24==*pixel && (ref&TILE_TOKEN_MASK)==TILE_RGB_TOKEN && l->backs)
        return l->backs[pixel-l->pixels];
    unsigned saved = sample;
    sample = ref >> 24 == *pixel ? (ref & TILE_TOKEN_MASK) : 0;
    Uint32 color = sampledColor(*pixel, subX, subY, ega, (ref & TILE_SPRITE_FLAG) != 0);
    if ((color >> 24) < 255 && l && l->backs)
    {
        unsigned back = l->backs[pixel - l->pixels];
        sample = back & TILE_TOKEN_MASK;
        if(sample==TILE_RGB_TOKEN) sample=0;
        int index = back >> 24;
        if (sample)
        {
            unsigned token = sample - 1;
            if (token / 256 >= 512)
                index = 16;
            else
                index = GRAP_BUF_TilePixel(token / 256, token % 16, token % 256 / 16);
        }
        color = over(color, TILESET_LastColor(index, subX, subY, ega));
    }
    sample = saved;
    return color;
}
static SDL_Surface *load(const char *filename)
{
    const ModTilesetSheet *sheet = MOD_TilesetSheet(filename);
    int overridden = 0;
    FILE *mod = sheet ? MOD_OpenResource(sheet->resource, &overridden) : NULL;
    if (overridden) {
        SDL_Surface *image = NULL;
        if (mod && fseek(mod, 0, SEEK_END) == 0) {
            long length = ftell(mod);
            if (length > 0 && length <= MOD_MAX_RESOURCE && fseek(mod, 0, SEEK_SET) == 0) {
                void *bytes = SDL_malloc((size_t)length);
                if (bytes && fread(bytes, 1, (size_t)length, mod) == (size_t)length) {
                    SDL_IOStream *stream = SDL_IOFromConstMem(bytes, (size_t)length);
                    if (stream)
                        image = SDL_LoadPNG_IO(stream, true);
                }
                SDL_free(bytes);
            }
        }
        if (mod)
            fclose(mod);
        return image;
    }
    /* Explicit developer overrides are authoritative; missing/corrupt sheets
     * must not silently mix with the built-in pack. Mods take precedence above. */
    const char *custom = SDL_getenv("IMPERA_TILESETS");
    if (custom && *custom) {
        char path[FILE_PATH_SIZE];
        SDL_snprintf(path, sizeof(path), "%s/%s", custom, filename);
        return SDL_LoadPNG(path);
    }
    for (size_t i = 0; i < sizeof(tileset_pngs) / sizeof(tileset_pngs[0]); ++i) {
        if (strcmp(filename, tileset_pngs[i].name) == 0) {
            SDL_IOStream *stream = SDL_IOFromConstMem(tileset_pngs[i].data, tileset_pngs[i].size);
            return stream ? SDL_LoadPNG_IO(stream, true) : NULL;
        }
    }
    return NULL;
}
static bool readProtection(int choice, int first, byte *output) {
    const ModTilesetSheet *spec = MOD_TilesetMask(choice, first);
    SDL_Surface *image = load(spec->filename);
    if (!image) {
        int found = 0;
        FILE *f = MOD_OpenResource(spec->resource, &found);
        if (f)
            fclose(f);
        return !found;
    }
    bool ok = image->w == spec->columns * spec->width && image->h == 16 * spec->height;
    for (int y = 0; ok && y < image->h; ++y)
        for (int x = 0; ok && x < image->w; ++x) {
            Uint8 r, g, b, a;
            ok = SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, &a) && a == 255 &&
                 ((r == 0 && g == 0 && b == 0) || (r == 255 && g == 255 && b == 255));
        }
    for (int t = first; ok && t < first + (choice == TILESET_SHARP ? 256 : 512); ++t)
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                int local = t - first;
                Uint8 r, g, b, a;
                SDL_ReadSurfacePixel(
                    image, (local % spec->columns) * spec->width + x * spec->width / 32,
                    (local / spec->columns) * spec->height + y * spec->height / 32, &r, &g, &b, &a);
                output[t * 1024 + y * 32 + x] = r == 255;
            }
    SDL_DestroySurface(image);
    return ok;
}
bool TILESET_ProtectedBlack(int tile, int x, int y) {
    return !selected && tile >= 0 && tile < 512 && x >= 0 && x < 16 && y >= 0 && y < 16 &&
           protection[tile][y * 64 + x * 2];
}
static int colorIndex(Uint32 value)
{
    for (int i = 16; i < 256; i++)
        if (palette[i] == value)
            return i;
    return 0;
}
static bool detectCustomMask(SDL_Surface *image)
{
    int mask = CUSTOM_DOS;
    /* Scan the source, not just resampled pixels: a single alpha pixel takes
     * precedence over any gray or black pixels anywhere in the PNG. */
    for (int y = 0; y < image->h; y++)
        for (int x = 0; x < image->w; x++)
        {
            Uint8 r, g, b, a;
            if (!SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, &a))
                return false;
            if (a < 255)
            {
                customMask = CUSTOM_ALPHA;
                return true;
            }
            if (r == 128 && g == 128 && b == 128)
                mask = CUSTOM_SHARP;
        }
    customMask = mask;
    return true;
}
static bool readSheet(SDL_Surface *image, int choice, int first, const byte *mask) {
    /* Apple II artwork has narrower source pixels, not missing tile columns. */
    int tileWidth = choice == TILESET_APPLE ? 14 : choice == TILESET_SHARP ? 32 : 16;
    int tileHeight = choice == TILESET_SHARP ? 32 : 16;
    int cols = choice == TILESET_SHARP ? 16 : 32;
    if (choice == TILESET_CUSTOM)
    {
        /* Fractional source cell widths are valid: use atlas-relative sampling
         * so every 2:1 PNG maps to the same 32-column, 16-row DOS tile IDs. */
        if (image->w <= 0 || image->h <= 0 || (int64_t)image->w != (int64_t)image->h * 2)
            return false;
        if (!detectCustomMask(image))
            return false;
        debug("Custom tileset transparency: %s", customMask == CUSTOM_ALPHA   ? "PNG alpha"
                                                 : customMask == CUSTOM_SHARP ? "Sharp gray"
                                                                              : "DOS black");
        palette[16] = 0xffffffffu; /* Provenance marker; native RGB comes from art. */
    }
    else if (image->w != cols * tileWidth || image->h != 16 * tileHeight)
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
                int px = choice == TILESET_CUSTOM ? (int)((int64_t)(col * 32 + x) * image->w / 1024)
                                                  : col * tileWidth + x * tileWidth / 32;
                int py = choice == TILESET_CUSTOM ? (int)((int64_t)(row * 32 + y) * image->h / 512)
                                                  : row * tileHeight + y * tileHeight / 32;
                if (!SDL_ReadSurfacePixel(image, px, py, &r, &g, &b, &a) ||
                    (choice != TILESET_CUSTOM && a != 255))
                    return false;
                /* Sharp's NPC sheet uses gray as its cutout/background mask. */
                bool background = choice == TILESET_SHARP && first == 256 ? r == 128 && g == 128 && b == 128
                                                                          : r == 0 && g == 0 && b == 0;
                if (choice == TILESET_CUSTOM)
                {
                    if (customMask == CUSTOM_ALPHA)
                        background = !a;
                    else if (customMask == CUSTOM_SHARP)
                        background = r == 128 && g == 128 && b == 128;
                    else
                        background = r == 0 && g == 0 && b == 0;
                }
                if (!r && !g && !b && a && mask[t * 1024 + y * 32 + x])
                    background = false;
                Uint32 opacity = choice == TILESET_CUSTOM && customMask == CUSTOM_ALPHA ? a : 255;
                Uint32 color = background ? 0 : (opacity << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
                art[t][y * 32 + x] = color;
                if (choice != TILESET_CUSTOM && color && !colorIndex(color))
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
                if(token==TILE_RGB_TOKEN) continue;
                token--;
                int color = token + 1 >= TILE_ART_TOKEN
                                ? 16
                                : GRAP_BUF_TilePixel(token / 256, token % 16, token % 256 / 16);
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
    if (choice == selected && choice != TILESET_CUSTOM && mountedRevision == MOD_Revision())
        return true;
    byte *nextProtection = calloc(1, sizeof(protection));
    if (!nextProtection)
        return false;
    if (!readProtection(choice, 0, nextProtection) ||
        (choice == TILESET_SHARP && !readProtection(choice, 256, nextProtection))) {
        free(nextProtection);
        DEBUG_Error("Tileset %s has an invalid transparency mask", names[choice]);
        return false;
    }
    if (choice == 0)
    {
        memcpy(protection, nextProtection, sizeof(protection));
        free(nextProtection);
        palette[16] = 0xff000000u;
        selected = 0;
        mountedRevision = MOD_Revision();
        revision++;
        TILESET_Refresh();
        return true;
    }
    /* Load and validate every file before replacing the active pack. */
    SDL_Surface *world =
        choice == TILESET_CUSTOM ? (*customPath ? SDL_LoadPNG(customPath) : NULL) : load(files[choice]);
    SDL_Surface *actors = choice == TILESET_SHARP ? load("Ultima_5_Tiles_SharpX68000_NPC.png") : NULL;
    if (!world || (choice == TILESET_SHARP && !actors))
    {
        SDL_DestroySurface(world);
        SDL_DestroySurface(actors);
        free(nextProtection);
        DEBUG_Error("Tileset %s unavailable: %s", names[choice], SDL_GetError());
        return false;
    }
    if (choice == TILESET_CUSTOM)
        debug("Loaded custom tileset %s (%dx%d)", customPath, world->w, world->h);
    Uint32 *oldArt = malloc(sizeof(art));
    int oldMask = customMask;
    Uint32 oldPalette[256];
    byte oldAvailable[512];
    if (!oldArt)
    {
        SDL_DestroySurface(world);
        SDL_DestroySurface(actors);
        free(nextProtection);
        DEBUG_Error("Cannot allocate tileset loading buffer");
        return false;
    }
    memcpy(oldArt, art, sizeof(art));
    memcpy(oldPalette, palette, sizeof(palette));
    memcpy(oldAvailable, available, sizeof(available));
    memset(available, 0, sizeof(available));
    memset(palette, 0, sizeof(palette));
    bool ok = readSheet(world, choice, 0, nextProtection) &&
              (choice != TILESET_SHARP || readSheet(actors, choice, 256, nextProtection));
    SDL_DestroySurface(world);
    SDL_DestroySurface(actors);
    if (!ok)
    {
        customMask = oldMask;
        memcpy(art, oldArt, sizeof(art));
        memcpy(palette, oldPalette, sizeof(palette));
        memcpy(available, oldAvailable, sizeof(available));
        DEBUG_Error("Tileset %s has invalid dimensions or palette", names[choice]);
    }
    else
    {
        memcpy(protection, nextProtection, sizeof(protection));
        selected = choice;
        mountedRevision = MOD_Revision();
        revision++;
        TILESET_Refresh();
        debug("Selected %s tileset", names[choice]);
    }
    free(nextProtection);
    free(oldArt);
    return ok;
}
int TILESET_Pixel(int tile, int x, int y)
{
    sample = 0;
    unsigned slot = TYPES_ArtSlot((uint32_t)tile);
    unsigned fw = 1, fh = 1;
    if (slot)
        TYPES_ArtSize(tile, D_5893_map_id == 0, &fw, &fh);
    if (slot && x >= 0 && y >= 0 && x < (int)(16 * fw) && y < (int)(16 * fh)) {
        sample = TILE_ART_TOKEN + (slot - 512) * 4096 + (((unsigned)tile >> 16) & 3) * 1024 +
                 y * 32 + x + 1;
        return 16;
    }
    if (!selected || tile < 0 || tile >= 512 || x < 0 || y < 0 || x >= 16 || y >= 16 || !available[tile])
        return -1;
    sample = (unsigned)(tile * 256 + y * 16 + x + 1);
    for (int sy = 0; sy < 2; sy++)
        for (int sx = 0; sx < 2; sx++)
        {
            Uint32 color = art[tile][(y * 2 + sy) * 32 + x * 2 + sx];
            if (color)
                return selected == TILESET_CUSTOM ? 16 : colorIndex(color);
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
