#include "pcspeaker.h"
#include <SDL3/SDL.h>
#include <limits.h>

static uint16_t divisor(uint16_t freq)
{
    /* Zero is an invalid frequency, not an invented replacement pitch. */
    return freq ? (uint16_t)(1193182u / freq) : 0;
}

static int signedWord(uint16_t word)
{ return word<0x8000u ? word : (int)word-0x10000; }

void PCSPK_Sequence(int kind,int a,int b,int c,int d,int e,
                    uint16_t* state,PCSPK_Emit emit,void* context)
{
    uint16_t freq=(uint16_t)a, duration=(uint16_t)b;
    if (kind==PCSPK_TONE) {
        emit(context,(PCSPK_Event){divisor(freq),freq!=0,true,duration});
    } else if (kind==PCSPK_NOISE && freq && state && (uint16_t)c>=100) {
        for (uint32_t elapsed=0;elapsed<duration;elapsed+=freq) {
            uint16_t next=(uint16_t)(*state+0x9248u);
            next=(uint16_t)((next>>3)|(next<<13));
            next^=0x9248u;
            *state=(uint16_t)(next+0x11u);
            uint16_t hz=(uint16_t)(100u+*state%((uint16_t)c-100u+1u));
            emit(context,(PCSPK_Event){divisor(hz),true,true,freq});
        }
    } else if (kind==PCSPK_PULSE) {
        uint16_t threshold=(uint16_t)d,accumulator=0;
        for(uint32_t i=0;i<(uint16_t)c;i++) {
            accumulator=(uint16_t)(accumulator+freq);
            emit(context,(PCSPK_Event){60,accumulator>threshold,i==0,(uint16_t)b});
            threshold=(uint16_t)(threshold+(uint16_t)e);
        }
    } else if (kind==PCSPK_SWEEP && (uint16_t)c && (uint16_t)d) {
        /* DOS int arithmetic: signed subtraction/product wrap at 16 bits;
         * signed division truncates toward zero. Frequency addition wraps. */
        int delta=signedWord((uint16_t)((uint16_t)b-freq));
        int product=signedWord((uint16_t)(delta*(int)(uint16_t)c));
        int step=product/signedWord((uint16_t)d);
        for(uint32_t elapsed=0;elapsed<(uint16_t)d;elapsed+=(uint16_t)c) {
            emit(context,(PCSPK_Event){divisor(freq),freq!=0,true,(uint16_t)c});
            freq=(uint16_t)(freq+step);
        }
    }
    emit(context,(PCSPK_Event){0,false,false,0}); /* PcspkOff */
}

typedef struct {
    float* pcm;
    uint64_t ticks,position,frames;
    double phase;
} Renderer;

static void renderEvent(void* context,PCSPK_Event event)
{
    Renderer* out=context;
    if (event.reload) out->phase=0;
    out->ticks+=event.delay;
    uint64_t end=(out->ticks*48000*PCSPK_DELAY_US+999999)/1000000;
    uint32_t count=event.divisor ? event.divisor : 65536;
    double advance=1193182.0/count/48000.0;
    while(out->position<end && out->position<out->frames) {
        /* Mode 3: odd counts spend one more PIT clock high than low. No
         * frequency clamps, envelope, or PWM duty substitution. */
        double high=(count+1)/2/(double)count;
        out->pcm[out->position++]=event.gate ? (out->phase<high?0.5f:-0.5f) : 0;
        out->phase+=advance;
        out->phase-=(uint32_t)out->phase;
    }
}

float* PCSPK_Render(int kind,int a,int b,int c,int d,int e,uint16_t* state,size_t* frames)
{
    *frames=0;
    uint64_t ticks=0;
    uint16_t rate=(uint16_t)a,duration=(uint16_t)b;
    if(kind==PCSPK_TONE) ticks=duration;
    else if(kind==PCSPK_NOISE && rate && (uint16_t)c>=100)
        ticks=((duration+rate-1u)/rate)*(uint64_t)rate;
    else if(kind==PCSPK_PULSE) ticks=(uint64_t)(uint16_t)c*(uint16_t)b;
    else if(kind==PCSPK_SWEEP && (uint16_t)c)
        ticks=(((uint16_t)d+(uint16_t)c-1u)/(uint16_t)c)*(uint64_t)(uint16_t)c;
    uint64_t count=(ticks*48000*PCSPK_DELAY_US+999999)/1000000;
    /* SDL's queued-data API takes an int byte count. Reject oversized buffers
     * rather than truncating the original sequence or stretching its timing. */
    if(!count || count>INT_MAX/sizeof(float)) return NULL;
    float* pcm=SDL_calloc((size_t)count,sizeof(float));
    if(!pcm) return NULL;
    Renderer out={pcm,0,0,count,0};
    PCSPK_Sequence(kind,a,b,c,d,e,state,renderEvent,&out);
    *frames=(size_t)count;
    return pcm;
}
