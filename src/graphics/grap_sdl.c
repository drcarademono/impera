#include "crt.h"
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
#include "macros.h"
#include "key/mouse.h"
#include <string.h>

#include <SDL3/SDL.h>

static SDL_Window* s_sdlWindow;
static SDL_Renderer* s_sdlRenderer;
static SDL_Surface* s_sdlSurface;
static SDL_Texture* s_sdlTexture;
static bool s_fullscreen;
static bool s_pixelUI;
static int s_windowedWidth=1280, s_windowedHeight=960;
static SDL_Texture* s_wideTexture;
static SDL_Texture* s_darknessTexture;
static SDL_Surface* s_wideSurface;
static byte* s_widePixels;
static byte* s_completedWidePixels;
static bool s_completedWideValid;
static int s_completedWideMap,s_completedWideLevel;
static bool s_expandedFrame;
static bool s_smoothMovement, s_mapDrawn, s_previousValid;
static float s_movementSpeed = 1.0f;
static bool s_customMovementSpeed;
static Uint32* s_previousPixels;
static int s_previousWidth, s_previousHeight, s_previousX, s_previousY, s_previousMap, s_previousLevel;
typedef struct ActorVisual {
    bool visible, sighted;
    int x,y,worldX,worldY,kind,level;
    Uint32 sprite[324], terrain[324];
} ActorVisual;
static ActorVisual s_previousActors[32];
static Uint32 s_egaPalette[16];

static void PatchActor(SDL_Surface* surface, const ActorVisual* actor, const Uint32* pixels, int left, int top, int right, int bottom)
{
    for (int y=0;y<18;y++) {
        if (actor->y+y < top || actor->y+y >= bottom) continue;
        for (int x=0;x<18;x++) {
            if (actor->x+x < left || actor->x+x >= right) continue;
            if (pixels[y*18+x] >> 24)
                ((Uint32*)((byte*)surface->pixels+(actor->y+y)*surface->pitch))[actor->x+x]=pixels[y*18+x];
        }
    }
}
static void CaptureActors(ActorVisual* actors, const byte* indices, int w,
                          int mapX,int mapY,int columns,int rows)
{
    memset(actors,0,sizeof(ActorVisual)*32);
    bool combat=D_5893_map_id>=128;
    for (int i=combat?0:1;i<32;i++) {
        ActorFmt* a=&D_5c5a[i];
        if (!a->_0_tile || !a->_1_animTile || (!combat && a->_4_z!=D_5895_map_level)) continue;
        int dx=a->_2_x-(combat?5:D_5896_map_x), dy=a->_3_y-(combat?5:D_5897_map_y);
        if (!D_5893_map_id) { dx=((dx+128)&255)-128; dy=((dy+128)&255)-128; }
        int col=columns/2+dx, row=rows/2+dy;
        ActorVisual* v=&actors[i];
        v->x=mapX+col*16-1; v->y=mapY+row*16-1;
        v->worldX=a->_2_x; v->worldY=a->_3_y;
        v->kind=a->_0_tile<0x30?a->_0_tile:a->_0_tile&0xfc;
        v->level=a->_4_z;
        if (!combat && !dx && !dy) continue;
        if (abs(dx)<=5 && abs(dy)<=5) {
            if (GetMapViewport(dx+5,dy+5)!=0 || GetActorMap(dx+5,dy+5)==0x16) continue;
        } else if (combat || !WIDE_Visible(dx,dy)) continue;
        v->sighted=true;
        if (col<0 || row<0 || col>=columns || row>=rows) continue;
        v->visible=true;
        int tile;
        if (abs(dx)<=5 && abs(dy)<=5) tile=256+GetActorMap(dx+5,dy+5);
        else {
            tile=WIDE_ActorTile(i);
            if(tile<0) {v->visible=v->sighted=false;continue;}
        }
        for (int y=0;y<18;y++) for (int x=0;x<18;x++) {
            int tx=x-1,ty=y-1;
            int nx=dx+(tx<0?-1:tx>=16?1:0),ny=dy+(ty<0?-1:ty>=16?1:0);
            int wx=a->_2_x+nx-dx, wy=a->_3_y+ny-dy;
            int color=WIDE_SpritePixel(tile,combat?a->_2_x-D_5896_map_x:dx,combat?a->_3_y-D_5897_map_y:dy,tx,ty);
            v->sprite[y*18+x]=color<0?0:s_egaPalette[color];
            v->terrain[y*18+x]=s_egaPalette[WIDE_TerrainPixel(combat?wx-D_5896_map_x:nx,combat?wy-D_5897_map_y:ny,(tx+16)%16,(ty+16)%16)&15];
        }
    }
}

