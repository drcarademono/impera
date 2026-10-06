#ifndef U5D_PCSPEAKER_H
#define U5D_PCSPEAKER_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
enum { PCSPK_PULSE, PCSPK_NOISE, PCSPK_TONE, PCSPK_SWEEP };
/* One calibrated delay unit maps to 50 microseconds in SDL. */
#define PCSPK_DELAY_US 50
/* A zero PIT register represents a count of 65536. */
typedef struct {
    uint16_t divisor;
    bool gate, reload;
    uint32_t delay;
} PCSPK_Event;
typedef void (*PCSPK_Emit)(void* context, PCSPK_Event event);
void PCSPK_Sequence(int kind, int a,int b,int c,int d,int e,
                    uint16_t* noiseState, PCSPK_Emit emit, void* context);
/* Parameters retain their original units: frequencies are Hz, PWM adds are
 * 16-bit words. Output is mono float PCM at 48 kHz. Caller owns SDL_free(). */
float* PCSPK_Render(int kind,int a,int b,int c,int d,int e,
                    uint16_t* noiseState,size_t* frames);
#endif
