#ifndef IMPERA_WORLD_MAP_H
#define IMPERA_WORLD_MAP_H
#include <stdio.h>
struct SDL_Surface;
void WORLD_MAP_ResetExploration(void);
unsigned WORLD_MAP_ExplorationEpoch(void);
/* Called only after a completed gameplay viewport is presented. */
void WORLD_MAP_RecordView(int columns,int rows,int mapY,int clipTop,int clipBottom);
int WORLD_MAP_WriteExploration(FILE* file);
int WORLD_MAP_ReadExploration(FILE* file,int apply);
/* Rebuild from the currently mounted terrain and mutable type state. */
struct SDL_Surface* WORLD_MAP_Generate(void);
/* Modal, at a gameplay command-input boundary only. */
void WORLD_MAP_Show(void);
#endif
