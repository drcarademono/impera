#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include "common/common.h"
#include "audio/audio.h"
#include "common/data_setup.h"
#include "common/file.h"
#include "audio/aud_sfx.h"
#include "audio/pcspeaker.h"
#include "audio/ambient_loop.h"
#include "vars.h"
#include "key/key.h"
#include "4000.h"
#include "graphics/grap_sdl.h"

static int queued, assigned, sleepCalls, sleepMs;
static size_t queuedFrames;
static int speedTracks;
static MIX_Track *effectTracks[2], *musicTrack;
static MIX_Track *lastAssigned;
static MIX_Audio *lastAudio;
static int plays;
bool __real_MIX_PlayTrack(MIX_Track*,SDL_PropertiesID);
bool __wrap_MIX_PlayTrack(MIX_Track *track,SDL_PropertiesID options) {
    plays++;return __real_MIX_PlayTrack(track,options);
}
bool __real_MIX_SetTrackFrequencyRatio(MIX_Track*,float);
bool __wrap_MIX_SetTrackFrequencyRatio(MIX_Track* track,float ratio) {
    assert(ratio==AUDIO_SoundSpeed());
    if(speedTracks<2)effectTracks[speedTracks]=track;
    speedTracks++;
    return __real_MIX_SetTrackFrequencyRatio(track,ratio);
}
void __real_TIME_SleepMs(int);
void __wrap_TIME_SleepMs(int ms) {sleepCalls++;sleepMs+=ms;__real_TIME_SleepMs(ms);}
const char* __wrap_SDL_GetBasePath(void) { return "Alongside/"; }
bool __real_SDL_PutAudioStreamData(SDL_AudioStream*,const void*,int);
bool __wrap_SDL_PutAudioStreamData(SDL_AudioStream* stream,const void* data,int len)
{
    /* Static SDL also routes mixer-internal writes through this wrapper.
     * Only the engine's mono float synthesis stream is under test here. */
    SDL_AudioSpec src,dst;
    assert(SDL_GetAudioStreamFormat(stream,&src,&dst));
    if(src.format!=SDL_AUDIO_F32 || src.channels!=1 || dst.channels!=1)
        return __real_SDL_PutAudioStreamData(stream,data,len);
    const float* pcm=data;
    bool audible=false;
    for (int i=0;i<len/(int)sizeof(float);i++) {
        assert(isfinite(pcm[i]) && fabsf(pcm[i])<=1);
        audible |= fabsf(pcm[i])>0.01f;
    }
    assert(audible || len<8*(int)sizeof(float)); /* A short raw span may contain only filter startup. */
    queuedFrames+=len/sizeof(float);
    queued++;
    return __real_SDL_PutAudioStreamData(stream,data,len);
}
bool __real_MIX_SetTrackAudio(MIX_Track*,MIX_Audio*);
bool __wrap_MIX_SetTrackAudio(MIX_Track* track,MIX_Audio* audio)
{ if(audio) {if(!assigned)musicTrack=track;assigned++;lastAssigned=track;lastAudio=audio;} return __real_MIX_SetTrackAudio(track,audio); }

