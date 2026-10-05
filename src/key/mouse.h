#ifndef U5D_MOUSE_H
#define U5D_MOUSE_H
#include "common/common.h"
void MOUSE_Initialize(void);
void MOUSE_Cleanup(void);
void MOUSE_UpdateCursor(void);
int MOUSE_CursorDirection(float x, float y);
void MOUSE_SetEnabled(bool enabled);
void MOUSE_SetCommandInput(bool enabled);
void MOUSE_Button(float x, float y, int button, bool down, int clicks);
void MOUSE_Cancel(void);
int MOUSE_PollCommand(void);
int MOUSE_TakeDirection(void);
int MOUSE_Direction(float dx, float dy);
int MOUSE_Action(int dx, int dy, bool mainAction);
#endif
