#ifndef U5D_DATA_SETUP_H
#define U5D_DATA_SETUP_H
#include "common.h"
#if defined(TARGET_SDL)
bool SETUP_Run(void);
bool SETUP_Validate(const char* directory,char* error,size_t capacity);
const char* SETUP_MusicDirectory(void);
bool SETUP_SetMusicDirectory(const char* directory);
#endif
#endif
