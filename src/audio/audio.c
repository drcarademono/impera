#include "common/common.h"

#include "audio.h"
#include "vars.h"

#include "aud_ops.h"

#if defined(TARGET_SDL)
extern AudioMusicDriverOps* AUDIO_SDL_GetMusicOps(void);
extern AudioSfxDriverOps* AUDIO_SDL_GetSfxOps(void);
#elif defined(TARGET_DOS32)
extern AudioMusicDriverOps* AUDIO_CDDA_GetMusicOps(void);
extern AudioSfxDriverOps* AUDIO_SPK_GetSfxOps(void);
extern AudioSfxDriverOps* AUDIO_ADLIB_GetSfxOps(void);
#endif

static AudioMusicDriverOps* s_musicOps;
static AudioSfxDriverOps* s_sfxOps;
static bool s_musicEnabled=true, s_soundEnabled=true;
static int s_requestedBgm, s_requestedQueue;
int AUDIO_MusicEnabled(void) { return s_musicEnabled; }
int AUDIO_SoundEnabled(void) { return s_soundEnabled; }
void AUDIO_SetMusicEnabled(int enabled)
{
    s_musicEnabled=enabled!=0;
    if (!s_musicOps) return;
    if (!s_musicEnabled) s_musicOps->StopBgm();
    else if(s_requestedBgm) {
        s_musicOps->PlayBgm(s_requestedBgm);
        if(s_requestedQueue) s_musicOps->QueueBgm(s_requestedQueue);
    }
}
void AUDIO_ReloadMusic(void)
{
#if defined(TARGET_SDL)
    extern void AUDIO_SDL_ReloadMusic(void);
    AUDIO_SDL_ReloadMusic();
    AUDIO_SetMusicEnabled(s_musicEnabled);
#endif
}
void AUDIO_SetSoundEnabled(int enabled)
{
    s_soundEnabled=enabled!=0;
    D_a9ce=s_soundEnabled;
    if (!s_soundEnabled && s_sfxOps) s_sfxOps->StopSfx();
}

void AUDIO_Initialize(void)
{
#if defined(TARGET_SDL)
    s_musicOps = AUDIO_SDL_GetMusicOps();
    s_sfxOps = AUDIO_SDL_GetSfxOps();
#elif defined(TARGET_DOS32)
    s_musicOps = AUDIO_CDDA_GetMusicOps();
    //s_sfxOps = AUDIO_SPK_GetSfxOps();
    s_sfxOps = AUDIO_ADLIB_GetSfxOps();
#endif

    if (s_musicOps)
        s_musicOps->Initialize();

    if (s_sfxOps)
        s_sfxOps->Initialize();
}

void AUDIO_Cleanup(void)
{
    if (s_musicOps)
        s_musicOps->Cleanup();

    if (s_sfxOps)
        s_sfxOps->Cleanup();
}

void AUDIO_PlaySfx(int id)
{
    if (!s_sfxOps || !s_soundEnabled)
        return;

    s_sfxOps->PlaySfx(id);
}

int AUDIO_HasSfx(int id)
{
#if defined(TARGET_SDL)
    extern bool AUDIO_SDL_HasSfx(int id);
    return AUDIO_SDL_HasSfx(id);
#else
    return s_sfxOps != NULL;
#endif
}

void AUDIO_PlayTitle1Sfx(void)
{
    if (!s_sfxOps || !s_soundEnabled)
        return;

    s_sfxOps->PlayTitle1Sfx();
}

void AUDIO_PlayTitle2Sfx(void)
{
    if (!s_sfxOps || !s_soundEnabled)
        return;

    s_sfxOps->PlayTitle2Sfx();
}

void AUDIO_StopSfx(void)
{
    if (!s_sfxOps)
        return;

    s_sfxOps->StopSfx();
}

int AUDIO_GetSfxType(void)
{
    if (!s_sfxOps || !s_soundEnabled)
        return SFX_TYPE_NONE;

    return s_sfxOps->GetSfxType();
}

void AUDIO_PlaySynthPulse(int freq, int delay, int dur, int pulseWidth, int pulseInc)
{
    if (!s_sfxOps || !s_soundEnabled)
        return;

    s_sfxOps->PlaySynthPulse(freq, delay, dur, pulseWidth, pulseInc);
}

void AUDIO_PlaySynthNoise(int rate, int dur, int limit)
{
    if (!s_sfxOps || !s_soundEnabled)
        return;

    s_sfxOps->PlaySynthNoise(rate, dur, limit);
}

void AUDIO_PlaySynthTone(int freq, int dur)
{
    if (!s_sfxOps || !s_soundEnabled)
        return;

    s_sfxOps->PlaySynthTone(freq, dur);
}

void AUDIO_PlaySynthSweepTone(int param_1, int param_2, int param_3, int param_4)
{
    if (!s_sfxOps || !s_soundEnabled)
        return;

    s_sfxOps->PlaySynthSweepTone(param_1, param_2, param_3, param_4);
}

void AUDIO_PlayBgm(int id)
{
    s_requestedBgm=id; s_requestedQueue=0;
    if (!s_musicOps || !s_musicEnabled)
        return;

    s_musicOps->PlayBgm(id);
}

void AUDIO_QueueBgm(int id)
{
    s_requestedQueue=id;
    if (!s_musicOps || !s_musicEnabled)
        return;

    s_musicOps->QueueBgm(id);
}

void AUDIO_StopBgm(void)
{
    s_requestedBgm=s_requestedQueue=0;
    if (!s_musicOps)
        return;

    s_musicOps->StopBgm();
}
