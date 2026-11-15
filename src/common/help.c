#include "common.h"
#include "help.h"
#include "engine_settings.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "graphics/tileset.h"
#include "graphics/crt.h"
#include "key/mouse.h"
#include "time/time.h"
#include <SDL3/SDL.h>
#include <string.h>

typedef struct { const char* key; const char* action; } HelpRow;
static const HelpRow commands[]={
    {"A","Attack"},{"B","Board transport"},{"C","Cast spell"},
    {"E","Enter location"},{"F","Fire cannon"},{"G","Get"},
    {"H","Camp/hull repair"},{"I","Ignite torch"},{"J","Jimmy lock"},
    {"K","Climb stairs"},{"L","Look"},{"M","Mix reagents"},
    {"N","New party order"},{"O","Open door/chest"},{"P","Push/pull"},
    {"Q","Quit/save"},{"R","Ready equipment"},{"S","Search"},{"T","Talk"},
    {"U","Use item"},{"V","Mapping gem"},{"X","Exit transport"},
    {"Y","Yell"},{"Z","Stats/menu"},{"Space","Pass"},
    {"1-6","Active player"},{"0","Rejoin party"},
    {"Ctrl+K","Karma"},{"Ctrl+E","Exit game"},{"Ctrl+B","Key buffer"},
    {"Ctrl+M","Turn around"},{"Ctrl+S","Sound"},{"Ctrl+V","Version"},
    {"Ctrl+H","Help"},{"Ctrl+O","Options"},{"Ctrl+W","Save"},
    {"Ctrl+L","Load"},{"Tab","Map"},
};
static void draw(void)
{
    TILESET_Copy(g_linearEgaBuffer0,NULL,320*200);
    memset(g_linearEgaBuffer0,0,320*200);ENGINE_UIFrame();
    ENGINE_UIText(92,12,"Keyboard Commands",15);
    for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
        int x=i<19?16:168,y=28+(i%19)*8;
        ENGINE_UIText(x,y,commands[i].key,i>=33?11:14);
        ENGINE_UIText(x+((int)strlen(commands[i].key)+1)*8,y,commands[i].action,15);
    }
    ENGINE_UIText(124,183,"Esc: Back",15);
    GRAP_BUF_MarkDirty();GRAP_BUF_Present();
}
void HELP_Show(void)
{
    extern void KEY_SDL_ClearInput(void);
    byte backup[320*200];TILESET_Register(backup,sizeof(backup));
    TILESET_Copy(backup,g_linearEgaBuffer0,sizeof(backup));memcpy(backup,g_linearEgaBuffer0,sizeof(backup));
    bool pixelUI=GRAP_SDL_PixelUI(),pointer=MOUSE_PointerMode();
    KEY_SDL_ClearInput();MOUSE_Cancel();TIME_PauseGameClock(true);
    GRAP_SDL_SetPixelUI(true);MOUSE_SetPointerMode(true);
    bool done=false;
    draw();
    while(!done) {
        SDL_Event e;bool cursorMoved=false;
        while(!done && SDL_PollEvent(&e)) {
            if(e.type==SDL_EVENT_QUIT || (e.type==SDL_EVENT_KEY_DOWN && e.key.key==SDLK_E && (e.key.mod&SDL_KMOD_CTRL))) exit(0);
            if(e.type==SDL_EVENT_KEY_DOWN) {
                if(e.key.key==SDLK_ESCAPE || (e.key.key==SDLK_H && (e.key.mod&SDL_KMOD_CTRL) && !e.key.repeat)) done=true;
            }
            if(e.type==SDL_EVENT_MOUSE_MOTION) cursorMoved=true;
            if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT) {
                float x,y;
                if(GRAP_SDL_MouseUIPoint(e.button.x,e.button.y,&x,&y) && y>=182 && y<192) {
                    if(x>=124 && x<196) done=true;
                }
            }
        }
        MOUSE_UpdateCursor();
        if(cursorMoved && CRT_Enabled() && !done) GRAP_BUF_Present();
        if(!done) SDL_Delay(16);
    }
    TIME_PauseGameClock(false);
    KEY_SDL_ClearInput();MOUSE_Cancel();
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP);
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION,SDL_EVENT_MOUSE_WHEEL);
    GRAP_SDL_SetPixelUI(pixelUI);MOUSE_SetPointerMode(pointer);
    TILESET_Copy(g_linearEgaBuffer0,backup,sizeof(backup));memcpy(g_linearEgaBuffer0,backup,sizeof(backup));
    TILESET_Unregister(backup);GRAP_BUF_MarkDirty();GRAP_BUF_Present();
}
