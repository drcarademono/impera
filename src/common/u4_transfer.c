#include "common.h"
#if defined(TARGET_SDL)
#include "u4_transfer.h"
#include "file_picker.h"
#include "file.h"
#include "engine_settings.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "key/mouse.h"
#include <SDL3/SDL.h>
#include <string.h>

bool U4_ReadTransfer(const char* path,void* character,void* virtues)
{
    byte data[0x1f6];
    FILE* f=FILE_Open(path,"rb");
    if(!f) { DEBUG_Error("Ultima IV transfer: cannot open %s",path);return false; }
    bool ok=fread(data,1,sizeof(data),f)==sizeof(data) && !ferror(f);
    fclose(f);
    if(ok) {
        const byte* c=data+8;
        for(int i=0;i<6;i++) {
            unsigned value=c[i*2]|((unsigned)c[i*2+1]<<8);
            if(value>(i<3?9999:70)) ok=false;
        }
        if(c[37]>7 || !c[20]) ok=false;
        for(int i=20;i<28 && c[i];i++) if(c[i]<32 || c[i]>126) ok=false;
    }
    if(!ok) { DEBUG_Error("Ultima IV transfer: truncated or invalid save %s",path);return false; }
    memcpy(character,data+8,40);memcpy(virtues,data+0x140,0xb6);
    debug("Ultima IV transfer: validated %s",path);return true;
}

typedef struct { void* character;void* virtues; } Transfer;
static bool acceptTransfer(const char* path,void* user)
{
    Transfer* transfer=user;
    return U4_ReadTransfer(path,transfer->character,transfer->virtues);
}
bool U4_SelectTransfer(void* character,void* virtues)
{
    Transfer transfer={character,virtues};
    return FILEPICKER_Select("Transfer from Ultima IV","Select your party.sav","party.sav",NULL,
                             NULL,"Return to Menu","Invalid or incomplete party.sav",acceptTransfer,&transfer);
}
#endif
