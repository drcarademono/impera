#ifndef U5D_SPRITES_H
#define U5D_SPRITES_H
#include "common/common.h"
#if defined(TARGET_SDL)
void SPRITES_SetEnabled(bool enabled);
void SPRITES_Load(void);
void SPRITES_Cleanup(void);
bool SPRITES_HasOverride(int tile);
byte SPRITES_Composite(int tile,int x,int y,byte background,byte original);
u32 SPRITES_RGBA(int tile,int x,int y,u32 original);
#else
#define SPRITES_HasOverride(tile) false
#define SPRITES_Composite(tile,x,y,b,o) (o)
#define SPRITES_Load() ((void)0)
#define SPRITES_Cleanup() ((void)0)
#endif
#endif