void GRAP_SDL_SetSmoothMovement(bool enabled) { s_smoothMovement = enabled; s_previousValid = false; }
bool GRAP_SDL_SmoothMovementEnabled(void) { return s_smoothMovement; }
void GRAP_SDL_SetMovementSpeed(float speed) { s_movementSpeed = speed; s_customMovementSpeed = true; }
bool GRAP_SDL_CustomMovementSpeed(void) { return s_customMovementSpeed; }
unsigned int GRAP_SDL_MovementInterval(void)
{
    /* Match the seven frame waits exactly, including millisecond rounding. */
    if (s_smoothMovement) return 7 * (unsigned int)SDL_roundf(16.0f / s_movementSpeed);
    return (unsigned int)SDL_roundf(160.0f / s_movementSpeed);
}
void GRAP_SDL_MapDrawn(void) { s_mapDrawn = true; }


void GRAP_SDL_CursorSize(int* width, int* height)
{
    int w = 320, h = 200, outputW, outputH;
    if (s_sdlWindow) SDL_GetWindowSize(s_sdlWindow, &w, &h);
    float sx = w / 320.0f, sy = h / 200.0f;
    if ((s_fullscreen || s_pixelUI) && SDL_GetRenderOutputSize(s_sdlRenderer, &outputW, &outputH)) {
        WideLayout l = WIDE_Layout(outputW, outputH);
        sx = l.scale * (float)w / outputW;
        sy = l.scale * (float)h / outputH;
    }
    *width = SDL_max(16, (int)SDL_roundf(16 * sx));
    *height = SDL_max(16, (int)SDL_roundf(16 * sy));
}

