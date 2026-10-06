#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "common/save_slots.h"
#include "common/file.h"
#include "graphics/grap_sdl.h"
#include "graphics/grap_buf.h"
#include "graphics/grap.h"
#include "funcs.h"
#include "savegame.h"
#include "vars.h"
#include "key/key.h"
#include "event/event.h"
void GRAP_SDL_Initialize(void);
void GRAP_SDL_Cleanup(void);
static void writeWorld(const char* file,byte value,int bytes)
{
    FILE* f=FILE_Open(file,"wb");assert(f);
    for(int i=0;i<bytes;i++) assert(fputc(value,f)!=EOF);
    assert(!fclose(f));
}
static SDL_EnumerationResult cleanup(void* unused,const char* directory,const char* name)
{
    (void)unused;char dir[512],file[512];
    SDL_snprintf(dir,sizeof(dir),"%s/%s",directory,name);
    const char* parts[]={"SAVED.GAM","SAVED.OOL","meta.txt","thumbnail.bmp"};
    for(int i=0;i<4;i++) { SDL_snprintf(file,sizeof(file),"%s/%s",dir,parts[i]);SDL_RemovePath(file); }
    SDL_RemovePath(dir);return SDL_ENUM_CONTINUE;
}
static int step;
static bool reloadCalled;
static void reload(void) { reloadCalled=true; }
static Uint32 cancel(void* unused,SDL_TimerID id,Uint32 interval)
{
    (void)unused;(void)id;(void)interval;
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_ESCAPE;
    assert(SDL_PushEvent(&e));return 0;
}
static Uint32 accept(void* unused,SDL_TimerID id,Uint32 interval)
{
    (void)unused;(void)id;(void)interval;
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_RETURN;
    assert(SDL_PushEvent(&e));return 0;
}
static bool captureBrowser;
static bool checkDrag, sawDrag;
static bool checkNoLegacy, sawNoLegacy;
bool __real_SDL_RenderPresent(SDL_Renderer* renderer);
bool __wrap_SDL_RenderPresent(SDL_Renderer* renderer)
{
    bool presented=__real_SDL_RenderPresent(renderer);
    if(checkDrag && g_linearEgaBuffer0[173*320+300]==15) {
        assert(g_linearEgaBuffer0[56*320+300]==7);
        sawDrag=true;
    }
    if(checkNoLegacy) {
        /* First named slot occupies the former legacy row and is selected. */
        assert(g_linearEgaBuffer0[39*320+16]==15);
        assert(g_linearEgaBuffer0[66*320+16]==15);
        assert(g_linearEgaBuffer0[67*320+16]==0);
        sawNoLegacy=true;checkNoLegacy=false;
    }
    if(captureBrowser && D_539c[0]) {
        bool title=true;
        for(int y=0;y<8;y++) for(int x=0;x<8;x++)
            if(g_linearEgaBuffer0[(12+y)*320+124+x]!=((D_539c[0]['S'*8+y]&(0x80>>x))?15:0)) title=false;
        if(title) {
            SDL_Surface* image=SDL_RenderReadPixels(renderer,NULL);assert(image);
            Uint8 r,g,b,a;
            assert(SDL_ReadSurfacePixel(image,270*4,80+70*4,&r,&g,&b,&a));
            assert(r==255 && g==0 && b==0); /* Thumbnail overlay, not EGA menu pixels. */
            assert(SDL_SaveBMP(image,"browser.bmp"));SDL_DestroySurface(image);
            captureBrowser=false;
        }
    }
    return presented;
}
static Uint32 input(void* unused,SDL_TimerID id,Uint32 interval)
{
    (void)unused;(void)id;(void)interval;
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_RETURN;
    if(step==1) { e.type=SDL_EVENT_TEXT_INPUT;e.text.text="Browser Save"; }
    if(step==3) return 0;
    assert(SDL_PushEvent(&e));step++;return 100;
}
int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO));GRAP_Initialize();ULTIMA_1158_InitTimer();
    assert(SDL_CreateDirectory("SAVEGAME"));
    SDL_EnumerateDirectory("SAVEGAME/slots",cleanup,NULL);
    writeWorld("SAVEGAME/BRIT.OOL",0x12,256);
    writeWorld("SAVEGAME/UNDER.OOL",0x34,256);
    writeWorld("SAVEGAME/SAVED.OOL",0x56,512);
    D_57a8=123;assert(FILE_WriteSavegameFile("SAVED.GAM")==0);
    SDL_Surface* preview=SDL_CreateSurface(160,100,SDL_PIXELFORMAT_ARGB8888);assert(preview);
    assert(SDL_FillSurfaceRect(preview,NULL,SDL_MapSurfaceRGB(preview,255,0,0)));
    assert(SDL_SaveBMP(preview,"preview.bmp"));SDL_DestroySurface(preview);
    SLOTS_ResetTime();SLOTS_StartTime();SDL_Delay(25);
    assert(SLOTS_Write("1","First Save",NULL));
    uint64_t first=SLOTS_PlayMilliseconds();assert(first>=25);
    D_57a8=456;SDL_Delay(25);assert(SLOTS_Write("2","Second Save",NULL));
    for(int i=3;i<90;i++) {
        char id[40];SDL_snprintf(id,sizeof(id),"%d",i);
        assert(SLOTS_Write(id,"Same name is allowed","preview.bmp"));
    }
    assert(!SLOTS_Write("../escape","Bad",NULL));
    assert(!SLOTS_Write("1","Bad\nName",NULL));
    assert(SLOTS_Load("1"));assert(FILE_ReadSavegameFile("SAVED.GAM")==0);assert(D_57a8==123);
    FILE* f=FILE_Open("SAVEGAME/SAVED.OOL","rb");assert(f);
    for(int i=0;i<512;i++) assert(fgetc(f)==(i<256?0x12:0x34));fclose(f);
    SLOTS_StartTime();assert(SLOTS_PlayMilliseconds()>=25 && SLOTS_PlayMilliseconds()<first+100);
    D_57a8=789;
    assert(SDL_RenamePath("SAVEGAME/BRIT.OOL","SAVEGAME/BRIT.backup"));
    assert(!SLOTS_Write("1","Failed overwrite",NULL));
    assert(SDL_RenamePath("SAVEGAME/BRIT.backup","SAVEGAME/BRIT.OOL"));
    assert(SLOTS_Load("1"));assert(FILE_ReadSavegameFile("SAVED.GAM")==0);assert(D_57a8==123);
    writeWorld("SAVEGAME/slots/2/SAVED.OOL",0,3);
    assert(!SLOTS_Load("2"));assert(FILE_ReadSavegameFile("SAVED.GAM")==0);assert(D_57a8==123);
    byte font[1024];f=FILE_Open("IBM.CH","rb");assert(f);
    assert(fread(font,1,1024,f)==1024);fclose(f);D_539c[0]=font;
    D_5893_map_id=0x40;D_58a4=0;memset(g_linearEgaBuffer0,3,320*200);
    GRAP_BUF_MarkDirty();GRAP_BUF_Present();
    /* A stale menu render must never become the next gameplay capture. */
    GRAP_SDL_SetPixelUI(true);memset(g_linearEgaBuffer0,15,320*200);
    GRAP_BUF_Present();GRAP_SDL_SetPixelUI(false);
    memset(g_linearEgaBuffer0,3,320*200);
    SDL_Surface* gameplay=GRAP_SDL_CaptureFrame();assert(gameplay);
    Uint8 r,g,b,a;
    assert(SDL_ReadSurfacePixel(gameplay,gameplay->w/2,gameplay->h/2,&r,&g,&b,&a));
    assert(r==0 && g==170 && b==170);SDL_DestroySurface(gameplay);
    captureBrowser=true;
    assert(SDL_AddTimer(100,input,NULL));
    assert(SLOTS_ShowSave());
    for(int i=0;i<320*200;i++) assert(g_linearEgaBuffer0[i]==3);
    assert(step==3 && !captureBrowser);
    /* Wheel scrolling and dragging the scrollbar must also allow mouse return. */
    SDL_Event event={0};event.type=SDL_EVENT_MOUSE_WHEEL;event.wheel.y=0;
    assert(SDL_PushEvent(&event));
    event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.button=SDL_BUTTON_LEFT;
    event.button.x=302*4;event.button.y=80+58*4;assert(SDL_PushEvent(&event));
    event.type=SDL_EVENT_MOUSE_MOTION;event.motion.x=290*4;event.motion.y=80+210*4;assert(SDL_PushEvent(&event));
    event.type=SDL_EVENT_MOUSE_BUTTON_UP;assert(SDL_PushEvent(&event));
    event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.button=SDL_BUTTON_LEFT;
    event.button.x=24*4;event.button.y=80+184*4;assert(SDL_PushEvent(&event));
    checkDrag=true;
    assert(!SLOTS_ShowLoad());assert(sawDrag);checkDrag=false;
    for(int i=0;i<320*200;i++) assert(g_linearEgaBuffer0[i]==3);
    /* Missing legacy files hide its row while named slots remain usable. */
    assert(SDL_RenamePath("SAVEGAME/SAVED.GAM","SAVEGAME/SAVED.GAM.test-backup"));
    checkNoLegacy=true;
    assert(SDL_AddTimer(50,cancel,NULL));assert(!SLOTS_ShowLoad());assert(sawNoLegacy);
    assert(SDL_AddTimer(50,accept,NULL));assert(SLOTS_ShowLoad());
    assert(SDL_RemovePath("SAVEGAME/SAVED.GAM.test-backup"));
    /* Shortcuts are inert outside gameplay, including title/cutscene mode. */
    SDL_Event shortcut={0};shortcut.type=SDL_EVENT_KEY_DOWN;shortcut.key.mod=SDL_KMOD_CTRL;
    KEY_SDL_SetGameplayInput(0);
    shortcut.key.key=SDLK_W;assert(SDL_PushEvent(&shortcut));assert(KEY_PollKey()==0x1a);
    shortcut.key.key=SDLK_L;assert(SDL_PushEvent(&shortcut));assert(KEY_PollKey()==0x1a);
    KEY_SDL_SetGameplayInput(1);EVT_SetImmediateExit(1);
    assert(SDL_PushEvent(&shortcut));assert(KEY_PollKey()==0x1a);
    EVT_SetImmediateExit(0);
    /* Both gameplay shortcuts open a browser; cancellation does not reload. */
    SLOTS_SetReloadCallback(reload);
    shortcut.key.key=SDLK_W;assert(SDL_PushEvent(&shortcut));
    assert(SDL_AddTimer(50,cancel,NULL));assert(KEY_PollKey()==0);assert(!reloadCalled);
    shortcut.key.key=SDLK_L;assert(SDL_PushEvent(&shortcut));
    assert(SDL_AddTimer(50,cancel,NULL));assert(KEY_PollKey()==0);assert(!reloadCalled);
    /* Selecting a load invokes the top-level restart hook. */
    assert(SDL_PushEvent(&shortcut));assert(SDL_AddTimer(50,accept,NULL));
    assert(KEY_PollKey()==0);assert(reloadCalled);
    SLOTS_SetReloadCallback(NULL);KEY_SDL_SetGameplayInput(0);
    /* Legacy directory spelling remains supported. */
    assert(SDL_RenamePath("SAVEGAME","savegame"));
    assert(SLOTS_Write("900","Lowercase directory",NULL));
    assert(SLOTS_Load("900"));
    assert(SDL_RenamePath("savegame","SAVEGAME"));
    /* A reload abandons the cursor poll while character advancement is disabled. */
    assert(SLOTS_Load("1"));D_538e=0;
    SLOTS_ReloadActiveGame();
    assert(D_538e==1 && D_57a8==123);
    assert(D_535e_textWindows[1].left==24 && D_535e_textWindows[1].right==39);
    SLOTS_ResetTime();SLOTS_StartTime();assert(SLOTS_PlayMilliseconds()<10);
    D_539c[0]=NULL;GRAP_Cleanup();SDL_Quit();
    puts("90+ named slots, party/world round trip, play time, failed overwrite, corrupt load and browser input passed");
    return 0;
}