static void waveFrames(const char* path,int frames)
{
    /* Small real WAV fixture; SDL detects the format from its contents. */
    unsigned char header[]={
        'R','I','F','F',36+96,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',96,0,0,0};
    for(int i=0;i<4;i++) {
        header[4+i]=(unsigned char)((36+frames*2)>>(8*i));
        header[40+i]=(unsigned char)((frames*2)>>(8*i));
    }
    FILE* f=fopen(path,"wb"); assert(f);
    assert(fwrite(header,1,sizeof(header),f)==sizeof(header));
    for(int i=0;i<frames;i++) { unsigned char sample[]={0,(unsigned char)(i%2?32:224)}; fwrite(sample,1,2,f); }
    fclose(f);
}

static void wave(const char* path) {waveFrames(path,48);}

int main(void)
{
    size_t frames;
    uint16_t state=0;
    float* tone=PCSPK_Render(PCSPK_TONE,1000,100,0,0,0,&state,&frames);
    assert(tone && frames>=4200 && frames<=4250); /* Slower-machine calibrated 88 ms, frequency in Hz */
    int transitions=0;
    for(size_t i=1;i<frames;i++) if ((tone[i]>0)!=(tone[i-1]>0)) transitions++;
    assert(transitions>=160 && transitions<=190); /* Band-limited square wave with AC coupling. */
    SDL_free(tone);
    assert(!PCSPK_Render(PCSPK_TONE,0,0,0,0,0,&state,&frames) && frames==0);
    D_a9ce=1;
    assert(SDL_Init(SDL_INIT_AUDIO));
    assert(SDL_CreateDirectory("Music")); assert(SDL_CreateDirectory("Sound"));
    wave("Music/02 - Britannic Lands.mp3");
    wave("Sound/step0.wav");
    AUDIO_Initialize();
    assert(AUDIO_HasSfx(73) && !AUDIO_HasSfx(74));
    assert(!AUDIO_UpdateAmbience(71)); /* Missing WAV keeps the native speaker path. */
    int beforeSpeedTracks=speedTracks;
    AUDIO_SetSoundSpeed(0.5f);
    assert(speedTracks==beforeSpeedTracks+8); /* Every effect/ambience/note voice; never music. */
    AUDIO_SetSoundSpeed(1);
    assert(!AUDIO_PlaySpeakerFootstep()); /* Keep recordings on the override path. */
    AUDIO_PlayBgm(2); assert(assigned==1);
    AUDIO_SetMusicVolume(0.37f);AUDIO_SetSoundVolume(0.23f);
    assert(fabsf(MIX_GetTrackGain(musicTrack)-0.3f*0.37f)<0.0001f);
    for(int i=0;i<2;i++)assert(fabsf(MIX_GetTrackGain(effectTracks[i])-0.2f*0.23f)<0.0001f);
    AUDIO_SetSoundVolume(0);
    for(int i=0;i<2;i++)assert(MIX_GetTrackGain(effectTracks[i])==0);
    assert(MIX_GetTrackGain(musicTrack)>0);
    AUDIO_SetMusicVolume(1);AUDIO_SetSoundVolume(1); /* descriptive numbered music filename */
    AUDIO_SetMusicEnabled(0);AUDIO_PlayBgm(2);assert(assigned==1);
    AUDIO_SetMusicEnabled(1);assert(assigned==2); /* resume requested music */
    AUDIO_DispatchWhiteNoise(1,25,1000);
    assert(assigned==3 && queued==0); /* WAV takes precedence */
    D_5420=0xabcd;
    AUDIO_DispatchWhiteNoise(1,25,1500);
    assert(D_5420==0xabcd); /* Speaker noise must never advance gameplay randomness. */
    assert(queued==1); /* individual missing WAV synthesizes */
    AUDIO_DispatchWhiteNoise(2,20,3000); assert(queued==2); /* unmapped effects also synthesize */
    AUDIO_DispatchPulse(1193,1,100,20000,-4); assert(queued==3);
    /* Audio delays pump the SDL event loop before dialogue can consume typing. */
    const char typed[] = "name\rjob\r";
    for (size_t i = 0; i < sizeof(typed) - 1; ++i) {
        SDL_Event key = {0};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.key = typed[i];
        assert(SDL_PushEvent(&key));
    }
    AUDIO_DispatchTone(1193,200); assert(queued==4);
    for (size_t i = 0; i < sizeof(typed) - 1; ++i)
        assert(KEY_PollKey() == typed[i]);
    assert(KEY_PollKey() == 0);
    AUDIO_DispatchSweepTone(1193,2386,1,40); assert(queued==5);
    AUDIO_PlayTitle1Sfx(); AUDIO_PlayTitle2Sfx(); assert(queued==5); /* No invented title definitions. */
    AUDIO_SetSoundEnabled(0);AUDIO_DispatchWhiteNoise(1,25,1000);assert(assigned==3);
    AUDIO_DispatchTone(1000,100);assert(queued==5);
    AUDIO_SetSoundEnabled(1);
    D_a9ce=0;
    AUDIO_DispatchTone(1000,100); assert(queued==5);
    D_a9ce=1;
    int beforeRaw=queued;AUDIO_SpeakerOn(1200);
    int speakerDelay=AUDIO_SpeakerDelay(20);assert(speakerDelay>=17 && speakerDelay<=19);
    AUDIO_SpeakerOff();assert(queued>beforeRaw);
    AUDIO_StopSfx(); AUDIO_StopBgm(); AUDIO_Cleanup();
    remove("Music/02 - Britannic Lands.mp3"); remove("Sound/step0.wav");
    assert(SDL_RemovePath("Music")); assert(SDL_RemovePath("Sound"));
    AUDIO_Initialize();
    size_t beforeFrames=queuedFrames;int beforeSleeps=sleepCalls;
    assert(AUDIO_FootstepsEnabled());
    AUDIO_SetFootstepsEnabled(0);
    assert(AUDIO_PlaySpeakerFootstep() && queuedFrames==beforeFrames);
    AUDIO_SetFootstepsEnabled(1);
    assert(AUDIO_PlaySpeakerFootstep());
    size_t stepFrames=queuedFrames-beforeFrames;
    assert(stepFrames>=1030 && stepFrames<=1070); /* ~22 ms, including the calibrated gap. */
    assert(sleepCalls==beforeSleeps); /* The audio gap must not pause the game thread. */
    /* Muting footsteps must preserve non-smooth movement pacing. */
    bool previousSmooth=GRAP_SDL_SmoothMovementEnabled();
    AUDIO_SetFootstepsEnabled(0);GRAP_SDL_SetSmoothMovement(false);
    size_t mutedFrames=queuedFrames;int mutedMs=sleepMs;
    ULTIMA_433e_AudioFootstep();
    assert(queuedFrames==mutedFrames && sleepMs>mutedMs);
    GRAP_SDL_SetSmoothMovement(true);mutedMs=sleepMs;
    ULTIMA_433e_AudioFootstep();
    assert(queuedFrames==mutedFrames && sleepMs==mutedMs);
    GRAP_SDL_SetSmoothMovement(previousSmooth);AUDIO_SetFootstepsEnabled(1);
    AUDIO_SetSoundSpeed(0.5f);
    int beforeMs=sleepMs;
    AUDIO_DispatchTone(1000,200);
    assert(sleepMs-beforeMs>=350 && sleepMs-beforeMs<=354); /* Twice the normal 176 ms. */
    AUDIO_SetSoundSpeed(1);
    AUDIO_Cleanup();
    assert(SDL_CreateDirectory("Alongside/Music"));
    assert(SDL_CreateDirectory("Alongside/Sound"));
    wave("Alongside/Music/06 - Greyson's Tale.mp3");
    wave("Alongside/Sound/step1.wav");
    AUDIO_Initialize();
    assert(!AUDIO_HasSfx(73) && AUDIO_HasSfx(74));
    AUDIO_PlayBgm(6); assert(assigned==4);
    assert(SDL_CreateDirectory("SelectedMusic"));
    wave("SelectedMusic/07 - Fanfare for the Virtuous.mp3");
    AUDIO_PlayBgm(7);assert(assigned==4); /* Not loaded until the new directory is selected. */
    assert(SETUP_SetMusicDirectory("SelectedMusic"));
    assert(!SETUP_SetMusicDirectory("missing-music-directory"));
    assert(!strcmp(SETUP_MusicDirectory(),"SelectedMusic"));
    AUDIO_ReloadMusic();assert(assigned==5); /* Requested track resumes without restart. */
    assert(AUDIO_HasSfx(74)); /* Sound effects are not reinitialized. */
    AUDIO_PlayBgm(6);assert(assigned==5); /* Old folder's tracks were released. */
    AUDIO_Cleanup();
    remove("SelectedMusic/07 - Fanfare for the Virtuous.mp3");
    assert(SDL_RemovePath("SelectedMusic"));remove("DATA.CFG");
    remove("Alongside/Music/06 - Greyson's Tale.mp3");
    remove("Alongside/Sound/step1.wav");
    assert(SDL_CreateDirectory("Sound"));
    const char *water=getenv("IMPERA_TEST_WATERFALL"), *fountain=getenv("IMPERA_TEST_FOUNTAIN");
    if(water && fountain) {
        size_t bytes;void *file=SDL_LoadFile(water,&bytes);assert(file);
        assert(SDL_SaveFile("Sound/watrfall.wav",file,bytes));SDL_free(file);
        file=SDL_LoadFile(fountain,&bytes);assert(file);
        assert(SDL_SaveFile("Sound/fountain.wav",file,bytes));SDL_free(file);
    } else {
        wave("Sound/watrfall.wav");wave("Sound/fountain.wav");
        /* Also exercise decoding/resampling of a 44.1 kHz stereo override. */
        FILE *stereo=fopen("Sound/fountain.wav","r+b");assert(stereo);
        const unsigned char format[]={2,0,0x44,0xac,0,0,0x10,0xb1,2,0,4,0};
        assert(!fseek(stereo,22,SEEK_SET));assert(fwrite(format,1,sizeof(format),stereo)==sizeof(format));
        assert(!fclose(stereo));
    }
    const char *accents[]={"chime","lute0","drip0","drip1","drip2"};
    for(int i=0;i<5;i++){char path[80];SDL_snprintf(path,sizeof(path),"Sound/%s.wav",accents[i]);waveFrames(path,48000);}
    AUDIO_Initialize();
    AUDIO_PlaySfx(1);MIX_Track *clock=lastAssigned;
    AUDIO_PlaySfx(71);assert(lastAssigned!=clock && MIX_TrackPlaying(clock));
    AUDIO_PlaySfx(30);MIX_Track *note=lastAssigned;
    assert(note!=clock && MIX_TrackPlaying(clock));
    AUDIO_PlaySfx(71);assert(lastAssigned!=note && lastAssigned!=clock && MIX_TrackPlaying(note));
    AUDIO_SetSoundVolume(0.4f);AUDIO_SetSoundSpeed(0.75f);
    assert(fabsf(MIX_GetTrackGain(note)-0.08f)<0.0001f && MIX_GetTrackFrequencyRatio(note)==0.75f);
    int notePlays=plays;AUDIO_PlaySfx(30);assert(plays==notePlays+1 && lastAssigned!=note); /* Retrigger actual notes. */
    AUDIO_SetSoundVolume(1);AUDIO_SetSoundSpeed(1);
    AUDIO_DispatchSweepTone(3200,3500,1,12);MIX_Audio *drip=lastAudio;
    AUDIO_PlaySfx(106);assert(lastAudio==drip);
    AUDIO_DispatchSweepTone(3200,3500,1,4);drip=lastAudio;
    AUDIO_PlaySfx(107);assert(lastAudio==drip); /* Select the proper recording variant. */
    assert(AUDIO_UpdateAmbience(71));
    MIX_Track *ambient=lastAssigned;MIX_Audio *loop=lastAudio;
    int beforePlays=plays,beforeAssigned=assigned;
    SDL_Delay(30);
    for(int i=0;i<50;i++)assert(AUDIO_UpdateAmbience(71));
    assert(plays==beforePlays && assigned==beforeAssigned); /* Repeated updates never restart. */
    assert(MIX_TrackPlaying(ambient));
    AUDIO_PlaySfx(71); /* The same recording is a one-shot outside ambient context. */
    assert(lastAssigned!=ambient && lastAudio!=loop && MIX_TrackPlaying(ambient));
    AUDIO_SetSoundVolume(0.37f);AUDIO_SetSoundSpeed(0.75f);
    assert(fabsf(MIX_GetTrackGain(ambient)-0.2f*0.37f)<0.0001f);
    assert(MIX_GetTrackFrequencyRatio(ambient)==0.75f);
    assert(AUDIO_UpdateAmbience(72));
    assert(lastAssigned!=ambient && MIX_TrackPlaying(lastAssigned));
    assert(MIX_TrackPlaying(ambient)); /* Old source remains during the crossfade. */
    SDL_Delay(120);assert(!MIX_TrackPlaying(ambient));
    ambient=lastAssigned;
    beforeAssigned=assigned;
    AUDIO_ReloadMusic();assert(MIX_TrackPlaying(ambient) && assigned==beforeAssigned);
    FILE_ReloadMods(); /* New mod revision must invalidate both raw and looped audio. */
    assert(AUDIO_UpdateAmbience(72) && assigned>beforeAssigned);
    ambient=lastAssigned;
    AUDIO_SetSoundEnabled(0);SDL_Delay(120);assert(!MIX_TrackPlaying(ambient));
    AUDIO_SetSoundEnabled(1);assert(AUDIO_UpdateAmbience(72));
    AUDIO_UpdateAmbience(0);SDL_Delay(120);assert(!MIX_TrackPlaying(lastAssigned));
    AUDIO_SetSoundVolume(1);AUDIO_SetSoundSpeed(1);AUDIO_Cleanup();
    for(int i=0;i<5;i++){char path[80];SDL_snprintf(path,sizeof(path),"Sound/%s.wav",accents[i]);remove(path);}
    remove("Sound/watrfall.wav");remove("Sound/fountain.wav");assert(SDL_RemovePath("Sound"));
    float edges[400];for(int i=0;i<200;i++){edges[i*2]=0.75f;edges[i*2+1]=-0.5f;}
    AUDIO_FadeRecordingEdges(edges,200,2,48000);
    assert(edges[0]==0 && edges[1]==0 && edges[398]==0 && edges[399]==0);
    assert(edges[200]==0.75f && edges[201]==-0.5f); /* Keep the body and duration intact. */
    /* A 50 ms overlap joins adjacent head samples at the seam, in both channels. */
    float seam[400];
    for(int i=0;i<200;i++){seam[i*2]=i/200.0f;seam[i*2+1]=-seam[i*2];}
    assert(AUDIO_CrossfadeLoop(seam,200,2,1000)==150);
    assert(fabsf(seam[298]-seam[0])<0.006f && fabsf(seam[299]-seam[1])<0.006f);
    for(int i=0;i<300;i++)assert(fabsf(seam[i])<=1);
    SDL_Quit();
    puts("External music, WAV overrides, and speaker fallback passed");
    return 0;
}
