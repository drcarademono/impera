#include "engine_settings.h"
#include "graphics/crt.h"
#include "graphics/tileset.h"
#include "file.h"
#include "data_setup.h"
#include "folder_picker.h"
#include "file_picker.h"
#include "movement.h"
#include "graphics/grap_sdl.h"
#include "graphics/grap_buf.h"
#include "graphics/animate.h"
#include "graphics/widescreen.h"
#include "key/mouse.h"
#include "audio/audio.h"
#include "vars.h"
#include "5000.h"
#include "macros.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char* keys[]={"video_mode","crt_filter","tileset","transparent","dithered_darkness",
    "music","sound","mouse","smooth","diagonal","movement_speed","animation_speed"};
static const char* labels[]={"Video Mode","CRT Filter","Tileset","Transparent Sprites","Dithered Darkness",
    "Music","Sound Effects","Mouse Control","Smooth Movement","Diagonal Movement","Movement Speed","Animation Speed"};
static bool isSpeed(int row) { return row==ENGINE_MOVEMENT_SPEED || row==ENGINE_ANIMATION_SPEED; }
static const char* optionStatus;
static const float ticks[]={0.5f,0.75f,1.0f};
float ENGINE_Get(int row)
{
    switch(row) {
    case ENGINE_FULLSCREEN:return GRAP_SDL_VideoMode();
    case ENGINE_TILESET:return TILESET_Selected();
    case ENGINE_MOUSE:return MOUSE_Enabled();
    case ENGINE_SMOOTH:return GRAP_SDL_SmoothMovementEnabled();
    case ENGINE_DIAGONAL:return MOVEMENT_Diagonal();
    case ENGINE_TRANSPARENT:return GRAP_BUF_TransparentSprites();
    case ENGINE_DITHERED_DARKNESS:return WIDE_DitheredDarkness();
    case ENGINE_CRT:return CRT_Enabled();
    case ENGINE_MUSIC:return AUDIO_MusicEnabled();
    case ENGINE_SOUND:return AUDIO_SoundEnabled();
    case ENGINE_MOVEMENT_SPEED:return GRAP_SDL_MovementSpeed();
    case ENGINE_ANIMATION_SPEED:return ANIMATION_Speed();
    default:return 0;
    }
}
void ENGINE_Set(int row,float value)
{
    if(!isfinite(value) || row<0 || row>=ENGINE_SETTING_COUNT) return;
    if(row==ENGINE_FULLSCREEN && value!=0 && value!=1 && value!=2) return;
    if(row==ENGINE_TILESET && (value<0 || value>=TILESET_COUNT || value!=(int)value)) return;
    if(row!=ENGINE_FULLSCREEN && row!=ENGINE_TILESET && !isSpeed(row) && value!=0 && value!=1) return;
    if(isSpeed(row) && (value<0.1f || value>10)) return;
    debug("Engine option %s=%.6g",keys[row],(double)value);
    switch(row) {
    case ENGINE_FULLSCREEN:GRAP_SDL_SetVideoMode((int)value);break;
    case ENGINE_TILESET:optionStatus=TILESET_Select((int)value)?NULL:"Tileset files unavailable";break;
    case ENGINE_MOUSE:MOUSE_SetEnabled(value!=0);break;
    case ENGINE_SMOOTH:GRAP_SDL_SetSmoothMovement(value!=0);break;
    case ENGINE_DIAGONAL:MOVEMENT_SetDiagonal(value!=0);break;
    case ENGINE_TRANSPARENT:GRAP_BUF_SetTransparentSprites(value!=0);break;
    case ENGINE_DITHERED_DARKNESS:WIDE_SetDitheredDarkness(value!=0);break;
    case ENGINE_CRT:CRT_SetEnabled(value!=0);break;
    case ENGINE_MUSIC:AUDIO_SetMusicEnabled(value!=0);break;
    case ENGINE_SOUND:AUDIO_SetSoundEnabled(value!=0);break;
    case ENGINE_MOVEMENT_SPEED:GRAP_SDL_SetMovementSpeed(value);break;
    case ENGINE_ANIMATION_SPEED:ANIMATION_SetSpeed(value);break;
    }
}
void ENGINE_Load(void)
{
    FILE* f=FILE_Open("ENGINE.CFG","r");
    if(!f) return;
    char line[FILE_PATH_SIZE+64],key[64],extra;
    float value;
    while(fgets(line,sizeof(line),f)) {
        if(!strncmp(line,"custom_tileset ",15)) {
            char* path=line+15;path[strcspn(path,"\r\n")]=0;
            TILESET_SetCustomPath(path);continue;
        }
        if(sscanf(line,"%63s %f %c",key,&value,&extra)==2) {
            if(!strcmp(key,"fullscreen") && (value==0 || value==1)) ENGINE_Set(ENGINE_FULLSCREEN,value);
            for(int i=0;i<ENGINE_SETTING_COUNT;i++) if(!strcmp(key,keys[i])) ENGINE_Set(i,value);
        }
    }
    fclose(f);
}
bool ENGINE_Save(void)
{
    FILE* f=FILE_Open("ENGINE.CFG.tmp","w");
    if(!f) { DEBUG_Error("Cannot write ENGINE.CFG.tmp");return false; }
    debug("Writing engine settings");
    bool ok=fprintf(f,"custom_tileset %s\n",TILESET_CustomPath())>=0;
    for(int i=0;i<ENGINE_SETTING_COUNT;i++) if(fprintf(f,"%s %.6g\n",keys[i],(double)ENGINE_Get(i))<0) ok=false;
    if(fclose(f)!=0) ok=false;
    char path[FILE_PATH_SIZE],temporary[FILE_PATH_SIZE];
    if(!ok || FILE_ResolvePath("ENGINE.CFG",path,sizeof(path),1)!=0) return false;
    if(FILE_ResolvePath("ENGINE.CFG.tmp",temporary,sizeof(temporary),0)!=0) return false;
    return SDL_RenamePath(temporary,path);
}
void ENGINE_UIRect(int x,int y,int w,int h,byte color)
{
    for(int j=y;j<y+h;j++) memset(g_linearEgaBuffer0+j*320+x,color,(size_t)w);
}
void ENGINE_UIText(int x,int y,const char* s,byte color)
{
    const byte* font=D_539c[0] ? D_539c[0] : D_5398_currentCharset;
    if(!font) return;
    for(;*s && x<=312;s++,x+=8)
        for(int j=0;j<8;j++) for(int i=0;i<8;i++)
            if(font[(unsigned char)*s*8+j] & (0x80>>i)) g_linearEgaBuffer0[(y+j)*320+x+i]=color;
}
/* Reuse the original IBM.CH border glyphs from the title menu. */
void ENGINE_UIFrame(void)
{
    for(int x=8;x<312;x+=8) {
        ENGINE_UIText(x,0,"\x7f",1);ENGINE_UIText(x,192,"\x7f",1);
    }
    for(int y=8;y<192;y+=8) {
        ENGINE_UIText(0,y,"\x7f",1);ENGINE_UIText(312,y,"\x7f",1);
    }
    ENGINE_UIText(0,0,"\x7b",1);ENGINE_UIText(312,0,"\x7c",1);
    ENGINE_UIText(0,192,"\x7d",1);ENGINE_UIText(312,192,"\x7e",1);
    ENGINE_UIRect(7,7,306,1,15);ENGINE_UIRect(7,7,1,186,15);
    ENGINE_UIRect(312,7,1,186,15);ENGINE_UIRect(7,192,306,1,15);
}
static int rowY(int row)
{
    if(row<ENGINE_MOVEMENT_SPEED) return 40+row*10;
    if(row==ENGINE_MOVEMENT_SPEED) return 140;
    if(row==ENGINE_ANIMATION_SPEED) return 160;
    return 180;
}
static bool s_gameplay;
static int s_videoChoice=-1;
static int s_dropdownRow=ENGINE_FULLSCREEN;
static int choiceTop(void) { return rowY(s_dropdownRow)+12; }
static int choices(void) { return s_dropdownRow==ENGINE_TILESET?TILESET_COUNT:3; }
static const char* videoLabel(int choice)
{
    if(choice==0) return "Windowed";
    if(choice==1) return "Fullscreen 4:3";
    static char label[32];
    int width,height;
    if(!GRAP_SDL_MonitorSize(&width,&height)) return "Fullscreen Native";
    int a=width,b=height;
    while(b) { int remainder=a%b;a=b;b=remainder; }
    width/=a;height/=a;
    /* Conventional monitor notation for the mathematically equivalent 8:5. */
    if(width==8 && height==5) { width=16;height=10; }
    SDL_snprintf(label,sizeof(label),"Fullscreen %d:%d",width,height);
    return label;
}
static const int videoModes[]={GRAP_VIDEO_WINDOWED,GRAP_VIDEO_FULLSCREEN_43,GRAP_VIDEO_FULLSCREEN};
static int videoChoice(void)
{
    for(int i=0;i<3;i++) if(videoModes[i]==GRAP_SDL_VideoMode()) return i;
    return 0;
}
static const char* choiceLabel(int choice) { return s_dropdownRow==ENGINE_TILESET?TILESET_Label(choice):videoLabel(choice); }
static int choiceWidth(void)
{
    int width=0;
    for(int i=0;i<choices();i++) width=SDL_max(width,(int)strlen(choiceLabel(i))*8);
    return width+16;
}
static int choiceLeft(void) { return 304-choiceWidth(); }
/* Center values over the checkbox/status column; long labels meet the
 * same right margin as the speed values instead of overflowing it. */
