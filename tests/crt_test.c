#undef NDEBUG
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include "graphics/crt.h"
static void white(SDL_Renderer* r)
{ assert(SDL_SetRenderDrawColor(r,255,255,255,255));assert(SDL_RenderClear(r)); }
static int red(SDL_Surface* s,int x,int y)
{ Uint8 r,g,b,a;assert(SDL_ReadSurfacePixel(s,x,y,&r,&g,&b,&a));return r; }
static void finish(SDL_Renderer* r)
{ CRT_EndFrame(r);assert(SDL_FlushRenderer(r)); }
int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_Surface* output=SDL_CreateSurface(960,800,SDL_PIXELFORMAT_ARGB8888);assert(output);
    SDL_Renderer* r=SDL_CreateSoftwareRenderer(output);assert(r);
    assert(!CRT_Enabled());CRT_BeginFrame(r);white(r);finish(r);
    assert(red(output,480,400)==255); /* disabled filter is an exact bypass */
    CRT_SetEnabled(true);CRT_BeginFrame(r);white(r);finish(r);
    assert(red(output,480,400)>red(output,480,401)+10); /* resolvable scanlines */
    assert(red(output,480,400)+red(output,480,401)>red(output,480,0)+red(output,480,1)); /* restrained edge shading */
    Uint8 a,b,c,d,e,f,g,h;
    assert(SDL_ReadSurfacePixel(output,480,401,&a,&b,&c,&d));
    assert(SDL_ReadSurfacePixel(output,481,401,&e,&f,&g,&h));
    assert(a>b && a>c && f>e && f>g); /* fine RGB grille, aligned channels */
    assert(a-b<=20 && f-e<=20); /* restrained mask strength */
    assert((red(output,480,400)+red(output,480,401))/2>=240); /* brightness retained */
    assert(red(output,0,0)==0); /* nearly flat curved corners */
    size_t bytes=(size_t)output->pitch*output->h;
    void* previous=malloc(bytes);assert(previous);memcpy(previous,output->pixels,bytes);
    CRT_ResumeFrame(r);finish(r);
    assert(memcmp(previous,output->pixels,bytes)==0);free(previous); /* no temporal flicker */
    CRT_ResumeFrame(r);assert(SDL_GetRenderTarget(r));
    assert(SDL_SetRenderDrawColor(r,128,128,128,255));assert(SDL_RenderClear(r));finish(r);
    int midtone=(red(output,480,400)+red(output,480,401))/2;
    assert(midtone>=120 && midtone<=140); /* no material darkening or washout */
    CRT_ResumeFrame(r);
    assert(SDL_SetRenderDrawColor(r,0,0,0,255));assert(SDL_RenderClear(r));
    SDL_FRect box={350,300,260,200};
    assert(SDL_SetRenderDrawColor(r,255,255,255,255));assert(SDL_RenderFillRect(r,&box));
    finish(r);
    assert(red(output,349,400)>0); /* low-intensity glow outside the bright shape */
    assert(red(output,100,100)==0); /* glow does not lift distant black */
    assert(SDL_SaveBMP(output,"crt-preview.bmp"));
    CRT_ResumeFrame(r);CRT_SetEnabled(false);CRT_BeginFrame(r);
    assert(!SDL_GetRenderTarget(r));white(r);finish(r);
    assert(red(output,480,401)==255); /* toggling off releases the target */
    SDL_Surface* small=SDL_CreateSurface(320,200,SDL_PIXELFORMAT_ARGB8888);assert(small);
    SDL_Renderer* next=SDL_CreateSoftwareRenderer(small);assert(next);
    CRT_SetEnabled(true);CRT_BeginFrame(r);white(r);finish(r);CRT_ResumeFrame(r);
    CRT_BeginFrame(next);white(next);finish(next); /* renderer/size change */
    assert(!SDL_GetRenderTarget(r));assert(red(small,160,100)>200);
    CRT_Cleanup();SDL_DestroyRenderer(next);SDL_DestroySurface(small);
    SDL_DestroyRenderer(r);SDL_DestroySurface(output);SDL_Quit();return 0;
}
