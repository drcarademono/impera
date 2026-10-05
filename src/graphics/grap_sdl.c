#include "common/common.h"
#include "common/settings.h"

#include "grap_buf.h"
#include "grap_ops.h"
#include "origin.h"
#include "wd.h"
#include "grap_sdl.h"
#include "widescreen.h"
#include "vars.h"
#include "funcs.h"
#include <string.h>

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
static bool s_smoothMovement, s_mapDrawn, s_previousValid;
static Uint32* s_previousPixels;
static int s_previousWidth, s_previousHeight, s_previousX, s_previousY, s_previousMap, s_previousLevel;
void GRAP_SDL_SetSmoothMovement(bool enabled) { s_smoothMovement = enabled; s_previousValid = false; }
void GRAP_SDL_MapDrawn(void) { s_mapDrawn = true; }


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
    free(s_previousPixels);
    s_previousPixels = NULL;
    s_previousValid = s_mapDrawn = false;

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

/* Snapshot only completed map redraws, never intermediate text updates. The
 * camera follows the party: terrain slides while the player stays centered. */
static void SmoothFrame(const byte* indices, int w, int h, int mapX, int mapY,
                        int columns, int rows, SDL_Texture* native, int sourceScale, SDL_FRect dst)
{
    if (!s_smoothMovement || !s_mapDrawn || !D_58a4 || D_5893_map_id > 32) return;
    s_mapDrawn = false;
    SDL_Surface* clean = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (!clean) { s_previousValid = false; return; }
    for (int y = 0; y < h; y++) {
        Uint32* row = (Uint32*)((byte*)clean->pixels + y * clean->pitch);
        for (int x = 0; x < w; x++) row[x] = s_egaPalette[indices[y*w+x] & 15];
    }
    int playerX = mapX + columns / 2 * 16, playerY = mapY + rows / 2 * 16;
    byte terrain = *ULTIMA_4402_GetTileAddr(D_5896_map_x, D_5897_map_y);
    for (int y = 0; y < 16; y++) {
        Uint32* row = (Uint32*)((byte*)clean->pixels + (playerY+y)*clean->pitch);
        for (int x = 0; x < 16; x++) row[playerX+x] = s_egaPalette[GRAP_BUF_TilePixel(D_b11e[terrain],x,y) & 15];
    }
    int dx = D_5896_map_x - s_previousX, dy = D_5897_map_y - s_previousY;
    if (!D_5893_map_id) { dx = ((dx+128)&255)-128; dy = ((dy+128)&255)-128; }
    bool animate = s_previousValid && s_previousWidth == w && s_previousHeight == h &&
        s_previousMap == D_5893_map_id && s_previousLevel == D_5895_map_level &&
        abs(dx) <= 1 && abs(dy) <= 1 && (dx || dy);
    if (animate) {
        debug("Smooth movement: offset=%d,%d\n", dx, dy);
        SDL_Texture* old = SDL_CreateTexture(s_sdlRenderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,w,h);
        SDL_Texture* next = SDL_CreateTextureFromSurface(s_sdlRenderer,clean);
        if (old && next) {
            SDL_UpdateTexture(old,NULL,s_previousPixels,w*4);
            SDL_SetTextureScaleMode(old,SDL_SCALEMODE_NEAREST);
            SDL_SetTextureScaleMode(next,SDL_SCALEMODE_NEAREST);
            float sx = dst.w / w, sy = dst.h / h;
            SDL_Rect clip = {(int)(dst.x+mapX*sx),(int)(dst.y+mapY*sy),
                             (int)(columns*16*sx),(int)(rows*16*sy)};
            SDL_FRect mapSrc = {mapX,mapY,columns*16,rows*16};
            SDL_FRect mapDst = {dst.x+mapX*sx,dst.y+mapY*sy,columns*16*sx,rows*16*sy};
            SDL_FRect playerSrc = {playerX*sourceScale,playerY*sourceScale,16*sourceScale,16*sourceScale};
            SDL_FRect playerDst = {dst.x+playerX*sx,dst.y+playerY*sy,16*sx,16*sy};
            for (int frame = 1; frame <= 8; frame++) {
                float t = frame / 8.0f;
                float progress = t*t*(3-2*t);
                SDL_RenderTexture(s_sdlRenderer,native,NULL,&dst);
                SDL_SetRenderClipRect(s_sdlRenderer,&clip);
                SDL_FRect a = mapDst, b = mapDst;
                a.x -= SDL_roundf(dx*16*sx*progress); a.y -= SDL_roundf(dy*16*sy*progress);
                b.x += SDL_roundf(dx*16*sx*(1-progress)); b.y += SDL_roundf(dy*16*sy*(1-progress));
                SDL_RenderTexture(s_sdlRenderer,old,&mapSrc,&a);
                SDL_RenderTexture(s_sdlRenderer,next,&mapSrc,&b);
                SDL_RenderTexture(s_sdlRenderer,native,&playerSrc,&playerDst);
                SDL_SetRenderClipRect(s_sdlRenderer,NULL);
                SDL_RenderPresent(s_sdlRenderer);
                SDL_PumpEvents();
                if (frame < 8) SDL_Delay(16);
            }
        }
        SDL_DestroyTexture(old); SDL_DestroyTexture(next);
    }
    if (s_previousWidth != w || s_previousHeight != h || !s_previousPixels) {
        free(s_previousPixels);
        s_previousPixels = malloc((size_t)w*h*4);
    }
    s_previousValid = s_previousPixels != NULL;
    if (s_previousValid) {
        for (int y = 0; y < h; y++) memcpy(s_previousPixels+y*w,(byte*)clean->pixels+y*clean->pitch,w*4);
        s_previousWidth=w; s_previousHeight=h; s_previousX=D_5896_map_x; s_previousY=D_5897_map_y;
        s_previousMap=D_5893_map_id; s_previousLevel=D_5895_map_level;
    }
    SDL_DestroySurface(clean);
}

extern void DisplayDebugMessages(void);

void GRAP_SDL_FlushFrame(void)
{
    s_expandedFrame = false;
    if (!D_58a4 || D_5893_map_id > 32) s_previousValid = false;
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
            SmoothFrame(s_widePixels,layout.width,layout.height,layout.mapX,layout.mapY,
                        layout.columns,layout.rows,s_wideTexture,1,dst);
        }
        else
        {
            SDL_FRect dst = {(width - 320 * layout.scale) / 2,
                             (height - 200 * layout.scale) / 2,
                             320 * layout.scale, 200 * layout.scale};
            SDL_RenderTexture(s_sdlRenderer, s_sdlTexture, &srcRect, &dst);
            SmoothFrame(g_linearEgaBuffer0,320,200,8,8,11,11,s_sdlTexture,2,dst);
        }
    }
    else {
        SDL_RenderTexture(s_sdlRenderer, s_sdlTexture, &srcRect, NULL);
        int w,h;
        if (SDL_GetRenderOutputSize(s_sdlRenderer,&w,&h)) {
            SDL_FRect dst = {0,0,w,h};
            SmoothFrame(g_linearEgaBuffer0,320,200,8,8,11,11,s_sdlTexture,2,dst);
        }
    }
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
