#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common/common.h"
#include "vars.h"
#include "macros.h"
#include "tiles.h"
#include "funcs.h"
#include "talk.h"
#include "key/mouse.h"
#include "graphics/grap_sdl.h"
#include "graphics/grap_buf.h"
#include "graphics/widescreen.h"
#include <SDL3/SDL.h>
static Uint64 ticks = 2000;
static float cursorX, cursorY;
static bool sawSleepingNpc;
void __wrap_ULTIMA_1850_PrintString(char* text)
{
    if (strstr(text, "Zzzzzz")) sawSleepingNpc = true;
}
void __wrap_ULTIMA_16ba_PrintChar(uint ch) { (void)ch; }
Uint64 __wrap_SDL_GetTicks(void) { return ticks; }
SDL_MouseButtonFlags __wrap_SDL_GetMouseState(float* x, float* y) { *x=cursorX; *y=cursorY; return SDL_BUTTON_RMASK; }
extern void GRAP_SDL_Initialize(void);
extern void GRAP_SDL_Cleanup(void);
extern void GRAP_SDL_FlushFrame(void);
int main(void)
{
    assert(MOUSE_Direction(-2,0)==U5_KEY_LEFT);
    assert(MOUSE_Direction(2,0)==U5_KEY_RIGHT);
    assert(MOUSE_Direction(0,-2)==U5_KEY_UP);
    assert(MOUSE_Direction(0,2)==U5_KEY_DOWN);
    assert(MOUSE_Direction(-2,-2)==U5_KEY_HOME);
    assert(MOUSE_Direction(2,-2)==U5_KEY_PGUP);
    assert(MOUSE_Direction(-2,2)==U5_KEY_END);
    assert(MOUSE_Direction(2,2)==U5_KEY_PGDN);
    assert(MOUSE_Direction(0,0)==0);
    D_5893_map_id=13; D_5896_map_x=16; D_5897_map_y=16; D_5895_map_level=0;
    memset(D_6608_map.town,1,32*32);
    D_5c5a[1]._0_tile=0x44; D_5c5a[1]._2_x=17; D_5c5a[1]._3_y=16;
    assert(MOUSE_Action(1,0,true)=='T');
    assert(MOUSE_Action(1,0,false)=='L');
    assert(MOUSE_Action(2,0,true)==0);
    assert(MOUSE_Action(1,1,true)==0);
    assert(MOUSE_Action(2,0,false)==0);
    D_5c5a[1]._0_tile=TILE_ACTOR_CHEST;
    assert(MOUSE_Action(1,0,true)=='O');
    D_5c5a[1]._0_tile=0;
    GetMap(17,16)=TILE_MAP_DOOR_B8;
    assert(MOUSE_Action(1,0,true)=='O');
    assert(SDL_Init(SDL_INIT_VIDEO));
    GRAP_SDL_Initialize();
    MOUSE_Initialize();
    int dx,dy; float rx,ry;
    assert(GRAP_SDL_MouseMapPoint(384,460.8f,&dx,&dy,&rx,&ry));
    assert(dx==0 && dy==0);
    assert(GRAP_SDL_MouseMapPoint(448,460.8f,&dx,&dy,&rx,&ry));
    assert(dx==1 && dy==0);
    assert(!GRAP_SDL_MouseMapPoint(1000,450,&dx,&dy,&rx,&ry));
    MOUSE_SetCommandInput(true);
    MOUSE_Button(448,460.8f,SDL_BUTTON_LEFT,true,1);
    assert(MOUSE_PollCommand()==0);
    ticks+=301;
    assert(MOUSE_PollCommand()=='L');
    MOUSE_SetCommandInput(false);
    assert(MOUSE_TakeDirection()==U5_KEY_RIGHT);
    assert(MOUSE_TakeDirection()==0);
    MOUSE_SetCommandInput(true);
    MOUSE_Button(448,460.8f,SDL_BUTTON_LEFT,true,1);
    ticks+=100;
    MOUSE_Button(448,460.8f,SDL_BUTTON_LEFT,true,2);
    assert(MOUSE_PollCommand()=='O');
    ticks+=400;
    assert(MOUSE_PollCommand()==0); /* no stray Look after a double click */
    D_5c5a[1]._0_tile=0x44;
    GetMap(17,16)=TILE_MAP_BED;
    MOUSE_Button(448,460.8f,SDL_BUTTON_LEFT,true,1);
    ticks+=100;
    MOUSE_Button(448,460.8f,SDL_BUTTON_LEFT,true,2);
    assert(MOUSE_PollCommand()=='T');
    MOUSE_SetCommandInput(false);
    assert(TALK_041c_TalkCmd()==0); /* real keyboard handler, including bed rules */
    assert(sawSleepingNpc && D_5876==1 && D_5878==0);
    MOUSE_SetCommandInput(true);
    MOUSE_Cancel();
    MOUSE_SetCommandInput(false);
    MOUSE_Button(448,460.8f,SDL_BUTTON_LEFT,true,1);
    ticks+=400;
    assert(MOUSE_PollCommand()==0);
    MOUSE_SetCommandInput(true);
    MOUSE_Button(448,460.8f,SDL_BUTTON_LEFT,true,1);
    D_5896_map_x++;
    ticks+=400;
    assert(MOUSE_PollCommand()==0); /* discard stale click targets */
    D_5896_map_x--;
    cursorX=448; cursorY=537.6f;
    MOUSE_Button(cursorX,cursorY,SDL_BUTTON_RIGHT,true,1);
    assert(MOUSE_PollCommand()==U5_KEY_PGDN);
    assert(MOUSE_PollCommand()==0);
    ticks+=160;
    assert(MOUSE_PollCommand()==U5_KEY_PGDN);
    MOUSE_Button(cursorX,cursorY,SDL_BUTTON_RIGHT,false,1);
    ticks+=160;
    assert(MOUSE_PollCommand()==0);
    GRAP_SDL_Cleanup();
    GRAP_SDL_SetFullscreen(true);
    GRAP_SDL_Initialize();
    byte* tiles=calloc(512,128);
    GRAP_BUF_LoadTileset(tiles);
    D_58a4=1; D_58a5=0;
    GRAP_SDL_FlushFrame();
    WideLayout l=WIDE_Layout(1024,768); /* dummy driver's desktop */
    float centerX=(1024-l.width*l.scale)/2+(l.mapX+(l.columns/2)*16+8)*l.scale;
    float centerY=(768-l.height*l.scale)/2+(l.mapY+(l.rows/2)*16+8)*l.scale;
    assert(GRAP_SDL_MouseMapPoint(centerX,centerY,&dx,&dy,&rx,&ry) && dx==0 && dy==0);
    assert(GRAP_SDL_MouseMapPoint(centerX+16*l.scale,centerY,&dx,&dy,&rx,&ry) && dx==1 && dy==0);
    assert(!GRAP_SDL_MouseMapPoint(1020,centerY,&dx,&dy,&rx,&ry));
    GRAP_SDL_Cleanup(); SDL_Quit();
    puts("Mouse directions, action ranges, click timing, modal gating, and coordinate mapping passed.");
}
