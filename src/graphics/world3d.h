#ifndef IMPERA_WORLD3D_H
#define IMPERA_WORLD3D_H
#include "common/common.h"
#include <SDL3/SDL.h>
bool WORLD3D_Enabled(void);
void WORLD3D_SetEnabled(bool enabled);
bool WORLD3D_Active(void);
void WORLD3D_SceneView(void);
void WORLD3D_MapDrawn(void);
int WORLD3D_Facing(void);       /* 0 north, 1 east, 2 south, 3 west. */
int WORLD3D_Direction(int key); /* Relative arrow -> compass arrow; no turning. */
int WORLD3D_Move(int key);      /* Left/right turn; Home/PageUp strafe. */
void WORLD3D_Render(SDL_Surface *surface, const Uint32 *palette);
void WORLD3D_RenderRegion(SDL_Surface *surface, const Uint32 *palette, int x, int y, int width,
                          int height, float pixelAspect);
bool WORLD3D_Animating(void);
void WORLD3D_CameraPose(float *x, float *y, float *angle);
bool WORLD3D_ActorPose(unsigned actor, float *x, float *y);
void WORLD3D_Cleanup(void);
bool WORLD3D_Pick(float u, float v, int *dx, int *dy);
#endif
