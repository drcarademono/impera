#include "mod/runtime.h"
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

static const char* keys[]={"video_mode","tileset","crt_filter","transparent","dithered_darkness",
    "music","sound","enhanced_look","mouse","smooth","diagonal","movement_speed","animation_speed"};
static const char* labels[]={"Video Mode","Tileset","CRT Filter","Transparent Sprites","Dithered Darkness",
    "Music","Sound Effects","Enhanced Look","Mouse Control","Smooth Movement","Diagonal Movement","Movement Speed","Animation Speed"};
static bool isSpeed(int row) { return row==ENGINE_MOVEMENT_SPEED || row==ENGINE_ANIMATION_SPEED; }
static const char* optionStatus;
static bool s_enhancedLook;
static const float ticks[]={0.5f,0.75f,1.0f};
float ENGINE_Get(int row)
{
    switch(row) {
    case ENGINE_FULLSCREEN:return GRAP_SDL_VideoMode();
    case ENGINE_TILESET:return TILESET_Selected();
    case ENGINE_ENHANCED_LOOK:return s_enhancedLook;
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
    if(row==ENGINE_TILESET && value==TILESET_ZHELKAS) value=TILESET_DOS;
    if(row==ENGINE_TILESET && (value<0 || value>=TILESET_COUNT || value!=(int)value)) return;
    if(row!=ENGINE_FULLSCREEN && row!=ENGINE_TILESET && !isSpeed(row) && value!=0 && value!=1) return;
    if(isSpeed(row) && (value<0.1f || value>10)) return;
    debug("Engine option %s=%.6g",keys[row],(double)value);
    switch(row) {
    case ENGINE_FULLSCREEN:GRAP_SDL_SetVideoMode((int)value);break;
    case ENGINE_TILESET:optionStatus=TILESET_Select((int)value)?NULL:"Tileset files unavailable";break;
    case ENGINE_ENHANCED_LOOK:s_enhancedLook=value!=0;break;
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
    byte color = (byte)MOD_FrameColor();
    for(int x=8;x<312;x+=8) {
        ENGINE_UIText(x,0,"\x7f",color);ENGINE_UIText(x,192,"\x7f",color);
    }
    for(int y=8;y<192;y+=8) {
        ENGINE_UIText(0,y,"\x7f",color);ENGINE_UIText(312,y,"\x7f",color);
    }
    ENGINE_UIText(0,0,"\x7b",color);ENGINE_UIText(312,0,"\x7c",color);
    ENGINE_UIText(0,192,"\x7d",color);ENGINE_UIText(312,192,"\x7e",color);
    ENGINE_UIRect(7,7,306,1,15);ENGINE_UIRect(7,7,1,186,15);
    ENGINE_UIRect(312,7,1,186,15);ENGINE_UIRect(7,192,306,1,15);
}
static int rowY(int row)
{
    return row == ENGINE_SETTING_COUNT ? 180 : 42 + row * 10;
}
static bool s_gameplay;
static int s_optionsTab, s_modSelected, s_modTop, s_modScroll;
static bool s_modDetails;

static void checkbox(int x, int y, bool enabled) {
    ENGINE_UIRect(x, y, 7, 7, 7);
    ENGINE_UIRect(x + 1, y + 1, 5, 5, enabled ? 10 : 0);
}
/* The native font has byte glyphs; decode UTF-8 before wrapping/truncating. */
static void modText(const char *text, char *out, size_t capacity) {
    size_t n = 0;
    while (*text && n + 1 < capacity) {
        unsigned c = (unsigned char)*text++;
        if (c >= 128) {
            unsigned extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
            c &= extra == 3 ? 7 : extra == 2 ? 15 : extra == 1 ? 31 : 127;
            while (extra-- && *text && ((unsigned char)*text & 192) == 128)
                c = (c << 6) | ((unsigned char)*text++ & 63);
        }
        out[n++] = c <= 255 ? (char)c : '?';
    }
    out[n] = 0;
}
static int descriptionLines(const char *text, int columns, int first, int visible, int x, int y) {
    char decoded[4608]; modText(text, decoded, sizeof(decoded));
    const char *p = decoded;
    int row = 0;
    while (*p) {
        char line[39]; int n = 0, space = -1;
        while (p[n] && p[n] != '\n' && p[n] != '\r' && n < columns) {
            line[n] = p[n] == '\t' ? ' ' : p[n];
            if (line[n] == ' ') space = n;
            ++n;
        }
        int consumed = n;
        if (n == columns && p[n] && p[n] != '\n' && p[n] != '\r' && space > 0)
            n = space, consumed = space + 1;
        line[n] = 0;
        if (row >= first && row < first + visible) ENGINE_UIText(x, y + (row - first) * 8, line, 15);
        ++row; p += consumed;
        if (*p == '\r') ++p;
        if (*p == '\n') ++p;
    }
    return row;
}
static void modImage(const ModInfo *info) {
    if (info && info->imageSize)
        GRAP_SDL_UIThumbnailPNG(0, info->image, info->imageSize, s_modDetails ? 16 : 144,
                               s_modDetails ? 60 : 44, 44, 24);
    else ENGINE_UIText(s_modDetails ? 16 : 144, s_modDetails ? 68 : 52, "No image", 8);
}
static void drawMods(void) {
    unsigned count = MOD_AvailableCount();
    if (!count) { ENGINE_UIText(40, 72, "No mods in the Mods folder", 7); return; }
    s_modSelected = SDL_clamp(s_modSelected, 0, (int)count - 1);
    const ModInfo *info = MOD_Available(s_modSelected);
    if (s_modDetails) {
        char title[257]; modText(info->title, title, sizeof(title)); title[36] = 0;
        ENGINE_UIText(16, 44, title, 15); modImage(info);
        checkbox(20, 164, info->enabled); ENGINE_UIText(32, 164, info->enabled ? "Enabled" : "Disabled", 15);
        char details[4608];
        SDL_snprintf(details, sizeof(details), "%s%s%s%s", *info->author ? "By " : "", info->author,
                     *info->author ? "\n\n" : "", *info->description ? info->description : "No description provided.");
        const char *description = details;
        int lines = descriptionLines(description, 28, 0, 0, 72, 60);
        s_modScroll = SDL_clamp(s_modScroll, 0, SDL_max(0, lines - 13));
        descriptionLines(description, 28, s_modScroll, 13, 72, 60);
        ENGINE_UIText(16, 180, "Esc: Back  Space: On/Off", 7);
    } else {
        if (s_modSelected < s_modTop) s_modTop = s_modSelected;
        if (s_modSelected >= s_modTop + 12) s_modTop = s_modSelected - 11;
        for (int row = 0; row < 12 && row + s_modTop < (int)count; ++row) {
            const ModInfo *item = MOD_Available(row + s_modTop); int y = 44 + row * 10;
            bool selected = row + s_modTop == s_modSelected;
            if (selected) ENGINE_UIRect(16, y - 1, 120, 10, 15);
            checkbox(20, y, item->enabled);
            char title[257]; modText(item->title, title, sizeof(title));
            if (strlen(title) > 12) { title[11] = '~'; title[12] = 0; }
            ENGINE_UIText(32, y, title, selected ? 0 : 15);
        }
        modImage(info);
        if (*info->author) {
            char author[300]; SDL_snprintf(author, sizeof(author), "By %s", info->author);
            char display[300]; modText(author, display, sizeof(display)); display[20] = 0;
            ENGINE_UIText(144, 76, display, 7);
        }
        descriptionLines(*info->description ? info->description : "No description provided.", 20, 0,
                         *info->author ? 9 : 10, 144, *info->author ? 88 : 76);
        ENGINE_UIText(16, 170, "Space: On/Off  Enter: Details", 7);
        ENGINE_UIText(16, 180, "Return", 15);
    }
    const char *status = optionStatus ? optionStatus : MOD_ChangesPending() ? "Restart Impera to apply changes" : *info->error ? "Mod could not load: see details" : NULL;
    if (status) {
        /* Show errors/restart requirements in place of the detail-page hint. */
        ENGINE_UIRect(16, 180, 288, 8, 0);
        char message[257]; modText(status, message, sizeof(message)); message[36] = 0;
        ENGINE_UIText(16, 180, message, 14);
    }
    if (s_modDetails && *info->error) {
        GRAP_SDL_ClearUIThumbnails();
        ENGINE_UIRect(72, 60, 232, 104, 0);
        descriptionLines(info->error, 28, 0, 13, 72, 60);
    }
}
static void toggleMod(void) {
    const ModInfo *info = MOD_Available(s_modSelected);
    if (!info) return;
    if (!MOD_SetEnabled(s_modSelected, !info->enabled)) { optionStatus = "Cannot save mod selection"; return; }
    optionStatus = NULL;
    if (!s_gameplay) {
        FILE_ReloadMods();
        TILESET_Select(TILESET_Selected());
        if (MOD_AvailableCount()) s_modSelected = SDL_min(s_modSelected, (int)MOD_AvailableCount() - 1);
    }
}

static int s_videoChoice=-1;
static int s_dropdownRow=ENGINE_FULLSCREEN;
static int choiceTop(void) { return rowY(s_dropdownRow)+12; }
static const int tilesetChoices[] = {TILESET_DOS, TILESET_AMIGA, TILESET_APPLE, TILESET_GRAY, TILESET_SHARP, TILESET_CUSTOM};
#define TILESET_CHOICES ((int)(sizeof(tilesetChoices)/sizeof(tilesetChoices[0])))
static int choices(void) { return s_dropdownRow==ENGINE_TILESET?TILESET_CHOICES:3; }
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

static const char* choiceLabel(int choice) { return s_dropdownRow==ENGINE_TILESET?TILESET_Label(tilesetChoices[choice]):videoLabel(choice); }
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
    int row=s_dropdownRow,choice=s_dropdownRow==ENGINE_TILESET?tilesetChoices[s_videoChoice]:s_videoChoice;
    s_videoChoice=-1;
    if(row==ENGINE_TILESET && choice==TILESET_CUSTOM) {
        const char* initial=*TILESET_CustomPath()?TILESET_CustomPath():SDL_GetUserFolder(SDL_FOLDER_HOME);
        FILEPICKER_Select("Custom Tileset","Choose a 2:1 PNG tileset",NULL,".png",initial,
                           "Return to Engine Options","Cannot load PNG: needs 2:1 aspect",acceptCustomTileset,NULL);
    } else ENGINE_Set(row,row==ENGINE_TILESET?choice:videoModes[choice]);
}
static void openChoice(int row) {
    s_dropdownRow=row;
    s_videoChoice=videoChoice();
    if(row==ENGINE_TILESET)
        for(int i=0;i<TILESET_CHOICES;i++)
            if(tilesetChoices[i]==TILESET_Selected()) { s_videoChoice=i; break; }
}
void ENGINE_DrawSettings(int selected)
{
    GRAP_SDL_ClearUIThumbnails();
    TILESET_Copy(g_linearEgaBuffer0, NULL, 320*200);
    memset(g_linearEgaBuffer0,0,320*200);
    ENGINE_UIFrame();
    ENGINE_UIText(104,12,"Engine Options",15);
    if (!s_optionsTab) ENGINE_UIRect(20, 25, 64, 10, 15);
    else ENGINE_UIRect(100, 25, 48, 10, 15);
    ENGINE_UIText(24,26,"Options",s_optionsTab ? 7 : 0);
    ENGINE_UIText(104,26,"Mods",s_optionsTab ? 0 : 7);
    if (s_optionsTab) { drawMods(); GRAP_BUF_MarkDirty(); GRAP_BUF_Present(); return; }
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
            checkbox(232,y+1,value != 0);
            ENGINE_UIText(248,y,value?"On":"Off",highlighted?0:(value?10:7));
        } else {
            ENGINE_UIRect(184,y+4,64,1,highlighted?0:7);
            for(int i=0;i<3;i++) ENGINE_UIRect(184+i*32,y+1,1,7,foreground);
            int knob=184+(int)SDL_roundf(SDL_clamp((value-0.5f)*128,0,64));
            ENGINE_UIRect(knob-2,y+1,5,7,highlighted?0:14);
            char number[12];SDL_snprintf(number,sizeof(number),"%.2f",(double)value);
            ENGINE_UIText(264,y,number,highlighted?0:14);

        }
    }
    if (optionStatus) ENGINE_UIText(24, 172, optionStatus, 7);
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
    s_optionsTab = 0; s_modDetails = false; s_modScroll = 0;
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
            if(event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_TAB) {
                s_optionsTab = !s_optionsTab; s_videoChoice = -1; drag = -1; s_modDetails = false;
                ENGINE_DrawSettings(selected); continue;
            }
            if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                float x, y;
                if(GRAP_SDL_MouseUIPoint(event.button.x, event.button.y, &x, &y) && y >= 24 && y < 37 && x >= 20 && x < 152) {
                    s_optionsTab = x >= 96; s_videoChoice = -1; drag = -1; s_modDetails = false;
                    ENGINE_DrawSettings(selected); continue;
                }
            }
            if(s_optionsTab) {
                int count = (int)MOD_AvailableCount();
                if(event.type == SDL_EVENT_KEY_DOWN) {
                    switch(event.key.key) {
                    case SDLK_ESCAPE: if(s_modDetails) s_modDetails = false; else done = true; break;
                    case SDLK_UP: if(s_modDetails) s_modScroll = SDL_max(0, s_modScroll - 1); else s_modSelected = SDL_max(0, s_modSelected - 1); break;
                    case SDLK_DOWN: if(s_modDetails) ++s_modScroll; else s_modSelected = SDL_min(SDL_max(0,count-1), s_modSelected + 1); break;
                    case SDLK_PAGEUP: if(s_modDetails) s_modScroll = SDL_max(0, s_modScroll - 12); else s_modSelected = SDL_max(0, s_modSelected - 12); break;
                    case SDLK_PAGEDOWN: if(s_modDetails) s_modScroll += 12; else s_modSelected = SDL_min(SDL_max(0,count-1), s_modSelected + 12); break;
                    case SDLK_RETURN: if(count) { s_modDetails = !s_modDetails; s_modScroll = 0; } break;
                    case SDLK_SPACE: if(!event.key.repeat) toggleMod(); break;
                    }
                }
                if(event.type == SDL_EVENT_MOUSE_WHEEL) {
                    int change = event.wheel.y > 0 ? -1 : 1;
                    if(s_modDetails) s_modScroll = SDL_max(0,s_modScroll + change * 3);
                    else s_modSelected = SDL_clamp(s_modSelected + change,0,SDL_max(0,count-1));
                }
                if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                    float x,y;
                    if(GRAP_SDL_MouseUIPoint(event.button.x,event.button.y,&x,&y)) {
                        if(s_modDetails) {
                            if(y>=162 && y<173 && x>=16 && x<136) toggleMod();
                            else if(y>=178) s_modDetails = false;
                        } else if(y>=44 && y<164 && x>=16 && x<136) {
                            int item = s_modTop + ((int)y - 44) / 10;
                            if(item<count) { s_modSelected=item; s_modScroll=0; if(event.button.clicks<=1) toggleMod(); }
                        } else if(x>=144 && y>=44 && y<174 && count) { s_modDetails=true; s_modScroll=0; }
                        else if(y>=178 && x<136) done=true;
                    }
                }
                ENGINE_DrawSettings(selected); continue;
            }
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
    GRAP_SDL_ClearUIThumbnails(); s_optionsTab = 0;
    if(changed && !ENGINE_Save()) fprintf(stderr,"Unable to save engine settings\n");
    KEY_SDL_ClearInput();MOUSE_Cancel();
    GRAP_SDL_SetPixelUI(false);
    MOUSE_SetPointerMode(false);
    TILESET_Copy(g_linearEgaBuffer0,backup,sizeof(backup));memcpy(g_linearEgaBuffer0,backup,sizeof(backup));
    TILESET_Unregister(backup);
    if (gameplay && previousTileset != TILESET_Revision() && GRAP_BUF_HasTileset() && D_58a4 &&
        (MAPS_Outside(D_5893_map_id) || D_5893_map_id >= 128))
        ULTIMA_56ac_DrawMap();
    GRAP_BUF_MarkDirty();GRAP_BUF_Present();
}
