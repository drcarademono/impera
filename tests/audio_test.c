#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include "common/common.h"
#include "audio/audio.h"
#include "audio/aud_sfx.h"
#include "audio/pcspeaker.h"
#include "vars.h"

static int queued, assigned;
const char* __wrap_SDL_GetBasePath(void) { return "Alongside/"; }
bool __real_SDL_PutAudioStreamData(SDL_AudioStream*,const void*,int);
bool __wrap_SDL_PutAudioStreamData(SDL_AudioStream* stream,const void* data,int len)
{
    const float* pcm=data;
    bool audible=false;
    for (int i=0;i<len/(int)sizeof(float);i++) {
        assert(isfinite(pcm[i]) && fabsf(pcm[i])<=1);
        audible |= fabsf(pcm[i])>0.01f;
    }
    assert(audible);
    queued++;
    return __real_SDL_PutAudioStreamData(stream,data,len);
}
bool __real_MIX_SetTrackAudio(MIX_Track*,MIX_Audio*);
bool __wrap_MIX_SetTrackAudio(MIX_Track* track,MIX_Audio* audio)
{ assert(audio); assigned++; return __real_MIX_SetTrackAudio(track,audio); }

static void wave(const char* path)
{
    /* Small real WAV fixture; SDL detects the format from its contents. */
    const unsigned char header[]={
        'R','I','F','F',36+96,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',96,0,0,0};
    FILE* f=fopen(path,"wb"); assert(f);
    assert(fwrite(header,1,sizeof(header),f)==sizeof(header));
    for(int i=0;i<48;i++) { unsigned char sample[]={0,(unsigned char)(i%2?32:224)}; fwrite(sample,1,2,f); }
    fclose(f);
}

int main(void)
{
    size_t frames;
    uint16_t state=0;
    float* tone=PCSPK_Render(PCSPK_TONE,1000,100,0,0,0,&state,&frames);
    assert(tone && frames==240); /* 100 delay units * 50 us, frequency in Hz */
    int transitions=0;
    for(size_t i=1;i<frames;i++) if ((tone[i]>0)!=(tone[i-1]>0)) transitions++;
    assert(transitions==9);
    SDL_free(tone);
    assert(!PCSPK_Render(PCSPK_TONE,0,0,0,0,0,&state,&frames) && frames==0);
    D_a9ce=1;
    assert(SDL_Init(SDL_INIT_AUDIO));
    assert(SDL_CreateDirectory("Music")); assert(SDL_CreateDirectory("Sound"));
    wave("Music/02 - Britannic Lands.mp3");
    wave("Sound/step0.wav");
    AUDIO_Initialize();
    assert(AUDIO_HasSfx(73) && !AUDIO_HasSfx(74));
    AUDIO_PlayBgm(2); assert(assigned==1); /* descriptive numbered music filename */
    AUDIO_DispatchWhiteNoise(1,25,1000);
    assert(assigned==2 && queued==0); /* WAV takes precedence */
    AUDIO_DispatchWhiteNoise(1,25,1500);
    assert(queued==1); /* individual missing WAV synthesizes */
    AUDIO_DispatchWhiteNoise(2,20,3000); assert(queued==2); /* unmapped effects also synthesize */
    AUDIO_DispatchPulse(1193,1,100,20000,-4); assert(queued==3);
    AUDIO_DispatchTone(1193,100); assert(queued==4);
    AUDIO_DispatchSweepTone(1193,2386,1,40); assert(queued==5);
    AUDIO_PlayTitle1Sfx(); AUDIO_PlayTitle2Sfx(); assert(queued==5); /* No invented title definitions. */
    D_a9ce=0;
    AUDIO_DispatchTone(1000,100); assert(queued==5);
    D_a9ce=1;
    AUDIO_StopSfx(); AUDIO_StopBgm(); AUDIO_Cleanup();
    remove("Music/02 - Britannic Lands.mp3"); remove("Sound/step0.wav");
    assert(SDL_RemovePath("Music")); assert(SDL_RemovePath("Sound"));
    assert(SDL_CreateDirectory("Alongside/Music"));
    assert(SDL_CreateDirectory("Alongside/Sound"));
    wave("Alongside/Music/06 - Greyson's Tale.mp3");
    wave("Alongside/Sound/step1.wav");
    AUDIO_Initialize();
    assert(!AUDIO_HasSfx(73) && AUDIO_HasSfx(74));
    AUDIO_PlayBgm(6); assert(assigned==3);
    AUDIO_Cleanup();
    remove("Alongside/Music/06 - Greyson's Tale.mp3");
    remove("Alongside/Sound/step1.wav");
    SDL_Quit();
    puts("External music, WAV overrides, and speaker fallback passed");
    return 0;
}
