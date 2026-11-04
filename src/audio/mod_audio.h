#ifndef IMPERA_MOD_AUDIO_H
#define IMPERA_MOD_AUDIO_H
#include "mod/audio_resource.h"
void AUDIO_MOD_Load(void);
int AUDIO_MOD_HasMusic(void);
const ModAudioClip *AUDIO_MOD_Clip(unsigned kind, unsigned id);
unsigned AUDIO_MOD_Assignment(unsigned scope, unsigned key, unsigned level, unsigned fallback);
#endif
