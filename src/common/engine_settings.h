#ifndef U5D_ENGINE_SETTINGS_H
#define U5D_ENGINE_SETTINGS_H
#include "common.h"
enum { ENGINE_FULLSCREEN, ENGINE_CRT, ENGINE_TILESET, ENGINE_TRANSPARENT, ENGINE_DITHERED_DARKNESS,
       ENGINE_MUSIC, ENGINE_SOUND, ENGINE_MOUSE, ENGINE_SMOOTH, ENGINE_DIAGONAL,
       ENGINE_MOVEMENT_SPEED, ENGINE_ANIMATION_SPEED, ENGINE_SETTING_COUNT };
void ENGINE_UIRect(int x,int y,int w,int h,byte color);
void ENGINE_UIText(int x,int y,const char* text,byte color);
void ENGINE_UIFrame(void);
float ENGINE_Get(int setting);
void ENGINE_Set(int setting,float value);
void ENGINE_Load(void);
bool ENGINE_Save(void);
void ENGINE_DrawSettings(int selected);
void ENGINE_ShowOptions(bool gameplay);
#endif
