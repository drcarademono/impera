#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "audio/pcspeaker.h"

static PCSPK_Event events[64];
static int count;
static void collect(void* unused,PCSPK_Event event)
{ (void)unused; assert(count<64); events[count++]=event; }
static void run(int kind,int a,int b,int c,int d,int e,uint16_t* state)
{ count=0; PCSPK_Sequence(kind,a,b,c,d,e,state,collect,NULL); }
int main(void)
{
    uint16_t state=0xfff0;
    run(PCSPK_TONE,1000,7,0,0,0,&state);
    assert(count==2 && events[0].divisor==1193 && events[0].delay==7);
    assert(events[0].gate && events[0].reload && !events[1].gate);
    /* Pulse accumulator and threshold both wrap, including negative inc. */
    run(PCSPK_PULSE,40000,3,4,50000,-30000,&state);
    const bool gates[]={false,false,false,true};
    for(int i=0;i<4;i++) {
        assert(events[i].divisor==60 && events[i].delay==3);
        assert(events[i].gate==gates[i] && events[i].reload==(i==0));
    }
    assert(count==5 && !events[4].gate);
    run(PCSPK_PULSE,1000,1,1,1000,0,&state);
    assert(!events[0].gate); /* equality is OFF, not ON */
    run(PCSPK_NOISE,3,7,1000,0,0,&state);
    assert(count==4 && state==0x5614);
    const uint16_t divisors[]={2571,1261,2330};
    for(int i=0;i<3;i++) {
        assert(events[i].divisor==divisors[i]);
        assert(events[i].delay==3 && events[i].gate && events[i].reload);
    }
    run(PCSPK_NOISE,3,3,1000,0,0,&state);
    assert(count==2 && state==0x0f54 && events[0].divisor==2840); /* state persists */
    /* 1000 -> 200, step=(-800*5)/300=-13, never a float sweep. */
    run(PCSPK_SWEEP,1000,200,5,300,0,&state);
    assert(count==61);
    for(int i=0;i<60;i++) {
        assert(events[i].divisor==(uint16_t)(1193182u/(1000-i*13)));
        assert(events[i].delay==5 && events[i].reload);
    }
    run(PCSPK_SWEEP,1000,2000,3,10,0,&state);
    assert(count==5); /* final delay is full tickStep, not a shortened tail */
    assert(events[3].divisor==1193182u/1900 && events[3].delay==3);
    /* Signed 16-bit multiplication wraps: 1000*40=-25536, /100=-255. */
    run(PCSPK_SWEEP,1000,2000,40,100,0,&state);
    assert(events[1].divisor==1193182u/745 && events[2].divisor==1193182u/490);
    puts("DOS speaker event sequences passed");
    return 0;
}
