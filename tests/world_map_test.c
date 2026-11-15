#undef NDEBUG
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <SDL3/SDL.h>
#include "common/common.h"
#include "common/world_map.h"
#include "common/engine_settings.h"
#include "event/event.h"
#include "graphics/grap.h"
#include "graphics/grap_sdl.h"
#include "graphics/grap_buf.h"
#include "graphics/widescreen.h"
#include "common/file.h"
#include "mod/maps.h"
#include "mod/types.h"
#include "macros.h"
#include "tiles.h"
#include "savegame.h"
#include "cast.h"
#include "key/key.h"
#include "key/mouse.h"
#include "time/time.h"
#include "vars.h"

static int castEffects,coordinateReadouts;
static unsigned spellMap,spellLevel;
int __wrap_ULTIMA_4988(void) { return 0; }
int __wrap_CAST2_00de_SelectSpell(void) { return 9; }
void __wrap_CAST2_0000_CastEffect(int level) { assert(level==2);++castEffects; }
void __wrap_CAST2_06ec(void) { ++coordinateReadouts; }
void __wrap_ULTIMA_1850_PrintString(char* text) { (void)text; }
void __wrap_ULTIMA_43ae_AudioSweepTone(int a,int b,int c,int d) { (void)a;(void)b;(void)c;(void)d; }
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
static SDL_Surface* fifth;
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
        if(frame<5) assert(TIME_GetTicksMs()==frozen);
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
            SDL_Event e={0};e.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.y=2;
            assert(SDL_PushEvent(&e));
        } else if(frame==3) {
            fourth=s; s=NULL;
            int w,h;assert(SDL_GetRenderOutputSize(r,&w,&h));
            SDL_Event e={0};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;
            e.button.x=w*0.3f;e.button.y=h*0.4f; assert(SDL_PushEvent(&e));
            e=(SDL_Event){0};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=w*0.3f+30;e.motion.y=h*0.4f+20;
            assert(SDL_PushEvent(&e));
            e=(SDL_Event){0};e.type=SDL_EVENT_MOUSE_BUTTON_UP;e.button.button=SDL_BUTTON_LEFT;
            assert(SDL_PushEvent(&e));
        } else if(frame==4) {
            fifth=s;s=NULL;
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
static void run(bool hasSextant,bool shortcut,bool spell)
{
    mode=shortcut; frame=0;
    bool marker=hasSextant || spell;
    D_57bc=hasSextant;D_587f=spell?12:0;D_5893_map_id=0;D_5895_map_level=0;
    D_5896_map_x=86;D_5897_map_y=103;
    int markerX=86,markerY=103,world=0;
    if(spell) {
        D_5893_map_id=spellMap;D_5895_map_level=spellLevel;
        assert(WORLD_MAP_MarkerPosition(1,&markerX,&markerY,&world));
    }
    SDL_Surface* reference=world?WORLD_MAP_Generate():artwork;assert(reference);
    D_5881=17;D_588b=33;
    ticks=(ticks+799)/800*800;frozen=TIME_GetTicksMs();running=true;
    KEY_SDL_SetGameplayInput(1);
    if(shortcut) {
        SDL_KeyboardEvent e={0};e.key=SDLK_TAB;
        KEY_SDL_ProcessKeyDown(e);
        assert(KEY_PollKey()==0);
    } else if(spell) {
        D_57f0[9]=1;D_55a8_party[0].level=8;D_55a8_party[0].mag=20;
        int effects=castEffects,readouts=coordinateReadouts;
        CAST_0dba_CastSpellCmd();
        assert(castEffects==effects+1 && coordinateReadouts==readouts);
        assert(D_57f0[9]==0 && D_55a8_party[0].mag==18);
    }
    else WORLD_MAP_Show();
    running=false;
    assert(frame==6);
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
    assert(!memcmp(second->pixels,fourth->pixels,size)); /* Wheel zoom in reverses zoom out. */
    assert(memcmp(fourth->pixels,fifth->pixels,size)); /* Dragging pans at restored scale. */
    SDL_FRect rect=viewport(first->w,first->h);
    int cx=(int)(rect.x+rect.w/2),cy=(int)(rect.y+rect.h/2);
    SDL_FPoint scale={rect.w/176,rect.h/176};
    if(videoMode==GRAP_VIDEO_FULLSCREEN) {
        WideLayout l=WIDE_Layout(first->w,first->h);
        scale=(SDL_FPoint){l.scale,l.scaleY};
    }
    /* Sample real artwork at native game-pixel scale, then after wheel zoom. */
    float centerX=marker?markerX+0.5f:128,centerY=marker?markerY+0.5f:128;
    for(int view=0;view<2;view++) {
        SDL_Surface* screenshot=view?third:first;
        float zoom=view?1/1.5625f:1;
        float xCenter=centerX+(view?8:0);
        for(int dy=-55;dy<=55;dy+=22) for(int dx=-55;dx<=55;dx+=22) {
            int ax=(int)xCenter+dx,ay=(int)centerY+dy;
            if(ax<1 || ay<1 || ax>=reference->w-1 || ay>=reference->h-1) continue;
            int sx=(int)(rect.x+rect.w/2+(ax+0.5f-xCenter)*scale.x*zoom);
            int sy=(int)(rect.y+rect.h/2+(ay+0.5f-centerY)*scale.y*zoom);
            unsigned actual=pixel(screenshot,sx,sy);
            bool matched=false;
            for(int oy=-1;oy<=1;oy++) for(int ox=-1;ox<=1;ox++)
                if(actual==pixel(reference,ax+ox,ay+oy)) matched=true;
            assert(matched);
        }
    }
    assert(pixel(first,first->w-10,first->h/2)==0); /* Sidebar remains untouched. */
    if(marker) {
        assert(pixel(first,cx,cy)==0xffffff); /* + centered on the party. */
        int movedX=(int)(rect.x+rect.w/2-8*scale.x);
        assert(pixel(second,movedX,cy)!=0xffffff); /* Flash is off 400 ms later. */
    }
    else {
        /* The map's center is visible, with no white position cross. */
        assert(pixel(first,cx,cy)!=0xffffff);
    }
    if(reference!=artwork) SDL_DestroySurface(reference);
    assert(SDL_SaveBMP(first,hasSextant?"sextant.bmp":"whole-map.bmp"));
    SDL_DestroySurface(first);SDL_DestroySurface(second);SDL_DestroySurface(third);SDL_DestroySurface(fourth);SDL_DestroySurface(fifth);
}
static void generation(void)
{
    /* Native compressed chunks, including implicit water and changed resources. */
    FILE* f=fopen("BRIT.DAT","wb");assert(f);
    unsigned char chunk[256];memset(chunk,5,sizeof(chunk));chunk[0]=7;
    for(int i=0;i<256;i++) assert(fwrite(chunk,1,sizeof(chunk),f)==sizeof(chunk));
    fclose(f);FILE_SetDataDirectory(".");
    SDL_Surface* image=WORLD_MAP_Generate();assert(image && image->w==256 && image->h==256);
    assert(pixel(image,0,0)==0x0426fc); /* Original implicit ocean chunk. */
    assert(pixel(image,32,0)==0xdcb604); /* Resource chunk 0 is desert here. */
    assert(pixel(image,33,0)==0x046e04); /* Grass color from the reference. */
    SDL_DestroySurface(image);
    f=fopen("BRIT.DAT","r+b");assert(f);fputc(8,f);fclose(f);
    image=WORLD_MAP_Generate();assert(image);
    assert(pixel(image,32,0)==0x049204);SDL_DestroySurface(image);
    D_58a4=1;D_5893_map_id=0;D_5895_map_level=0;D_589b=32;D_589c=0;
    memset(D_6608,5,1024);D_6608[0]=9;
    image=WORLD_MAP_Generate();assert(image);
    assert(pixel(image,32,0)==0x04b604);SDL_DestroySurface(image);
    D_58a4=0;D_589b=D_589c=0;
    remove("BRIT.DAT");

    /* Modded dimensions and mutable terrain take precedence over native files. */
    unsigned width=80,height=96,size=MAPS_HEADER+MAPS_RECORD+width*height;
    unsigned char* bytes=calloc(1,size);assert(bytes);
    memcpy(bytes,"IMAPS001",8);TYPES_Put32(bytes+8,1);
    unsigned char* r=bytes+MAPS_HEADER;TYPES_Put32(r,MAPS_RECORD+width*height);
    r[10]=width;r[12]=height;r[14]=254;
    strcpy((char*)r+24,"Modded Britannia");strcpy((char*)r+88,"BRIT.DAT");
    memset(r+MAPS_RECORD,5,width*height);
    assert(MAPS_Mount(bytes,size));
    *MAPS_ActiveCell(0,0,2,3)=7;
    *MAPS_ActiveCell(0,0,4,5)=9;
    image=WORLD_MAP_Generate();assert(image && image->w==80 && image->h==96);
    assert(pixel(image,2,3)==0xdcb604);
    assert(pixel(image,4,5)==0x04b604);SDL_DestroySurface(image);
    *MAPS_ActiveCell(0,0,2,3)=1;
    image=WORLD_MAP_Generate();assert(image);
    assert(pixel(image,2,3)==0x0426fc);SDL_DestroySurface(image);

    /* Custom terrain uses its base profile, not a stale pixel in the PNG. */
    unsigned char types[TYPES_HEADER+TYPES_RECORD+4+16*16*4]={0};
    memcpy(types,"ITYPES01",8);types[8]=1;TYPES_Put32(types+24,1);
    TypeDef def;TYPES_Vanilla(0,2,&def);def.id=512;def.frames=1;
    strcpy(def.name,"Modded shallows");TYPES_Encode(types+TYPES_HEADER,&def);
    TYPES_Put32(types+TYPES_HEADER,sizeof(types)-TYPES_HEADER);
    types[TYPES_HEADER+TYPES_RECORD+1]=16;
    types[TYPES_HEADER+TYPES_RECORD+2]=100;
    assert(TYPES_Validate(types,sizeof(types)));
    TYPES_Mount(types,sizeof(types),NULL,0);
    assert(TYPES_Set((TypeCell){.x=4,.y=5},512));
    image=WORLD_MAP_Generate();assert(image);
    assert(pixel(image,4,5)==0x0492fc);SDL_DestroySurface(image);
    TYPES_Mount(NULL,0,NULL,0);assert(MAPS_Mount(NULL,0));free(bytes);
}

static void underworld(void)
{
    unsigned size=MAPS_HEADER+MAPS_RECORD+256*256;
    unsigned char* bytes=calloc(1,size);assert(bytes);
    memcpy(bytes,"IMAPS001",8);TYPES_Put32(bytes+8,1);
    unsigned char* r=bytes+MAPS_HEADER;TYPES_Put32(r,MAPS_RECORD+256*256);
    r[5]=1;r[11]=r[13]=1;r[14]=254;
    strcpy((char*)r+24,"Underworld");strcpy((char*)r+88,"UNDER.DAT");
    memset(r+MAPS_RECORD,5,256*256);assert(MAPS_Mount(bytes,size));
    D_5893_map_id=0;D_5895_map_level=255;D_58a4=1;D_58a5=50;
    D_5896_map_x=D_5897_map_y=128;memset(D_ab02,5,sizeof(D_ab02));
    *MAPS_ActiveCell(0,-1,129,128)=TILE_MAP_WALL;
    *MAPS_ActiveCell(0,-1,128,129)=TILE_MAP_WALL;
    assert(WIDE_Visible(1,0) && !WIDE_Visible(2,0) && !WIDE_Visible(1,1));
    GetMapViewport(4,5)=255; /* Authoritative native-view hidden cell. */
    WORLD_MAP_ResetExploration();
    SDL_Surface* image=WORLD_MAP_Generate();assert(image);
    assert(pixel(image,128,128)==0 && pixel(image,129,128)==0);SDL_DestroySurface(image);
    /* Generating/zooming a map cannot reveal anything. A presented game map can. */
    GRAP_BUF_Present();
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,128)==0);SDL_DestroySurface(image);
    GRAP_SDL_MapDrawn();GRAP_BUF_Present();
    image=WORLD_MAP_Generate();assert(image);
    assert(pixel(image,128,128)==0x046e04);
    assert(pixel(image,129,128)!=0); /* The near wall is visible. */
    assert(pixel(image,130,128)==0 && pixel(image,129,129)==0); /* Behind wall/corner. */
    assert(pixel(image,127,128)==0 && pixel(image,122,128)==0); /* Hidden/offscreen. */
    SDL_DestroySurface(image);
    /* Repaint a changed visibility field without a new map draw (e.g. resize). */
    GetMapViewport(4,5)=5;GRAP_BUF_Present();
    image=WORLD_MAP_Generate();assert(image && pixel(image,127,128)==0x046e04);SDL_DestroySurface(image);
    WORLD_MAP_ResetExploration();GRAP_BUF_Present(); /* Previous save's frame is stale. */
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,128)==0);SDL_DestroySurface(image);
    WORLD_MAP_RecordGem(128,128,32,32);
    image=WORLD_MAP_Generate();assert(image);
    assert(pixel(image,130,128)==0x046e04 && pixel(image,129,129)==0x046e04);
    assert(pixel(image,159,159)==0x046e04 && pixel(image,160,160)==0);
    SDL_DestroySurface(image);
    WORLD_MAP_ResetExploration();GRAP_SDL_SetPixelUI(true);
    GRAP_SDL_MapDrawn();GRAP_BUF_Present();
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,128)==0);SDL_DestroySurface(image);
    GRAP_SDL_SetPixelUI(false);
    memset(D_ab02,5,sizeof(D_ab02));
    D_58a5=0;WORLD_MAP_RecordView(11,11,8,8,184);
    image=WORLD_MAP_Generate();assert(image);
    assert(pixel(image,128,128)==0x046e04 && pixel(image,127,128)==0);SDL_DestroySurface(image);
    D_5896_map_x=140;
    WORLD_MAP_RecordView(11,11,8,8,184);
    *MAPS_ActiveCell(0,-1,128,128)=7; /* Remote change is not yet witnessed. */
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,128)==0x046e04);SDL_DestroySurface(image);
    D_5896_map_x=128;WORLD_MAP_RecordView(11,11,8,8,184);
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,128)==0xdcb604);SDL_DestroySurface(image);

    /* Save validation is read-only; restore replaces, rather than unions, history. */
    FILE* state=tmpfile();assert(state && WORLD_MAP_WriteExploration(state));
    long length=ftell(state);assert(length>16);
    WORLD_MAP_ResetExploration();rewind(state);assert(WORLD_MAP_ReadExploration(state,0));
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,128)==0);SDL_DestroySurface(image);
    rewind(state);assert(WORLD_MAP_ReadExploration(state,1));
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,128)==0xdcb604);SDL_DestroySurface(image);
    assert(FILE_WriteSavegameFile("underworld.gam")==0);
    WORLD_MAP_ResetExploration();assert(FILE_ReadSavegameFile("underworld.gam")==0);
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,128)==0xdcb604);SDL_DestroySurface(image);
    remove("underworld.gam");
    assert(!fseek(state,16,SEEK_SET));int original=fgetc(state);
    assert(!fseek(state,16,SEEK_SET));fputc(original^1,state);fflush(state);rewind(state);
    assert(!WORLD_MAP_ReadExploration(state,1));
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,128)==0xdcb604);SDL_DestroySurface(image);
    fclose(state);
    state=tmpfile();assert(state);assert(WORLD_MAP_ReadExploration(state,1));fclose(state);
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,128)==0);SDL_DestroySurface(image);
    state=tmpfile();assert(state);assert(fwrite("IUWMAP01",1,8,state)==8);rewind(state);
    assert(!WORLD_MAP_ReadExploration(state,1));fclose(state);

    D_58a5=50;D_5896_map_x=0;
    WORLD_MAP_RecordView(11,11,8,8,184);
    image=WORLD_MAP_Generate();assert(image && pixel(image,255,128)==0x046e04);SDL_DestroySurface(image);
    D_5896_map_x=128;WORLD_MAP_ResetExploration();
    WORLD_MAP_RecordView(21,11,8,8,184); /* Larger viewport reveals additional cells. */
    image=WORLD_MAP_Generate();assert(image);
    assert(pixel(image,122,128)==0x046e04 && pixel(image,117,128)==0);SDL_DestroySurface(image);
    WORLD_MAP_ResetExploration();WORLD_MAP_RecordView(11,13,-8,8,184);
    image=WORLD_MAP_Generate();assert(image && pixel(image,128,122)==0);SDL_DestroySurface(image);
    WORLD_MAP_ResetExploration();assert(MAPS_Mount(NULL,0));free(bytes);
    D_5893_map_id=D_5895_map_level=D_58a4=0;
    memset(D_ab02,0,sizeof(D_ab02));
}

