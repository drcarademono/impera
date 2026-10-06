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
static char initialIds[4][41];static int initialCount;
static SDL_EnumerationResult initialSlots(void* unused,const char* dir,const char* id)
{
    (void)unused;
    if(id[0]=='.') return SDL_ENUM_CONTINUE;
    char file[512],name[40];SDL_snprintf(file,sizeof(file),"%s/%s/meta.txt",dir,id);
    FILE* f=FILE_Open(file,"r");assert(f);assert(fgets(name,sizeof(name),f));
    unsigned long long ms;assert(fscanf(f,"%llu",&ms)==1);
    unsigned int map,level,x,y;assert(fscanf(f," location %u %u %u %u",&map,&level,&x,&y)==4);
    assert((map==13 || map==2) && level==0);fclose(f);
    assert(!strcmp(name,"New Hero\n") && ms==0);
    assert(initialCount<4);SDL_strlcpy(initialIds[initialCount++],id,41);
    return SDL_ENUM_CONTINUE;
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
static int deleteStep;
static bool deleteConfirm;
static Uint32 deleteInput(void* unused,SDL_TimerID id,Uint32 interval)
{
    (void)unused;(void)id;(void)interval;
    SDL_Keycode keys[]={SDLK_DOWN,SDLK_DELETE,deleteConfirm?SDLK_Y:SDLK_N,SDLK_ESCAPE};
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.key=keys[deleteStep++];
    assert(SDL_PushEvent(&e));return deleteStep==4?0:50;
}
static bool captureBrowser;
static int expectedLocation=-1;
static bool checkDrag, sawDrag;
static bool checkNoLegacy, sawNoLegacy;
bool __real_SDL_RenderPresent(SDL_Renderer* renderer);
bool __wrap_SDL_RenderPresent(SDL_Renderer* renderer)
{
    bool presented=__real_SDL_RenderPresent(renderer);
    if(expectedLocation>=0) {
        for(int y=0;y<8;y++) for(int x=0;x<8;x++)
            assert(g_linearEgaBuffer0[(49+y)*320+20+x]==((D_539c[0][expectedLocation*8+y]&(0x80>>x))?0:15));
        expectedLocation=-1;
    }
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
    /* Legacy stays hidden by default even with valid working files. */
    checkNoLegacy=true;sawNoLegacy=false;
    assert(SDL_AddTimer(50,cancel,NULL));assert(!SLOTS_ShowLoad());assert(sawNoLegacy);
    SLOTS_SetLegacyEnabled(true);
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
    assert(!SLOTS_Delete("../1"));
    deleteStep=0;deleteConfirm=false;
    assert(SDL_AddTimer(50,deleteInput,NULL));assert(!SLOTS_ShowLoad());
    assert(SLOTS_Load("900"));
    deleteStep=0;deleteConfirm=true;
    assert(SDL_AddTimer(50,deleteInput,NULL));assert(!SLOTS_ShowLoad());
    assert(!SLOTS_Load("900"));
    assert(!SLOTS_Delete("900"));
    /* New-character state becomes an ordinary named slot, with fresh worlds. */
    char location[64];
    SLOTS_LocationName(2,0,location,sizeof(location));assert(!strcmp(location,"Britain"));
    SLOTS_LocationName(0,0,location,sizeof(location));assert(!strcmp(location,"Britannia"));
    SLOTS_LocationName(0,255,location,sizeof(location));assert(!strcmp(location,"Underworld"));
    SLOTS_LocationName(33,3,location,sizeof(location));assert(!strcmp(location,"Dungeon Deceit"));
    SLOTS_LocationName(17,0,location,sizeof(location));assert(!strcmp(location,"Lord British's Castle"));
    SLOTS_LocationName(255,0,location,sizeof(location));assert(!strcmp(location,"Unknown Location"));
    SDL_EnumerateDirectory("SAVEGAME/slots",cleanup,NULL);
    strcpy(D_55a8_party[0].name,"New Hero");D_57a8=678;D_5893_map_id=13;D_5895_map_level=0;
    memset(D_b21e,0,256);memset(D_b31e,0xab,256);
    writeWorld("SAVEGAME/BRIT.OOL",0x12,256);writeWorld("SAVEGAME/UNDER.OOL",0x34,256);
    assert(SLOTS_CreateInitial());
    D_57a8=789;D_5893_map_id=2;D_5896_map_x=15;D_5897_map_y=30;
    assert(SLOTS_CreateInitial());
    SDL_EnumerateDirectory("SAVEGAME/slots",initialSlots,NULL);assert(initialCount==2);
    /* Journey Onward's ordinary menu selects the newest game with legacy off. */
    SLOTS_SetLegacyEnabled(false);D_57a8=0;
    expectedLocation='B';
    assert(SDL_AddTimer(50,accept,NULL));assert(SLOTS_ShowLoad());assert(expectedLocation==-1);
    assert(FILE_ReadSavegameFile("SAVED.GAM")==0);assert(D_57a8==789);
    assert(!strcmp(D_55a8_party[0].name,"New Hero"));assert(D_5893_map_id==2);
    f=FILE_Open("SAVEGAME/SAVED.OOL","rb");assert(f);
    for(int i=0;i<512;i++) assert(fgetc(f)==(i<256?0:0xab));fclose(f);
    /* Older two-line metadata remains loadable. */
    const char* latest=strcmp(initialIds[0],initialIds[1])>0?initialIds[0]:initialIds[1];
    char meta[512];SDL_snprintf(meta,sizeof(meta),"SAVEGAME/slots/%s/meta.txt",latest);
    f=FILE_Open(meta,"w");assert(f);assert(fputs("Old Format\n123\n",f)>=0);fclose(f);
    assert(SLOTS_Load(latest));expectedLocation='U';
    assert(SDL_AddTimer(50,cancel,NULL));assert(!SLOTS_ShowLoad());assert(expectedLocation==-1);
    D_539c[0]=NULL;GRAP_Cleanup();SDL_Quit();
    puts("90+ named slots, party/world round trip, play time, failed overwrite, corrupt load and browser input passed");
    return 0;
}
