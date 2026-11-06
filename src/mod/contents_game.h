#ifndef IMPERA_CONTENTS_GAME_H
#define IMPERA_CONTENTS_GAME_H
#include "contents.h"
unsigned CONTENTS_GAME_Grant(const ContentsItem *item, unsigned quantity);
/* Actions: Search=0 Open=1 Get=2 Jimmy=3 Unlock magic=4. Returns handled. */
int CONTENTS_GAME_Interact(int action, int x, int y, int actor);
int CONTENTS_GAME_Shape(int x, int y);
int CONTENTS_GAME_LocationShape(unsigned map, int floor, int x, int y);
int CONTENTS_GAME_DungeonTile(int tile, int x, int y);
int CONTENTS_GAME_Command(int x, int y);
#endif
