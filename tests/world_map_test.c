#undef NDEBUG
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <SDL3/SDL.h>
#include "common/common.h"
#include "common/world_map.h"
#include "event/event.h"
#include "graphics/grap.h"
#include "graphics/grap_sdl.h"
#include "graphics/grap_buf.h"
#include "graphics/widescreen.h"
#include "world_map_image.h"
#include "key/key.h"
#include "key/mouse.h"
#include "time/time.h"
#include "vars.h"

extern void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent);
extern void KEY_SDL_ClearInput(void);
static Uint64 ticks;
Uint64 __wrap_SDL_GetTicks(void) { return ticks; }
static int frame, mode;
static bool running;
static u32 frozen;
static SDL_Surface* first;
static SDL_Surface* second;
static SDL_Surface* third;
static SDL_Surface* fourth;
static SDL_Surface* artwork;
static int videoMode;
static void key(SDL_Keycode code,bool repeat)
{
    SDL_Event e={0}; e.type=SDL_EVENT_KEY_DOWN; e.key.key=code; e.key.repeat=repeat;
    assert(SDL_PushEvent(&e));
}
static SDL_FRect viewport(int w,int h)
{
    if(videoMode==GRAP_VIDEO_WINDOWED)
        return (SDL_FRect){8*w/320.0f,8*h/200.0f,176*w/320.0f,176*h/200.0f};
    if(videoMode==GRAP_VIDEO_FULLSCREEN) {
        WideLayout l=WIDE_Layout(w,h);
        return (SDL_FRect){(w-l.width*l.scale)/2+8*l.scale,
            (h-l.height*l.scaleY)/2+8*l.scaleY,(l.sidebarX-16)*l.scale,(l.height-16)*l.scaleY};
    }
    float scale=SDL_min(w/320.0f,h/240.0f),nw=320*scale,nh=240*scale;
    return (SDL_FRect){(w-nw)/2+8*nw/320,(h-nh)/2+12*nh/200,176*nw/320,176*nh/200};
}
static unsigned pixel(SDL_Surface* s,int x,int y)
{
    Uint8 r,g,b,a; assert(SDL_ReadSurfacePixel(s,x,y,&r,&g,&b,&a));
    return (unsigned)r<<16 | (unsigned)g<<8 | b;
}
bool __real_SDL_RenderPresent(SDL_Renderer*);
bool __wrap_SDL_RenderPresent(SDL_Renderer* r)
{
    if(running) {
        SDL_Surface* s=SDL_RenderReadPixels(r,NULL); assert(s);
        if(frame<4) assert(TIME_GetTicksMs()==frozen);
        if(frame==0) {
            first=s; s=NULL;
            /* Held Tab must not close the modal. Other command keys are consumed. */
            key(SDLK_TAB,true); key(SDLK_A,false); key(SDLK_RIGHT,false);
        } else if(frame==1) {
            second=s; s=NULL;
            SDL_Event e={0};e.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.y=-2;
            assert(SDL_PushEvent(&e));
        } else if(frame==2) {
            third=s; s=NULL;
            int w,h;assert(SDL_GetRenderOutputSize(r,&w,&h));
            SDL_Event e={0};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;
            e.button.x=w*0.3f;e.button.y=h*0.4f; assert(SDL_PushEvent(&e));
            e=(SDL_Event){0};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=w*0.3f+30;e.motion.y=h*0.4f+20;
            assert(SDL_PushEvent(&e));
            e=(SDL_Event){0};e.type=SDL_EVENT_MOUSE_BUTTON_UP;e.button.button=SDL_BUTTON_LEFT;
            assert(SDL_PushEvent(&e));
        } else if(frame==3) {
            fourth=s;s=NULL;
            key(mode?SDLK_TAB:SDLK_ESCAPE,false);
            key(SDLK_DOWN,false); /* Closing input must not leak into gameplay. */
        } else {
            /* The EGA background is restored exactly on close. */
            assert(pixel(s,s->w/3,s->h/3)==0);
        }
        SDL_DestroySurface(s);
        ++frame; ticks+=400;
    }
    return __real_SDL_RenderPresent(r);
}
static void run(bool hasSextant,bool shortcut)
{
    mode=shortcut; frame=0;
    D_57bc=hasSextant;D_5893_map_id=0;D_5895_map_level=0;
    D_5896_map_x=86;D_5897_map_y=103;
    D_5881=17;D_588b=33;
    ticks=(ticks+799)/800*800;frozen=TIME_GetTicksMs();running=true;
    KEY_SDL_SetGameplayInput(1);
    if(shortcut) {
        SDL_KeyboardEvent e={0};e.key=SDLK_TAB;
        KEY_SDL_ProcessKeyDown(e);
        assert(KEY_PollKey()==0);
    } else WORLD_MAP_Show();
    running=false;
    assert(frame==5);
    assert(D_5881==17 && D_588b==33);
    assert(D_5896_map_x==86 && D_5897_map_y==103);
    assert(TIME_GetTicksMs()==frozen+400); /* Only the restored frame ran unpaused. */
    KEY_SDL_SetGameplayInput(0);
    assert(KEY_PollKey()==0);
    assert(!MOUSE_PointerMode());
    assert(first->w==second->w && first->pitch==second->pitch);
    size_t size=(size_t)first->pitch*first->h;
    assert(memcmp(first->pixels,second->pixels,size));
    assert(memcmp(second->pixels,third->pixels,size));
    assert(memcmp(third->pixels,fourth->pixels,size));
    SDL_FRect rect=viewport(first->w,first->h);
    int cx=(int)(rect.x+rect.w/2),cy=(int)(rect.y+rect.h/2);
    float scale=SDL_min(rect.w,rect.h)/256*(hasSextant?3:1);
    /* Check geographic alignment using real artwork, independently of the +. */
    int sx=(int)(cx+13*scale),sy=(int)(cy+7*scale);
    float centerX=hasSextant?86.5f:128,centerY=hasSextant?103.5f:128;
    int ax=(int)(centerX+(sx-(rect.x+rect.w/2))/scale);
    int ay=(int)(centerY+(sy-(rect.y+rect.h/2))/scale);
    assert(pixel(first,sx,sy)==pixel(artwork,ax,ay));
    assert(pixel(first,first->w-10,first->h/2)==0); /* Sidebar remains untouched. */
    if(hasSextant) {
        assert(pixel(first,cx,cy)==0xffffff); /* + centered on the party. */
        int movedX=(int)(rect.x+rect.w/2-8*scale/3);
        assert(pixel(second,movedX,cy)!=0xffffff); /* Flash is off 400 ms later. */
    }
    else {
        /* The map's center is visible, with no white position cross. */
        assert(pixel(first,cx,cy)!=0xffffff);
    }
    assert(SDL_SaveBMP(first,hasSextant?"sextant.bmp":"whole-map.bmp"));
    SDL_DestroySurface(first);SDL_DestroySurface(second);SDL_DestroySurface(third);SDL_DestroySurface(fourth);
}
int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO));
    GRAP_Initialize();
    byte* bank=calloc(512,128);assert(bank);GRAP_BUF_LoadTileset(bank);
    memset(g_linearEgaBuffer0,0,320*200);
    D_58a4=0; /* Native windowed layout, with a black gameplay buffer. */
    GRAP_SDL_SetVideoMode(GRAP_VIDEO_WINDOWED);
    GRAP_BUF_Present();KEY_SDL_ClearInput();SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    /* A Tab in non-gameplay input retains its original character behavior. */
    SDL_KeyboardEvent e={0};e.key=SDLK_TAB;KEY_SDL_ProcessKeyDown(e);
    assert(KEY_PollKey()=='\t');
    SDL_IOStream* io=SDL_IOFromConstMem(world_map_png,WORLD_MAP_PNG_SIZE);assert(io);
    artwork=SDL_LoadPNG_IO(io,true);assert(artwork);
    run(false,false); run(true,true);
    D_58a4=1;
    for(videoMode=GRAP_VIDEO_FULLSCREEN;videoMode<=GRAP_VIDEO_FULLSCREEN_43;videoMode++) {
        GRAP_SDL_SetVideoMode(videoMode);
        GRAP_BUF_Present();SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
        run(false,false);run(true,true);
    }
    SDL_DestroySurface(artwork);
    GRAP_Cleanup();SDL_Quit();
    puts("World map controls, centering, pause, and restoration passed");
    return 0;
}
