#include "common.h"
#if defined(TARGET_SDL)
#include "folder_picker.h"
#include "file.h"
#include "engine_settings.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "key/mouse.h"
#include <SDL3/SDL.h>
#include <string.h>

typedef struct { char* name;bool directory; } Entry;
typedef struct { Entry* entries;int count;bool failed; } Listing;
static SDL_EnumerationResult enumerate(void* user,const char* dirname,const char* name)
{
    Listing* list=user;char path[FILE_PATH_SIZE];SDL_PathInfo info;
    if(SDL_snprintf(path,sizeof(path),"%s/%s",dirname,name)>=(int)sizeof(path)) return SDL_ENUM_CONTINUE;
    if(!SDL_GetPathInfo(path,&info)) return SDL_ENUM_CONTINUE;
    bool dir=info.type==SDL_PATHTYPE_DIRECTORY;
    if(!dir) return SDL_ENUM_CONTINUE;
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
    ENGINE_UIText(64,18,"Choose Music Directory",15);
    char text[35];size_t length=strlen(directory);
    SDL_snprintf(text,sizeof(text),"%s%s",length>33?"...":"",directory+(length>33?length-30:0));
    ENGINE_UIText(24,35,text,7);
    ENGINE_UIText(24,50,"Choose folder with MP3 files",7);
    for(int row=0;row<7 && top+row<=l->count+1;row++) {
        int index=top+row;int y=66+row*14;
        if(index==selected) ENGINE_UIRect(20,y-1,276,10,15);
        if(!index) SDL_strlcpy(text,"Select this folder",sizeof(text));
        else if(index==1) SDL_strlcpy(text,"[..] Parent directory",sizeof(text));
        else SDL_snprintf(text,sizeof(text),"%s%.29s",l->entries[index-2].directory?"/":"",l->entries[index-2].name);
        ENGINE_UIText(24,y,text,index==selected?0:15);
    }
    if(l->count+2>7) {
        ENGINE_UIRect(298,65,2,98,8);
        ENGINE_UIRect(298,65+top*98/(l->count+2),2,SDL_max(4,7*98/(l->count+2)),15);
    }
    ENGINE_UIText(24,168,error?error:l->failed?"Cannot list this directory":"Enter: Select   Esc: Back",error || l->failed?12:7);
    if(back) ENGINE_UIRect(20,182,276,10,15);
    ENGINE_UIText(24,183,"Cancel",back?0:15);
    GRAP_BUF_MarkDirty();GRAP_BUF_Present();
}
bool FOLDER_SelectMusic(const char* initial,bool (*accept)(const char*))
{
    extern void KEY_SDL_ClearInput(void);
    char directory[FILE_PATH_SIZE];char* cwd=SDL_GetCurrentDirectory();
    if(!cwd) return false;
    SDL_PathInfo info;
    SDL_strlcpy(directory,initial && *initial && SDL_GetPathInfo(initial,&info) && info.type==SDL_PATHTYPE_DIRECTORY?initial:cwd,sizeof(directory));SDL_free(cwd);
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
                case SDLK_DOWN:selected=SDL_min(list.count+1,selected+1);break;
                case SDLK_PAGEUP:selected=SDL_max(0,selected-7);break;
                case SDLK_PAGEDOWN:selected=SDL_min(list.count+1,selected+7);break;
                case SDLK_RETURN:activate=!event.key.repeat;break;
                case SDLK_BACKSPACE:selected=1;activate=true;break;
                }
            }
            if(event.type==SDL_EVENT_MOUSE_WHEEL) {
                float steps=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;
                selected=SDL_clamp(selected-(int)steps,0,list.count+1);
            }
            if(event.type==SDL_EVENT_MOUSE_MOTION || (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT)) {
                float x,y;bool click=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN;
                back=false;
                if(GRAP_SDL_MouseUIPoint(click?event.button.x:event.motion.x,click?event.button.y:event.motion.y,&x,&y) && x>=20 && x<296) {
                    back=y>=180 && y<194;
                    if(y>=65 && y<163 && top+(int)(y-65)/14<=list.count+1) { selected=top+(int)(y-65)/14;activate=click; }
                    if(click && y>=180 && y<194) done=true;
                }
            }
            if(activate && !done) {
                if(!selected) {
                    if(accept(directory)) { result=true;done=true; }
                    else error="Cannot save music folder";
                    continue;
                }
                char next[FILE_PATH_SIZE];
                if(selected==1) {
                    SDL_strlcpy(next,directory,sizeof(next));size_t n=strlen(next);
                    while(n>1 && (next[n-1]=='/' || next[n-1]=='\\')) next[--n]=0;
                    char* slash=strrchr(next,'/');char* backslash=strrchr(next,'\\');
                    if(backslash && (!slash || backslash>slash)) slash=backslash;
                    if(slash) slash[1]=0;
                } else if(SDL_snprintf(next,sizeof(next),"%s/%s",directory,list.entries[selected-2].name)>=(int)sizeof(next)) { error="Path is too long";continue; }
                SDL_strlcpy(directory,next,sizeof(directory));scan(&list,directory);selected=top=0;error=NULL;
            }
            if(selected<top) top=selected;
            if(selected>=top+7) top=selected-6;
        }
        MOUSE_UpdateCursor();SDL_Delay(16);
    }
    clear(&list);KEY_SDL_ClearInput();MOUSE_Cancel();/* The enclosing Engine Options screen still owns pixel UI and pointer mode. */
    memcpy(g_linearEgaBuffer0,backup,sizeof(backup));GRAP_BUF_MarkDirty();GRAP_BUF_Present();return result;
}
#endif
