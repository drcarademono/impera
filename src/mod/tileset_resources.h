#ifndef IMPERA_TILESET_RESOURCES_H
#define IMPERA_TILESET_RESOURCES_H
#include <string.h>
#include <stddef.h>
#include <stdint.h>
typedef struct ModTilesetSheet {
    const char *resource, *filename, *title;
    int columns, width, height, offset;
} ModTilesetSheet;
static const ModTilesetSheet modTilesetSheets[] = {
    {"TILE_AMI.PNG", "Ultima_5_Tiles_Amiga.png", "Amiga", 32, 16, 16, 0},
    {"TILE_APP.PNG", "Ultima_5_Tiles_AppleII.png", "Apple II", 32, 14, 16, 0},
    {"TILE_GRY.PNG", "Ultima_5_Tiles_Grayscale.png", "Grayscale", 32, 16, 16, 0},
    {"TILE_SHW.PNG", "Ultima_5_Tiles_SharpX68000_World.png", "Sharp X68000 — Terrain", 16, 32, 32,
     0},
    {"TILE_SHN.PNG", "Ultima_5_Tiles_SharpX68000_NPC.png", "Sharp X68000 — Actors", 16, 32, 32,
     256},
    {"MASK_DOS.PNG", "Ultima_5_Mask_DOS.png", "DOS mask", 32, 16, 16, 0},
    {"MASK_AMI.PNG", "Ultima_5_Mask_Amiga.png", "Amiga mask", 32, 16, 16, 0},
    {"MASK_APP.PNG", "Ultima_5_Mask_AppleII.png", "Apple II mask", 32, 14, 16, 0},
    {"MASK_GRY.PNG", "Ultima_5_Mask_Grayscale.png", "Grayscale mask", 32, 16, 16, 0},
    {"MASK_SHW.PNG", "Ultima_5_Mask_Sharp_World.png", "Sharp terrain mask", 16, 32, 32, 0},
    {"MASK_SHN.PNG", "Ultima_5_Mask_Sharp_NPC.png", "Sharp actors mask", 16, 32, 32, 256},
    {"MASK_CUS.PNG", "Ultima_5_Mask_Custom.png", "Custom tileset mask", 32, 16, 16, 0}};
static inline int MOD_IsTilesetMask(const char *name) {
    return !strncmp(name, "MASK_", 5) || strstr(name, "Ultima_5_Mask_") == name;
}
static inline const ModTilesetSheet *MOD_TilesetMask(unsigned variant, unsigned first) {
    static const char *names[] = {"MASK_DOS.PNG", "MASK_AMI.PNG", "MASK_APP.PNG",
                                  "MASK_GRY.PNG", "MASK_SHW.PNG", "MASK_CUS.PNG"};
    const char *name = variant == 4 && first == 256 ? "MASK_SHN.PNG" : names[variant % 6];
    for (unsigned i = 0; i < sizeof(modTilesetSheets) / sizeof(modTilesetSheets[0]); ++i)
        if (!strcmp(modTilesetSheets[i].resource, name))
            return &modTilesetSheets[i];
    return NULL;
}
static inline const ModTilesetSheet *MOD_TilesetSheet(const char *name) {
    for (unsigned i = 0; i < sizeof(modTilesetSheets) / sizeof(modTilesetSheets[0]); i++)
        if (!strcmp(name, modTilesetSheets[i].resource) ||
            !strcmp(name, modTilesetSheets[i].filename))
            return &modTilesetSheets[i];
    return NULL;
}
static inline int MOD_TilesetPngHeader(const ModTilesetSheet *sheet, const unsigned char *data,
                                       size_t size) {
    static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 33 || memcmp(data, signature, 8) || memcmp(data + 12, "IHDR", 4))
        return 0;
    uint32_t width = ((uint32_t)data[16] << 24) | ((uint32_t)data[17] << 16) |
                     ((uint32_t)data[18] << 8) | data[19];
    uint32_t height = ((uint32_t)data[20] << 24) | ((uint32_t)data[21] << 16) |
                      ((uint32_t)data[22] << 8) | data[23];
    return width == (uint32_t)(sheet->columns * sheet->width) &&
           height == (uint32_t)(16 * sheet->height);
}
#endif
