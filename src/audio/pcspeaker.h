#ifndef U5D_PCSPEAKER_H
#define U5D_PCSPEAKER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { PCSPK_PULSE, PCSPK_NOISE, PCSPK_TONE, PCSPK_SWEEP };
/* Stable slower-machine reference: DOSBox simple/386_slow, 1000 cycles.
 * Original calibration is 438, below its 16-bit counter overflow. The old
 * 5000-cycle profile wrapped that counter and made effects much too short.
 * DOSBox instruction budgets are not physical PC clock frequencies. */
#define PCSPK_CLOCK 1000000u
#define PCSPK_CALIBRATION 438u
#define PCSPK_NOISE_SEED 0x7664u
#define PCSPK_RATE 48000u
#define PCSPK_OVERSAMPLE 8u
#define PCSPK_FILTER_TAPS 97u
typedef struct {
    uint16_t divisor;
    bool gate, reload;
    uint32_t delay;  /* Original delay argument, retained for register traces. */
    uint64_t clocks; /* Time until the next register change. */
} PCSPK_Event;
typedef void (*PCSPK_Emit)(void *context, PCSPK_Event event);
typedef struct {
    uint16_t noise;
    uint64_t clocks, samples;
    unsigned filterPosition, count, pending;
    float history[PCSPK_FILTER_TAPS];
    double previousInput, previousOutput, phase;
    bool gate;
} PCSPK_State;
void PCSPK_Init(PCSPK_State *, uint16_t noise);
uint64_t PCSPK_Clocks(int kind, int a, int b, int c, int d, int e);
void PCSPK_Sequence(int kind, int a, int b, int c, int d, int e, uint16_t *noiseState,
                    PCSPK_Emit emit, void *context);
/* Shared runtime / Workshop renderer. The state preserves resampling, speaker
 * filtering and the independent DOS noise generator across consecutive calls. */
float *PCSPK_RenderSpan(PCSPK_State *, unsigned frequency, uint64_t clocks, bool reload,
                        size_t *frames);
float *PCSPK_RenderState(PCSPK_State *, int kind, int a, int b, int c, int d, int e,
                         size_t *frames);
/* Standalone clip convenience API. Caller owns SDL_free(). */
float *PCSPK_Render(int kind, int a, int b, int c, int d, int e, uint16_t *noiseState,
                    size_t *frames);
#endif
