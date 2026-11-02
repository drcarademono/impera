#include "common/common.h"
#include "party.h"
#include "runtime.h"
#include "package.h"
#include "vars.h"
#include <stdlib.h>
#include <string.h>
static unsigned count=16, configCount, maxActive=6;
static unsigned char config[IMPERA_PARTY_CAPACITY*PARTY_RECORD_SIZE];
static unsigned get32(const unsigned char *p) {return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static void put32(unsigned char *p,unsigned v) {for(int i=0;i<4;i++) p[i]=v>>(8*i);}
unsigned PARTY_RosterCount(void) {return count;}
unsigned PARTY_MaxActive(void) {return maxActive;}
int PARTY_Configure(void) {
    configCount=0;maxActive=6;
    int found=0;FILE *f=MOD_OpenResource("PARTY.OVR",&found);
    if(!f) return !found;
    unsigned char data[32+sizeof(config)];
    size_t n=fread(data,1,sizeof(data),f);int extra=fgetc(f);fclose(f);
    if(extra!=EOF || !PARTY_Validate(data,n)) return 0;
    configCount=PARTY_Count(data,n);maxActive=get32(data+12);memcpy(config,data+32,n-32);return 1;
}
void PARTY_InitExtras(void) {
    memset(D_55a8_party+16,0,(IMPERA_PARTY_CAPACITY-16)*32);
    count=configCount?configCount:16;
    for(unsigned i=16;i<count;i++) memcpy(D_55a8_party+i,config+i*PARTY_RECORD_SIZE,32);
}
unsigned PARTY_CombatType(unsigned member) {
    if(member>=count || !configCount) return 0;
    if(!member) return get32(config+32); /* Character creation can rename the Avatar. */
    for(unsigned i=1;i<configCount;i++)
        if(PARTY_NameEqual(D_55a8_party[member].name,(const char *)config+i*PARTY_RECORD_SIZE))
            return get32(config+i*PARTY_RECORD_SIZE+32);
    return 0;
}
int PARTY_ReadState(FILE *f,int apply) {
    long start=ftell(f);unsigned char h[24];size_t n=fread(h,1,24,f);
    if(n<8 || memcmp(h,"IPRSAV01",8)) {
        if(ferror(f) || fseek(f,start,SEEK_SET)) return 0;
        if(apply) PARTY_InitExtras();
        return 1;
    }
    if(n!=24) return 0;
    unsigned total=get32(h+8);
    if( get32(h+12)<1 || get32(h+12)>8 || get32(h+20) || total<16 || total>IMPERA_PARTY_CAPACITY) return 0;
    unsigned char records[(IMPERA_PARTY_CAPACITY-16)*32];
    size_t bytes=(total-16)*32;
    if(fread(records,1,bytes,f)!=bytes || MOD_Crc32(records,bytes)!=get32(h+16)) return 0;
    for(unsigned i=0;i<total-16;i++) if(!memchr(records+i*32,0,9)) return 0;
    if(apply) {PARTY_InitExtras();memcpy(D_55a8_party+16,records,bytes);count=total;if(!configCount) maxActive=get32(h+12);}
    return 1;
}
int PARTY_WriteState(FILE *f) {
    if(count==16 && !configCount && maxActive==6) return 1;
    unsigned char h[24]="IPRSAV01";
    size_t bytes=(count-16)*32;put32(h+8,count);put32(h+12,PARTY_MaxActive());put32(h+16,MOD_Crc32(D_55a8_party+16,bytes));
    return fwrite(h,1,24,f)==24 && fwrite(D_55a8_party+16,1,bytes,f)==bytes;
}
