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
