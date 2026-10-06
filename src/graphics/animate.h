#ifndef _GRAPHICS_ANIMATE_H
#define _GRAPHICS_ANIMATE_H

#include "common/common.h"

void AnimateTileset(byte* tileset);
void ANIMATION_SetSpeed(float speed);
float ANIMATION_Speed(void);
int ANIMATION_NextSteps(void);
void AnimateTimeTileset(byte* tileset, int mode, byte hour, byte minute);
void AnimateTile_BuildMoongateTile(byte* tileset, int visibleRows, byte floorType, byte* backup);
void AnimateTile_RestoreMoongateTile(byte* tileset, byte* backup);

#endif
