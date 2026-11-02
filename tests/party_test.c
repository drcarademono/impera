#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "common/common.h"
#include "common/file.h"
#include "mod/party.h"
#include "mod/types.h"
#include "mod/package.h"
#include "mod/runtime.h"
#include "mod/actors.h"
#include "text/dialogue_game.h"
#include "savegame.h"
#include "vars.h"
#include "macros.h"
#include "funcs.h"
#include "graphics/grap.h"
#include "graphics/grap_buf.h"
static void put(FILE *f,unsigned n) {for(int i=0;i<4;i++) assert(fputc(n>>(i*8),f)!=EOF);}
static void entry(FILE *f,const char *name,const void *data,size_t n) {
    put(f,strlen(name));assert(fwrite(name,1,strlen(name),f)==strlen(name));
    put(f,0);put(f,n);put(f,0);put(f,MOD_Crc32(data,n));put(f,1);put(f,0);put(f,n);
    assert(fwrite(data,1,n,f)==n);
}
static void print(void *h,uint16_t c) {(void)h;(void)c;}
static void string(void *h,const char *s) {(void)h;(void)s;}
static int column(void *h) {(void)h;return 0;}
static int key(void *h) {(void)h;return ' ';}
int main(void) {
    unsigned char config[32+20*36]={0};memcpy(config,"IPARTY01",8);
    TYPES_Put32(config+8,20);TYPES_Put32(config+12,8);
    for(int i=0;i<20;i++) {
        unsigned char *r=config+32+i*36;
        snprintf((char *)r,9,"Buddy%02d",i);r[9]=11;r[10]='F';r[11]='G';
        r[12]=r[13]=r[14]=20;r[16]=r[18]=100;r[22]=1;
        for(int e=25;e<31;e++) r[e]=255;
        if(i<16) memcpy(D_55a8_party+i,r,32);
    }
    TYPES_Put32(config+32+17*36+32,60000);
    TYPES_Put32(config+16,MOD_Crc32(config+32,sizeof(config)-32));
    assert(PARTY_Validate(config,sizeof(config)));
    config[12]=9;assert(!PARTY_Validate(config,sizeof(config)));config[12]=8;
    config[20]=1;assert(!PARTY_Validate(config,sizeof(config)));config[20]=0;
    unsigned char types[32+256+4+32*32*4+4+16*16*4]={0};memcpy(types,"ITYPES01",8);types[8]=23;TYPES_Put32(types+24,1);
    TypeDef actor;TYPES_Vanilla(1,80,&actor);actor.id=60000;actor.width=actor.height=2;actor.frames=2;
    strcpy(actor.name,"Custom companion");TYPES_Encode(types+32,&actor);TYPES_Put32(types+32,sizeof(types)-32);
    types[32+257]=16;types[32+258]=100;
    unsigned world=32+256+4+32*32*4;types[world]=6;types[world+1]=16;types[world+2]=100;
    assert(TYPES_Validate(types,sizeof(types)));
    assert(SDL_CreateDirectory("Mods"));assert(SDL_CreateDirectory("SAVEGAME"));
    FILE *f=fopen("Mods/party.imperamod","wb");assert(f);
    assert(fwrite("IMOD0029",1,8,f)==8);put(f,5);put(f,2);assert(fwrite("Party",1,5,f)==5);
    entry(f,"PARTY.OVR",config,sizeof(config));entry(f,"TYPES.OVR",types,sizeof(types));fclose(f);
    assert(!SDL_setenv_unsafe("U5D_MODS_DIR","Mods",1));MOD_MountDirectory(".");
    assert(MOD_LoadedCount()==1 && PARTY_Configure());PARTY_InitExtras();
    assert(PARTY_RosterCount()==20 && PARTY_MaxActive()==8);
    assert(!strcmp(D_55a8_party[17].name,"Buddy17") && PARTY_CombatType(17)==60000);
    /* Joining matches the whole name despite many companions sharing a prefix. */
    uint8_t dialogue[32]={0};const char *name="buddy17";
    for(unsigned i=0;i<strlen(name);i++) dialogue[i]=name[i]|128;
    ImperaTalk talk={0};ImperaTalkStorage storage;IMPERA_TalkBind(&talk,&storage,0);
    talk.data=dialogue;talk.size=sizeof(dialogue);talk.print=print;talk.string=string;talk.column=column;talk.getChar=key;
    *talk.cursor=dialogue;
    ImperaDialogueGame game={0};D_585b=7;game.party=(uint8_t *)D_55a8_party;game.partyCount=&D_585b;
    game.rosterCount=20;game.maxParty=8;game.npc=-1;
    assert(IMPERA_GameJoin(&game,&talk)==1 && D_585b==8);
    assert(!strcmp(D_55a8_party[7].name,"Buddy17") && PARTY_CombatType(7)==60000);
    D_5893_map_id=255;D_ba14[7].flags=COMBAT_FLAGS_PLAYER;D_ba14[7].entityIdx=7;D_ba14[7].actorIdx=7;
    D_5c5a[7]._0_tile=D_5c5a[7]._1_animTile=80;
    assert(ACTORS_Instance(7)==60000 && ACTORS_Sprite(7,336)>=512);
    assert(SDL_Init(SDL_INIT_VIDEO));GRAP_Initialize();
    static byte font[2048];memset(font,255,sizeof(font));for(int n=0;n<4;n++) D_539c[n]=font;
    D_5398_currentCharset=font;D_538e=1;
    ULTIMA_1c22_SetTextWindowSize(0,0,0,39,24);ULTIMA_1b94_SelectTextWindow(0);
    ULTIMA_637e_DrawFrame();
    assert(D_535e_textWindows[1].bottom==11 && D_535e_textWindows[2].top==13);
    D_58a4=0;D_58a1=4;D_587b=D_589e=255;
    memset(D_ad14,4,sizeof(D_ad14));
    for(int n=0;n<6;n++) {D_1724[n]=3+n%3;D_172c[n]=4+n/3;}
    ULTIMA_6936();unsigned players=0;
    for(int n=0;n<32;n++) if(D_ba14[n].flags & COMBAT_FLAGS_PLAYER) {
        players++;unsigned member=D_ba14[n].entityIdx,art=PARTY_CombatType(member),a=D_ba14[n].actorIdx;
        if(!art) art=(D_5c5a[a]._0_tile-64)/4;
        assert(ACTORS_CanPlace(art,D_ba14[n].x,D_ba14[n].y,a));
    }
    assert(players==8);
    int monster=ULTIMA_6506(16,0,0,0,0);assert(monster>=8);
    ULTIMA_2900_UpdateVitalsDisplay();GRAP_Cleanup();SDL_Quit();
    D_5893_map_id=0;
    assert(FILE_WriteSavegameFile("SAVED.GAM")==0);
    D_585b=1;memset(D_55a8_party+16,0,4*32);
    assert(FILE_ReadSavegameFile("SAVED.GAM")==0 && D_585b==8 && PARTY_RosterCount()==20);
    assert(!strcmp(D_55a8_party[7].name,"Buddy17") && !strcmp(D_55a8_party[17].name,"Buddy07"));
    assert(PARTY_CombatType(7)==60000);
    /* A full eight-member party rejects another Join without changing the roster. */
    assert(IMPERA_GameJoin(&game,&talk)==0 && D_585b==8);
    D_585b=7;name="Buddy01";
    for(unsigned i=0;i<strlen(name);i++) dialogue[i]=name[i]|128;
    assert(IMPERA_GameJoin(&game,&talk)==1 && D_585b==7);D_585b=8;
    remove("Mods/party.imperamod");MOD_MountDirectory(".");assert(PARTY_Configure());
    assert(PARTY_MaxActive()==6);PARTY_InitExtras();assert(PARTY_RosterCount()==16);
    remove("SAVEGAME/SAVED.GAM");remove("Mods/mods.cfg");
    SDL_unsetenv_unsafe("U5D_MODS_DIR");
    puts("Extended companions, custom combat art, eight-member recruitment and saves passed.");
}
