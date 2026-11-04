#include "ambient_loop.h"
#include <string.h>
size_t AUDIO_CrossfadeLoop(float *pcm, size_t frames, int channels,
                           unsigned rate) {
  if (!pcm || channels < 1 || !frames)
    return frames;
  size_t overlap =
      rate / 20; /* 50 ms; short clips retain at least 75% of their length. */
  if (overlap > frames / 4)
    overlap = frames / 4;
  if (overlap < 2)
    return frames;
  for (size_t i = 0; i < overlap; i++) {
    float weight = (float)i / (float)(overlap - 1);
    for (int c = 0; c < channels; c++) {
      size_t tail = (frames - overlap + i) * channels + c;
      pcm[tail] = pcm[tail] * (1 - weight) + pcm[i * channels + c] * weight;
    }
  }
  memmove(pcm, pcm + overlap * channels,
          (frames - overlap) * channels * sizeof(float));
  return frames - overlap;
}

void AUDIO_FadeRecordingEdges(float *pcm, size_t frames, int channels,
                              unsigned rate) {
  if (!pcm || channels < 1)
    return;
  size_t ramp = rate / 500;
  if (ramp > frames / 4)
    ramp = frames / 4;
  if (ramp < 2)
    return;
  for (size_t i = 0; i < ramp; i++) {
    float gain = (float)i / (ramp - 1);
    for (int c = 0; c < channels; c++) {
      pcm[i * channels + c] *= gain;
      pcm[(frames - 1 - i) * channels + c] *= gain;
    }
  }
}
