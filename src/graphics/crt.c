#include "crt.h"
#include <SDL3/SDL.h>
#include <math.h>

static bool enabled,active;
static SDL_Renderer* owner;
static SDL_Texture *frame,*glow,*mask;
static int width,height;
void CRT_SetEnabled(bool value) { enabled=value; }
bool CRT_Enabled(void) { return enabled; }
void CRT_Cleanup(void)
{
    if(owner && active) SDL_SetRenderTarget(owner,NULL);
    SDL_DestroyTexture(frame);SDL_DestroyTexture(glow);SDL_DestroyTexture(mask);
    frame=glow=mask=NULL;owner=NULL;active=false;width=height=0;
}
void CRT_BeginFrame(SDL_Renderer* renderer)
{
    if(owner && active) SDL_SetRenderTarget(owner,NULL);
    active=false;
    if(!enabled) { if(frame) CRT_Cleanup();return; }
    int w,h;
    if(!SDL_GetRenderOutputSize(renderer,&w,&h) || w<=0 || h<=0) return;
    if(owner!=renderer || width!=w || height!=h) {
        CRT_Cleanup();owner=renderer;width=w;height=h;
        frame=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w,h);
        glow=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w/4?w/4:1,h/4?h/4:1);
        SDL_Surface* surface=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_ARGB8888);
        if(surface) {
            /* VGA doubles 200-line DOS modes to approximately 400 beam lines.
             * Below that output resolution average the beam to avoid aliasing.
             * Phosphor stripes are physical display pixels, not game pixels. */
            for(int y=0;y<h;y++) {
                Uint32* pixels=(Uint32*)((byte*)surface->pixels+y*surface->pitch);
                float beam=h>=800?0.88f+0.12f*cosf(6.2831853f*y*400.0f/h):0.94f;
                float ny=2.0f*(y+0.5f)/h-1;
                for(int x=0;x<w;x++) {
                    float nx=2.0f*(x+0.5f)/w-1;
                    float edge=1-0.055f*(nx*nx+ny*ny);
                    int rgb[3];
                    for(int c=0;c<3;c++) rgb[c]=(int)(255*beam*edge*(w>=640 && x%3!=c?0.94f:1));
                    pixels[x]=0xff000000u|(rgb[0]<<16)|(rgb[1]<<8)|rgb[2];
                }
            }
            mask=SDL_CreateTextureFromSurface(renderer,surface);SDL_DestroySurface(surface);
        }
        if(!frame || !glow || !mask) {
            DEBUG_Error("CRT filter unavailable: %s",SDL_GetError());CRT_Cleanup();return;
        }
        SDL_SetTextureBlendMode(frame,SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(frame,SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(glow,SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(glow,18,18,18);
        SDL_SetTextureScaleMode(glow,SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(mask,SDL_BLENDMODE_MOD);
        debug("CRT filter buffers: %dx%d",w,h);
    }
    active=SDL_SetRenderTarget(renderer,frame);
}
void CRT_EndFrame(SDL_Renderer* renderer)
{
    if(!active) return;
    SDL_SetRenderTarget(renderer,glow);
    SDL_RenderTexture(renderer,frame,NULL,NULL);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderTexture(renderer,frame,NULL,NULL);
    /* Broad, low-intensity phosphor glow; preserve original palette and text. */
    SDL_RenderTexture(renderer,glow,NULL,NULL);
    SDL_RenderTexture(renderer,mask,NULL,NULL);
}
void CRT_ResumeFrame(SDL_Renderer* renderer)
{
    if(active) SDL_SetRenderTarget(renderer,frame);
}
