#ifndef IMPERA_DIALOGUE_LINKS_H
#define IMPERA_DIALOGUE_LINKS_H
#include "common/common.h"
void DLINK_Enable(bool enabled);
bool DLINK_Enabled(void);
void DLINK_Begin(const byte *data, size_t size);
void DLINK_End(void);
void DLINK_Flush(void);
void DLINK_Prompt(bool enabled);
void DLINK_Reading(bool reading);
void DLINK_Character(int ch, const byte *glyph, byte foreground, int x, int y);
void DLINK_Clear(int left, int top, int right, int bottom);
void DLINK_Scroll(int left, int top, int right, int bottom, int amount);
void DLINK_Refresh(void);
bool DLINK_Click(float x, float y);
#endif
