/* Synthetic sprite fixtures have their own cutouts, unrelated to vanilla art.
 * Override the real artwork's protection mask while testing those fixtures. */
#include <SDL3/SDL.h>
static char *syntheticPreviousTilesets;
static void syntheticMaskBegin(void) {
    const char *previous = SDL_getenv("IMPERA_TILESETS");
    syntheticPreviousTilesets = previous ? SDL_strdup(previous) : NULL;
    assert(SDL_CreateDirectory("synthetic-tileset"));
    SDL_Surface *mask = SDL_CreateSurface(512, 256, SDL_PIXELFORMAT_RGBA32);
    assert(mask);
    assert(SDL_FillSurfaceRect(mask, NULL, SDL_MapSurfaceRGBA(mask, 0, 0, 0, 255)));
    assert(SDL_SavePNG(mask, "synthetic-tileset/Ultima_5_Mask_DOS.png"));
    SDL_DestroySurface(mask);
    assert(SDL_setenv_unsafe("IMPERA_TILESETS", "synthetic-tileset", 1) == 0);
}
static void syntheticMaskEnd(void) {
    if (syntheticPreviousTilesets) {
        assert(SDL_setenv_unsafe("IMPERA_TILESETS", syntheticPreviousTilesets, 1) == 0);
        SDL_free(syntheticPreviousTilesets);
    } else
        assert(SDL_unsetenv_unsafe("IMPERA_TILESETS") == 0);
    assert(SDL_RemovePath("synthetic-tileset/Ultima_5_Mask_DOS.png"));
    assert(SDL_RemovePath("synthetic-tileset"));
}
