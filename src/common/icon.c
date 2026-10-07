#include "common.h"
#include "icon.h"
#include "icon_pixels.h"
#include "debug.h"

void IMPERA_SetWindowIcon(SDL_Window* window)
{
    SDL_Surface* icon = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32,
                                            (void*)impera_icon_rgba, 32 * 4);
    if (!icon || !SDL_SetWindowIcon(window, icon))
        DEBUG_Error("Cannot set Impera window icon: %s", SDL_GetError());
    SDL_DestroySurface(icon);
}
