#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "common/common.h"
#include "common/file.h"
#include "common/data_setup.h"
static int pickerCalls,inputStep;
static bool captures[2];
bool __real_SDL_RenderPresent(SDL_Renderer* renderer);
bool __wrap_SDL_RenderPresent(SDL_Renderer* renderer)
{
    int which=pickerCalls?1:0;
    if(!captures[which]) {
        SDL_Surface* image=SDL_RenderReadPixels(renderer,NULL);assert(image);
        assert(SDL_SaveBMP(image,which?"setup-game-font.bmp":"setup-fallback.bmp"));
        SDL_DestroySurface(image);captures[which]=true;
    }
    return __real_SDL_RenderPresent(renderer);
}
void __wrap_SDL_ShowOpenFolderDialog(SDL_DialogFileCallback cb,void* data,SDL_Window* window,const char* location,bool many)
{
    (void)window;(void)location;assert(!many);
    const char* paths[]={pickerCalls++==0?TEST_GAME_DIR:".",NULL};cb(data,paths,-1);
}
static Uint32 choose(void* data,SDL_TimerID id,Uint32 interval)
{
    (void)data;(void)id;(void)interval;
    SDL_Keycode keys[]={SDLK_RETURN,SDLK_DOWN,SDLK_RETURN,SDLK_DOWN,SDLK_DOWN,SDLK_RETURN};
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.key=keys[inputStep++];assert(SDL_PushEvent(&e));
    return inputStep==6?0:100;
}
static Uint32 cancel(void* data,SDL_TimerID id,Uint32 interval)
{
    (void)data;(void)id;(void)interval;
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_ESCAPE;assert(SDL_PushEvent(&e));return 0;
}
int main(void)
{
    char error[120];
    assert(SETUP_Validate(TEST_GAME_DIR,error,sizeof(error)));
    assert(!SETUP_Validate("missing",error,sizeof(error)));assert(strstr(error,"Missing/incomplete"));
    SDL_RemovePath("DATA.CFG");SDL_unsetenv_unsafe("U5D_DATA_DIR");
    assert(SDL_Init(0));assert(SDL_AddTimer(200,cancel,NULL));
    assert(!SETUP_Run()); /* Works with no game font or assets in the runtime. */
    assert(SDL_Init(0));assert(SDL_AddTimer(100,choose,NULL));assert(SETUP_Run());
    assert(pickerCalls==2 && inputStep==6);assert(!strcmp(SETUP_MusicDirectory(),"."));
    FILE* f=fopen("DATA.CFG","r");assert(f);char selected[FILE_PATH_SIZE];assert(fgets(selected,sizeof(selected),f));
    assert(strstr(selected,TEST_GAME_DIR));fclose(f);
    assert(SETUP_Run());
    f=FILE_Open("IBM.CH","rb");assert(f);assert(fseek(f,0,SEEK_END)==0 && ftell(f)>=1024);fclose(f);
    f=FILE_Open("SAVEGAME/BRIT.OOL","rb");assert(f);fclose(f);
    /* Asset writes never modify the selected installation. */
    f=FILE_Open("IBM.CH","wb");assert(f);assert(fputs("local",f)>=0);fclose(f);
    f=FILE_Open("IBM.CH","rb");assert(f);assert(fseek(f,0,SEEK_END)==0 && ftell(f)>=1024);fclose(f);
    SDL_RemovePath("IBM.CH");
    f=fopen("DATA.CFG","w");assert(f);fputs("missing\n\n",f);fclose(f);
    assert(SDL_Init(0));assert(SDL_AddTimer(200,cancel,NULL));assert(!SETUP_Run());
    SDL_RemovePath("DATA.CFG");FILE_SetDataDirectory(NULL);
    puts("Asset-free setup cancellation, validation, saved paths and external read-only assets passed");
    return 0;
}
