#include "pcspeaker.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>

float* PCSPK_Render(int kind, int a, int b, int c, int d, int e, size_t* frames)
{
    /* The DOS pulse/noise loops were CPU calibrated. Use the port's existing
     * duration approximations; PIT square waves preserve their speaker timbre. */
    double ms = kind == PCSPK_PULSE ? c / 20.0 :
                kind == PCSPK_NOISE ? b / 10.0 :
                kind == PCSPK_TONE ? b * 0.8 : d * 1.5;
    *frames = 0;
    if (ms <= 0) return NULL;
    ms = fmin(ms,10000.0);
    size_t count = (size_t)ceil(ms * 48.0);
    float* pcm = SDL_malloc(count * sizeof(float));
    if (!pcm) return NULL;
    double phase = 0, divisor = a > 0 ? a : 1;
    uint32_t noise = 0x6d2b79f5u; /* private RNG: never perturb game randomness */
    size_t hold = a > 0 ? (size_t)a * 48 : 48;
    for (size_t i = 0; i < count; ++i) {
        if (kind == PCSPK_NOISE && i % hold == 0) {
            noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
            divisor = 1 + noise % (unsigned)(c > 0 ? c : 1);
        } else if (kind == PCSPK_SWEEP) {
            double steps = d > 0 && c > 0 ? floor((double)i / count * d / c) : 0;
            double total = d > 0 && c > 0 ? ceil((double)d / c) : 1;
            divisor = a + (b-a) * steps / total;
        }
        divisor = fmax(divisor,1);
        phase += fmin(1193182.0 / divisor,20000.0) / 48000.0;
        phase -= floor(phase);
        double duty = 0.5;
        if (kind == PCSPK_PULSE) {
            double width = d + e * ((double)i / 48000.0 * 1193182.0 / divisor) / (b > 0 ? b : 1);
            duty = fmin(0.95,fmax(0.05,width / 65536.0));
        }
        /* Remove PWM DC offset and soften the start/end to avoid clicks. */
        double value = phase < duty ? 1-duty : -duty;
        double envelope = fmin(1.0,fmin((i+1)/48.0,(count-i)/48.0));
        pcm[i] = (float)(value * envelope);
    }
    *frames = count;
    return pcm;
}
