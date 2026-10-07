#ifndef U5D_GRAP_SDL_H
#define U5D_GRAP_SDL_H

#include "common/common.h"

struct SDL_Surface;
struct SDL_Surface* GRAP_SDL_CaptureFrame(void);
void GRAP_SDL_UIThumbnail(int index,const char* path,int x,int y,int w,int h);
void GRAP_SDL_ClearUIThumbnails(void);

/* Set before BACKEND_Initialize creates the window. */
enum { GRAP_VIDEO_WINDOWED, GRAP_VIDEO_FULLSCREEN, GRAP_VIDEO_FULLSCREEN_43 };
void GRAP_SDL_SetVideoMode(int mode);
int GRAP_SDL_VideoMode(void);
void GRAP_SDL_SetFullscreen(bool fullscreen);
bool GRAP_SDL_Fullscreen(void);
float GRAP_SDL_MovementSpeed(void);
void GRAP_SDL_SetPixelUI(bool enabled);
void GRAP_SDL_SetSmoothMovement(bool enabled);
bool GRAP_SDL_SmoothMovementEnabled(void);
void GRAP_SDL_SetMovementSpeed(float speed);
bool GRAP_SDL_CustomMovementSpeed(void);
unsigned int GRAP_SDL_MovementInterval(void);
void GRAP_SDL_MapDrawn(void);
void GRAP_SDL_CursorSize(int* width, int* height);
bool GRAP_SDL_MouseUIPoint(float x, float y, float* ux, float* uy);
bool GRAP_SDL_MouseMapPoint(float x, float y, int* dx, int* dy, float* rx, float* ry);

#endif
