#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include "common/engine_settings.h"
#include "common/data_setup.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "vars.h"
#include "macros.h"
#include "event/event.h"
#include "key/key.h"
#include "key/mouse.h"
#include "time/time.h"
void GRAP_SDL_Initialize(void);
void GRAP_SDL_Cleanup(void);

static void key(SDL_Keycode code)
{ SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.key=code;assert(SDL_PushEvent(&e)); }
static int musicPickerMode,musicInputStep;
static Uint32 musicInput(void* data,SDL_TimerID timer,Uint32 interval)
{
    (void)data;(void)timer;
    int step=musicInputStep++;
    if(!step) {
        for(int i=0;i<ENGINE_MUSIC;i++) key(SDLK_DOWN);
        key(SDLK_RETURN);return interval;
    }
    if(step==1 && musicPickerMode>=4) { key(SDLK_ESCAPE);return 0; }
    if(step==1) {
        if(musicPickerMode==3) { key(SDLK_DOWN);key(SDLK_DOWN);key(SDLK_RETURN); }
        else key(musicPickerMode?SDLK_RETURN:SDLK_ESCAPE);
        return interval;
    }
    if(step==2 && musicPickerMode==3) { key(SDLK_RETURN);return interval; }
    key(SDLK_ESCAPE);
    return step==2 && musicPickerMode==2?interval:0;
}
static Uint32 closeGameplayOptions(void* userdata, SDL_TimerID timer, Uint32 interval)
{
    (void)userdata;(void)timer;(void)interval;
    for(int i=0;i<ENGINE_MUSIC;i++) key(SDLK_DOWN);
    key(SDLK_RETURN); /* Toggle music to prove the shortcut opened the screen. */
    key(SDLK_ESCAPE);
    return 0;
}
int main(int argc, char** argv)
{
    remove("ENGINE.CFG");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO));
    GRAP_SDL_Initialize();
    D_5893_map_id=0x40;D_58a4=0;
    byte font[1024];FILE* f=fopen("IBM.CH","rb");assert(f);
    assert(fread(font,1,sizeof(font),f)==sizeof(font));fclose(f);D_539c[0]=font;
    if(argc==2 && !strcmp(argv[1],"music_picker")) {
        const int modes[]={0,2,3,4,5};
        for(size_t i=0;i<sizeof(modes)/sizeof(modes[0]);i++) {
            musicPickerMode=modes[i];
            ENGINE_Set(ENGINE_MUSIC,musicPickerMode==5);musicInputStep=0;
            if(musicPickerMode==2) assert(SDL_CreateDirectory("DATA.CFG.pending"));
            if(musicPickerMode==3) assert(SDL_CreateDirectory("MusicChoice"));
            assert(SDL_AddTimer(80,musicInput,NULL));ENGINE_ShowOptions(false);
            bool success=musicPickerMode==3 || musicPickerMode==4;
            assert(ENGINE_Get(ENGINE_MUSIC)==success);
            if(success) {
                char* cwd=SDL_GetCurrentDirectory();assert(cwd);
                assert(strstr(SETUP_MusicDirectory(),cwd));SDL_free(cwd);
                assert(strstr(SETUP_MusicDirectory(),"MusicChoice"));
                FILE* config=fopen("DATA.CFG","r");assert(config);
                char line[4096];assert(fgets(line,sizeof(line),config));
                assert(fgets(line,sizeof(line),config));line[strcspn(line,"\r\n")]=0;
                assert(!strcmp(line,SETUP_MusicDirectory()));fclose(config);
            }
            if(musicPickerMode==2) assert(SDL_RemovePath("DATA.CFG.pending"));
        }
        assert(SDL_RemovePath("MusicChoice"));remove("DATA.CFG");remove("ENGINE.CFG");
        GRAP_SDL_Cleanup();SDL_Quit();return 0;
    }
    if(argc==2 && !strcmp(argv[1],"gameplay_options")) {
        memset(g_linearEgaBuffer0,3,320*200);
        ENGINE_Set(ENGINE_MUSIC,1);
        SDL_Event shortcut={0};shortcut.type=SDL_EVENT_KEY_DOWN;
        shortcut.key.key=SDLK_O;shortcut.key.mod=SDL_KMOD_CTRL;
        assert(SDL_PushEvent(&shortcut));
        assert(SDL_AddTimer(50,closeGameplayOptions,NULL));
        assert(KEY_PollKey()==0);
        assert(ENGINE_Get(ENGINE_MUSIC)==0);
        for(int i=0;i<320*200;i++) assert(g_linearEgaBuffer0[i]==3);
        ENGINE_Set(ENGINE_MUSIC,1);ENGINE_Load();
        assert(ENGINE_Get(ENGINE_MUSIC)==0);
        remove("ENGINE.CFG");
        GRAP_SDL_Cleanup();SDL_Quit();
        return 0;
    }
    if(argc==2) {
        SDL_Event exitKey={0};exitKey.type=SDL_EVENT_KEY_DOWN;
        exitKey.key.key=SDLK_E;exitKey.key.mod=SDL_KMOD_CTRL;
        assert(SDL_PushEvent(&exitKey));
        if(!strcmp(argv[1],"gameplay")) {
            /* Default and restored gameplay modes deliver Ctrl+E to the vanilla prompt. */
            assert(KEY_PollKey()==U5_KEY_CTRL_E);
            int previous=EVT_SetImmediateExit(1);
            assert(previous==0);
            assert(EVT_SetImmediateExit(previous)==1);
            assert(SDL_PushEvent(&exitKey));
            TIME_SleepMs(1);
            assert(KEY_PollKey()==U5_KEY_CTRL_E);
            return 0;
        }
        if(strcmp(argv[1],"settings")) EVT_SetImmediateExit(1);
        if(!strcmp(argv[1],"settings")) ENGINE_ShowOptions(false);
        else if(!strcmp(argv[1],"cutscene")) TIME_SleepMs(100);
        else KEY_PollKey(); /* Main-menu keyboard path. */
        fputs("Ctrl+E was consumed without exiting\n",stderr);
        return 1;
    }
    ENGINE_Set(ENGINE_MOVEMENT_SPEED,0.5f);ENGINE_Set(ENGINE_ANIMATION_SPEED,0.75f);
    ENGINE_Set(ENGINE_MOUSE,1);assert(ENGINE_Get(ENGINE_MOUSE)==1);
    ENGINE_Set(ENGINE_MOUSE,0);
    ENGINE_Set(ENGINE_DITHERED_DARKNESS,1);
    assert(ENGINE_Get(ENGINE_CRT)==0);ENGINE_Set(ENGINE_CRT,1);
    assert(ENGINE_Save());ENGINE_Set(ENGINE_CRT,0);ENGINE_Set(ENGINE_DITHERED_DARKNESS,0);ENGINE_Set(ENGINE_MOVEMENT_SPEED,1);ENGINE_Load();
    assert(ENGINE_Get(ENGINE_DITHERED_DARKNESS)==1);
    assert(ENGINE_Get(ENGINE_CRT)==1);ENGINE_Set(ENGINE_CRT,0);
    ENGINE_Set(ENGINE_DITHERED_DARKNESS,0);
    assert(ENGINE_Get(ENGINE_MOVEMENT_SPEED)==0.5f);
    f=fopen("ENGINE.CFG","w");assert(f);fputs("movement_speed nan\nanimation_speed 99\nfullscreen 3\n",f);fclose(f);
    ENGINE_Load();assert(ENGINE_Get(ENGINE_MOVEMENT_SPEED)==0.5f && ENGINE_Get(ENGINE_ANIMATION_SPEED)==0.75f);
    ENGINE_Set(ENGINE_MOVEMENT_SPEED,1);ENGINE_Set(ENGINE_ANIMATION_SPEED,1);
    ENGINE_Set(ENGINE_FULLSCREEN,1);assert(ENGINE_Get(ENGINE_FULLSCREEN)==1);
    ENGINE_Set(ENGINE_FULLSCREEN,0);assert(ENGINE_Get(ENGINE_FULLSCREEN)==0);
    /* Character creation can leave saved gameplay flags set. A title or
     * cutscene frame must still be identical with darkness on or off. */
    memset(g_linearEgaBuffer0,15,320*200);
    D_58a4=1;D_5893_map_id=0;
    int previousExit=EVT_SetImmediateExit(1);
    for(int mode=0;mode<=2;mode++) {
        GRAP_SDL_SetVideoMode(mode);
        ENGINE_Set(ENGINE_DITHERED_DARKNESS,0);
        SDL_Surface* plain=GRAP_SDL_CaptureFrame();assert(plain);
        ENGINE_Set(ENGINE_DITHERED_DARKNESS,1);
        SDL_Surface* dark=GRAP_SDL_CaptureFrame();assert(dark);
        assert(plain->w==dark->w && plain->h==dark->h && plain->pitch==dark->pitch);
        assert(!memcmp(plain->pixels,dark->pixels,(size_t)plain->pitch*plain->h));
        SDL_DestroySurface(plain);SDL_DestroySurface(dark);
    }
    EVT_SetImmediateExit(previousExit);D_58a4=0;
    GRAP_SDL_SetVideoMode(0);ENGINE_Set(ENGINE_DITHERED_DARKNESS,0);
    GRAP_SDL_SetPixelUI(true);ENGINE_DrawSettings(ENGINE_MOVEMENT_SPEED);
    for(int y=0;y<8;y++) for(int x=0;x<8;x++)
        assert(g_linearEgaBuffer0[(12+y)*320+104+x]==((font['E'*8+y]&(0x80>>x))?15:0));
    /* Both ends of the settings content have four pixels of inset. */
    for(int x=9;x<311;x++) {
        assert(g_linearEgaBuffer0[11*320+x]==0);
        assert(g_linearEgaBuffer0[188*320+x]==0);
    }
    /* Title-menu frame glyphs and white selection with black lettering. */
    assert(g_linearEgaBuffer0[0*320+8]==1);
    assert(g_linearEgaBuffer0[7*320+7]==15);
    assert(g_linearEgaBuffer0[80*320+0]==1);
    assert(g_linearEgaBuffer0[139*320+16]==15);
    for(int y=0;y<8;y++) for(int x=0;x<8;x++)
        assert(g_linearEgaBuffer0[(140+y)*320+24+x]==((font['M'*8+y]&(0x80>>x))?0:15));
    int count;SDL_Window** windows=SDL_GetWindows(&count);assert(count==1);
    SDL_Renderer* renderer=SDL_GetRenderer(windows[0]);SDL_free(windows);
    SDL_Surface* shot=SDL_RenderReadPixels(renderer,NULL);assert(shot);
    assert(SDL_SaveBMP(shot,"settings.bmp"));
    /* Real gameplay state must not split options into a map and sidebar. */
    byte* tiles=malloc(512*128);assert(tiles);memset(tiles,0x11,512*128);
    GRAP_BUF_LoadTileset(tiles);
    D_5893_map_id=13;D_58a4=1;
    ENGINE_Set(ENGINE_SMOOTH,1);
    GRAP_SDL_MapDrawn();
    ENGINE_DrawSettings(ENGINE_MOVEMENT_SPEED);
    SDL_Surface* gameplayShot=SDL_RenderReadPixels(renderer,NULL);assert(gameplayShot);
    assert(gameplayShot->w==shot->w && gameplayShot->h==shot->h);
    SDL_Surface* reference=SDL_ConvertSurface(shot,SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface* actual=SDL_ConvertSurface(gameplayShot,SDL_PIXELFORMAT_ARGB8888);
    assert(reference && actual);
    /* Smooth toggle was changed, so redraw the reference with the same values. */
    D_5893_map_id=0x40;D_58a4=0;ENGINE_DrawSettings(ENGINE_MOVEMENT_SPEED);
    SDL_DestroySurface(shot);shot=SDL_RenderReadPixels(renderer,NULL);assert(shot);
    SDL_DestroySurface(reference);reference=SDL_ConvertSurface(shot,SDL_PIXELFORMAT_ARGB8888);assert(reference);
    for(int y=0;y<actual->h;y++)
        assert(!memcmp((byte*)reference->pixels+y*reference->pitch,
                       (byte*)actual->pixels+y*actual->pitch,actual->w*4));
    assert(SDL_SaveBMP(gameplayShot,"gameplay-settings.bmp"));
    SDL_DestroySurface(reference);SDL_DestroySurface(actual);
    SDL_DestroySurface(shot);SDL_DestroySurface(gameplayShot);
    D_5893_map_id=13;D_58a4=1;
    MOUSE_SetPointerMode(true);assert(MOUSE_CursorDirection(100,200)==0);
    MOUSE_SetPointerMode(false);
    D_5893_map_id=0x40;D_58a4=0;
    GRAP_SDL_SetPixelUI(false);
    memset(g_linearEgaBuffer0,3,320*200);
    for(int i=0;i<ENGINE_MUSIC;i++) key(SDLK_DOWN);
    key(SDLK_RETURN); /* music off */
    key(SDLK_DOWN);key(SDLK_RETURN); /* effects off */
    key(SDLK_DOWN);key(SDLK_LEFT); /* movement 0.75 */
    key(SDLK_DOWN);key(SDLK_LEFT); /* animation 0.75 */
    SDL_Event e={0};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;
    e.button.x=184*4;e.button.y=80+144*4;assert(SDL_PushEvent(&e));
    e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=248*4;e.motion.y=80+144*4;assert(SDL_PushEvent(&e));
    e.type=SDL_EVENT_MOUSE_BUTTON_UP;assert(SDL_PushEvent(&e));
    key(SDLK_ESCAPE);
    ENGINE_ShowOptions(false);
    assert(ENGINE_Get(ENGINE_MUSIC)==0 && ENGINE_Get(ENGINE_SOUND)==0);
    assert(ENGINE_Get(ENGINE_MOVEMENT_SPEED)==1 && ENGINE_Get(ENGINE_ANIMATION_SPEED)==0.75f);
    for(int i=0;i<320*200;i++) assert(g_linearEgaBuffer0[i]==3); /* restores title art */
    ENGINE_Set(ENGINE_ANIMATION_SPEED,1);ENGINE_Load();
    assert(ENGINE_Get(ENGINE_ANIMATION_SPEED)==0.75f);
    /* Hovering either return row selects it: Enter closes without toggling fullscreen. */
    for(int gameplay=0;gameplay<2;gameplay++) {
        bool fullscreen=GRAP_SDL_Fullscreen();
        SDL_Event hover={0};hover.type=SDL_EVENT_MOUSE_MOTION;
        hover.motion.x=24*4;hover.motion.y=80+184*4;
        assert(SDL_PushEvent(&hover));key(SDLK_RETURN);key(SDLK_ESCAPE);
        ENGINE_ShowOptions(gameplay!=0);
        assert(GRAP_SDL_Fullscreen()==fullscreen);
    }
    /* The dropdown chooses a mode, persists it, and Escape dismisses it
     * without leaving the parent options screen. */
    key(SDLK_RETURN);key(SDLK_DOWN);key(SDLK_RETURN);key(SDLK_ESCAPE);
    ENGINE_ShowOptions(false);
    assert(GRAP_SDL_VideoMode()==GRAP_VIDEO_FULLSCREEN_43);
    GRAP_SDL_SetPixelUI(true);ENGINE_DrawSettings(ENGINE_FULLSCREEN);
    int ow,oh,ww,wh;assert(SDL_GetRenderOutputSize(renderer,&ow,&oh));
    SDL_Window* window=SDL_GetRenderWindow(renderer);assert(SDL_GetWindowSize(window,&ww,&wh));
    int scale=SDL_min(ow/320,oh/240);if(scale<1) scale=1;
    float left=(ow-320*scale)/2.0f,top=(oh-240*scale)/2.0f,ux,uy;
    assert(GRAP_SDL_MouseUIPoint((left+160*scale)*ww/ow,
        (top+60*scale)*wh/oh,&ux,&uy));
    assert(SDL_fabsf(ux-160)<0.01f && SDL_fabsf(uy-50)<0.01f);
    /* The blue frame begins at the 4:3 top edge, not the old 16:10 inset. */
    SDL_Surface* aspectShot=SDL_RenderReadPixels(renderer,NULL);assert(aspectShot);
    Uint8 ar,ag,ab,aa;
    assert(SDL_ReadSurfacePixel(aspectShot,(int)(left+8.5f*scale),
        (int)(top+0.6f*scale),&ar,&ag,&ab,&aa));
    assert(ar==0 && ag==0 && ab==170);
    assert(SDL_SaveBMP(aspectShot,"engine-options-4-3.bmp"));SDL_DestroySurface(aspectShot);
    GRAP_SDL_SetPixelUI(false);
    ENGINE_Set(ENGINE_FULLSCREEN,0);ENGINE_Load();
    assert(GRAP_SDL_VideoMode()==GRAP_VIDEO_FULLSCREEN_43);
    ENGINE_Set(ENGINE_FULLSCREEN,0);
    key(SDLK_RETURN);key(SDLK_DOWN);key(SDLK_ESCAPE);key(SDLK_ESCAPE);
    ENGINE_ShowOptions(false);assert(GRAP_SDL_VideoMode()==GRAP_VIDEO_WINDOWED);
    /* Older configurations retain the original monitor-fullscreen behavior. */
    f=fopen("ENGINE.CFG","w");assert(f);fputs("fullscreen 1\n",f);fclose(f);
    ENGINE_Load();assert(GRAP_SDL_VideoMode()==GRAP_VIDEO_FULLSCREEN);
    ENGINE_Set(ENGINE_FULLSCREEN,0);
    /* Mouse opens the dropdown and chooses its second entry. */
    SDL_Event choose={0};choose.type=SDL_EVENT_MOUSE_BUTTON_DOWN;choose.button.button=SDL_BUTTON_LEFT;
    choose.button.x=160*4;choose.button.y=80+44*4;assert(SDL_PushEvent(&choose));
    choose.button.y=80+65*4;assert(SDL_PushEvent(&choose));key(SDLK_ESCAPE);
    ENGINE_ShowOptions(false);assert(GRAP_SDL_VideoMode()==GRAP_VIDEO_FULLSCREEN_43);
    ENGINE_Set(ENGINE_FULLSCREEN,0);
    remove("ENGINE.CFG");D_539c[0]=NULL;GRAP_SDL_Cleanup();SDL_Quit();
    puts("Engine settings persistence, live options, keyboard, and slider dragging passed");
    return 0;
}
