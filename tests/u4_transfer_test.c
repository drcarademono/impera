#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "common/u4_transfer.h"
#include "graphics/grap_buf.h"
#include "vars.h"
void GRAP_SDL_Initialize(void);
void GRAP_SDL_Cleanup(void);
static int mode;
static void key(SDL_Keycode code)
{ SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.key=code;assert(SDL_PushEvent(&e)); }
static Uint32 input(void* user,SDL_TimerID id,Uint32 interval)
{
    (void)user;(void)id;(void)interval;
    if(mode) { /* Directories may be present; the save sorts after them. */
        for(int i=0;i<20;i++) key(SDLK_DOWN);
        key(SDLK_RETURN); }
    if(mode!=1) key(SDLK_ESCAPE);
    return 0;
}
static void writeSave(const byte* data,size_t size)
{ FILE* f=fopen("PARTY.SAV","wb");assert(f);assert(fwrite(data,1,size,f)==size);assert(!fclose(f)); }
int main(void)
{
    byte data[0x1f6]={0},character[40],virtues[0xb6];
    memset(character,0x55,sizeof(character));memset(virtues,0x66,sizeof(virtues));
    assert(!U4_ReadTransfer("missing-u4.sav",character,virtues));
    writeSave(data,sizeof(data)-1);assert(!U4_ReadTransfer("PARTY.SAV",character,virtues));
    for(size_t i=0;i<sizeof(character);i++) assert(character[i]==0x55);
    memcpy(data+8+20,"Avatar",7);data[8+2]=100;data[8+6]=30;data[8+8]=30;data[8+10]=30;
    data[0x140]=0x42;writeSave(data,sizeof(data));
    assert(U4_ReadTransfer("party.sav",character,virtues));
    assert(!memcmp(character,data+8,40));assert(!memcmp(virtues,data+0x140,sizeof(virtues)));
    data[8+37]=8;writeSave(data,sizeof(data));assert(!U4_ReadTransfer("PARTY.SAV",character,virtues));
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO));GRAP_SDL_Initialize();
    byte font[1024];FILE* f=fopen(TEST_GAME_DIR "/IBM.CH","rb");assert(f);assert(fread(font,1,1024,f)==1024);fclose(f);D_539c[0]=font;
    memset(g_linearEgaBuffer0,3,320*200);
    mode=0;assert(SDL_AddTimer(50,input,NULL));assert(!U4_SelectTransfer(character,virtues));
    mode=2;assert(SDL_AddTimer(50,input,NULL));assert(!U4_SelectTransfer(character,virtues));
    data[8+37]=0;writeSave(data,sizeof(data));
    mode=1;assert(SDL_AddTimer(50,input,NULL));assert(U4_SelectTransfer(character,virtues));
    for(int i=0;i<320*200;i++) assert(g_linearEgaBuffer0[i]==3);
    remove("PARTY.SAV");GRAP_SDL_Cleanup();SDL_Quit();
    puts("U4 missing/truncated/invalid saves, exact reads, picker cancellation and selection passed");
}
