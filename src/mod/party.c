#include "party.h"
#include "package.h"
#include <string.h>
static unsigned word32(const unsigned char *p) {
    return p[0] | (unsigned)p[1]<<8 | (unsigned)p[2]<<16 | (unsigned)p[3]<<24;
}
int PARTY_NameEqual(const char *a,const char *b) {
    for(unsigned i=0;i<9;i++) {
        unsigned x=(unsigned char)a[i],y=(unsigned char)b[i];
        if(x>='a' && x<='z') x-=32;if(y>='a' && y<='z') y-=32;
        if(x!=y) return 0;if(!x) return 1;
    }
    return 0;
}
unsigned PARTY_Count(const void *data, size_t size) {
    const unsigned char *p=data;
    return p && size>=32 ? word32(p+8) : 0;
}
int PARTY_Validate(const void *data, size_t size) {
    const unsigned char *p=data;
    unsigned count=PARTY_Count(data,size);
    if(!p || size<32 || memcmp(p,"IPARTY01",8) || count<16 || count>IMPERA_PARTY_CAPACITY ||
       size!=32+(size_t)count*PARTY_RECORD_SIZE || MOD_Crc32(p+32,size-32)!=word32(p+16)) return 0;
    if(word32(p+12)<1 || word32(p+12)>8) return 0;
    for(unsigned i=20;i<32;i++) if(p[i]) return 0;
    for(unsigned i=0;i<count;i++) {
        const unsigned char *r=p+32+i*PARTY_RECORD_SIZE;
        if(!memchr(r,0,9)) return 0;
        for(unsigned n=0;n<9 && r[n];n++) if(r[n]<32 || r[n]>126) return 0;
        unsigned art=word32(r+32);
        if(art && (art<320 || art>65535)) return 0;
        if(r[0]) for(unsigned j=0;j<i;j++)
            if(PARTY_NameEqual((const char *)r,(const char *)p+32+j*PARTY_RECORD_SIZE)) return 0;
    }
    return 1;
}
