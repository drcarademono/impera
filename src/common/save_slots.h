#ifndef U5D_SAVE_SLOTS_H
#define U5D_SAVE_SLOTS_H
#include "common.h"
#if defined(TARGET_SDL)
void SLOTS_SetLegacyEnabled(bool enabled);
void SLOTS_ResetTime(void);
void SLOTS_StartTime(void);
bool SLOTS_ShowSave(void);
bool SLOTS_ShowLoad(void);
bool SLOTS_ShowLoadInGame(void);
void SLOTS_SetReloadCallback(void (*callback)(void));
void SLOTS_RequestReload(void);
void SLOTS_ReloadActiveGame(void);
/* Storage APIs also used by round-trip tests. */
uint64_t SLOTS_PlayMilliseconds(void);
bool SLOTS_Write(const char* id,const char* name,const char* thumbnail);
bool SLOTS_Load(const char* id);
bool SLOTS_Delete(const char* id);
#endif
#endif
