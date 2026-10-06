#ifndef U5D_GRAP_SDL_H
#define U5D_GRAP_SDL_H

#include "common/common.h"

/* Set before BACKEND_Initialize creates the window. */
void GRAP_SDL_SetFullscreen(bool fullscreen);
void GRAP_SDL_SetSmoothMovement(bool enabled);
bool GRAP_SDL_SmoothMovementEnabled(void);
void GRAP_SDL_MapDrawn(void);
void GRAP_SDL_CursorSize(int* width, int* height);
bool GRAP_SDL_MouseUIPoint(float x, float y, float* ux, float* uy);
bool GRAP_SDL_MouseMapPoint(float x, float y, int* dx, int* dy, float* rx, float* ry);

#endif