bool GRAP_SDL_MouseUIPoint(float x, float y, float* ux, float* uy)
{
    int w, h, ow, oh;
    if (!s_sdlWindow || !SDL_GetWindowSize(s_sdlWindow, &w, &h) || w <= 0 || h <= 0) return false;
    if (!s_fullscreen && !s_pixelUI) { *ux = x * 320 / w; *uy = y * 200 / h; return true; }
    if (!SDL_GetRenderOutputSize(s_sdlRenderer, &ow, &oh)) return false;
    WideLayout l = WIDE_Layout(ow, oh);
    int cw = s_expandedFrame ? l.width : 320, ch = s_expandedFrame ? l.height : 200;
    *ux = (x * ow / w - (ow - cw * l.scale) / 2) / l.scale;
    *uy = (y * oh / h - (oh - ch * l.scale) / 2) / l.scale;
    if (*ux < 0 || *uy < 0 || *ux >= cw || *uy >= ch) return false;
    /* The original right-hand 128 pixels move to the far edge in widescreen. */
    if (s_expandedFrame) {
        if (*ux < l.sidebarX) return false;
        *ux -= l.sidebarX - 192;
    }
    return true;
}

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
    if (s_sdlWindow && s_fullscreen != fullscreen) {
        if (fullscreen) SDL_GetWindowSize(s_sdlWindow,&s_windowedWidth,&s_windowedHeight);
        if (!SDL_SetWindowFullscreen(s_sdlWindow,fullscreen)) {
            debug("Cannot change fullscreen: %s\n",SDL_GetError());
            return;
        }
        if (!fullscreen) SDL_SetWindowSize(s_sdlWindow,s_windowedWidth,s_windowedHeight);
        SDL_SetTextureScaleMode(s_sdlTexture,fullscreen || s_pixelUI ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
        s_previousValid=false;
    }
    s_fullscreen = fullscreen;
}
bool GRAP_SDL_Fullscreen(void) { return s_fullscreen; }
float GRAP_SDL_MovementSpeed(void) { return s_movementSpeed; }
void GRAP_SDL_SetPixelUI(bool enabled)
{
    s_pixelUI=enabled;
    s_previousValid = s_mapDrawn = false;
    if(s_sdlTexture) SDL_SetTextureScaleMode(s_sdlTexture,s_fullscreen || enabled ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
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
    s_windowedWidth=windowWidth; s_windowedHeight=windowHeight;

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

    if(!s_sdlSurface) { DEBUG_Error("Cannot create render surface: %s",SDL_GetError());exit(EXIT_FAILURE); }
    s_sdlTexture = SDL_CreateTextureFromSurface(s_sdlRenderer, s_sdlSurface);
    if(!s_sdlTexture) { DEBUG_Error("Cannot create render texture: %s",SDL_GetError());exit(EXIT_FAILURE); }
    debug("Renderer=%s video=%s",SDL_GetRendererName(s_sdlRenderer),SDL_GetCurrentVideoDriver());
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
    CRT_Cleanup();
    GRAP_BUF_Cleanup();
    free(s_previousPixels);
    s_previousPixels = NULL;
    s_previousValid = s_mapDrawn = false;

    SDL_DestroyTexture(s_darknessTexture);s_darknessTexture=NULL;
    SDL_DestroyTexture(s_wideTexture);
    SDL_DestroySurface(s_wideSurface);
    free(s_widePixels);
    free(s_completedWidePixels);s_completedWidePixels=NULL;s_completedWideValid=false;
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
    if (!s_pixelUI) for (int y = 0; y < hiresHeight; y++)
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

static int s_darknessColumns,s_darknessRows,s_darknessMap,s_darknessLevel;
static void PrepareDarkness(int columns,int rows,bool completed)
{
    if(!WIDE_DitheredDarkness() || s_pixelUI || !D_58a4 || D_5893_map_id>32) {
        SDL_DestroyTexture(s_darknessTexture);s_darknessTexture=NULL;return;
    }
    if(!completed && s_darknessTexture && columns==s_darknessColumns && rows==s_darknessRows && s_darknessMap==D_5893_map_id && s_darknessLevel==D_5895_map_level) return;
    /* One tile of padding keeps the mask over every exposed scrolling edge. */
    int width=(columns+2)*16,height=(rows+2)*16;
    byte* mask=malloc((size_t)width*height);
    SDL_Surface* surface=SDL_CreateSurface(width,height,SDL_PIXELFORMAT_ARGB8888);
    if(mask && surface && WIDE_DarknessMask(mask,columns+2,rows+2)) {
        static const byte pattern[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
        for(int y=0;y<height;y++) {
            Uint32* pixels=(Uint32*)((byte*)surface->pixels+y*surface->pitch);
            for(int x=0;x<width;x++) pixels[x]=pattern[(y&3)*4+(x&3)]<mask[y*width+x]?0xff000000:0;
        }
        SDL_Texture* next=SDL_CreateTextureFromSurface(s_sdlRenderer,surface);
        if(next) {
            SDL_SetTextureBlendMode(next,SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(next,SDL_SCALEMODE_NEAREST);
            SDL_DestroyTexture(s_darknessTexture);s_darknessTexture=next;
            s_darknessColumns=columns;s_darknessRows=rows;s_darknessMap=D_5893_map_id;s_darknessLevel=D_5895_map_level;
        } else DEBUG_Error("Cannot create dithered darkness texture: %s",SDL_GetError());
    } else DEBUG_Error("Cannot allocate dithered darkness mask");
    free(mask);SDL_DestroySurface(surface);
}
static void DrawDarkness(SDL_FRect dst,int width,int height,int mapX,int mapY,int columns,int rows)
{
    if(!s_darknessTexture) return;
    SDL_FRect map={dst.x+mapX*dst.w/width,dst.y+mapY*dst.h/height,columns*16*dst.w/width,rows*16*dst.h/height};
    SDL_FRect source={16,16,columns*16,rows*16};
    SDL_RenderTexture(s_sdlRenderer,s_darknessTexture,&source,&map);
}

/* Snapshot only completed map redraws, never intermediate text updates. The
 * camera follows the party: terrain slides while the player stays centered. */
static void SmoothFrame(const byte* indices, int w, int h, int mapX, int mapY,
                        int columns, int rows, SDL_Texture* native, int sourceScale, SDL_FRect dst)
{
    if (s_pixelUI || !s_smoothMovement || !s_mapDrawn || !D_58a4 || (D_5893_map_id>32 && D_5893_map_id<128)) return;
    s_mapDrawn = false;
    SDL_Surface* clean = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (!clean) { s_previousValid = false; return; }
    for (int y = 0; y < h; y++) {
        Uint32* row = (Uint32*)((byte*)clean->pixels + y * clean->pitch);
        for (int x = 0; x < w; x++) row[x] = s_egaPalette[indices[y*w+x] & 15];
    }
    int playerX = mapX + columns / 2 * 16, playerY = mapY + rows / 2 * 16;
    bool combat=D_5893_map_id>=128;
    for (int y = -1; !combat && y <= 16; y++) {
        Uint32* row = (Uint32*)((byte*)clean->pixels + (playerY+y)*clean->pitch);
        for (int x = -1; x <= 16; x++) {
            if (WIDE_SpritePixel(256+GetActorMap(5,5),0,0,x,y)<0) continue;
            row[playerX+x] = s_egaPalette[WIDE_TerrainPixel(x<0?-1:x>=16?1:0,y<0?-1:y>=16?1:0,(x+16)%16,(y+16)%16) & 15];
        }
    }
    int dx = D_5896_map_x - s_previousX, dy = D_5897_map_y - s_previousY;
    if (!D_5893_map_id) { dx = ((dx+128)&255)-128; dy = ((dy+128)&255)-128; }
    if (combat) dx=dy=0;
    ActorVisual actors[32];
    bool moving[32]={false};
    CaptureActors(actors,indices,w,mapX,mapY,columns,rows);
    bool compatible = s_previousValid && s_previousWidth == w && s_previousHeight == h &&
        s_previousMap == D_5893_map_id && s_previousLevel == D_5895_map_level &&
        abs(dx) <= 1 && abs(dy) <= 1;
    bool animate=compatible && (dx || dy);
    if (compatible) for (int i=0;i<32;i++) {
        ActorVisual* old=&s_previousActors[i]; ActorVisual* next=&actors[i];
        int ax=next->worldX-old->worldX, ay=next->worldY-old->worldY;
        if (!D_5893_map_id) { ax=((ax+128)&255)-128; ay=((ay+128)&255)-128; }
        moving[i]=(old->visible || next->visible) && old->sighted && next->sighted &&
                  old->kind==next->kind && old->level==next->level &&
                  abs(ax)<=1 && abs(ay)<=1 && (ax || ay);
        if (moving[i]) {
            animate=true;
            if (next->visible) {
                for(int y=0;y<18;y++) for(int x=0;x<18;x++)
                    if((next->sprite[y*18+x]>>24) && next->x+x>=mapX && next->x+x<mapX+columns*16 && next->y+y>=mapY && next->y+y<mapY+rows*16)
                        ((Uint32*)((byte*)clean->pixels+(next->y+y)*clean->pitch))[next->x+x]=next->terrain[y*18+x];
            }
            if (old->visible)
                for (int y=0;y<18;y++) for(int x=0;x<18;x++)
                    if((old->sprite[y*18+x]>>24) && old->x+x>=mapX && old->x+x<mapX+columns*16 && old->y+y>=mapY && old->y+y<mapY+rows*16)
                        s_previousPixels[(old->y+y)*w+old->x+x]=old->terrain[y*18+x];
        }
    }
    if (animate) {
        debug("Smooth movement: offset=%d,%d\n", dx, dy);
        /* One padded current map covers all exposed edges, including diagonal
         * corner gaps. Never splice old/new water animation phases together. */
        int paddedWidth=(columns+2)*16,paddedHeight=(rows+2)*16;
        bool* hidden=calloc((size_t)(columns+2)*(rows+2),sizeof(bool));
        SDL_Surface* padded=hidden?SDL_CreateSurface(paddedWidth,paddedHeight,SDL_PIXELFORMAT_ARGB8888):NULL;
        if(padded) for(int row=-1;row<=rows;row++) for(int col=-1;col<=columns;col++) {
            bool interior=col>=0 && col<columns && row>=0 && row<rows;
            int tx=col-columns/2,ty=row-rows/2;
            bool lit=combat || (abs(tx)<=5 && abs(ty)<=5 ? GetMapViewport(tx+5,ty+5)!=255 : WIDE_Visible(tx,ty));
            hidden[(row+1)*(columns+2)+col+1]=!lit;
            bool valid=WIDE_MapTile(tx,ty)!=255;
            for(int y=0;y<16;y++) {
                Uint32* dest=(Uint32*)((byte*)padded->pixels+((row+1)*16+y)*padded->pitch)+(col+1)*16;
                if(interior && (combat || lit)) memcpy(dest,(byte*)clean->pixels+(mapY+row*16+y)*clean->pitch+(mapX+col*16)*4,16*4);
                else for(int x=0;x<16;x++) dest[x]=s_egaPalette[valid?WIDE_TerrainPixel(tx,ty,x,y)&15:0];
            }
        }
        SDL_Texture* next=padded?SDL_CreateTextureFromSurface(s_sdlRenderer,padded):NULL;
        SDL_DestroySurface(padded);
        if (next) {
            SDL_SetTextureScaleMode(next,SDL_SCALEMODE_NEAREST);
            SDL_Texture* actorTextures[32]={NULL};
            for (int i=0;i<32;i++) if (moving[i]) {
                actorTextures[i]=SDL_CreateTexture(s_sdlRenderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,18,18);
                if (actorTextures[i]) {
                    SDL_UpdateTexture(actorTextures[i],NULL,
                        actors[i].visible?actors[i].sprite:s_previousActors[i].sprite,18*4);
                    SDL_SetTextureScaleMode(actorTextures[i],SDL_SCALEMODE_NEAREST);
                    SDL_SetTextureBlendMode(actorTextures[i],SDL_BLENDMODE_BLEND);
                }
            }
            float sx = dst.w / w, sy = dst.h / h;
            SDL_Rect clip = {(int)(dst.x+mapX*sx),(int)(dst.y+mapY*sy),
                             (int)(columns*16*sx),(int)(rows*16*sy)};
            SDL_FRect mapSrc = {0,0,paddedWidth,paddedHeight};
            SDL_FRect mapDst = {dst.x+mapX*sx,dst.y+mapY*sy,columns*16*sx,rows*16*sy};
            SDL_Texture* playerTexture=NULL;
            SDL_FRect playerDst = {dst.x+(playerX-1)*sx,dst.y+(playerY-1)*sy,18*sx,18*sy};
            if (!combat) {
                Uint32 sprite[324];
                for(int y=0;y<18;y++) for(int x=0;x<18;x++) {
                    int color=WIDE_SpritePixel(256+GetActorMap(5,5),0,0,x-1,y-1);
                    sprite[y*18+x]=color<0?0:s_egaPalette[color];
                }
                playerTexture=SDL_CreateTexture(s_sdlRenderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,18,18);
                if(playerTexture) {
                    SDL_UpdateTexture(playerTexture,NULL,sprite,18*4);
                    SDL_SetTextureScaleMode(playerTexture,SDL_SCALEMODE_NEAREST);
                    SDL_SetTextureBlendMode(playerTexture,SDL_BLENDMODE_BLEND);
                }
            }
            for (int frame = 1; frame <= 8; frame++) {
                float t = frame / 8.0f;
                /* Constant speed avoids braking and restarting at every tile. */
                float progress = t;
                /* SDL invalidates the backbuffer after every present. Clear
                 * letterbox areas as well as redrawing the game each time. */
                SDL_RenderClear(s_sdlRenderer);
                SDL_RenderTexture(s_sdlRenderer,native,NULL,&dst);
                SDL_SetRenderClipRect(s_sdlRenderer,&clip);
                SDL_FRect b = {mapDst.x-16*sx,mapDst.y-16*sy,(columns+2)*16*sx,(rows+2)*16*sy};
                b.x += SDL_roundf(dx*16*sx*(1-progress)); b.y += SDL_roundf(dy*16*sy*(1-progress));
                SDL_RenderTexture(s_sdlRenderer,next,&mapSrc,&b);
                for (int i=31;i>=0;i--) if (moving[i]) {
                    ActorVisual* a=&s_previousActors[i]; ActorVisual* b=&actors[i];
                    SDL_FRect rect={dst.x+SDL_roundf((a->x+(b->x-a->x)*progress)*sx),
                                    dst.y+SDL_roundf((a->y+(b->y-a->y)*progress)*sy),18*sx,18*sy};
                    if (actorTextures[i]) SDL_RenderTexture(s_sdlRenderer,actorTextures[i],NULL,&rect);
                    else if (b->visible) {
                        SDL_FRect src={b->x*sourceScale,b->y*sourceScale,16*sourceScale,16*sourceScale};
                        SDL_RenderTexture(s_sdlRenderer,native,&src,&rect);
                    }
                }
                if (playerTexture) SDL_RenderTexture(s_sdlRenderer,playerTexture,NULL,&playerDst);
                /* Cover sprites as well as ground with the authoritative
                 * central visibility and expanded LOS result. */
                if(!combat && s_darknessTexture) {
                    /* Keep wall/door cutouts and the ordered pattern attached to
                     * their terrain throughout the same interpolated movement. */
                    SDL_RenderTexture(s_sdlRenderer,s_darknessTexture,NULL,&b);
                }
                else if(!combat) for(int row=-1;row<=rows;row++) for(int col=-1;col<=columns;col++) if(hidden[(row+1)*(columns+2)+col+1]) {
                    SDL_FRect shadow={b.x+(col+1)*16*sx,b.y+(row+1)*16*sy,16*sx,16*sy};
                    SDL_RenderFillRect(s_sdlRenderer,&shadow);
                }
                SDL_SetRenderClipRect(s_sdlRenderer,NULL);
                CRT_EndFrame(s_sdlRenderer);
                SDL_RenderPresent(s_sdlRenderer);
                CRT_ResumeFrame(s_sdlRenderer);
                SDL_PumpEvents();
                MOUSE_UpdateCursor();
                if (frame < 8) SDL_Delay((Uint32)SDL_roundf(16.0f / s_movementSpeed));
            }
            SDL_DestroyTexture(playerTexture);
            for (int i=0;i<32;i++) SDL_DestroyTexture(actorTextures[i]);
        }
        SDL_DestroyTexture(next);
        free(hidden);
        /* The caller still presents the completed frame (and may capture it).
         * Rebuild that backbuffer after the last interpolation presentation. */
        SDL_RenderClear(s_sdlRenderer);
        SDL_RenderTexture(s_sdlRenderer,native,NULL,&dst);
    }
    for (int i=0;i<32;i++) if (moving[i] && actors[i].visible) PatchActor(clean,&actors[i],actors[i].sprite,mapX,mapY,mapX+columns*16,mapY+rows*16);
    memcpy(s_previousActors,actors,sizeof(actors));
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

/* Thumbnails are rendered at display resolution over the pixel UI. */
static SDL_Texture* s_uiThumbnails[4];
static SDL_FRect s_uiThumbnailRects[4];
void GRAP_SDL_ClearUIThumbnails(void)
{
    for(int i=0;i<4;i++) { SDL_DestroyTexture(s_uiThumbnails[i]);s_uiThumbnails[i]=NULL; }
}
static bool s_captureRequested;
static SDL_Surface* s_capturedFrame;
SDL_Surface* GRAP_SDL_CaptureFrame(void)
{
    /* Render the current gameplay buffer even within the same timer tick.
       SDL backbuffer contents are undefined after SDL_RenderPresent. */
    s_captureRequested=true;
    s_capturedFrame=NULL;
    GRAP_BUF_Present();
    s_captureRequested=false;
    SDL_Surface* frame=s_capturedFrame;
    s_capturedFrame=NULL;
    return frame;
}
void GRAP_SDL_UIThumbnail(int i,const char* path,int x,int y,int w,int h)
{
    if(i<0 || i>=4) return;
    SDL_DestroyTexture(s_uiThumbnails[i]);s_uiThumbnails[i]=NULL;
    SDL_Surface* image=SDL_LoadBMP(path);
    if(image) {
        s_uiThumbnails[i]=SDL_CreateTextureFromSurface(s_sdlRenderer,image);
        SDL_DestroySurface(image);
        if(s_uiThumbnails[i]) SDL_SetTextureScaleMode(s_uiThumbnails[i],SDL_SCALEMODE_LINEAR);
    }
    s_uiThumbnailRects[i]=(SDL_FRect){x,y,w,h};
}
extern void DisplayDebugMessages(void);

void GRAP_SDL_FlushFrame(void)
{
    CRT_BeginFrame(s_sdlRenderer);
    bool completedMap=s_mapDrawn;
    s_expandedFrame = false;
    if(!s_fullscreen || s_pixelUI || !D_58a4) s_completedWideValid=false;
    if (!D_58a4 || (D_5893_map_id>32 && D_5893_map_id<128)) s_previousValid = false;
    LinearToRGB();

    SDL_UpdateTexture(s_sdlTexture, NULL, s_sdlSurface->pixels, s_sdlSurface->pitch);

    SDL_FRect srcRect = {0, 0, hiresWidth, hiresHeight};
    SDL_SetRenderDrawColor(s_sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(s_sdlRenderer);
    if (s_fullscreen || s_pixelUI)
    {
        int width, height;
        if (!SDL_GetRenderOutputSize(s_sdlRenderer, &width, &height)) return;
        WideLayout layout = WIDE_Layout(width, height);
        if (!s_wideSurface || s_wideSurface->w != layout.width || s_wideSurface->h != layout.height)
        {
            SDL_DestroyTexture(s_wideTexture);
            SDL_DestroySurface(s_wideSurface);
            free(s_widePixels);
            free(s_completedWidePixels);s_completedWideValid=false;
            s_completedWidePixels=malloc((size_t)layout.width*layout.height);
            s_wideSurface = SDL_CreateSurface(layout.width, layout.height, SDL_PIXELFORMAT_ARGB8888);
            s_widePixels = malloc((size_t)layout.width * layout.height);
            if (!s_wideSurface || !s_widePixels || !s_completedWidePixels)
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
        if (!s_pixelUI && WIDE_Compose(s_widePixels, layout))
        {
            s_expandedFrame = true;
            /* UI/effect updates can flush between a movement/light change and
             * the original map redraw. Keep expanded tiles from that same
             * completed map; central tiles and modal effects remain live. */
            if(!completedMap && s_completedWideValid && s_completedWideMap==D_5893_map_id && s_completedWideLevel==D_5895_map_level) {
                for(int row=0;row<layout.rows;row++) for(int col=0;col<layout.columns;col++) {
                    if(abs(col-layout.columns/2)<=5 && abs(row-layout.rows/2)<=5) continue;
                    for(int y=0;y<16;y++) {
                        int offset=(layout.mapY+row*16+y)*layout.width+layout.mapX+col*16;
                        memcpy(s_widePixels+offset,s_completedWidePixels+offset,16);
                    }
                }
            }
            memcpy(s_completedWidePixels,s_widePixels,(size_t)layout.width*layout.height);
            s_completedWideMap=D_5893_map_id;s_completedWideLevel=D_5895_map_level;s_completedWideValid=true;
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
            PrepareDarkness(layout.columns,layout.rows,completedMap);
            SmoothFrame(s_widePixels,layout.width,layout.height,layout.mapX,layout.mapY,
                        layout.columns,layout.rows,s_wideTexture,1,dst);
            DrawDarkness(dst,layout.width,layout.height,layout.mapX,layout.mapY,layout.columns,layout.rows);
        }
        else
        {
            s_completedWideValid=false;
            SDL_FRect dst = {(width - 320 * layout.scale) / 2,
                             (height - 200 * layout.scale) / 2,
                             320 * layout.scale, 200 * layout.scale};
            SDL_RenderTexture(s_sdlRenderer, s_sdlTexture, &srcRect, &dst);
            PrepareDarkness(11,11,completedMap);
            SmoothFrame(g_linearEgaBuffer0,320,200,8,8,11,11,s_sdlTexture,2,dst);
            DrawDarkness(dst,320,200,8,8,11,11);
        }
    }
    else {
        SDL_RenderTexture(s_sdlRenderer, s_sdlTexture, &srcRect, NULL);
        int w,h;
        if (SDL_GetRenderOutputSize(s_sdlRenderer,&w,&h)) {
            SDL_FRect dst = {0,0,w,h};
            PrepareDarkness(11,11,completedMap);
            SmoothFrame(g_linearEgaBuffer0,320,200,8,8,11,11,s_sdlTexture,2,dst);
            DrawDarkness(dst,320,200,8,8,11,11);
        }
    }
    if(s_pixelUI) {
        int w,h;
        if(SDL_GetRenderOutputSize(s_sdlRenderer,&w,&h)) {
            WideLayout l=WIDE_Layout(w,h);
            for(int i=0;i<4;i++) if(s_uiThumbnails[i]) {
                SDL_FRect r=s_uiThumbnailRects[i];
                r.x=(w-320*l.scale)/2+r.x*l.scale;
                r.y=(h-200*l.scale)/2+r.y*l.scale;
                r.w*=l.scale;r.h*=l.scale;
                SDL_RenderTexture(s_sdlRenderer,s_uiThumbnails[i],NULL,&r);
            }
        }
    }
    s_mapDrawn=false;
    if(s_captureRequested) s_capturedFrame=SDL_RenderReadPixels(s_sdlRenderer,NULL);
    CRT_EndFrame(s_sdlRenderer);
    SDL_RenderPresent(s_sdlRenderer);
    CRT_ResumeFrame(s_sdlRenderer);
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
