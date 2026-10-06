#include "crt.h"
#include <SDL3/SDL.h>
#include <math.h>

static bool enabled,active;
static SDL_Renderer* owner;
static SDL_Texture *frame,*soft,*glow,*halo,*mask;
static int width,height;
void CRT_SetEnabled(bool value) { enabled=value; }
bool CRT_Enabled(void) { return enabled; }
void CRT_Cleanup(void)
{
    if(owner && active) SDL_SetRenderTarget(owner,NULL);
    SDL_DestroyTexture(frame);SDL_DestroyTexture(soft);SDL_DestroyTexture(glow);SDL_DestroyTexture(halo);SDL_DestroyTexture(mask);
    frame=soft=glow=halo=mask=NULL;owner=NULL;active=false;width=height=0;
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
        soft=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w,h);
        halo=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w/8?w/8:1,h/8?h/8:1);
        glow=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w/4?w/4:1,h/4?h/4:1);
        SDL_Surface* surface=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_ARGB8888);
        if(surface) {
            /* VGA doubles 200-line DOS modes to approximately 400 beam lines.
             * Below that output resolution average the beam to avoid aliasing.
             * The DOS Monitor preset disables RGB stripes by default; all
             * channels remain aligned and neutral. */
            for(int y=0;y<h;y++) {
                Uint32* pixels=(Uint32*)((byte*)surface->pixels+y*surface->pitch);
                float beam=h>=800?0.925f+0.075f*cosf(6.2831853f*y*400.0f/h):0.925f;
                float ny=2.0f*(y+0.5f)/h-1;
                for(int x=0;x<w;x++) {
                    float nx=2.0f*(x+0.5f)/w-1;
                    float edge=1-0.04f*(nx*nx+ny*ny);
                    int rgb[3];
                    for(int c=0;c<3;c++) rgb[c]=(int)(255*beam*edge);
                    pixels[x]=0xff000000u|(rgb[0]<<16)|(rgb[1]<<8)|rgb[2];
                }
            }
            mask=SDL_CreateTextureFromSurface(renderer,surface);SDL_DestroySurface(surface);
        }
        if(!frame || !soft || !glow || !halo || !mask) {
            DEBUG_Error("CRT filter unavailable: %s",SDL_GetError());CRT_Cleanup();return;
        }
        SDL_SetTextureBlendMode(frame,SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(frame,SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(glow,SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(glow,20,20,20);
        SDL_SetTextureBlendMode(halo,SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(halo,10,10,10);
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
        vertex->position=(SDL_FPoint){width*(0.5f+0.5f*nx*(1-0.015f*ny*ny)),
                                      height*(0.5f+0.5f*ny*(1-0.020f*nx*nx))};
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
    SDL_SetRenderTarget(renderer,soft);
    SDL_SetTextureBlendMode(frame,SDL_BLENDMODE_NONE);
    SDL_SetTextureColorMod(frame,209,209,209);
    SDL_RenderTexture(renderer,frame,NULL,NULL);
    SDL_SetTextureBlendMode(frame,SDL_BLENDMODE_ADD);
    SDL_SetTextureColorMod(frame,13,13,13);
    float horizontal=width/320.0f*0.30f,vertical=height/200.0f*0.20f;
    SDL_FRect left={-horizontal,0,width,height},right={horizontal,0,width,height};
    SDL_RenderTexture(renderer,frame,NULL,&left);SDL_RenderTexture(renderer,frame,NULL,&right);
    SDL_SetTextureColorMod(frame,3,3,3);
    SDL_FRect above={0,-vertical,width,height},below={0,vertical,width,height};
    SDL_RenderTexture(renderer,frame,NULL,&above);SDL_RenderTexture(renderer,frame,NULL,&below);
    SDL_SetTextureBlendMode(frame,SDL_BLENDMODE_NONE);SDL_SetTextureColorMod(frame,255,255,255);
    SDL_SetRenderTarget(renderer,glow);SDL_RenderTexture(renderer,frame,NULL,NULL);
    SDL_SetRenderTarget(renderer,halo);SDL_RenderTexture(renderer,frame,NULL,NULL);
    SDL_SetRenderTarget(renderer,soft);
    SDL_RenderTexture(renderer,glow,NULL,NULL);SDL_RenderTexture(renderer,halo,NULL,NULL);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    SDL_SetTextureBlendMode(soft,SDL_BLENDMODE_NONE);ScreenMesh(renderer,soft,255);
    SDL_RenderTexture(renderer,mask,NULL,NULL);
    /* Compensate beam attenuation rather than leaving the display darker. */
    SDL_SetTextureBlendMode(soft,SDL_BLENDMODE_ADD);ScreenMesh(renderer,soft,10);
    SDL_SetTextureBlendMode(soft,SDL_BLENDMODE_NONE);
}
void CRT_ResumeFrame(SDL_Renderer* renderer)
{
    if(active) SDL_SetRenderTarget(renderer,frame);
}
