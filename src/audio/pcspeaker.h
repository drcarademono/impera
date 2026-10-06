#ifndef U5D_PCSPEAKER_H
#define U5D_PCSPEAKER_H
#include <stddef.h>
/* PIT divisors, not frequencies in Hz. Output is mono float PCM at 48 kHz. */
enum { PCSPK_PULSE, PCSPK_NOISE, PCSPK_TONE, PCSPK_SWEEP };
float* PCSPK_Render(int kind, int a, int b, int c, int d, int e, size_t* frames);
#endif
