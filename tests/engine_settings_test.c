#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "common/engine_settings.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "vars.h"
void GRAP_SDL_Initialize(void);
void GRAP_SDL_Cleanup(void);

static void key(SDL_Keycode code)
{ SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.key=code;assert(SDL_PushEvent(&e)); }
int main(void)
{
    remove("ENGINE.CFG");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO));
    GRAP_SDL_Initialize();
    D_5893_map_id=0x40;D_58a4=0;
    byte font[1024];FILE* f=fopen("IBM.CH","rb");assert(f);
    assert(fread(font,1,sizeof(font),f)==sizeof(font));fclose(f);D_539c[0]=font;
    ENGINE_Set(ENGINE_MOVEMENT_SPEED,0.5f);ENGINE_Set(ENGINE_ANIMATION_SPEED,0.75f);
    ENGINE_Set(ENGINE_MOUSE,1);assert(ENGINE_Get(ENGINE_MOUSE)==1);
    ENGINE_Set(ENGINE_MOUSE,0);
    assert(ENGINE_Save());ENGINE_Set(ENGINE_MOVEMENT_SPEED,1);ENGINE_Load();
    assert(ENGINE_Get(ENGINE_MOVEMENT_SPEED)==0.5f);
    f=fopen("ENGINE.CFG","w");assert(f);fputs("movement_speed nan\nanimation_speed 99\nfullscreen 3\n",f);fclose(f);
    ENGINE_Load();assert(ENGINE_Get(ENGINE_MOVEMENT_SPEED)==0.5f && ENGINE_Get(ENGINE_ANIMATION_SPEED)==0.75f);
    ENGINE_Set(ENGINE_MOVEMENT_SPEED,1);ENGINE_Set(ENGINE_ANIMATION_SPEED,1);
    ENGINE_Set(ENGINE_FULLSCREEN,1);assert(ENGINE_Get(ENGINE_FULLSCREEN)==1);
    ENGINE_Set(ENGINE_FULLSCREEN,0);assert(ENGINE_Get(ENGINE_FULLSCREEN)==0);
    GRAP_SDL_SetPixelUI(true);ENGINE_DrawSettings(7);
    for(int y=0;y<8;y++) for(int x=0;x<8;x++)
        assert(g_linearEgaBuffer0[(16+y)*320+96+x]==((font['E'*8+y]&(0x80>>x))?15:0));
    int count;SDL_Window** windows=SDL_GetWindows(&count);assert(count==1);
    SDL_Renderer* renderer=SDL_GetRenderer(windows[0]);SDL_free(windows);
    SDL_Surface* shot=SDL_RenderReadPixels(renderer,NULL);assert(shot);
    assert(SDL_SaveBMP(shot,"settings.bmp"));SDL_DestroySurface(shot);
    GRAP_SDL_SetPixelUI(false);
    memset(g_linearEgaBuffer0,3,320*200);
    for(int i=0;i<5;i++) key(SDLK_DOWN);
    key(SDLK_RETURN); /* music off */
    key(SDLK_DOWN);key(SDLK_RETURN); /* effects off */
    key(SDLK_DOWN);key(SDLK_LEFT); /* movement 0.75 */
    key(SDLK_DOWN);key(SDLK_LEFT); /* animation 0.75 */
    SDL_Event e={0};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;
    e.button.x=184*4;e.button.y=80+148*4;assert(SDL_PushEvent(&e));
    e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=248*4;e.motion.y=80+148*4;assert(SDL_PushEvent(&e));
    e.type=SDL_EVENT_MOUSE_BUTTON_UP;assert(SDL_PushEvent(&e));
    key(SDLK_ESCAPE);
    ENGINE_ShowSettings();
    assert(ENGINE_Get(ENGINE_MUSIC)==0 && ENGINE_Get(ENGINE_SOUND)==0);
    assert(ENGINE_Get(ENGINE_MOVEMENT_SPEED)==1 && ENGINE_Get(ENGINE_ANIMATION_SPEED)==0.75f);
    for(int i=0;i<320*200;i++) assert(g_linearEgaBuffer0[i]==3); /* restores title art */
    ENGINE_Set(ENGINE_ANIMATION_SPEED,1);ENGINE_Load();
    assert(ENGINE_Get(ENGINE_ANIMATION_SPEED)==0.75f);
    remove("ENGINE.CFG");D_539c[0]=NULL;GRAP_SDL_Cleanup();SDL_Quit();
    puts("Engine settings persistence, live options, keyboard, and slider dragging passed");
    return 0;
}
