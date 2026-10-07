#ifndef U5D_MOUSE_H
#define U5D_MOUSE_H
#include "common/common.h"
#if defined(TARGET_SDL)
void MOUSE_MenuSet(int x, int y, int width, int count, int selected);
void MOUSE_MenuEnd(void);
int MOUSE_MenuRead(int x, int y, int width, int count, int selected);
#else
#define MOUSE_MenuSet(x,y,w,n,s) ((void)0)
#define MOUSE_MenuEnd() ((void)0)
#define MOUSE_MenuRead(x,y,w,n,s) ULTIMA_266c_GetChar()
#endif
void MOUSE_SetCombatAimInput(int entity, int range);
void MOUSE_EndCombatAimInput(void);
void MOUSE_ClearCombatAttack(void);
bool MOUSE_CombatAttackTarget(int entity, int range, int* distance);
void MOUSE_Initialize(void);
void MOUSE_Cleanup(void);
void MOUSE_ReleaseFilteredCursors(void);
struct SDL_Renderer;
bool MOUSE_DrawFilteredCursor(struct SDL_Renderer* renderer,float curvatureX,float curvatureY);
void MOUSE_UpdateCursor(void);
void MOUSE_SetPointerMode(bool enabled);
int MOUSE_CursorDirection(float x, float y);
void MOUSE_SetEnabled(bool enabled);
bool MOUSE_Enabled(void);
void MOUSE_SetCommandInput(bool enabled);
void MOUSE_BeginDirectionInput(bool talk);
void MOUSE_EndDirectionInput(void);
void MOUSE_Button(float x, float y, int button, bool down, int clicks);
void MOUSE_Cancel(void);
int MOUSE_PollCommand(void);
int MOUSE_TakeDirection(void);
bool MOUSE_TakeTarget(int* dx, int* dy);
int MOUSE_Direction(float dx, float dy);
int MOUSE_Action(int dx, int dy, bool mainAction);
#endif
