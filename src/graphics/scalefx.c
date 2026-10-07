#include "scalefx.h"
#include "crt.h"
#include "common/file.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* Original five-pass ScaleFX by Sp00kyFox. Shader sources and their MIT
 * notices live in textures/shaders/scalefx; no game data is embedded. */
#define FUNCTIONS(X) \
X(void,GetIntegerv,(GLenum p,GLint* v)) \
X(const GLubyte*,GetString,(GLenum p)) \
X(GLboolean,IsEnabled,(GLenum p)) \
X(void,Enable,(GLenum p)) X(void,Disable,(GLenum p)) \
X(void,Viewport,(GLint x,GLint y,GLsizei w,GLsizei h)) \
X(void,ActiveTexture,(GLenum p)) X(void,BindTexture,(GLenum p,GLuint t)) \
X(void,GenTextures,(GLsizei n,GLuint* t)) X(void,DeleteTextures,(GLsizei n,const GLuint* t)) \
X(void,GetTexParameteriv,(GLenum p,GLenum k,GLint* v)) \
X(void,TexParameteri,(GLenum p,GLenum k,GLint v)) \
X(void,TexImage2D,(GLenum p,GLint l,GLint f,GLsizei w,GLsizei h,GLint b,GLenum format,GLenum type,const void* data)) \
X(GLuint,CreateShader,(GLenum type)) X(void,ShaderSource,(GLuint s,GLsizei n,const GLchar* const* strings,const GLint* sizes)) \
X(void,CompileShader,(GLuint s)) X(void,GetShaderiv,(GLuint s,GLenum p,GLint* v)) \
X(void,GetShaderInfoLog,(GLuint s,GLsizei n,GLsizei* size,GLchar* log)) X(void,DeleteShader,(GLuint s)) \
X(GLuint,CreateProgram,(void)) X(void,AttachShader,(GLuint p,GLuint s)) \
X(void,BindAttribLocation,(GLuint p,GLuint i,const GLchar* name)) X(void,LinkProgram,(GLuint p)) \
X(void,GetProgramInfoLog,(GLuint p,GLsizei n,GLsizei* size,GLchar* log)) \
X(void,GetProgramiv,(GLuint p,GLenum k,GLint* v)) X(void,DeleteProgram,(GLuint p)) X(void,UseProgram,(GLuint p)) \
X(GLint,GetUniformLocation,(GLuint p,const GLchar* name)) \
X(void,Uniform1i,(GLint l,GLint v)) X(void,Uniform2f,(GLint l,GLfloat x,GLfloat y)) \
X(void,UniformMatrix4fv,(GLint l,GLsizei n,GLboolean transpose,const GLfloat* v)) \
X(void,GenFramebuffers,(GLsizei n,GLuint* b)) X(void,BindFramebuffer,(GLenum p,GLuint b)) \
X(void,BlitFramebuffer,(GLint sx0,GLint sy0,GLint sx1,GLint sy1,GLint dx0,GLint dy0,GLint dx1,GLint dy1,GLbitfield mask,GLenum filter)) \
X(void,FramebufferTexture2D,(GLenum p,GLenum a,GLenum t,GLuint id,GLint l)) \
X(GLenum,CheckFramebufferStatus,(GLenum p)) X(void,DeleteFramebuffers,(GLsizei n,const GLuint* b)) \
X(void,GenVertexArrays,(GLsizei n,GLuint* v)) X(void,BindVertexArray,(GLuint v)) X(void,DeleteVertexArrays,(GLsizei n,const GLuint* v)) \
X(void,GenBuffers,(GLsizei n,GLuint* b)) X(void,BindBuffer,(GLenum p,GLuint b)) X(void,DeleteBuffers,(GLsizei n,const GLuint* b)) \
X(void,BufferData,(GLenum p,GLsizeiptr size,const void* data,GLenum usage)) \
X(void,EnableVertexAttribArray,(GLuint i)) \
X(void,VertexAttribPointer,(GLuint i,GLint n,GLenum type,GLboolean norm,GLsizei stride,const void* offset)) \
X(void,DrawArrays,(GLenum p,GLint start,GLsizei count))
#define DECLARE(ret,name,args) static ret (APIENTRY *fx##name) args;
FUNCTIONS(DECLARE)
#undef DECLARE
static bool enabled,failed,ready;
static SDL_Renderer* owner;
static GLuint programs[5],textures[5],fbo,sourceFbo,vao,vbo;
static int textureW,textureH;
void SCALEFX_SetEnabled(bool value) { enabled=value;failed=false;if(value) CRT_SetEnabled(false); }
bool SCALEFX_Enabled(void) { return enabled; }
void SCALEFX_Cleanup(void)
{
    if(ready) {
        for(int i=0;i<5;i++) if(programs[i]) fxDeleteProgram(programs[i]);
        fxDeleteTextures(5,textures);fxDeleteFramebuffers(1,&fbo);fxDeleteFramebuffers(1,&sourceFbo);
        fxDeleteVertexArrays(1,&vao);fxDeleteBuffers(1,&vbo);
    }
    memset(programs,0,sizeof(programs));memset(textures,0,sizeof(textures));
    ready=false;owner=NULL;textureW=textureH=0;
}
static GLuint compile(const char* source,GLenum type,bool es)
{
    GLuint shader=fxCreateShader(type);
    const char* strings[]={es?"#version 300 es\n":"#version 130\n",
        type==GL_VERTEX_SHADER?"#define VERTEX\n":"#define FRAGMENT\n",source};
    fxShaderSource(shader,3,strings,NULL);fxCompileShader(shader);
    GLint ok;fxGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) { char log[1024];fxGetShaderInfoLog(shader,sizeof(log),NULL,log);DEBUG_Error("ScaleFX shader: %s",log);fxDeleteShader(shader);return 0; }
    return shader;
}
static bool initialize(SDL_Renderer* renderer)
{
    const char* backend=SDL_GetRendererName(renderer);
    if(strcmp(backend,"opengl") && strcmp(backend,"opengles2")) { DEBUG_Error("ScaleFX unsupported renderer: %s",backend);return false; }
#define LOAD(ret,name,args) fx##name=(ret (APIENTRY*) args)SDL_GL_GetProcAddress("gl" #name);if(!fx##name) { DEBUG_Error("ScaleFX missing OpenGL function: gl" #name);return false; }
    FUNCTIONS(LOAD)
#undef LOAD
    ready=true;owner=renderer;
    const char* version=(const char*)fxGetString(GL_VERSION);
    bool es=version && strstr(version,"OpenGL ES");
    debug("ScaleFX initializing: renderer=%s OpenGL=%s",backend,version?version:"unknown");
    for(int i=0;i<5;i++) {
        char path[128];SDL_snprintf(path,sizeof(path),"textures/shaders/scalefx/scalefx-pass%d.glsl",i);
        FILE* file=FILE_Open(path,"rb");if(!file) { DEBUG_Error("ScaleFX cannot open shader %s: %s",path,strerror(errno));return false; }
        fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
        if(size<=0 || size>100000) { DEBUG_Error("ScaleFX invalid shader size: %s (%ld bytes)",path,size);fclose(file);return false; }
        char* source=malloc((size_t)size+1);if(!source) { fclose(file);return false; }
        bool read=fread(source,1,(size_t)size,file)==(size_t)size;fclose(file);source[size]=0;
        const char* body=strchr(source,'\n');
        GLuint vs=read && body?compile(body+1,GL_VERTEX_SHADER,es):0;
        GLuint fs=read && body?compile(body+1,GL_FRAGMENT_SHADER,es):0;
        free(source);
        if(!vs || !fs) { DEBUG_Error("ScaleFX could not compile shader pass %d (%s)",i,path); if(vs)fxDeleteShader(vs);if(fs)fxDeleteShader(fs);return false; }
        GLuint p=programs[i]=fxCreateProgram();fxAttachShader(p,vs);fxAttachShader(p,fs);
        fxBindAttribLocation(p,0,"VertexCoord");fxBindAttribLocation(p,1,"TexCoord");
        fxLinkProgram(p);fxDeleteShader(vs);fxDeleteShader(fs);
        GLint ok;fxGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok) { char log[1024];fxGetProgramInfoLog(p,sizeof(log),NULL,log);DEBUG_Error("ScaleFX shader pass %d link failed: %s",i,log);return false; }
    }
    fxGenFramebuffers(1,&fbo);fxGenFramebuffers(1,&sourceFbo);fxGenTextures(5,textures);fxGenVertexArrays(1,&vao);fxGenBuffers(1,&vbo);
    return true;
}
bool SCALEFX_Apply(SDL_Renderer* renderer,SDL_Texture* input,int width,int height,int scale)
{
    if(!enabled || failed) return false;
    SDL_FlushRenderer(renderer);
    GLuint original=(GLuint)SDL_GetNumberProperty(SDL_GetTextureProperties(input),SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER,0);
    if(!original) original=(GLuint)SDL_GetNumberProperty(SDL_GetTextureProperties(input),SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER,0);
    if(!original) { failed=true;DEBUG_Error("ScaleFX requires an OpenGL/OpenGL ES 3 renderer; using unfiltered output");return false; }
    /* Restore SDL's exact state after custom passes; its internal cache must
     * continue to agree with the GL context. We use a private VAO and buffers. */
    GLint oldProgram,oldFbo,oldReadFbo,oldVao,oldBuffer,oldActive,oldTextures[3],viewport[4];
    if(!ready && !initialize(renderer)) { SCALEFX_Cleanup();failed=true;DEBUG_Error("ScaleFX initialization failed; using unfiltered output");return false; }
    fxGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);fxGetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFbo);
    fxGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldReadFbo);
    fxGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);fxGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);
    fxGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);fxGetIntegerv(GL_VIEWPORT,viewport);
    for(int i=0;i<3;i++) { fxActiveTexture(GL_TEXTURE0+i);fxGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTextures[i]); }
    fxActiveTexture(GL_TEXTURE0);fxBindTexture(GL_TEXTURE_2D,original);
    GLint oldMin,oldMag;
    fxGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&oldMin);
    fxGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&oldMag);
    fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    GLboolean blend=fxIsEnabled(GL_BLEND),scissor=fxIsEnabled(GL_SCISSOR_TEST);
    fxDisable(GL_BLEND);fxDisable(GL_SCISSOR_TEST);
    int w=SDL_max(1,width/SDL_max(1,scale)),h=SDL_max(1,height/SDL_max(1,scale));
    /* Pass 0 uses integer texel offsets. Reconstruct the native pixel grid
     * before running it; feeding an already enlarged image would miss edges. */
    fxActiveTexture(GL_TEXTURE2);fxBindTexture(GL_TEXTURE_2D,textures[4]);
    if(textureW!=w || textureH!=h) {
        fxTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    fxBindFramebuffer(GL_READ_FRAMEBUFFER,sourceFbo);
    fxFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,original,0);
    fxBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo);
    fxFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[4],0);
    fxBlitFramebuffer(0,0,width,height,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    fxBindVertexArray(vao);fxBindBuffer(GL_ARRAY_BUFFER,vbo);
    const GLfloat vertices[]={-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1};
    fxBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    fxEnableVertexAttribArray(0);fxEnableVertexAttribArray(1);
    fxVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(GLfloat),(void*)0);
    fxVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(GLfloat),(void*)(2*sizeof(GLfloat)));
    bool ok=true;
    for(int i=0;i<5;i++) {
        fxActiveTexture(GL_TEXTURE0);fxBindTexture(GL_TEXTURE_2D,i?textures[i-1]:textures[4]);
        if(i<4) {
            fxBindFramebuffer(GL_FRAMEBUFFER,fbo);fxActiveTexture(GL_TEXTURE2);fxBindTexture(GL_TEXTURE_2D,textures[i]);
            if(textureW!=w || textureH!=h) {
                fxTexImage2D(GL_TEXTURE_2D,0,i<2?GL_RGBA32F:GL_RGBA8,w,h,0,GL_RGBA,GL_FLOAT,NULL);
                fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            }
            fxFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);
            if(fxCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) { ok=false;break; }
            fxViewport(0,0,w,h);
        } else { fxBindFramebuffer(GL_FRAMEBUFFER,oldFbo);fxViewport(0,0,width,height); }
        GLuint program=programs[i];fxUseProgram(program);
        GLfloat identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        /* SDL target textures have top-down coordinates. Flip only the final
         * presentation; intermediate passes and original samples stay aligned. */
        if(i==4) identity[5]=-1;
        fxUniformMatrix4fv(fxGetUniformLocation(program,"MVPMatrix"),1,GL_FALSE,identity);
        fxUniform2f(fxGetUniformLocation(program,"TextureSize"),w,h);fxUniform2f(fxGetUniformLocation(program,"InputSize"),w,h);
        fxUniform2f(fxGetUniformLocation(program,"OutputSize"),i==4?width:w,i==4?height:h);
        fxUniform2f(fxGetUniformLocation(program,"PassPrev5TextureSize"),w,h);
        fxUniform1i(fxGetUniformLocation(program,"Texture"),0);
        fxActiveTexture(GL_TEXTURE1);fxBindTexture(GL_TEXTURE_2D,i==2?textures[0]:textures[4]);
        fxUniform1i(fxGetUniformLocation(program,i==2?"PassPrev2Texture":"PassPrev5Texture"),1);
        fxDrawArrays(GL_TRIANGLE_STRIP,0,4);
    }
    if(ok) { textureW=w;textureH=h; }
    else { failed=true;DEBUG_Error("ScaleFX floating-point framebuffer unavailable; using unfiltered output"); }
    fxActiveTexture(GL_TEXTURE0);fxBindTexture(GL_TEXTURE_2D,original);
    fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,oldMin);fxTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,oldMag);
    fxBindFramebuffer(GL_DRAW_FRAMEBUFFER,oldFbo);fxBindFramebuffer(GL_READ_FRAMEBUFFER,oldReadFbo);fxViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    fxUseProgram(oldProgram);fxBindVertexArray(oldVao);fxBindBuffer(GL_ARRAY_BUFFER,oldBuffer);
    for(int i=0;i<3;i++) { fxActiveTexture(GL_TEXTURE0+i);fxBindTexture(GL_TEXTURE_2D,oldTextures[i]); }
    fxActiveTexture(oldActive);if(blend)fxEnable(GL_BLEND);if(scissor)fxEnable(GL_SCISSOR_TEST);
    return ok;
}
