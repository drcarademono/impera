#ifndef IMPERA_TILESET_H
#define IMPERA_TILESET_H
#include "common/common.h"
#include <SDL3/SDL.h>
enum
{
    TILESET_DOS,
    TILESET_AMIGA,
    TILESET_APPLE,
    TILESET_GRAY,
    TILESET_SHARP,
    TILESET_CUSTOM,
    TILESET_COUNT
};
int TILESET_Selected(void);
unsigned TILESET_Revision(void);
const char *TILESET_Label(int choice);
bool TILESET_Select(int choice);
const char *TILESET_CustomPath(void);
bool TILESET_SetCustomPath(const char *path);
bool TILESET_SelectCustom(const char *path);
void TILESET_Animate(void);
int TILESET_Pixel(int tile, int x, int y); /* -1: DOS fallback */
Uint32 TILESET_Color(int color, const Uint32 *ega);
Uint32 TILESET_LastColor(int color, int subX, int subY, const Uint32 *ega);
Uint32 TILESET_BufferColor(const byte *pixel, int subX, int subY, const Uint32 *ega);
/* Register/copy alongside legacy byte buffers to preserve native artwork.
 * Copy with a NULL source clears provenance for UI or blanked regions. */
void TILESET_Register(byte *pixels, size_t size);
void TILESET_Unregister(byte *pixels);
void TILESET_RecordSprite(byte *pixel, int color, bool transparent);
Uint32 TILESET_LastSpriteColor(int color, int subX, int subY, const Uint32 *ega, bool transparent);
void TILESET_Record(byte *pixel, int color);
void TILESET_Copy(byte *destination, const byte *source, size_t size);
/* Tile/SpritePixel selects the sample consumed by LastColor and Record. */
unsigned TILESET_Sample(void);
void TILESET_SetSample(unsigned sample);
void TILESET_Refresh(void);
#endif
