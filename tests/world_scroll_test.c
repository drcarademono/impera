#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common/common.h"
#include "vars.h"
#include "outsubs.h"
#include "mainout.h"

static int loadX,loadY,loads,visibility;
void __wrap_OUTSUBS_01b4(int dx,int dy) {loadX=dx;loadY=dy;loads++;}
void __wrap_ULTIMA_5e4a(void) {visibility++;}

static void testShift(int dx,int dy)
{
    byte original[1024];
    for(int block=0;block<4;block++)
        for(int i=0;i<256;i++) original[block*256+i]=(byte)(block*43+i);
    memcpy(D_6608,original,sizeof(original));
    OUTSUBS_02c8(dx,dy);
    for(int y=0;y<2;y++) for(int x=0;x<2;x++) {
        int sx=x+dx,sy=y+dy;
        int source=sx>=0 && sx<2 && sy>=0 && sy<2?sx+sy*2:x+y*2;
        assert(memcmp(D_6608+(x+y*2)*256,original+source*256,256)==0);
    }
}

static void testMove(int x,int y,int dx,int dy,int shiftX,int shiftY)
{
    D_589b=D_589c=0x40;
    D_5896_map_x=(byte)(0x40+x);D_5897_map_y=(byte)(0x40+y);
    loads=visibility=0;
    MAINOUT_0354(dx,dy);
    assert(D_589b==((0x40+shiftX*16)&255));
    assert(D_589c==((0x40+shiftY*16)&255));
    assert(D_5896_map_x==0x40+x+dx && D_5897_map_y==0x40+y+dy);
    assert(loads==((shiftX||shiftY)?1:0) && visibility==loads);
    if(loads) assert(loadX==shiftX && loadY==shiftY);
}

int main(void)
{
    /* Northeast reproduces the original fortified memcpy overflow first. */
    testShift(1,-1);
    for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) testShift(dx,dy);
    for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
        if(!dx&&!dy) continue;
        testMove(dx<0?5:dx>0?26:16,dy<0?5:dy>0?26:16,dx,dy,dx,dy);
        testMove(16,16,dx,dy,0,0);
        if(dx&&dy) {
            testMove(dx<0?5:26,16,dx,dy,dx,0);
            testMove(16,dy<0?5:26,dx,dy,0,dy);
        }
    }
    D_589b=D_589c=0;
    D_5896_map_x=D_5897_map_y=5;
    MAINOUT_0354(-1,-1);
    assert(D_589b==240 && D_589c==240);
    puts("World block scrolling and diagonal boundary tests passed.");
    return 0;
}
