#include "common.h"
#include "world_map.h"
#if defined(IMPERA_EMBED_WORLD_MAP)
#include "world_map_image.h"
#else
static const unsigned char world_map_png[] = {0};
#define WORLD_MAP_PNG_SIZE 0
#endif
#include "debug.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "key/key.h"
#include "key/mouse.h"
#include "mod/maps.h"
#include "mod/runtime.h"
#include "time/time.h"
#include "vars.h"
#include <SDL3/SDL.h>

extern void KEY_SDL_ClearInput(void);

/* Interior coordinates are local to the building/dungeon, not Britannia. */
static bool partyPosition(int* x, int* y)
{
    int world = D_5895_map_level;
    unsigned map = D_5893_map_id;
    *x = D_5896_map_x; *y = D_5897_map_y;
    if(map) {
        if(!MAPS_ReturnPoint(map, &world, x, y)) {
            if(map > 40 || map == 25) return false;
            *x = MOD_TableValue(7, map-1, D_1e8a[map-1]);
            *y = MOD_TableValue(8, map-1, D_1eb2[map-1]);
            world = 0;
        }
    }
    return world == 0;
}

void WORLD_MAP_Show(void)
{
    SDL_Surface* image = SDL_LoadPNG("textures/maps/U5brit.png");
    if(!image) {
        char path[4096];
        const char* base=SDL_GetBasePath();
        if(base && SDL_snprintf(path,sizeof(path),"%stextures/maps/U5brit.png",base)<(int)sizeof(path))
            image=SDL_LoadPNG(path);
    }
    if(!image && WORLD_MAP_PNG_SIZE) {
        SDL_IOStream* io = SDL_IOFromConstMem(world_map_png, WORLD_MAP_PNG_SIZE);
        if(io) image = SDL_LoadPNG_IO(io, true);
    }
    if(image && (image->w != 256 || image->h != 256)) {
        DEBUG_Error("World map must be 256x256: textures/maps/U5brit.png");
        SDL_DestroySurface(image); image = NULL;
    }
    if(!image) {
        DEBUG_Error("Add the map artwork at textures/maps/U5brit.png and rebuild");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Map artwork missing",
            "Add the 256x256 map at textures/maps/U5brit.png and rebuild Impera.",NULL);
        KEY_SDL_ClearInput(); MOUSE_Cancel(); return;
    }
    if(!GRAP_SDL_SetWorldMap(image)) { SDL_DestroySurface(image); return; }
    SDL_DestroySurface(image);

    int px=0, py=0;
    bool marker = D_57bc && partyPosition(&px, &py);
    float cx = marker ? px+0.5f : 128, cy = marker ? py+0.5f : 128;
    float zoom = marker ? 3 : 1;
    bool pointer = MOUSE_PointerMode(), done=false, dragging=false;
    float dragX=0, dragY=0;
    KEY_SDL_ClearInput(); MOUSE_Cancel(); MOUSE_SetPointerMode(true);
    TIME_PauseGameClock(true);
    while(!done) {
        GRAP_SDL_WorldMapView(cx,cy,zoom,px+0.5f,py+0.5f,marker && (SDL_GetTicks()/400)%2==0);
        GRAP_BUF_MarkDirty(); GRAP_BUF_Present();
        SDL_Event e;
        while(!done && SDL_PollEvent(&e)) {
            if(e.type==SDL_EVENT_QUIT || (e.type==SDL_EVENT_KEY_DOWN &&
               e.key.key==SDLK_E && (e.key.mod & SDL_KMOD_CTRL))) exit(0);
            if(e.type==SDL_EVENT_KEY_DOWN) {
                switch(e.key.key) {
                case SDLK_TAB: if(!e.key.repeat) done=true; break;
                case SDLK_ESCAPE: done=true; break;
                case SDLK_LEFT: cx-=8/zoom; break;
                case SDLK_RIGHT: cx+=8/zoom; break;
                case SDLK_UP: cy-=8/zoom; break;
                case SDLK_DOWN: cy+=8/zoom; break;
                }
            }
            if(e.type==SDL_EVENT_MOUSE_WHEEL) {
                float direction=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1;
                cx-=e.wheel.x*direction*12/zoom;
                cy-=e.wheel.y*direction*12/zoom;
            }
            if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT)
                dragging=GRAP_SDL_WorldMapPoint(e.button.x,e.button.y,&dragX,&dragY);
            if(e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT) dragging=false;
            if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { dragging=false; KEY_SDL_ClearInput(); }
            if(e.type==SDL_EVENT_MOUSE_MOTION && dragging) {
                float x,y;
                /* Keep dragging outside the viewport until button-up. */
                GRAP_SDL_WorldMapPoint(e.motion.x,e.motion.y,&x,&y);
                cx-=x-dragX; cy-=y-dragY; dragX=x; dragY=y;
            }
            cx=SDL_clamp(cx,0,256); cy=SDL_clamp(cy,0,256);
        }
        MOUSE_UpdateCursor();
        if(!done) SDL_Delay(16);
    }
    TIME_PauseGameClock(false);
    GRAP_SDL_SetWorldMap(NULL);
    KEY_SDL_ClearInput(); MOUSE_Cancel();
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP);
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION,SDL_EVENT_MOUSE_WHEEL);
    MOUSE_SetPointerMode(pointer);
    GRAP_BUF_MarkDirty(); GRAP_BUF_Present();
}