static void markerRules(void)
{
    int x,y,world;
    D_57bc=1;D_5893_map_id=D_5895_map_level=0;D_5896_map_x=86;D_5897_map_y=103;
    const int hours[]={0,5,6,12,18,19,23};
    for(unsigned i=0;i<sizeof(hours)/sizeof(hours[0]);i++) {
        D_587f=hours[i];
        assert(WORLD_MAP_MarkerPosition(0,&x,&y,&world)==(hours[i]<=5 || hours[i]>=19));
        assert(WORLD_MAP_MarkerPosition(1,&x,&y,&world));
        assert(x==86 && y==103 && world==0);
    }
    D_57bc=0;assert(!WORLD_MAP_MarkerPosition(0,&x,&y,&world));
    assert(WORLD_MAP_MarkerPosition(1,&x,&y,&world));
    D_57bc=1;D_587f=0;D_5895_map_level=255;
    assert(!WORLD_MAP_MarkerPosition(0,&x,&y,&world));
    assert(WORLD_MAP_MarkerPosition(1,&x,&y,&world) && world==-1);
    D_5893_map_id=33;D_5895_map_level=2;
    assert(!WORLD_MAP_MarkerPosition(0,&x,&y,&world));
    assert(WORLD_MAP_MarkerPosition(1,&x,&y,&world) && world==0);
    assert(x==D_1e8a[32] && y==D_1eb2[32]); /* Britannia entrance, not dungeon coordinates. */
    D_5893_map_id=40;
    assert(WORLD_MAP_MarkerPosition(1,&x,&y,&world) && world==0);
    D_5893_map_id=1;
    assert(!WORLD_MAP_MarkerPosition(0,&x,&y,&world));
    assert(WORLD_MAP_MarkerPosition(1,&x,&y,&world) && world==0);
    D_5893_map_id=D_5895_map_level=D_57bc=D_58a4=0;
    assert(ENGINE_Get(ENGINE_WORLD_MAP));
    ENGINE_Set(ENGINE_WORLD_MAP,0);GRAP_BUF_Present();
    assert(!WORLD_MAP_ShowSpell());
    D_57f0[9]=1;D_55a8_party[0].level=8;D_55a8_party[0].mag=20;
    int readouts=coordinateReadouts;CAST_0dba_CastSpellCmd();
    assert(coordinateReadouts==readouts+1 && D_57f0[9]==0);
    D_5893_map_id=33;D_5895_map_level=2;D_57f0[9]=1;
    CAST_0dba_CastSpellCmd();assert(D_57f0[9]==1); /* Original restriction when disabled. */
    D_5893_map_id=D_5895_map_level=0;
    KEY_SDL_SetGameplayInput(1);
    SDL_KeyboardEvent e={0};e.key=SDLK_TAB;KEY_SDL_ProcessKeyDown(e);
    assert(KEY_PollKey()==0);KEY_SDL_SetGameplayInput(0);
    ENGINE_Set(ENGINE_WORLD_MAP,1);
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
    markerRules();
    generation();
    underworld();
    FILE_SetDataDirectory(TEST_GAME_DIR);
    artwork=WORLD_MAP_Generate();assert(artwork);
    run(false,false,false);run(true,true,false);
    run(false,false,true);run(false,true,false);
    spellMap=33;spellLevel=2;run(false,false,true);
    spellMap=0;spellLevel=255;run(false,false,true);
    spellLevel=0;run(false,true,false);
    D_58a4=1;
    for(videoMode=GRAP_VIDEO_FULLSCREEN;videoMode<=GRAP_VIDEO_FULLSCREEN_43;videoMode++) {
        GRAP_SDL_SetVideoMode(videoMode);
        GRAP_BUF_Present();SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
        run(false,false,false);run(true,true,false);
    }
    SDL_DestroySurface(artwork);
    GRAP_Cleanup();SDL_Quit();
    puts("World map controls, centering, pause, and restoration passed");
    return 0;
}