static int choiceTextX(const char* label)
{
    int width=(int)strlen(label)*8;
    return SDL_min(252-width/2,296-width);
}
static bool acceptCustomTileset(const char* path,void* user)
{
    (void)user;
    return TILESET_SelectCustom(path);
}
static void acceptChoice(void)
{
    int row=s_dropdownRow,choice=s_videoChoice;
    s_videoChoice=-1;
    if(row==ENGINE_TILESET && choice==TILESET_CUSTOM) {
        const char* initial=*TILESET_CustomPath()?TILESET_CustomPath():SDL_GetUserFolder(SDL_FOLDER_HOME);
        FILEPICKER_Select("Custom Tileset","Choose a 2:1 PNG tileset",NULL,".png",initial,
                           "Return to Engine Options","Cannot load PNG: needs 2:1 aspect",acceptCustomTileset,NULL);
    } else ENGINE_Set(row,row==ENGINE_TILESET?choice:videoModes[choice]);
}
static void openChoice(int row) { s_dropdownRow=row;s_videoChoice=row==ENGINE_TILESET?TILESET_Selected():videoChoice(); }
void ENGINE_DrawSettings(int selected)
{
    memset(g_linearEgaBuffer0,0,320*200);
    ENGINE_UIFrame();
    ENGINE_UIText(104,12,"Engine Options",15);
    ENGINE_UIText(24,26,optionStatus?optionStatus:"Arrows / Enter   Esc: Back",7);
    for(int row=0;row<=ENGINE_SETTING_COUNT;row++) {
        int y=rowY(row);
        bool highlighted=row==selected && s_videoChoice<0;
        byte foreground=highlighted?0:15;
        if(highlighted) ENGINE_UIRect(16,y-1,288,10,15);
        ENGINE_UIText(24,y,row==ENGINE_SETTING_COUNT?(s_gameplay?"Return to Game":"Return to Menu"):labels[row],foreground);
        if(row==ENGINE_SETTING_COUNT) continue;
        float value=ENGINE_Get(row);
        if(row==ENGINE_FULLSCREEN || row==ENGINE_TILESET) {
            const char* label=row==ENGINE_TILESET?TILESET_Label(TILESET_Selected()):videoLabel(videoChoice());
            ENGINE_UIText(choiceTextX(label),y,label,foreground);
        } else if(!isSpeed(row)) {
            ENGINE_UIRect(232,y+1,7,7,7);
            ENGINE_UIRect(233,y+2,5,5,value?10:0);
            ENGINE_UIText(248,y,value?"On":"Off",highlighted?0:(value?10:7));
        } else {
            ENGINE_UIRect(184,y+4,64,1,highlighted?0:7);
            for(int i=0;i<3;i++) ENGINE_UIRect(184+i*32,y+1,1,7,foreground);
            int knob=184+(int)SDL_roundf(SDL_clamp((value-0.5f)*128,0,64));
            ENGINE_UIRect(knob-2,y+1,5,7,highlighted?0:14);
            char number[12];SDL_snprintf(number,sizeof(number),"%.2f",(double)value);
            ENGINE_UIText(264,y,number,highlighted?0:14);
            ENGINE_UIText(172,y+9,"0.5",7);ENGINE_UIText(200,y+9,"0.75",7);ENGINE_UIText(244,y+9,"1",7);
        }
    }
    if(s_videoChoice>=0) {
        int left=choiceLeft(),top=choiceTop(),width=choiceWidth(),height=choices()*11+4;
        /* Black surround separates the popup from partially covered settings. */
        ENGINE_UIRect(left-2,top-2,width+4,height+4,0);
        ENGINE_UIRect(left,top,width,height,15);
        ENGINE_UIRect(left+1,top+1,width-2,height-2,0);
        for(int i=0;i<choices();i++) {
            int y=top+3+i*11;
            if(i==s_videoChoice) ENGINE_UIRect(left+2,y-1,width-4,10,15);
            const char* label=choiceLabel(i);
            ENGINE_UIText(left+8,y,label,i==s_videoChoice?0:15);
        }
    }
    GRAP_BUF_MarkDirty();GRAP_BUF_Present();
}
static void adjust(int row,int direction)
{
    float value=ENGINE_Get(row);
    if(row==ENGINE_MUSIC && !value && !*SETUP_MusicDirectory()) {
        if(FOLDER_SelectMusic(SETUP_MusicDirectory(),SETUP_SetMusicDirectory)) {
            AUDIO_ReloadMusic();ENGINE_Set(ENGINE_MUSIC,1);
            optionStatus="Music folder saved";
        } else optionStatus="Music selection cancelled";
    } else if(!isSpeed(row)) ENGINE_Set(row,!value);
    else {
        float next=value;
        if(direction>0) { for(int i=0;i<3;i++) if(ticks[i]>value+0.001f) { next=ticks[i];break; } }
        else { for(int i=2;i>=0;i--) if(ticks[i]<value-0.001f) { next=ticks[i];break; } }
        ENGINE_Set(row,next);
    }
}
void ENGINE_ShowOptions(bool gameplay)
{
    s_gameplay=gameplay;
    unsigned previousTileset=TILESET_Revision();
    optionStatus=NULL;
    s_videoChoice=-1;
    byte backup[320*200];TILESET_Register(backup,sizeof(backup));
    TILESET_Copy(backup,g_linearEgaBuffer0,sizeof(backup));memcpy(backup,g_linearEgaBuffer0,sizeof(backup));
    extern void KEY_SDL_ClearInput(void);
    KEY_SDL_ClearInput();MOUSE_Cancel();
    GRAP_SDL_SetPixelUI(true);
    MOUSE_SetPointerMode(true);
    int selected=0,drag=-1;bool done=false,changed=false;
    ENGINE_DrawSettings(selected);
    while(!done) {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type==SDL_EVENT_QUIT ||
               (event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_E &&
                (event.key.mod & SDL_KMOD_CTRL))) { if(changed) ENGINE_Save();exit(0); }
            if(s_videoChoice>=0) {
                if(event.type==SDL_EVENT_KEY_DOWN) {
                    switch(event.key.key) {
                    case SDLK_ESCAPE:s_videoChoice=-1;break;
                    case SDLK_UP:s_videoChoice=(s_videoChoice+choices()-1)%choices();break;
                    case SDLK_DOWN:s_videoChoice=(s_videoChoice+1)%choices();break;
                    case SDLK_RETURN:case SDLK_SPACE:
                        acceptChoice();changed=true;break;
                    }
                } else if(event.type==SDL_EVENT_MOUSE_MOTION ||
                          (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT)) {
                    float x,y;
                    float ex=event.type==SDL_EVENT_MOUSE_MOTION?event.motion.x:event.button.x;
                    float ey=event.type==SDL_EVENT_MOUSE_MOTION?event.motion.y:event.button.y;
                    if(GRAP_SDL_MouseUIPoint(ex,ey,&x,&y) && x>=choiceLeft()+2 && x<302 && y>=choiceTop()+2 && y<choiceTop()+2+choices()*11) {
                        s_videoChoice=SDL_clamp((int)(y-choiceTop()-2)/11,0,choices()-1);
                        if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) {
                            acceptChoice();changed=true;
                        }
                    } else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) s_videoChoice=-1;
                }
                ENGINE_DrawSettings(selected);continue;
            }
            if(event.type==SDL_EVENT_KEY_DOWN) {
                switch(event.key.key) {
                case SDLK_ESCAPE:done=true;break;
                case SDLK_UP:selected=(selected+ENGINE_SETTING_COUNT)%(ENGINE_SETTING_COUNT+1);break;
                case SDLK_DOWN:selected=(selected+1)%(ENGINE_SETTING_COUNT+1);break;
                case SDLK_LEFT:case SDLK_RIGHT:case SDLK_RETURN:case SDLK_SPACE:
                    if(event.key.repeat && !isSpeed(selected)) break;
                    if(selected==ENGINE_SETTING_COUNT) { done=true;break; }
                    if(selected==ENGINE_FULLSCREEN || selected==ENGINE_TILESET) openChoice(selected);
                    else { adjust(selected,event.key.key==SDLK_LEFT?-1:1);changed=true; }
                    break;
                }
            }
            if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT) {
                float x,y;
                if(GRAP_SDL_MouseUIPoint(event.button.x,event.button.y,&x,&y) && x>=16 && x<304)
                    for(int row=0;row<=ENGINE_SETTING_COUNT;row++) if(y>=rowY(row)-1 && y<rowY(row)+9) {
                        selected=row;
                        if(row==ENGINE_FULLSCREEN || row==ENGINE_TILESET) openChoice(row);
                        else if(row==ENGINE_SETTING_COUNT) done=true;
                        else if(isSpeed(row) && x>=176 && x<=256) {
                            int tick=SDL_clamp((int)SDL_roundf((x-184)/32),0,2);
                            ENGINE_Set(row,ticks[tick]);changed=true;
                            drag=row;
                        } else { adjust(row,1);changed=true; }
                    }
            }
            if(event.type==SDL_EVENT_MOUSE_BUTTON_UP) drag=-1;
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST) drag=-1;
            if(event.type==SDL_EVENT_MOUSE_MOTION && drag<0) {
                float x,y;
                if(GRAP_SDL_MouseUIPoint(event.motion.x,event.motion.y,&x,&y) && x>=16 && x<304)
                    for(int row=0;row<=ENGINE_SETTING_COUNT;row++)
                        if(y>=rowY(row)-1 && y<rowY(row)+9) selected=row;
            }
            if(event.type==SDL_EVENT_MOUSE_MOTION && drag>=0) {
                float x,y;
                if(GRAP_SDL_MouseUIPoint(event.motion.x,event.motion.y,&x,&y)) {
                    int tick=SDL_clamp((int)SDL_roundf((x-184)/32),0,2);
                    ENGINE_Set(drag,ticks[tick]);changed=true;
                }
            }
            if(event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_DOWN ||
               event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type==SDL_EVENT_MOUSE_MOTION || drag>=0) ENGINE_DrawSettings(selected);
        }
        MOUSE_UpdateCursor();
        SDL_Delay(16);
    }
    if(changed && !ENGINE_Save()) fprintf(stderr,"Unable to save engine settings\n");
    KEY_SDL_ClearInput();MOUSE_Cancel();
    GRAP_SDL_SetPixelUI(false);
    MOUSE_SetPointerMode(false);
    TILESET_Copy(g_linearEgaBuffer0,backup,sizeof(backup));memcpy(g_linearEgaBuffer0,backup,sizeof(backup));
    TILESET_Unregister(backup);
    if(gameplay && previousTileset!=TILESET_Revision() && GRAP_BUF_HasTileset() &&
       D_58a4 && (D_5893_map_id<=32 || D_5893_map_id>=128)) ULTIMA_56ac_DrawMap();
    GRAP_BUF_MarkDirty();GRAP_BUF_Present();
}
