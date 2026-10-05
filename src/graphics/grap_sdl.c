#include "common/common.h"
#include "common/settings.h"

#include "grap_buf.h"
#include "grap_ops.h"
#include "origin.h"
#include "wd.h"
#include "grap_sdl.h"
#include "widescreen.h"

#include <SDL3/SDL.h>

static SDL_Window* s_sdlWindow;
static SDL_Renderer* s_sdlRenderer;
static SDL_Surface* s_sdlSurface;
static SDL_Texture* s_sdlTexture;
static bool s_fullscreen;
static SDL_Texture* s_wideTexture;
static SDL_Surface* s_wideSurface;
static byte* s_widePixels;
static bool s_expandedFrame;

bool GRAP_SDL_MouseMapPoint(float x, float y, int* dx, int* dy, float* rx, float* ry)
{
    int width, height, windowW, windowH;
    if (!s_sdlWindow || !s_sdlRenderer || !SDL_GetWindowSize(s_sdlWindow, &windowW, &windowH) ||
        !SDL_GetRenderOutputSize(s_sdlRenderer, &width, &height) || windowW <= 0 || windowH <= 0)
        return false;
    float gx = x * width / windowW, gy = y * height / windowH;
    int mapX = 8, mapY = 8, columns = 11, rows = 11;
    if (s_fullscreen) {
        WideLayout l = WIDE_Layout(width, height);
        int canvasW = s_expandedFrame ? l.width : 320;
        int canvasH = s_expandedFrame ? l.height : 200;
        gx = (gx - (width - canvasW * l.scale) / 2) / l.scale;
        gy = (gy - (height - canvasH * l.scale) / 2) / l.scale;
        if (s_expandedFrame) { mapX = l.mapX; mapY = l.mapY; columns = l.columns; rows = l.rows; }
    } else {
        gx = gx * 320 / width;
        gy = gy * 200 / height;
    }
    if (gx < mapX || gy < mapY || gx >= mapX + columns * 16 || gy >= mapY + rows * 16)
        return false;
    *dx = (int)((gx - mapX) / 16) - columns / 2;
    *dy = (int)((gy - mapY) / 16) - rows / 2;
    *rx = (gx - mapX - columns / 2 * 16 - 8) / 16;
    *ry = (gy - mapY - rows / 2 * 16 - 8) / 16;
    return true;
}

void GRAP_SDL_SetFullscreen(bool fullscreen)
{
    s_fullscreen = fullscreen;
}

int windowWidth = 1280;
int windowHeight = 960;

static Uint32 s_egaPalette[16] =
{
    0xff000000, 0xff0000aa, 0xff00aa00, 0xff00aaaa, 0xffaa0000, 0xffaa00aa, 0xffaa5500, 0xffaaaaaa,
    0xff555555, 0xff5555ff, 0xff55ff55, 0xff55ffff, 0xffff5555, 0xffff55ff, 0xffffff55, 0xffffffff,
};

void GRAP_SDL_FlushFrame(void);

void AUDIO_SDL_Init(void);
void AUDIO_SDL_Cleanup(void);

