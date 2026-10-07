#include "crt.h"
#include "key/mouse.h"
#include <SDL3/SDL.h>
#include <math.h>

/* DOS Monitor preset. Keep tuning in normalized strengths. */
static const float scanlineStrength=0.28f,scanlineSharpness=0.58f,maskStrength=0.08f;
static const float bloomStrength=0.07f,halationStrength=0.05f,vignetteStrength=0.05f;
static const float horizontalSoftness=0.20f,verticalSoftness=0.04f;
static const float curvatureX=0.004f,curvatureY=0.007f;
/* Contrast is a black-anchored gain: black remains black. Gamma is unity;
 * all channels share the same coordinates (zero chromatic aberration). */
static const float displayBrightness=1.04f,displayContrast=1.02f;
static float averageBeam;
static bool enabled,active;
static SDL_Renderer* owner;
static SDL_Texture *frame,*scene,*soft,*glow,*halo,*mask;
static int width,height;
void CRT_SetEnabled(bool value) { enabled=value;if(!value && SDL_WasInit(SDL_INIT_VIDEO)) SDL_ShowCursor(); }
bool CRT_Enabled(void) { return enabled; }
void CRT_Cleanup(void)
{
    if(owner && active) SDL_SetRenderTarget(owner,NULL);
    if(SDL_WasInit(SDL_INIT_VIDEO)) SDL_ShowCursor();
    SDL_DestroyTexture(scene);SDL_DestroyTexture(frame);SDL_DestroyTexture(soft);SDL_DestroyTexture(glow);SDL_DestroyTexture(halo);SDL_DestroyTexture(mask);
    frame=scene=soft=glow=halo=mask=NULL;owner=NULL;active=false;width=height=0;
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
        scene=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w,h);
        soft=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w,h);
        halo=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w/8?w/8:1,h/8?h/8:1);
        glow=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w/4?w/4:1,h/4?h/4:1);
        SDL_Surface* surface=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_ARGB8888);
        if(surface) {
            /* VGA doubles 200-line DOS modes to approximately 400 beam lines.
             * Below that output resolution average the beam to avoid aliasing.
             * A fine RGB grille modulates intensity only, never channel
             * positions. Average it in small windows to avoid colour aliasing. */
            averageBeam=0;
            for(int i=0;i<1024;i++)
                averageBeam+=1-scanlineStrength*powf(0.5f-0.5f*cosf(6.2831853f*i/1024),scanlineSharpness);
            averageBeam/=1024;
            /* Match compensation to the beam samples actually displayed. */
            if(h>=800) {
                averageBeam=0;
                for(int y=0;y<h;y++) averageBeam+=1-scanlineStrength*powf(
                    0.5f-0.5f*cosf(6.2831853f*y*400.0f/h),scanlineSharpness);
                averageBeam/=h;
            }
            for(int y=0;y<h;y++) {
                Uint32* pixels=(Uint32*)((byte*)surface->pixels+y*surface->pitch);
                float beam=averageBeam;
                if(h>=800) beam=1-scanlineStrength*powf(
                    0.5f-0.5f*cosf(6.2831853f*y*400.0f/h),scanlineSharpness);
                float ny=2.0f*(y+0.5f)/h-1;
                for(int x=0;x<w;x++) {
                    float nx=2.0f*(x+0.5f)/w-1;
                    float edge=1-vignetteStrength*0.5f*(nx*nx+ny*ny);
                    int rgb[3];
                    for(int c=0;c<3;c++) {
                        float grille=w>=640?(x%3==c?1:1-maskStrength):1-maskStrength*2/3;
                        rgb[c]=(int)(255*beam*edge*grille);
                    }
                    pixels[x]=0xff000000u|(rgb[0]<<16)|(rgb[1]<<8)|rgb[2];
                }
            }
            mask=SDL_CreateTextureFromSurface(renderer,surface);SDL_DestroySurface(surface);
        }
        if(!frame || !scene || !soft || !glow || !halo || !mask) {
            DEBUG_Error("CRT filter unavailable: %s",SDL_GetError());CRT_Cleanup();return;
        }
        SDL_SetTextureBlendMode(frame,SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(frame,SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(glow,SDL_BLENDMODE_ADD);
        Uint8 bloom=(Uint8)(255*bloomStrength+0.5f);
        SDL_SetTextureColorMod(glow,bloom,bloom,bloom);
        SDL_SetTextureBlendMode(halo,SDL_BLENDMODE_ADD);
        Uint8 halation=(Uint8)(255*halationStrength+0.5f);
        SDL_SetTextureColorMod(halo,halation,halation,halation);
        SDL_SetTextureScaleMode(halo,SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(soft,SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(soft,SDL_SCALEMODE_LINEAR);
        SDL_SetTextureScaleMode(glow,SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(mask,SDL_BLENDMODE_MOD);
        debug("CRT filter buffers: %dx%d",w,h);
    }
    active=SDL_SetRenderTarget(renderer,frame);
}
/* All channels use the same mesh: nearly-flat barrel curvature without
 * chromatic aberration. The source is already scaled by the game renderer. */
static void ScreenMesh(SDL_Renderer* renderer,SDL_Texture* texture,int intensity)
{
    enum { columns=32,rows=24 };
    SDL_Vertex vertices[(columns+1)*(rows+1)];
    int indices[columns*rows*6],count=0;
    for(int y=0;y<=rows;y++) for(int x=0;x<=columns;x++) {
        float u=(float)x/columns,v=(float)y/rows,nx=2*u-1,ny=2*v-1;
        SDL_Vertex* vertex=&vertices[y*(columns+1)+x];
        vertex->position=(SDL_FPoint){width*(0.5f+0.5f*nx*(1-curvatureX*ny*ny)),
                                      height*(0.5f+0.5f*ny*(1-curvatureY*nx*nx))};
        float brightness=intensity/255.0f;
        vertex->color=(SDL_FColor){brightness,brightness,brightness,1};
        vertex->tex_coord=(SDL_FPoint){u,v};
        if(x<columns && y<rows) {
            int i=y*(columns+1)+x;
            indices[count++]=i;indices[count++]=i+1;indices[count++]=i+columns+1;
            indices[count++]=i+1;indices[count++]=i+columns+2;indices[count++]=i+columns+1;
        }
    }
    SDL_RenderGeometry(renderer,texture,vertices,(columns+1)*(rows+1),indices,count);
}
void CRT_EndFrame(SDL_Renderer* renderer)
{
    if(!active) return;
    /* DOS Monitor: Lottes-style beam modulation and phosphor reconstruction,
     * expressed in SDL render passes to work on OpenGL and software alike. */
    SDL_SetRenderTarget(renderer,scene);
    SDL_SetTextureBlendMode(frame,SDL_BLENDMODE_NONE);
    SDL_RenderTexture(renderer,frame,NULL,NULL);
    MOUSE_DrawFilteredCursor(renderer,curvatureX,curvatureY);
    SDL_SetRenderTarget(renderer,soft);
    SDL_SetTextureBlendMode(scene,SDL_BLENDMODE_NONE);
    Uint8 center=(Uint8)(255*(1-horizontalSoftness-verticalSoftness)+0.5f);
    SDL_SetTextureColorMod(scene,center,center,center);
    SDL_RenderTexture(renderer,scene,NULL,NULL);
    SDL_SetTextureBlendMode(scene,SDL_BLENDMODE_ADD);
    Uint8 softness=(Uint8)(255*horizontalSoftness*0.5f+0.5f);
    SDL_SetTextureColorMod(scene,softness,softness,softness);
    float horizontal=width/320.0f*0.30f,vertical=height/200.0f*0.20f;
    SDL_FRect left={-horizontal,0,width,height},right={horizontal,0,width,height};
    SDL_RenderTexture(renderer,scene,NULL,&left);SDL_RenderTexture(renderer,scene,NULL,&right);
    Uint8 verticalWeight=(Uint8)(255*verticalSoftness*0.5f+0.5f);
    SDL_SetTextureColorMod(scene,verticalWeight,verticalWeight,verticalWeight);
    SDL_FRect above={0,-vertical,width,height},below={0,vertical,width,height};
    SDL_RenderTexture(renderer,scene,NULL,&above);SDL_RenderTexture(renderer,scene,NULL,&below);
    SDL_SetTextureBlendMode(scene,SDL_BLENDMODE_NONE);SDL_SetTextureColorMod(scene,255,255,255);
    SDL_SetRenderTarget(renderer,glow);SDL_RenderTexture(renderer,scene,NULL,NULL);
    SDL_SetRenderTarget(renderer,halo);SDL_RenderTexture(renderer,scene,NULL,NULL);
    SDL_SetRenderTarget(renderer,soft);
    SDL_RenderTexture(renderer,glow,NULL,NULL);SDL_RenderTexture(renderer,halo,NULL,NULL);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    SDL_SetTextureBlendMode(soft,SDL_BLENDMODE_NONE);ScreenMesh(renderer,soft,255);
    SDL_RenderTexture(renderer,mask,NULL,NULL);
    /* Compensate beam attenuation rather than leaving the display darker. */
    SDL_SetTextureBlendMode(soft,SDL_BLENDMODE_ADD);
    float reconstruction=(center+2*softness+2*verticalWeight)/255.0f+bloomStrength+halationStrength;
    float attenuation=averageBeam*(1-maskStrength*2/3);
    int compensation=(int)(255*(displayBrightness*displayContrast/reconstruction-attenuation)+0.5f);
    ScreenMesh(renderer,soft,compensation);
    SDL_SetTextureBlendMode(soft,SDL_BLENDMODE_NONE);
}
void CRT_ResumeFrame(SDL_Renderer* renderer)
{
    if(active) SDL_SetRenderTarget(renderer,frame);
}

void CRT_RefreshCursor(void)
{
    if(active && enabled && owner) {
        CRT_EndFrame(owner);SDL_RenderPresent(owner);CRT_ResumeFrame(owner);
    }
}
