#ifndef IMPERA_AMBIENT_LOOP_H
#define IMPERA_AMBIENT_LOOP_H
#include <stddef.h>
/* Join tail to head in place; returns the shortened, seamless loop length. */
size_t AUDIO_CrossfadeLoop(float *pcm, size_t frames, int channels,
                           unsigned rate);
/* Short ramps at both file edges, preserving the interior and clip length. */
void AUDIO_FadeRecordingEdges(float *pcm, size_t frames, int channels,
                              unsigned rate);
#endif
