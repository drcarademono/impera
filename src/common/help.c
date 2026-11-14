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
typedef struct { const char* title; HelpRow rows[14]; } HelpPage;
static const HelpPage pages[]={
    {"Game commands A-M",{
        {"A","Attack"},{"B","Board transport"},{"C","Cast a spell"},
        {"D","Unassigned"},{"E","Enter a location"},{"F","Fire ship cannons"},
        {"G","Get an item"},{"H","Hole up / rest"},{"I","Ignite a torch"},
        {"J","Jimmy a lock"},{"K","Klimb"},{"L","Look"},{"M","Mix reagents"},
        {"","Depends on location."}}},
    {"Game commands N-Z",{
        {"N","New party order"},{"O","Open"},{"P","Push"},
        {"Q","Save game (then Y)"},{"R","Ready equipment"},{"S","Search"},
        {"T","Talk"},{"U","Use an item"},{"V","View with a gem"},
        {"W","Unassigned"},{"X","X-it transport"},{"Y","Yell"},{"Z","Z-stats / inventory"},
        {"","Depends on location."}}},
    {"Movement and party",{
        {"Arrows","Move / pick direction"},{"Numpad","8/2/4/6: N/S/W/E"},
        {"Home/PgUp","Northwest / northeast"},{"End/PgDn","Southwest / southeast"},
        {"Numpad","7/9/1/3: diagonals"},{"2 arrows","Diagonal when enabled"},
        {"Space","Pass / stop sailing"},{"1-9","Set active player"},{"0","Clear active player"},
        {"Dungeon:",""},{"Up/Down","Advance / back up"},{"Left/Right","Turn left / right"},
        {"Enter / .","Turn around"},{"Esc/Space","Cancel direction prompt"}}},
    {"Combat",{
        {"Arrows","Move / choose target"},{"A","Attack / aim"},{"C","Cast a spell"},
        {"G / J","Get / Jimmy"},{"K / O","Klimb / Open"},{"P / R","Push / Ready"},
        {"S / U","Search / Use item"},{"Y / Z","Yell / Z-stats"},
        {"1-8 / 0","Set / clear active plr"},{"Space","Pass the turn"},
        {"Ctrl+S","Toggle sound"},{"Ctrl+B","Toggle key buffering"},
        {"Esc","Escape after victory"},{"","Other commands vary."}}},
    {"Engine and map",{
        {"Ctrl+H","Open / close help"},{"Ctrl+O","Engine Options"},
        {"Ctrl+W","Save Game browser"},{"Ctrl+L","Load Game browser"},
        {"Tab","Open / close world map"},{"Map:",""},{"Arrows","Pan the map"},
        {"Drag","Pan with left button"},{"Wheel","Zoom out / in"},{"Esc / Tab","Close map"},
        {"","Map/save/load shortcuts"},{"","at travel prompts only."},
        {"","Sextant: flashing +"},{"","Map/help pause gameplay"}}},
    {"Menus and other keys",{
        {"Ctrl+S","Toggle sound"},{"Ctrl+B","Toggle key buffering"},
        {"Ctrl+K","Display karma"},{"Ctrl+V","Display engine version"},
        {"Ctrl+E","Exit (confirm in game)"},{"Menu O","Engine Options"},
        {"Arrows","Select / adjust options"},{"Enter","Select / confirm"},
        {"Tab","Switch Options tabs"},{"PgUp/PgDn","Page save/load slots"},
        {"Delete","Delete save (confirm)"},{"Esc","Back / cancel"},
        {"Backspace","Erase input character"},{"Ctrl+M","Enter in game prompts"}}},
};
#define PAGE_COUNT ((int)(sizeof(pages)/sizeof(pages[0])))
static void draw(int page)
{
    TILESET_Copy(g_linearEgaBuffer0,NULL,320*200);
    memset(g_linearEgaBuffer0,0,320*200);ENGINE_UIFrame();
    ENGINE_UIText(108,18,"Keyboard Help",15);
    char title[35];SDL_snprintf(title,sizeof(title),"%d/%d  %s",page+1,PAGE_COUNT,pages[page].title);
    ENGINE_UIText(24,33,title,11);
    for(int row=0;row<14;row++) {
        const HelpRow* entry=&pages[page].rows[row];
        if(entry->key) ENGINE_UIText(24,48+row*9,entry->key,14);
        if(entry->action) ENGINE_UIText(112,48+row*9,entry->action,15);
    }
    ENGINE_UIText(24,182,"< Previous",page?15:8);
    ENGINE_UIText(136,182,"Next >",page+1<PAGE_COUNT?15:8);
    ENGINE_UIText(232,182,"Esc:Back",15);
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
    int page=0;bool done=false,redraw=true;
    while(!done) {
        if(redraw) { draw(page);redraw=false; }
        SDL_Event e;bool cursorMoved=false;
        while(!done && SDL_PollEvent(&e)) {
            int previous=page;
            if(e.type==SDL_EVENT_QUIT || (e.type==SDL_EVENT_KEY_DOWN && e.key.key==SDLK_E && (e.key.mod&SDL_KMOD_CTRL))) exit(0);
            if(e.type==SDL_EVENT_KEY_DOWN) {
                if(e.key.key==SDLK_ESCAPE || (e.key.key==SDLK_H && (e.key.mod&SDL_KMOD_CTRL) && !e.key.repeat)) done=true;
                switch(e.key.key) {
                case SDLK_LEFT:case SDLK_UP:case SDLK_PAGEUP:page--;break;
                case SDLK_RIGHT:case SDLK_DOWN:case SDLK_PAGEDOWN:case SDLK_TAB:case SDLK_SPACE:case SDLK_RETURN:page++;break;
                case SDLK_HOME:page=0;break;
                case SDLK_END:page=PAGE_COUNT-1;break;
                }
            }
            if(e.type==SDL_EVENT_MOUSE_WHEEL) {
                float delta=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-e.wheel.y:e.wheel.y;
                page+=delta>0?-1:delta<0?1:0;
            }
            if(e.type==SDL_EVENT_MOUSE_MOTION) cursorMoved=true;
            if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT) {
                float x,y;
                if(GRAP_SDL_MouseUIPoint(e.button.x,e.button.y,&x,&y) && y>=178 && y<194) {
                    if(x>=24 && x<112) page--;
                    else if(x>=136 && x<200) page++;
                    else if(x>=232 && x<296) done=true;
                }
            }
            page=SDL_clamp(page,0,PAGE_COUNT-1);redraw|=page!=previous;
        }
        MOUSE_UpdateCursor();
        if(cursorMoved && CRT_Enabled() && !redraw && !done) GRAP_BUF_Present();
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