void GRAP_SDL_Initialize(void)
{
    windowWidth = SETTINGS_GetInt("window", "width", windowWidth);
    windowHeight = SETTINGS_GetInt("window", "height", windowHeight);

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (s_fullscreen)
    {
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        if (!mode)
        {
            DEBUG_Error("Cannot detect desktop resolution: %s\n", SDL_GetError());
            exit(EXIT_FAILURE);
        }
        windowWidth = mode->w;
        windowHeight = mode->h;
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    if (!SDL_CreateWindowAndRenderer("Ultima V: Warriors of Destiny", windowWidth, windowHeight,
                                    flags, &s_sdlWindow, &s_sdlRenderer))
    {
        DEBUG_Error("Cannot create game window: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    debug("Game window: %dx%d fullscreen=%d\n", windowWidth, windowHeight, s_fullscreen);

    s_sdlSurface = SDL_CreateSurface(hiresWidth, hiresHeight, SDL_GetPixelFormatForMasks(32, 0xff0000, 0xff00, 0xff, 0xff000000));

    s_sdlTexture = SDL_CreateTextureFromSurface(s_sdlRenderer, s_sdlSurface);
    SDL_SetTextureScaleMode(s_sdlTexture, s_fullscreen ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);

    if (SDL_MUSTLOCK(s_sdlSurface))
    {
        exit(0);
    }

    if (s_sdlSurface->pitch != hiresWidth * 4)
    {
        exit(0);
    }

    GRAP_BUF_Initialize(GRAP_SDL_FlushFrame);
}

void GRAP_SDL_Cleanup(void)
{
    GRAP_BUF_Cleanup();

    SDL_DestroyTexture(s_wideTexture);
    SDL_DestroySurface(s_wideSurface);
    free(s_widePixels);
    s_wideTexture = NULL;
    s_wideSurface = NULL;
    s_widePixels = NULL;

    SDL_DestroyTexture(s_sdlTexture);
    SDL_DestroySurface(s_sdlSurface);
    SDL_DestroyRenderer(s_sdlRenderer);
    SDL_DestroyWindow(s_sdlWindow);

    s_sdlTexture = NULL;
    s_sdlSurface = NULL;
    s_sdlRenderer = NULL;
    s_sdlWindow = NULL;
}

static void LinearToRGB(void)
{
    Uint32* pixels = s_sdlSurface->pixels;
    int pitch = s_sdlSurface->pitch / 4;

    for (int y = 0; y < loresHeight; y++)
    {
        for (int x = 0; x < loresWidth; x++)
        {
            Uint32 color = s_egaPalette[g_linearEgaBuffer0[y * loresWidth + x] & 0xf];
            pixels[(y * 2) * pitch + (x * 2)] = color;
            pixels[(y * 2) * pitch + (x * 2 + 1)] = color;
            pixels[(y * 2 + 1) * pitch + (x * 2)] = color;
            pixels[(y * 2 + 1) * pitch + (x * 2 + 1)] = color;
        }
    }

#if defined(ENABLE_GRAP_OVERLAY)
    for (int y = 0; y < hiresHeight; y++)
    {
        for (int x = 0; x < hiresWidth; x++)
        {
            byte pixel = g_linearOverlayBuffer[y * hiresWidth + x] & 0xf;
            if (pixel != 0)
            {
                pixels[y * pitch + x] = s_egaPalette[pixel];
            }
            // pixels[y * pitch + x] |= egaPalette[pLinearOverlayBuffer[y * hiresWidth + x] & 0xf];
        }
    }
#endif
}

extern void DisplayDebugMessages(void);

void GRAP_SDL_FlushFrame(void)
{
    s_expandedFrame = false;
    LinearToRGB();

    SDL_UpdateTexture(s_sdlTexture, NULL, s_sdlSurface->pixels, s_sdlSurface->pitch);

    SDL_FRect srcRect = {0, 0, hiresWidth, hiresHeight};
    SDL_SetRenderDrawColor(s_sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(s_sdlRenderer);
    if (s_fullscreen)
    {
        int width, height;
        if (!SDL_GetRenderOutputSize(s_sdlRenderer, &width, &height)) return;
        WideLayout layout = WIDE_Layout(width, height);
        if (!s_wideSurface || s_wideSurface->w != layout.width || s_wideSurface->h != layout.height)
        {
            SDL_DestroyTexture(s_wideTexture);
            SDL_DestroySurface(s_wideSurface);
            free(s_widePixels);
            s_wideSurface = SDL_CreateSurface(layout.width, layout.height, SDL_PIXELFORMAT_ARGB8888);
            s_widePixels = malloc((size_t)layout.width * layout.height);
            if (!s_wideSurface || !s_widePixels)
            {
                DEBUG_Error("Cannot allocate expanded game viewport\n");
                exit(EXIT_FAILURE);
            }
            s_wideTexture = SDL_CreateTexture(s_sdlRenderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, layout.width, layout.height);
            if (!s_wideTexture)
            {
                DEBUG_Error("Cannot create expanded viewport texture: %s\n", SDL_GetError());
                exit(EXIT_FAILURE);
            }
            SDL_SetTextureScaleMode(s_wideTexture, SDL_SCALEMODE_NEAREST);
            debug("Expanded layout: %dx%d tiles, pixel scale=%d\n", layout.columns, layout.rows, layout.scale);
        }
        if (WIDE_Compose(s_widePixels, layout))
        {
            s_expandedFrame = true;
            for (int y = 0; y < layout.height; y++)
            {
                Uint32* row = (Uint32*)((byte*)s_wideSurface->pixels + y * s_wideSurface->pitch);
                for (int x = 0; x < layout.width; x++)
                    row[x] = s_egaPalette[s_widePixels[y * layout.width + x] & 15];
            }
            SDL_UpdateTexture(s_wideTexture, NULL, s_wideSurface->pixels, s_wideSurface->pitch);
            SDL_FRect dst = {(width - layout.width * layout.scale) / 2,
                             (height - layout.height * layout.scale) / 2,
                             layout.width * layout.scale, layout.height * layout.scale};
            SDL_RenderTexture(s_sdlRenderer, s_wideTexture, NULL, &dst);
        }
        else
        {
            SDL_FRect dst = {(width - 320 * layout.scale) / 2,
                             (height - 200 * layout.scale) / 2,
                             320 * layout.scale, 200 * layout.scale};
            SDL_RenderTexture(s_sdlRenderer, s_sdlTexture, &srcRect, &dst);
        }
    }
    else
        SDL_RenderTexture(s_sdlRenderer, s_sdlTexture, &srcRect, NULL);
    SDL_RenderPresent(s_sdlRenderer);
}

static GraphicsDriverOps s_winOps =
{
    .Initialize = GRAP_SDL_Initialize,
    .Cleanup = GRAP_SDL_Cleanup,
    .Present = GRAP_BUF_Present,
    .FlushPendingPresent = GRAP_BUF_FlushPendingPresent,
    .SetPenColor = GRAP_BUF_SetPenColor,
    .SetPage = GRAP_BUF_SetPage,
    .PrintChar = GRAP_BUF_PrintChar,
    .ScrollWindow = GRAP_BUF_ScrollWindow,
    .Line = GRAP_BUF_Line,
    .Pset = GRAP_BUF_Pset,
    .FillWindow = GRAP_BUF_FillWindow,
    .LoadTileset = GRAP_BUF_LoadTileset,
    .UnloadTileset = GRAP_BUF_UnloadTileset,
    .AnimateTileset = GRAP_BUF_AnimateTileset,
    .UpdateTimeTileset = GRAP_BUF_UpdateTimeTileset,
    .ShowNextWDFrame = GRAP_BUF_ShowNextWDFrame,
    .AnimateWD = GRAP_BUF_AnimateWD,
    .AnimateOriginLogo = GRAP_BUF_AnimateOriginLogo,
    .PutAnimatedMoongateTile = GRAP_BUF_PutAnimatedMoongateTile,
    .PutTileRevealStep = GRAP_BUF_PutTileRevealStep,
    .PutTile = GRAP_BUF_PutTile,
    .PutImage = GRAP_BUF_PutImage,
    .PutBitImage = GRAP_BUF_PutBitImage,
    .TransferPage = GRAP_BUF_TransferPage,
    .TransferPage_Reveal = GRAP_BUF_TransferPage_Reveal
};

GraphicsDriverOps* GRAP_SDL_GetOps(void)
{
    return &s_winOps;
}
