#undef NDEBUG
#include "audio/aud_mus.h"
#include "audio/audio.h"
#include "audio/mod_audio.h"
#include "common/common.h"
#include "mod/runtime.h"
#include "vars.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put(FILE *f, unsigned n) {
    for (int i = 0; i < 4; i++)
        assert(fputc(n >> (i * 8), f) != EOF);
}
int main(void) {
    unsigned char resource[24 + 32 + 4 + 140 + 4 * 16] = {0};
    memcpy(resource, "IAUDIO01", 8);
    MA_Put(resource + 8, 1);
    MA_Put(resource + 12, 4);
    unsigned char *c = resource + 24;
    MA_Put(c + 4, 18);
    MA_Put(c + 8, 1);
    MA_Put(c + 12, 75);
    MA_Put(c + 24, 4);
    MA_Put(c + 28, 140);
    memcpy(c + 32, "Test", 4);
    unsigned char *wav = c + 36;
    memcpy(wav, "RIFF", 4);
    MA_Put(wav + 4, 132);
    memcpy(wav + 8, "WAVEfmt ", 8);
    MA_Put(wav + 16, 16);
    wav[20] = wav[22] = 1;
    MA_Put(wav + 24, 48000);
    MA_Put(wav + 28, 96000);
    wav[32] = 2;
    wav[34] = 16;
    memcpy(wav + 36, "data", 4);
    MA_Put(wav + 40, 96);
    for (int i = 44; i < 140; i += 2) {
        wav[i] = 255;
        wav[i + 1] = 63;
    }
    unsigned char *r = wav + 140;
    unsigned routes[][4] = {{1, 1, 0, 18}, {2, 4, 0, 18}, {3, 1, 0, 18}, {4, 94, 0, 68}};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            MA_Put(r + i * 16 + j * 4, routes[i][j]);
    MA_Put(resource + 16, MOD_Crc32(resource + 24, sizeof(resource) - 24));
    assert(MA_Validate(resource, sizeof(resource)));
    assert(SDL_CreateDirectory("Mods"));
    FILE *f = fopen("Mods/audio.imperamod", "wb");
    assert(f);
    fwrite("IMOD0030", 1, 8, f);
    put(f, 5);
    put(f, 1);
    fwrite("Audio", 1, 5, f);
    put(f, 9);
    fwrite("AUDIO.OVR", 1, 9, f);
    put(f, 0);
    put(f, sizeof(resource));
    put(f, 0);
    put(f, MOD_Crc32(resource, sizeof(resource)));
    put(f, 1);
    put(f, 0);
    put(f, sizeof(resource));
    fwrite(resource, 1, sizeof(resource), f);
    fclose(f);
    assert(!SDL_setenv_unsafe("U5D_MODS_DIR", "Mods", 1));
    MOD_MountDirectory(".");
    assert(MOD_LoadedCount() == 1);
    assert(AUDIO_MOD_HasMusic());
    assert(AUDIO_MOD_Assignment(1, 1, 0, 9) == 18);
    assert(AUDIO_MOD_Assignment(1, 2, 0, 9) == 9);
    assert(AUDIO_MOD_Clip(0, 18)->gain == 75);
    assert(SDL_Init(SDL_INIT_AUDIO));
    AUDIO_Initialize();
    D_a9ce = 1;
    AUDIO_PlayBgm(1);
    AUDIO_PlayEventMusic(4, 4);
    D_5893_map_id = 1;
    AUDIO_PlayBgmPerMap();
    assert(AUDIO_HasSfx(94));
    AUDIO_PlaySfx(94);
    AUDIO_StopSfx();
    AUDIO_StopBgm();
    AUDIO_Cleanup();
    SDL_Quit();
    remove("Mods/audio.imperamod");
    MOD_MountDirectory(".");
    assert(!AUDIO_MOD_HasMusic());
    assert(!AUDIO_MOD_Clip(0, 18));
    return 0;
}
