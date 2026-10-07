#include "common/common.h"
#include "common/file.h"
#include "common/data_setup.h"
#if defined(TARGET_SDL)
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

extern char font8x8_basic[128][8];
static char game[FILE_PATH_SIZE],music[FILE_PATH_SIZE];
static byte font[1024];
static bool hasFont;
static SDL_Mutex* lock;
static bool picking,selectionReady;
static int picker;
static char chosen[FILE_PATH_SIZE],pickerError[120];
const char* SETUP_MusicDirectory(void) { return music; }

bool SETUP_Validate(const char* directory,char* error,size_t capacity)
{
    const char* required[]={
        "BRIT.CBT","BRIT.DAT","BRIT.OOL","BRITISH.BIT","BRITISH.PTH",
        "CASTLE.DAT","CASTLE.NPC","CASTLE.TLK","CREATE.16","DNG1.16",
        "DNG2.16","DNG3.16","DUNGEON.CBT","DUNGEON.DAT","DWELLING.DAT",
        "DWELLING.NPC","DWELLING.TLK","END.DAT","END1.16","END2.16",
        "ENDMSG.DAT","ENDSC.16","IBM.CH","INIT.OOL","ITEMS.16",
        "KARMA.DAT","KEEP.DAT","KEEP.NPC","KEEP.TLK","LOOK2.DAT",
        "MISCMAPS.DAT","MISCMSG.DAT","MON0.16","MON1.16","MON2.16",
        "MON3.16","MON4.16","MON5.16","MON6.16","MON7.16",
        "QUESTION.DAT","RUNES.CH","SHOPPE.DAT","SIGNS.DAT","STARTSC.16",
        "STORY.DAT","STORY1.16","STORY2.16","STORY3.16","STORY4.16",
        "STORY5.16","STORY6.16","TEXT.16","TILES.16","TITLE.BIT",
        "TOWNE.DAT","TOWNE.NPC","TOWNE.TLK","ULTIMA.16","UNDER.DAT",
        "UNDER.OOL","WD.BIT","INIT.GAM"
    };
    for(size_t i=0;i<SDL_arraysize(required);i++) {
        char path[FILE_PATH_SIZE],resolved[FILE_PATH_SIZE];SDL_PathInfo info;
        bool ok=directory && *directory && SDL_snprintf(path,sizeof(path),"%s/%s",directory,required[i])<(int)sizeof(path)
            && FILE_ResolvePath(path,resolved,sizeof(resolved),0)==0 && SDL_GetPathInfo(resolved,&info)
            && info.type==SDL_PATHTYPE_FILE && info.size>0;
        if(ok && !strcmp(required[i],"INIT.GAM")) ok=info.size>=4192;
        if(ok && (!strcmp(required[i],"IBM.CH") || !strcmp(required[i],"RUNES.CH"))) ok=info.size>=1024;
        if(!ok) { SDL_snprintf(error,capacity,"Missing/incomplete %s",required[i]);return false; }
    }
    if(capacity) *error=0;return true;
}
static void loadFont(void)
{
    char p[FILE_PATH_SIZE],resolved[FILE_PATH_SIZE];hasFont=false;
    if(SDL_snprintf(p,sizeof(p),"%s/IBM.CH",game)>=(int)sizeof(p) || FILE_ResolvePath(p,resolved,sizeof(resolved),0)!=0) return;
    FILE* f=fopen(resolved,"rb");if(f) { hasFont=fread(font,1,sizeof(font),f)==sizeof(font);fclose(f); }
}
static void rect(SDL_Renderer* r,int x,int y,int w,int h,int color)
{
    SDL_SetRenderDrawColor(r,color==1 || color==3?0:color==2?170:255,color==1 || color==3?0:color==2?170:255,color==3?0:color==1 || color==2?170:255,255);
    SDL_FRect box={(float)x,(float)y,(float)w,(float)h};SDL_RenderFillRect(r,&box);
}
static void text(SDL_Renderer* r,int x,int y,const char* s,int color)
{
    for(int i=0;s[i] && i<36;i++) {
        unsigned char c=(unsigned char)s[i];if(c>127) c='?';
        for(int dy=0;dy<8;dy++) for(int dx=0;dx<8;dx++)
            if(hasFont?(font[c*8+dy]&(0x80>>dx)):(font8x8_basic[c][dy]&(1<<dx))) rect(r,x+i*8+dx,y+dy,1,1,color);
    }
}
static void SDLCALL folderChosen(void* unused,const char* const* paths,int filter)
{
    (void)unused;(void)filter;SDL_LockMutex(lock);
    chosen[0]=pickerError[0]=0;
    if(!paths) SDL_strlcpy(pickerError,"Directory picker unavailable",sizeof(pickerError));
    else if(paths[0]) {
        if(strlen(paths[0])>=sizeof(chosen)) SDL_strlcpy(pickerError,"Directory path is too long",sizeof(pickerError));
        else SDL_strlcpy(chosen,paths[0],sizeof(chosen));
    }
    selectionReady=true;SDL_UnlockMutex(lock);
}
static bool saveConfig(void)
{
    if(strpbrk(game,"\r\n") || strpbrk(music,"\r\n")) return false;
    FILE* f=fopen("DATA.CFG.pending","w");if(!f) return false;
    bool ok=fprintf(f,"%s\n%s\n",game,music)>=0;if(fclose(f)!=0) ok=false;
    if(ok) ok=SDL_RenamePath("DATA.CFG.pending","DATA.CFG");
    if(!ok) SDL_RemovePath("DATA.CFG.pending");return ok;
}
static bool seedWorlds(void)
{
    char save[FILE_PATH_SIZE];
    if(FILE_ResolvePath("SAVEGAME",save,sizeof(save),1)!=0 || !SDL_CreateDirectory(save)) return false;
    const char* names[]={"BRIT.OOL","UNDER.OOL","SAVED.GAM","SAVED.OOL"};
    for(int i=0;i<4;i++) {
        char p[FILE_PATH_SIZE],resolved[FILE_PATH_SIZE];SDL_snprintf(p,sizeof(p),"%s/%s",save,names[i]);
        if(FILE_ResolvePath(p,resolved,sizeof(resolved),0)==0) continue;
        FILE* in=FILE_Open(names[i],"rb");if(!in) { if(i>=2) continue;return false; }
        FILE* out=fopen(p,"wb");if(!out) { fclose(in);return false; }
        unsigned char buf[4096];size_t n;bool ok=true;
        while((n=fread(buf,1,sizeof(buf),in))) if(fwrite(buf,1,n,out)!=n) { ok=false;break; }
        if(ferror(in)) ok=false;fclose(in);if(fclose(out)!=0) ok=false;
        if(!ok) { SDL_RemovePath(p);return false; }
    }
    return true;
}
bool SETUP_Run(void)
{
    debug("Game-data setup begins");
    char error[120]={0};bool configured=false;
    FILE* f=fopen("DATA.CFG","r");
    if(f) { configured=fgets(game,sizeof(game),f)!=NULL;if(!fgets(music,sizeof(music),f)) music[0]=0;fclose(f);
        game[strcspn(game,"\r\n")]=0;music[strcspn(music,"\r\n")]=0; }
    const char* env=SDL_getenv("U5D_DATA_DIR");
    if(env && *env) { SDL_strlcpy(game,env,sizeof(game));configured=true; }
    /* Existing prepared runtimes remain usable without another import. */
    if(!configured && SETUP_Validate(".",error,sizeof(error))) SDL_strlcpy(game,".",sizeof(game));
    bool valid=SETUP_Validate(game,error,sizeof(error));
    debug("Game-data validation configured=%d valid=%d reason=%s",configured,valid,error);
    SDL_PathInfo musicInfo;
    if(*music && (!SDL_GetPathInfo(music,&musicInfo) || musicInfo.type!=SDL_PATHTYPE_DIRECTORY)) valid=false;
    if(!configured || !valid) {
        {
            if(!SDL_Init(SDL_INIT_VIDEO)) { DEBUG_Error("Setup video initialization failed: %s",SDL_GetError());return false; }
            SDL_Window* window=NULL;SDL_Renderer* r=NULL;
            if(!SDL_CreateWindowAndRenderer("Impera - An Ultima 5 Engine",960,600,SDL_WINDOW_RESIZABLE,&window,&r)) { DEBUG_Error("Setup window creation failed: %s",SDL_GetError());SDL_Quit();return false; }
            SDL_SetRenderLogicalPresentation(r,320,200,SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
            lock=SDL_CreateMutex();if(!lock) { SDL_DestroyRenderer(r);SDL_DestroyWindow(window);SDL_Quit();return false; }
            loadFont();int selected=0;bool done=false,accepted=false,cancel=false;
            error[0]=0;
            while(!done) {
                SDL_LockMutex(lock);
                if(selectionReady) {
                    if(*pickerError) SDL_strlcpy(error,pickerError,sizeof(error));
                    else if(*chosen) {
                        SDL_strlcpy(picker==0?game:music,chosen,FILE_PATH_SIZE);
                        if(picker==0) { valid=SETUP_Validate(game,error,sizeof(error));loadFont(); }
                    }
                    selectionReady=false;picking=false;
                }
                SDL_UnlockMutex(lock);
                if(cancel && !picking) break;
                SDL_SetRenderDrawColor(r,0,0,0,255);SDL_RenderClear(r);
                rect(r,5,5,310,190,1);rect(r,8,8,304,184,0);
                SDL_SetRenderDrawColor(r,0,0,0,255);SDL_FRect inner={9,9,302,182};SDL_RenderFillRect(r,&inner);
                text(r,52,18,"Impera - An Ultima 5 Engine",0);
                text(r,16,36,"Locate your Ultima 5 game files",2);
                const char* labels[]={"Ultima 5 Directory","Music (Optional)","Clear Music","Start Game"};
                int ys[]={56,94,132,156};
                for(int i=0;i<4;i++) {
                    if(selected==i) rect(r,14,ys[i]-2,292,12,0);
                    text(r,20,ys[i],labels[i],selected==i?3:0);
                }
                text(r,20,72,*game?game:"Not selected",2);text(r,20,110,*music?music:"None",2);
                text(r,16,178,*error?error:"Enter: Choose    Esc: Quit",2);SDL_RenderPresent(r);
                SDL_Event e;bool activate=false;
                while(SDL_PollEvent(&e)) {
                    if(e.type==SDL_EVENT_QUIT || (e.type==SDL_EVENT_KEY_DOWN && (e.key.key==SDLK_ESCAPE || (e.key.key==SDLK_E && (e.key.mod&SDL_KMOD_CTRL))))) cancel=true;
                    if(picking || cancel) continue;
                    if(e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                        if(e.key.key==SDLK_UP) selected=SDL_max(0,selected-1);
                        if(e.key.key==SDLK_DOWN) selected=SDL_min(3,selected+1);
                        if(e.key.key==SDLK_RETURN || e.key.key==SDLK_SPACE) activate=true;
                    }
                    if(e.type==SDL_EVENT_MOUSE_MOTION || (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT)) {
                        float x,y;bool click=e.type==SDL_EVENT_MOUSE_BUTTON_DOWN;
                        SDL_RenderCoordinatesFromWindow(r,click?e.button.x:e.motion.x,click?e.button.y:e.motion.y,&x,&y);
                        for(int i=0;i<4;i++) if(x>=14 && x<306 && y>=ys[i]-2 && y<ys[i]+10) { selected=i;activate=click; }
                    }
                }
                if(activate && !cancel && !picking) {
                    if(selected<2) { picker=selected;picking=true;SDL_ShowOpenFolderDialog(folderChosen,NULL,window,selected?music:game,false); }
                    else if(selected==2) music[0]=0;
                    else if(SETUP_Validate(game,error,sizeof(error))) {
                        SDL_PathInfo info;
                        if(*music && (!SDL_GetPathInfo(music,&info) || info.type!=SDL_PATHTYPE_DIRECTORY)) SDL_strlcpy(error,"Music folder not found",sizeof(error));
                        else if(!saveConfig()) SDL_strlcpy(error,"Cannot save DATA.CFG",sizeof(error));
                        else { done=accepted=true; }
                    }
                }
                SDL_Delay(16);
            }
            SDL_DestroyMutex(lock);lock=NULL;SDL_DestroyRenderer(r);SDL_DestroyWindow(window);SDL_Quit();
            if(!accepted) return false;
        }
    }
    debug("Game data directory=%s; music directory=%s",game,music);
    FILE_SetDataDirectory(game);
    if(!seedWorlds()) { DEBUG_Error("Cannot initialize writable SAVEGAME directory");return false; }
    return true;
}
#endif
