#include "engine_settings.h"
#include "file.h"
#include "movement.h"
#include "graphics/grap_sdl.h"
#include "graphics/grap_buf.h"
#include "graphics/animate.h"
#include "key/mouse.h"
#include "audio/audio.h"
#include "vars.h"
#include "macros.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char* keys[]={"fullscreen","mouse","smooth","diagonal","transparent",
    "music","sound","movement_speed","animation_speed"};
static const char* labels[]={"Fullscreen","Mouse Control","Smooth Movement","Diagonal Movement",
    "Transparent Sprites","Music","Sound Effects","Movement Speed","Animation Speed"};
static const float ticks[]={0.5f,0.75f,1.0f};
float ENGINE_Get(int row)
{
    switch(row) {
    case ENGINE_FULLSCREEN:return GRAP_SDL_Fullscreen();
    case ENGINE_MOUSE:return MOUSE_Enabled();
    case ENGINE_SMOOTH:return GRAP_SDL_SmoothMovementEnabled();
    case ENGINE_DIAGONAL:return MOVEMENT_Diagonal();
    case ENGINE_TRANSPARENT:return GRAP_BUF_TransparentSprites();
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
    if(row<ENGINE_MOVEMENT_SPEED && value!=0 && value!=1) return;
    if(row>=ENGINE_MOVEMENT_SPEED && (value<0.1f || value>10)) return;
    switch(row) {
    case ENGINE_FULLSCREEN:GRAP_SDL_SetFullscreen(value!=0);break;
    case ENGINE_MOUSE:MOUSE_SetEnabled(value!=0);break;
    case ENGINE_SMOOTH:GRAP_SDL_SetSmoothMovement(value!=0);break;
    case ENGINE_DIAGONAL:MOVEMENT_SetDiagonal(value!=0);break;
    case ENGINE_TRANSPARENT:GRAP_BUF_SetTransparentSprites(value!=0);break;
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
    char line[128],key[64],extra;
    float value;
    while(fgets(line,sizeof(line),f))
        if(sscanf(line,"%63s %f %c",key,&value,&extra)==2)
            for(int i=0;i<ENGINE_SETTING_COUNT;i++) if(!strcmp(key,keys[i])) ENGINE_Set(i,value);
    fclose(f);
}
bool ENGINE_Save(void)
{
    FILE* f=FILE_Open("ENGINE.CFG.tmp","w");
    if(!f) return false;
    bool ok=true;
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
static int rowY(int row) { return row<7 ? 40+row*14 : row==7?140:row==8?160:180; }
static bool s_gameplay;
void ENGINE_DrawSettings(int selected)
{
    memset(g_linearEgaBuffer0,0,320*200);
    ENGINE_UIFrame();
    ENGINE_UIText(104,12,"Engine Options",15);
    ENGINE_UIText(32,26,"Arrows / Enter   Esc: Back",7);
    for(int row=0;row<=ENGINE_SETTING_COUNT;row++) {
        int y=rowY(row);
        bool highlighted=row==selected;
        byte foreground=highlighted?0:15;
        if(highlighted) ENGINE_UIRect(16,y-1,288,10,15);
        ENGINE_UIText(24,y,row==ENGINE_SETTING_COUNT?(s_gameplay?"Return to Game":"Return to Menu"):labels[row],foreground);
        if(row==ENGINE_SETTING_COUNT) continue;
        float value=ENGINE_Get(row);
        if(row<ENGINE_MOVEMENT_SPEED) {
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
    GRAP_BUF_MarkDirty();GRAP_BUF_Present();
}
static void adjust(int row,int direction)
{
    float value=ENGINE_Get(row);
    if(row<ENGINE_MOVEMENT_SPEED) ENGINE_Set(row,!value);
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
    byte backup[320*200];memcpy(backup,g_linearEgaBuffer0,sizeof(backup));
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
            if(event.type==SDL_EVENT_KEY_DOWN) {
                switch(event.key.key) {
                case SDLK_ESCAPE:done=true;break;
                case SDLK_UP:selected=(selected+ENGINE_SETTING_COUNT)%(ENGINE_SETTING_COUNT+1);break;
                case SDLK_DOWN:selected=(selected+1)%(ENGINE_SETTING_COUNT+1);break;
                case SDLK_LEFT:case SDLK_RIGHT:case SDLK_RETURN:case SDLK_SPACE:
                    if(event.key.repeat && selected<ENGINE_MOVEMENT_SPEED) break;
                    if(selected==ENGINE_SETTING_COUNT) { done=true;break; }
                    adjust(selected,event.key.key==SDLK_LEFT?-1:1);changed=true;break;
                }
            }
            if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT) {
                float x,y;
                if(GRAP_SDL_MouseUIPoint(event.button.x,event.button.y,&x,&y) && x>=16 && x<304)
                    for(int row=0;row<=ENGINE_SETTING_COUNT;row++) if(y>=rowY(row)-1 && y<rowY(row)+9) {
                        selected=row;
                        if(row==ENGINE_SETTING_COUNT) done=true;
                        else if(row>=ENGINE_MOVEMENT_SPEED && x>=176 && x<=256) {
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
    memcpy(g_linearEgaBuffer0,backup,sizeof(backup));GRAP_BUF_MarkDirty();GRAP_BUF_Present();
}
