#undef NDEBUG
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include "graphics/crt.h"
#include "graphics/scalefx.h"
#ifdef TEST_FILTERED_CURSOR
static bool testCursor;
static float cursorX=100;
bool __wrap_MOUSE_DrawFilteredCursor(SDL_Renderer* r,float cx,float cy)
{
    (void)cx;(void)cy;
    if(!testCursor) return false;
    SDL_FRect rect={cursorX,100,16,16};
    assert(SDL_SetRenderDrawColor(r,255,255,255,255));
    return SDL_RenderFillRect(r,&rect);
}
#endif
static void white(SDL_Renderer* r)
{ assert(SDL_SetRenderDrawColor(r,255,255,255,255));assert(SDL_RenderClear(r)); }
static int red(SDL_Surface* s,int x,int y)
{ Uint8 r,g,b,a;assert(SDL_ReadSurfacePixel(s,x,y,&r,&g,&b,&a));return r; }
static void finish(SDL_Renderer* r)
{ CRT_EndFrame(r);assert(SDL_FlushRenderer(r)); }
static int scalefxTest(void)
{
    SDL_Window* window=SDL_CreateWindow("ScaleFX test",960,600,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    if(!window) { SDL_Log("%s",SDL_GetError());return 77; }
    SDL_Renderer* r=SDL_CreateRenderer(window,"opengl");
    if(!r) { SDL_Log("%s",SDL_GetError());SDL_DestroyWindow(window);return 77; }
    SDL_Surface* source=SDL_CreateSurface(320,200,SDL_PIXELFORMAT_ARGB8888);assert(source);
    for(int y=0;y<200;y++) for(int x=0;x<320;x++) {
        Uint32* row=(Uint32*)((char*)source->pixels+y*source->pitch);
        row[x]=y<16?0xffff0000:y>=184?0xff0000ff:x>=y && x<y+4?0xffffffff:0xff000000;
    }
    SDL_Texture* native=SDL_CreateTextureFromSurface(r,source);assert(native);
    SDL_SetTextureScaleMode(native,SDL_SCALEMODE_NEAREST);
    SDL_Texture* input=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,960,600);assert(input);
    SDL_SetRenderTarget(r,input);SDL_RenderTexture(r,native,NULL,NULL);
    SDL_SetRenderTarget(r,NULL);SDL_RenderTexture(r,input,NULL,NULL);
    SDL_Surface* before=SDL_RenderReadPixels(r,NULL);assert(before);
    SCALEFX_SetEnabled(true);assert(!CRT_Enabled());
    assert(SCALEFX_Apply(r,input,960,600,3));
    SDL_Surface* after=SDL_RenderReadPixels(r,NULL);assert(after);
    Uint8 red,green,blue,alpha;
    assert(SDL_ReadSurfacePixel(after,20,20,&red,&green,&blue,&alpha));
    assert(red==255 && green==0 && blue==0); /* orientation and palette */
    assert(SDL_ReadSurfacePixel(after,20,580,&red,&green,&blue,&alpha));
    assert(red==0 && green==0 && blue==255);
    int changed=0;
    for(int y=0;y<600;y++) for(int x=0;x<960;x++) {
        Uint8 br,bg,bb,ba;
        assert(SDL_ReadSurfacePixel(before,x,y,&br,&bg,&bb,&ba));
        assert(SDL_ReadSurfacePixel(after,x,y,&red,&green,&blue,&alpha));
        assert((red==0 || red==255) && (green==0 || green==255) && (blue==0 || blue==255));
        changed+=br!=red || bg!=green || bb!=blue;
    }
    assert(changed>0); /* genuine edge interpolation, not a nearest-neighbor bypass */
    /* Custom GL draws must not disturb SDL's state cache or later draws. */
    assert(SDL_SetRenderDrawColor(r,0,255,0,255));assert(SDL_RenderClear(r));
    SDL_Surface* restored=SDL_RenderReadPixels(r,NULL);assert(restored);
    assert(SDL_ReadSurfacePixel(restored,100,100,&red,&green,&blue,&alpha));
    assert(red==0 && green==255 && blue==0);SDL_DestroySurface(restored);
    assert(SCALEFX_Apply(r,input,960,600,3));
    SDL_Surface* repeat=SDL_RenderReadPixels(r,NULL);assert(repeat);
    assert(SDL_ReadSurfacePixel(repeat,20,20,&red,&green,&blue,&alpha));
    assert(red==255 && green==0 && blue==0);SDL_DestroySurface(repeat);
    /* ScaleFX must also preserve orientation when feeding a CRT target. */
    SDL_Texture* output=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,960,600);assert(output);
    SDL_SetRenderTarget(r,output);
    assert(SCALEFX_Apply(r,input,960,600,3));
    SDL_Surface* target=SDL_RenderReadPixels(r,NULL);assert(target);
    assert(SDL_ReadSurfacePixel(target,20,20,&red,&green,&blue,&alpha));
    assert(red==255 && green==0 && blue==0);SDL_DestroySurface(target);
    SDL_SetRenderTarget(r,NULL);SDL_DestroyTexture(output);
    CRT_SetCombinedEnabled();assert(CRT_Enabled() && SCALEFX_Enabled());
    CRT_BeginFrame(r);SDL_RenderTexture(r,native,NULL,NULL);CRT_EndFrame(r);
    SDL_Surface* combined=SDL_RenderReadPixels(r,NULL);assert(combined);
    assert(SDL_ReadSurfacePixel(combined,20,20,&red,&green,&blue,&alpha));
    assert(red>150 && blue<30);
    assert(SDL_ReadSurfacePixel(combined,20,580,&red,&green,&blue,&alpha));
    assert(blue>150 && red<30);
    SDL_SaveBMP(combined,"scalefx-crt-preview.bmp");SDL_DestroySurface(combined);
    CRT_Cleanup();CRT_SetEnabled(false);
    SDL_SaveBMP(after,"scalefx-preview.bmp");
    SDL_DestroySurface(before);SDL_DestroySurface(after);
    SCALEFX_Cleanup();SCALEFX_SetEnabled(false);
    SDL_DestroyTexture(input);SDL_DestroyTexture(native);SDL_DestroySurface(source);
    SDL_DestroyRenderer(r);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
int main(int argc,char** argv)
{
    assert(SDL_Init(SDL_INIT_VIDEO));
    if(argc>1 && !strcmp(argv[1],"--scalefx")) return scalefxTest();
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
    assert((red(output,480,400)+red(output,480,401))/2>=230); /* bright whites retained with stronger scanlines */
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
#ifdef TEST_FILTERED_CURSOR
    testCursor=true;CRT_ResumeFrame(r);finish(r);
    assert(red(output,108,108)>180); /* cursor enters the CRT image */
    assert(red(output,108,108)!=red(output,108,109)); /* scanlines affect cursor */
    cursorX=200;CRT_ResumeFrame(r);finish(r);
    assert(red(output,108,108)==0); /* backing frame has no cursor trails */
    assert(red(output,208,108)>180);
    testCursor=false;
#endif
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
