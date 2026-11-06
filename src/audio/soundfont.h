#ifndef IMPERA_SOUNDFONT_H
#define IMPERA_SOUNDFONT_H
#include <stdbool.h>
#include <stddef.h>
/* Optional player-owned SF2 bank; recordings always take precedence. */
bool SF_Load(const char *path);
const char *SF_Path(void);
int SF_Count(void);
bool SF_Enabled(void);
void SF_SetEnabled(bool enabled);
const char *SF_Name(int index);
int SF_Preset(int instrument);
void SF_SetPreset(int instrument, int preset);
unsigned SF_Revision(void);
/* Caller owns the returned interleaved stereo PCM (free with SDL_free). */
float *SF_Render(int effect, float speed, size_t *frames);
#endif
