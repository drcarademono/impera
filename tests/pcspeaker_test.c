#undef NDEBUG
#include "audio/pcspeaker.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

static PCSPK_Event events[64];
static int count;
static void collect(void *unused, PCSPK_Event event) {
    (void)unused;
    assert(count < 64);
    events[count++] = event;
}
static void run(int kind, int a, int b, int c, int d, int e, uint16_t *state) {
    count = 0;
    PCSPK_Sequence(kind, a, b, c, d, e, state, collect, NULL);
}
int main(void) {
    uint16_t state = 0xfff0;
    run(PCSPK_TONE, 1000, 7, 0, 0, 0, &state);
    assert(count == 2 && events[0].divisor == 1193 && events[0].delay == 7);
    assert(events[0].gate && events[0].reload && !events[1].gate);
    /* Pulse accumulator and threshold both wrap, including negative inc. */
    run(PCSPK_PULSE, 40000, 3, 4, 50000, -30000, &state);
    const bool gates[] = {false, false, false, true};
    for (int i = 0; i < 4; i++) {
        assert(events[i].divisor == 60 && events[i].delay == 3);
        assert(events[i].gate == gates[i] && events[i].reload == (i == 0));
    }
    assert(count == 5 && !events[4].gate);
    run(PCSPK_PULSE, 1000, 1, 1, 1000, 0, &state);
    assert(!events[0].gate); /* equality is OFF, not ON */
    run(PCSPK_NOISE, 3, 7, 1000, 0, 0, &state);
    assert(count == 4 && state == 0x5614);
    const uint16_t divisors[] = {2571, 1261, 2330};
    for (int i = 0; i < 3; i++) {
        assert(events[i].divisor == divisors[i]);
        assert(events[i].delay == 3 && events[i].gate && events[i].reload);
    }
    run(PCSPK_NOISE, 3, 3, 1000, 0, 0, &state);
    assert(count == 2 && state == 0x0f54 && events[0].divisor == 2840); /* state persists */
    /* 1000 -> 200, step=(-800*5)/300=-13, never a float sweep. */
    run(PCSPK_SWEEP, 1000, 200, 5, 300, 0, &state);
    assert(count == 61);
    for (int i = 0; i < 60; i++) {
        assert(events[i].divisor == (uint16_t)(1193182u / (1000 - i * 13)));
        assert(events[i].delay == 5 && events[i].reload);
    }
    run(PCSPK_SWEEP, 1000, 2000, 3, 10, 0, &state);
    assert(count == 5); /* final delay is full tickStep, not a shortened tail */
    assert(events[3].divisor == 1193182u / 1900 && events[3].delay == 3);
    /* Signed 16-bit multiplication wraps: 1000*40=-25536, /100=-255. */
    run(PCSPK_SWEEP, 1000, 2000, 40, 100, 0, &state);
    assert(events[1].divisor == 1193182u / 745 && events[2].divisor == 1193182u / 490);
    /* Measured DOSBox reference: tone uses the full calibrated delay,
     * noise shifts it by four, PWM divides it by 24. */
    PCSPK_State renderer;
    PCSPK_Init(&renderer, PCSPK_NOISE_SEED);
    size_t frames;
    float *pcm = PCSPK_RenderState(&renderer, PCSPK_TONE, 1000, 200, 0, 0, 0, &frames);
    assert(pcm && frames >= 2390 && frames <= 2410);
    SDL_free(pcm);
    PCSPK_Init(&renderer, PCSPK_NOISE_SEED);
    pcm = PCSPK_RenderState(&renderer, PCSPK_NOISE, 1, 25, 1000, 0, 0, &frames);
    assert(pcm && frames >= 24 && frames <= 30);
    SDL_free(pcm);
    /* DOS's independent initial noise word is 0x7664. Its first PIT
     * writes are 0830,1132,04d3,099c,0578 (verified in ULTIMA.EXE). */
    state = PCSPK_NOISE_SEED;
    run(PCSPK_NOISE, 1, 5, 1000, 0, 0, &state);
    const uint16_t originalNoise[] = {0x0830, 0x1132, 0x04d3, 0x099c, 0x0578};
    for (int i = 0; i < 5; i++)
        assert(events[i].divisor == originalNoise[i]);
    PCSPK_Init(&renderer, PCSPK_NOISE_SEED);
    pcm = PCSPK_RenderState(&renderer, PCSPK_PULSE, 3116, 1, 2000, 20000, -10, &frames);
    assert(pcm && frames > 1300 && frames < 1600);
    double best = 0, oldBand = 0;
    int peak = 0;
    for (int hz = 800; hz <= 3500; hz += 10) {
        double re = 0, im = 0;
        for (size_t i = 0; i < frames; i++) {
            double angle = 6.283185307179586 * hz * i / 48000.0;
            re += pcm[i] * cos(angle);
            im += pcm[i] * sin(angle);
        }
        double power = re * re + im * im;
        if (hz <= 1100 && power > oldBand)
            oldBand = power;
        if (hz >= 2500 && power > best) {
            best = power;
            peak = hz;
        }
    }
    assert(peak >= 2850 && peak <= 3150 && best > oldBand * 10);
    SDL_free(pcm);
    /* Consecutive small calls retain fractional samples and filter state. */
    PCSPK_Init(&renderer, PCSPK_NOISE_SEED);
    size_t total = 0;
    for (int i = 0; i < 100; i++) {
        pcm = PCSPK_RenderState(&renderer, PCSPK_TONE, 1000, 3, 0, 0, 0, &frames);
        assert(pcm);
        total += frames;
        SDL_free(pcm);
    }
    assert(total >= 3600 && total <= 3650);
    /* Mode 3 defers a new count until the current half-cycle ends. */
    PCSPK_Init(&renderer, PCSPK_NOISE_SEED);
    renderer.count = renderer.pending = 1000;
    renderer.phase = 100;
    renderer.gate = true;
    pcm = PCSPK_RenderSpan(&renderer, 2000, 100, true, &frames);
    SDL_free(pcm);
    assert(renderer.count == 1000 && renderer.pending == 596 && renderer.phase > 100 &&
           renderer.phase < 130);
    pcm = PCSPK_RenderSpan(&renderer, 2000, 2000, false, &frames);
    SDL_free(pcm);
    assert(renderer.count == 596);
    pcm = PCSPK_RenderSpan(&renderer, 0, 0, false, &frames);
    SDL_free(pcm);
    pcm = PCSPK_RenderSpan(&renderer, 500, 100, true, &frames);
    SDL_free(pcm);
    assert(renderer.count == 2386 && renderer.phase < 30); /* GATE restarts pending count. */
    puts("DOS speaker event sequences passed");
    return 0;
}
