#include "common.h"
#if defined(TARGET_SDL)
#include "u4_transfer.h"
#include "file.h"
#include "engine_settings.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "key/mouse.h"
#include <SDL3/SDL.h>
#include <string.h>

bool U4_ReadTransfer(const char* path,void* character,void* virtues)
{
    byte data[0x1f6];
    FILE* f=FILE_Open(path,"rb");
    if(!f) { DEBUG_Error("Ultima IV transfer: cannot open %s",path);return false; }
    bool ok=fread(data,1,sizeof(data),f)==sizeof(data) && !ferror(f);
    fclose(f);
    if(ok) {
        const byte* c=data+8;
        for(int i=0;i<6;i++) {
            unsigned value=c[i*2]|((unsigned)c[i*2+1]<<8);
            if(value>(i<3?9999:70)) ok=false;
        }
        if(c[37]>7 || !c[20]) ok=false;
        for(int i=20;i<28 && c[i];i++) if(c[i]<32 || c[i]>126) ok=false;
    }
    if(!ok) { DEBUG_Error("Ultima IV transfer: truncated or invalid save %s",path);return false; }
    memcpy(character,data+8,40);memcpy(virtues,data+0x140,0xb6);
    debug("Ultima IV transfer: validated %s",path);return true;
}

typedef struct { char* name;bool directory; } Entry;
typedef struct { Entry* entries;int count;bool failed; } Listing;
static SDL_EnumerationResult enumerate(void* user,const char* dirname,const char* name)
{
    Listing* list=user;char path[FILE_PATH_SIZE];SDL_PathInfo info;
    if(SDL_snprintf(path,sizeof(path),"%s/%s",dirname,name)>=(int)sizeof(path)) return SDL_ENUM_CONTINUE;
    if(!SDL_GetPathInfo(path,&info)) return SDL_ENUM_CONTINUE;
    bool dir=info.type==SDL_PATHTYPE_DIRECTORY;
    if(!dir && (info.type!=SDL_PATHTYPE_FILE || !FILE_NameEqual(name,"party.sav"))) return SDL_ENUM_CONTINUE;
    Entry* entries=SDL_realloc(list->entries,(size_t)(list->count+1)*sizeof(Entry));
    if(!entries) { list->failed=true;return SDL_ENUM_FAILURE; }
    list->entries=entries;char* copy=SDL_strdup(name);
    if(!copy) { list->failed=true;return SDL_ENUM_FAILURE; }
    list->entries[list->count++]=(Entry){copy,dir};return SDL_ENUM_CONTINUE;
}
static int compare(const void* a,const void* b)
{
    const Entry* x=a;const Entry* y=b;
    if(x->directory!=y->directory) return x->directory?-1:1;
    return SDL_strcasecmp(x->name,y->name);
}
static void clear(Listing* l)
{
    for(int i=0;i<l->count;i++) SDL_free(l->entries[i].name);
    SDL_free(l->entries);memset(l,0,sizeof(*l));
}
static void scan(Listing* l,const char* directory)
{
    clear(l);
    if(!SDL_EnumerateDirectory(directory,enumerate,l)) l->failed=true;
    if(l->count>1) qsort(l->entries,l->count,sizeof(Entry),compare);
}
static void draw(const Listing* l,const char* directory,int selected,int top,const char* error,bool back)
{
    memset(g_linearEgaBuffer0,0,320*200);ENGINE_UIFrame();
    ENGINE_UIText(72,18,"Transfer from Ultima IV",15);
    char text[35];size_t length=strlen(directory);
    SDL_snprintf(text,sizeof(text),"%s%s",length>33?"...":"",directory+(length>33?length-30:0));
    ENGINE_UIText(24,35,text,7);
    ENGINE_UIText(24,50,"Select your party.sav",7);
    for(int row=0;row<7 && top+row<=l->count;row++) {
        int index=top+row;int y=66+row*14;
        if(index==selected) ENGINE_UIRect(20,y-1,276,10,15);
        if(!index) SDL_strlcpy(text,"[..] Parent directory",sizeof(text));
        else SDL_snprintf(text,sizeof(text),"%s%.29s",l->entries[index-1].directory?"/":"",l->entries[index-1].name);
        ENGINE_UIText(24,y,text,index==selected?0:15);
    }
    if(l->count>=7) {
        ENGINE_UIRect(298,65,2,98,8);
        ENGINE_UIRect(298,65+top*98/(l->count+1),2,SDL_max(4,7*98/(l->count+1)),15);
    }
    ENGINE_UIText(24,168,error?error:l->failed?"Cannot list this directory":"Enter: Select   Esc: Back",error || l->failed?12:7);
    if(back) ENGINE_UIRect(20,182,276,10,15);
    ENGINE_UIText(24,183,"Return to Menu",back?0:15);
    GRAP_BUF_MarkDirty();GRAP_BUF_Present();
}
bool U4_SelectTransfer(void* character,void* virtues)
{
    extern void KEY_SDL_ClearInput(void);
    char directory[FILE_PATH_SIZE];char* cwd=SDL_GetCurrentDirectory();
    if(!cwd) return false;
    SDL_strlcpy(directory,cwd,sizeof(directory));SDL_free(cwd);
    byte backup[320*200];memcpy(backup,g_linearEgaBuffer0,sizeof(backup));
    KEY_SDL_ClearInput();MOUSE_Cancel();MOUSE_SetPointerMode(true);GRAP_SDL_SetPixelUI(true);
    Listing list={0};scan(&list,directory);
    int selected=0,top=0;bool done=false,result=false,back=false;const char* error=NULL;
    while(!done) {
        draw(&list,directory,selected,top,error,back);SDL_Event event;
        while(SDL_PollEvent(&event)) {
            bool activate=false;
            if(event.type==SDL_EVENT_QUIT || (event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_E && (event.key.mod&SDL_KMOD_CTRL))) exit(0);
            if(event.type==SDL_EVENT_KEY_DOWN) {
                switch(event.key.key) {
                case SDLK_ESCAPE:done=true;break;
                case SDLK_UP:selected=SDL_max(0,selected-1);break;
                case SDLK_DOWN:selected=SDL_min(list.count,selected+1);break;
                case SDLK_PAGEUP:selected=SDL_max(0,selected-7);break;
                case SDLK_PAGEDOWN:selected=SDL_min(list.count,selected+7);break;
                case SDLK_RETURN:activate=!event.key.repeat;break;
                case SDLK_BACKSPACE:selected=0;activate=true;break;
                }
            }
            if(event.type==SDL_EVENT_MOUSE_WHEEL) {
                float steps=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;
                selected=SDL_clamp(selected-(int)steps,0,list.count);
            }
            if(event.type==SDL_EVENT_MOUSE_MOTION || (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT)) {
                float x,y;bool click=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN;
                back=false;
                if(GRAP_SDL_MouseUIPoint(click?event.button.x:event.motion.x,click?event.button.y:event.motion.y,&x,&y) && x>=20 && x<296) {
                    back=y>=180 && y<194;
                    if(y>=65 && y<163 && top+(int)(y-65)/14<=list.count) { selected=top+(int)(y-65)/14;activate=click; }
                    if(click && y>=180 && y<194) done=true;
                }
            }
            if(activate && !done) {
                char next[FILE_PATH_SIZE];bool dir=!selected || list.entries[selected-1].directory;
                if(!selected) {
                    SDL_strlcpy(next,directory,sizeof(next));size_t n=strlen(next);
                    while(n>1 && (next[n-1]=='/' || next[n-1]=='\\')) next[--n]=0;
                    char* slash=strrchr(next,'/');char* backslash=strrchr(next,'\\');
                    if(backslash && (!slash || backslash>slash)) slash=backslash;
                    if(slash) slash[1]=0;
                } else if(SDL_snprintf(next,sizeof(next),"%s/%s",directory,list.entries[selected-1].name)>=(int)sizeof(next)) { error="Path is too long";continue; }
                if(dir) { SDL_strlcpy(directory,next,sizeof(directory));scan(&list,directory);selected=top=0;error=NULL; }
                else if(U4_ReadTransfer(next,character,virtues)) { result=true;done=true; }
                else error="Invalid or incomplete party.sav";
            }
            if(selected<top) top=selected;
            if(selected>=top+7) top=selected-6;
        }
        MOUSE_UpdateCursor();SDL_Delay(16);
    }
    clear(&list);KEY_SDL_ClearInput();MOUSE_Cancel();MOUSE_SetPointerMode(false);GRAP_SDL_SetPixelUI(false);
    memcpy(g_linearEgaBuffer0,backup,sizeof(backup));GRAP_BUF_MarkDirty();GRAP_BUF_Present();return result;
}
#endif
