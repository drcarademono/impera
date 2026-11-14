#include "common.h"
#include "world_map.h"
#include "world_map_palette.h"
#include "file.h"
#include "funcs.h"
#include "mod/types.h"
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
#include <string.h>

extern void KEY_SDL_ClearInput(void);

/* Interior coordinates are local to the building/dungeon, not Britannia. */
static bool partyPosition(int* x, int* y)
{
    int world = D_5895_map_level;
    unsigned map = D_5893_map_id;
    *x = D_5896_map_x; *y = D_5897_map_y;
    if(map) {
        if(!MAPS_ReturnPoint(map, &world, x, y)) {
            const unsigned char* r=MAPS_Record(MAPS_IsDungeon(map)?4:1,map,0);
            if(r && (int8_t)r[14]!=-2) {
                world=(int8_t)r[14]; *x=r[15]; *y=r[16];
            } else {
                if(map > 40 || map == 25) return false;
                *x = MOD_TableValue(7, map-1, D_1e8a[map-1]);
                *y = MOD_TableValue(8, map-1, D_1eb2[map-1]);
                world = 0;
            }
        }
    }
    return world == 0;
}

SDL_Surface* WORLD_MAP_Generate(void)
{
    int w=MAPS_ActiveWidth(0,0), h=MAPS_ActiveHeight(0,0);
    SDL_Surface* image=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_ARGB8888);
    if(!image) return NULL;
    const unsigned char* extended=MAPS_ActiveCell(0,0,0,0);
    FILE* file=extended?NULL:FILE_Open("BRIT.DAT","rb");
    if(!extended && !file) { SDL_DestroySurface(image); return NULL; }
    unsigned char blocks[256][256],loaded[256]={0};
    const uint8_t* index=MOD_BritanniaIndex();
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        unsigned tile;
        if(extended) tile=*MAPS_ActiveCell(0,0,x,y);
        else {
            int block=(y/16)*16+x/16, resource=index[block];
            if(!loaded[block]) {
                if(resource==255) memset(blocks[block],1,256);
                else if(fseek(file,resource*256,SEEK_SET) || fread(blocks[block],1,256,file)!=256) {
                    fclose(file); SDL_DestroySurface(image); return NULL;
                }
                loaded[block]=1;
            }
            tile=blocks[block][(y%16)*16+x%16];
            /* Include live native terrain changes in the resident world blocks. */
            if(D_58a4 && !D_5893_map_id && !D_5895_map_level &&
               ((x-D_589b)&255)<32 && ((y-D_589c)&255)<32)
                tile=*ULTIMA_4402_GetTileAddr(x,y);
        }
        TypeCell cell={0}; cell.x=x; cell.y=y;
        uint32_t type=TYPES_At(cell,tile);
        TypeDef def;
        if(TYPES_Get(type,&def) && !def.kind) tile=def.base;
        Uint32* row=(Uint32*)((unsigned char*)image->pixels+y*image->pitch);
        row[x]=0xff000000u | world_map_colors[tile&255];
    }
    if(file) fclose(file);
    return image;
}

void WORLD_MAP_Show(void)
{
    SDL_Surface* image=WORLD_MAP_Generate();
    if(!image) {
        DEBUG_Error("Cannot generate the world map from active Britannia terrain");
        KEY_SDL_ClearInput(); MOUSE_Cancel(); return;
    }
    int width=image->w,height=image->h;
    if(!GRAP_SDL_SetWorldMap(image)) { SDL_DestroySurface(image); return; }
    SDL_DestroySurface(image);

    int px=0, py=0;
    bool marker = D_57bc && partyPosition(&px, &py);
    float cx = marker ? px+0.5f : width/2.0f, cy = marker ? py+0.5f : height/2.0f;
    float zoom=1;
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
                zoom=SDL_clamp(zoom*SDL_powf(1.25f,e.wheel.y*direction),0.25f,8.0f);
                cx-=e.wheel.x*direction*12/zoom;
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
            cx=SDL_clamp(cx,0,width); cy=SDL_clamp(cy,0,height);
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
