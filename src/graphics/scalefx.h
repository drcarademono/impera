#ifndef IMPERA_SCALEFX_H
#define IMPERA_SCALEFX_H
#include "common/common.h"
struct SDL_Renderer;
struct SDL_Texture;
void SCALEFX_SetEnabled(bool enabled);
bool SCALEFX_Enabled(void);
bool SCALEFX_Apply(struct SDL_Renderer* renderer,struct SDL_Texture* input,int width,int height,int scale);
void SCALEFX_Cleanup(void);
#endif
