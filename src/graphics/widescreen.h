#ifndef U5D_WIDESCREEN_H
#define U5D_WIDESCREEN_H
#include "common/common.h"
typedef struct WideLayout {
    int scale, width, height, columns, rows, mapX, mapY, sidebarX;
} WideLayout;
WideLayout WIDE_Layout(int width, int height);
bool WIDE_Compose(byte* pixels, WideLayout layout);
#endif
