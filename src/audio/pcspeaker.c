#include "pcspeaker.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <math.h>
#include <string.h>

static uint16_t divisor(uint16_t freq) { return freq ? (uint16_t)(1193182u / freq) : 0; }
static int signedWord(uint16_t w) { return w < 0x8000u ? w : (int)w - 0x10000; }
static uint64_t delayClocks(unsigned calibration, unsigned units) {
    return (uint64_t)(units ? units : 65536u) * (2u * (calibration ? calibration : 65536u) + 3u);
}
void PCSPK_Sequence(int kind, int a, int b, int c, int d, int e, uint16_t *state, PCSPK_Emit emit,
                    void *context) {
    uint16_t freq = (uint16_t)a, duration = (uint16_t)b;
    if (kind == PCSPK_TONE && freq) {
        emit(context, (PCSPK_Event){divisor(freq), true, true, duration,
                                    delayClocks(PCSPK_CALIBRATION, duration) + 29});
    } else if (kind == PCSPK_NOISE && freq && state && (uint16_t)c >= 100) {
        uint16_t elapsed = 0;
        for (unsigned i = 0; i < 65536; i++) {
            uint16_t next = (uint16_t)(*state + 0x9248u);
            next = (uint16_t)((next >> 3) | (next << 13));
            next ^= 0x9248u;
            *state = (uint16_t)(next + 0x11u);
            /* This is the separate, unmasked RNG at DOS DS:545c, not rand(). */
            uint16_t hz = (uint16_t)(100u + *state % ((uint16_t)c - 100u + 1u));
            elapsed = (uint16_t)(elapsed + freq);
            int last = signedWord(elapsed) >= signedWord(duration);
            emit(context,
                 (PCSPK_Event){divisor(hz), true, true, freq,
                               delayClocks(PCSPK_CALIBRATION >> 4, freq) + (last ? 12 : 31)});
            if (last)
                break;
        }
    } else if (kind == PCSPK_PULSE) {
        uint16_t threshold = (uint16_t)d, accumulator = 0;
        unsigned iterations = (uint16_t)c ? (uint16_t)c : 65536u;
        for (unsigned i = 0; i < iterations; i++) {
            accumulator = (uint16_t)(accumulator + freq);
            bool gate = accumulator > threshold;
            unsigned overhead = (i + 1 == iterations) ? (gate ? 12 : 13) : (gate ? 18 : 19);
            /* The original divides its calibration by 24 here, whereas
             * noise shifts by four and tone uses the full delay loop.
             * IN/OUT 61h also consume an I/O delay in the DOSBox reference. */
            emit(context,
                 (PCSPK_Event){60, gate, i == 0, (uint16_t)b,
                               delayClocks(PCSPK_CALIBRATION / 24, (uint16_t)b) + overhead + 8});
            threshold = (uint16_t)(threshold + (uint16_t)e);
        }
    } else if (kind == PCSPK_SWEEP && (uint16_t)c && signedWord((uint16_t)d) > 0 && freq) {
        int delta = signedWord((uint16_t)((uint16_t)b - freq));
        int product = signedWord((uint16_t)(delta * (int)(uint16_t)c));
        int step = product / signedWord((uint16_t)d);
        uint16_t elapsed = 0;
        for (unsigned i = 0; i < 65536; i++) {
            uint16_t after = (uint16_t)(elapsed + (uint16_t)c);
            int last = signedWord(after) >= signedWord((uint16_t)d);
            emit(context,
                 (PCSPK_Event){divisor(freq), freq != 0, true, (uint16_t)c,
                               delayClocks(PCSPK_CALIBRATION, (uint16_t)c) + (last ? 35 : 48)});
            elapsed = after;
            freq = (uint16_t)(freq + step);
            if (last)
                break;
        }
    }
    emit(context, (PCSPK_Event){0, false, false, 0, 0});
}
void PCSPK_Init(PCSPK_State *state, uint16_t noise) {
    memset(state, 0, sizeof(*state));
    state->noise = noise;
    state->count = state->pending = 65536;
}
static void measure(void *ctx, PCSPK_Event e) { *(uint64_t *)ctx += e.clocks; }
uint64_t PCSPK_Clocks(int kind, int a, int b, int c, int d, int e) {
    uint64_t clocks = 0;
    uint16_t seed = PCSPK_NOISE_SEED;
    PCSPK_Sequence(kind, a, b, c, d, e, &seed, measure, &clocks);
    return clocks;
}
/* Windowed-sinc decimation, rather than point sampling a 19.9 kHz carrier
 * whose gates can change faster than the output sample rate. No pitch clamp. */
