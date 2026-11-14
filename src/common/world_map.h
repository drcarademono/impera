#ifndef IMPERA_WORLD_MAP_H
#define IMPERA_WORLD_MAP_H
struct SDL_Surface;
/* Rebuild from the currently mounted terrain and mutable type state. */
struct SDL_Surface* WORLD_MAP_Generate(void);
/* Modal, at a gameplay command-input boundary only. */
void WORLD_MAP_Show(void);
#endif
