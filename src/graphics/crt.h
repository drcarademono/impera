#ifndef IMPERA_CRT_H
#define IMPERA_CRT_H
#include "common/common.h"
struct SDL_Renderer;
void CRT_SetEnabled(bool enabled);
void CRT_SetCombinedEnabled(void);
bool CRT_Enabled(void);
void CRT_BeginFrame(struct SDL_Renderer* renderer);
void CRT_EndFrame(struct SDL_Renderer* renderer);
void CRT_ResumeFrame(struct SDL_Renderer* renderer);
void CRT_Cleanup(void);
void CRT_RefreshCursor(void);
#endif
