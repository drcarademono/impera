#ifndef U5D_WIDESCREEN_H
#define U5D_WIDESCREEN_H
#include "common/common.h"
typedef struct WideLayout {
    int scale, width, height, columns, rows, mapX, mapY, sidebarX;
} WideLayout;
WideLayout WIDE_Layout(int width, int height);
byte WIDE_MapTile(int dx, int dy);
byte WIDE_GroundTile(int dx, int dy);
int WIDE_SpritePixel(int tile, int dx, int dy, int x, int y);
byte WIDE_TerrainPixel(int dx, int dy, int x, int y);
void WIDE_SetDitheredDarkness(bool enabled);
bool WIDE_DitheredDarkness(void);
bool WIDE_DarknessMask(byte* mask,int columns,int rows);
bool WIDE_Visible(int dx, int dy);
int WIDE_ActorTile(int actor);
bool WIDE_Compose(byte* pixels, WideLayout layout);
#endif