static void coefficients(double *out) {
    double sum = 0;
    const double pi = 3.14159265358979323846, cutoff = 0.055;
    for (unsigned i = 0; i < PCSPK_FILTER_TAPS; i++) {
        double n = (double)i - (PCSPK_FILTER_TAPS - 1) / 2;
        double sinc = n ? sin(2 * pi * cutoff * n) / (pi * n) : 2 * cutoff;
        double window = 0.42 - 0.5 * cos(2 * pi * i / (PCSPK_FILTER_TAPS - 1)) +
                        0.08 * cos(4 * pi * i / (PCSPK_FILTER_TAPS - 1));
        out[i] = sinc * window;
        sum += out[i];
    }
    for (unsigned i = 0; i < PCSPK_FILTER_TAPS; i++)
        out[i] /= sum;
}
typedef struct {
    PCSPK_State *state;
    float *pcm;
    size_t position, capacity;
    double phase;
    bool gate;
    const double *filter;
} Renderer;
static void renderEvent(void *ctx, PCSPK_Event e) {
    Renderer *out = ctx;
    PCSPK_State *s = out->state;
    /* In mode 3 a count write is pending until the current half-cycle
     * ends. Reloading immediately on EVERY noise frequency silences its
     * low phases. A GATE rising edge loads the pending count immediately. */
    if (e.reload)
        s->pending = e.divisor ? e.divisor : 65536u;
    if (e.gate && !out->gate) {
        s->count = s->pending;
        out->phase = 0;
    }
    out->gate = e.gate;
    s->clocks += e.clocks;
    uint64_t end = (s->clocks * (PCSPK_RATE * PCSPK_OVERSAMPLE) + PCSPK_CLOCK - 1) / PCSPK_CLOCK;
    const double advance = 1193182.0 / (PCSPK_RATE * PCSPK_OVERSAMPLE);
    while (s->samples < end) {
        /* The physical pin is high/low, not a bipolar carrier multiplied
         * by a gate. Its average carries the audible pulse waveform. */
        float pin = e.gate && out->phase < (s->count + 1) / 2 ? 1.0f : 0.0f;
        s->history[s->filterPosition] = pin;
        s->filterPosition = (s->filterPosition + 1) % PCSPK_FILTER_TAPS;
        if (e.gate) {
            double remaining = advance;
            while (remaining > 0) {
                bool high = out->phase < (s->count + 1) / 2;
                double boundary = high ? (s->count + 1) / 2 : s->count;
                double left = boundary - out->phase;
                if (remaining < left) {
                    out->phase += remaining;
                    break;
                }
                remaining -= left;
                s->count = s->pending;
                out->phase = high ? (s->count + 1) / 2 : 0;
            }
        } else
            out->phase = 0;
        s->samples++;
        if (s->samples % PCSPK_OVERSAMPLE == 0 && out->position < out->capacity) {
            double value = 0;
            unsigned p = s->filterPosition;
            for (unsigned i = 0; i < PCSPK_FILTER_TAPS; i++) {
                p = p ? p - 1 : PCSPK_FILTER_TAPS - 1;
                value += out->filter[i] * s->history[p];
            }
            /* AC coupling removes the otherwise permanent DC offset. */
            double ac = value - s->previousInput + 0.9973854300793629 * s->previousOutput;
            s->previousInput = value;
            s->previousOutput = ac;
            out->pcm[out->position++] = (float)(ac > 1 ? 1 : ac < -1 ? -1 : ac);
        }
    }
}
static const double *filterCoefficients(void) {
    /* Rendering runs on the host/editor thread, never in the mixer callback. */
    static double filter[PCSPK_FILTER_TAPS];
    static bool ready;
    if (!ready) {
        coefficients(filter);
        ready = true;
    }
    return filter;
}
static float *render(PCSPK_State *s, uint64_t clocks, int kind, int a, int b, int c, int d, int e,
                     bool span, bool reload, size_t *frames) {
    *frames = 0;
    if (!clocks && !span)
        return NULL;
    uint64_t end =
        ((s->clocks + clocks) * (PCSPK_RATE * PCSPK_OVERSAMPLE) + PCSPK_CLOCK - 1) / PCSPK_CLOCK;
    uint64_t count = end / PCSPK_OVERSAMPLE - s->samples / PCSPK_OVERSAMPLE;
    if (count > PCSPK_RATE * 60u || count > INT_MAX / sizeof(float))
        return NULL;
    float *pcm = count ? SDL_calloc((size_t)count, sizeof(float)) : NULL;
    if (count && !pcm)
        return NULL;
    Renderer out = {s, pcm, 0, (size_t)count, s->phase, s->gate, filterCoefficients()};
    if (span)
        renderEvent(&out, (PCSPK_Event){divisor((uint16_t)a), a != 0, reload, 0, clocks});
    else
        PCSPK_Sequence(kind, a, b, c, d, e, &s->noise, renderEvent, &out);
    s->phase = out.phase;
    s->gate = out.gate;
    uint64_t seconds = s->clocks / PCSPK_CLOCK;
    s->clocks -= seconds * PCSPK_CLOCK;
    s->samples -= seconds * (PCSPK_RATE * PCSPK_OVERSAMPLE);
    *frames = out.position;
    return pcm;
}
float *PCSPK_RenderState(PCSPK_State *s, int kind, int a, int b, int c, int d, int e,
                         size_t *frames) {
    uint64_t clocks = 0;
    uint16_t seed = s->noise;
    PCSPK_Sequence(kind, a, b, c, d, e, &seed, measure, &clocks);
    return render(s, clocks, kind, a, b, c, d, e, false, false, frames);
}
float *PCSPK_RenderSpan(PCSPK_State *s, unsigned frequency, uint64_t clocks, bool reload,
                        size_t *frames) {
    return render(s, clocks, 0, frequency, 0, 0, 0, 0, true, reload, frames);
}
float *PCSPK_Render(int kind, int a, int b, int c, int d, int e, uint16_t *noise, size_t *frames) {
    PCSPK_State state;
    PCSPK_Init(&state, noise ? *noise : PCSPK_NOISE_SEED);
    float *pcm = PCSPK_RenderState(&state, kind, a, b, c, d, e, frames);
    if (noise)
        *noise = state.noise;
    return pcm;
}
