#include "common/common.h"
#include "common/file.h"
#include <errno.h>
#include <string.h>

#include "audio.h"
#include "aud_ops.h"
#include "sfx_map.h"
#include "pcspeaker.h"

#include <stdio.h>

// TODO: clean up

#include <SDL3_mixer/SDL_mixer.h>

static MIX_Mixer* s_mixer;

static MIX_Track* s_bgmTrack;
static MIX_Track* s_sfxTrack;
static MIX_Track* s_synthTrack;
static SDL_AudioStream* s_synthStream;

static SDL_PropertiesID s_bgmTrackProp;
static SDL_PropertiesID s_synthTrackProp;

static SDL_AudioSpec s_mixerSpec;

static MIX_Audio* s_bgm[20];
static MIX_Audio* s_sfx[256];

static int s_currentBgmId;
static int s_queuedBgmId;

static void AUDIO_SDL_LoadBgmTable(void);
static void AUDIO_SDL_LoadSfxTable(void);
static void PlaySpeaker(int kind,int a,int b,int c,int d,int e);
static bool ResolveAudioPath(const char* name,char* resolved,size_t capacity)
{
    if (FILE_ResolvePath(name,resolved,capacity,0)==0) return true;
    const char* base=SDL_GetBasePath();
    char alongside[FILE_PATH_SIZE];
    if (!base || SDL_snprintf(alongside,sizeof(alongside),"%s%s",base,name)>=(int)sizeof(alongside)) return false;
    return FILE_ResolvePath(alongside,resolved,capacity,0)==0;
}
static MIX_Audio* AUDIO_SDL_LoadAudio(const char* fileName, bool predecode)
{
    char resolved[FILE_PATH_SIZE];
    if (!ResolveAudioPath(fileName, resolved, sizeof(resolved)))
    {
        SDL_SetError("Cannot resolve audio file '%s': %s", fileName, strerror(errno));
        return NULL;
    }
    return MIX_LoadAudio(s_mixer, resolved, predecode);
}
static void AUDIO_SDL_PlayBgmSub(int id);
static void SDLCALL AUDIO_SDL_OnBgmStopped(void* userdata, MIX_Track* track);

static void PrintError(void)
{
    const char* err = SDL_GetError();
    if (err)
    {
        debug("%s",err);
    }
}

static void AUDIO_SDL_Init(void)
{
    MIX_Init();
    s_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!s_mixer) { fprintf(stderr, "Audio device unavailable: %s\n", SDL_GetError()); return; }
    MIX_GetMixerFormat(s_mixer, &s_mixerSpec);

    s_bgmTrack = MIX_CreateTrack(s_mixer);
    s_sfxTrack = MIX_CreateTrack(s_mixer);
    s_synthTrack = MIX_CreateTrack(s_mixer);
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, 48000 };
    s_synthStream = SDL_CreateAudioStream(&spec, &spec);
    MIX_SetTrackAudioStream(s_synthTrack, s_synthStream);
    MIX_SetTrackGain(s_synthTrack, 0.2f);
    s_synthTrackProp = SDL_CreateProperties();
    SDL_SetBooleanProperty(s_synthTrackProp,MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN,false);

    MIX_SetTrackGain(s_bgmTrack, 0.3f);
    MIX_SetTrackGain(s_sfxTrack, 0.2f);

    s_bgmTrackProp = SDL_CreateProperties();
    SDL_SetNumberProperty(s_bgmTrackProp, MIX_PROP_PLAY_LOOPS_NUMBER, -1);

    MIX_SetTrackStoppedCallback(s_bgmTrack, AUDIO_SDL_OnBgmStopped, NULL);

    AUDIO_SDL_LoadBgmTable();
    AUDIO_SDL_LoadSfxTable();
    SDL_ClearError(); /* Optional missing files are not playback failures. */
}

static void AUDIO_SDL_Cleanup(void)
{
    MIX_SetTrackStoppedCallback(s_bgmTrack, NULL, NULL);
    MIX_DestroyTrack(s_bgmTrack);
    MIX_DestroyTrack(s_sfxTrack);
    MIX_DestroyTrack(s_synthTrack);
    SDL_DestroyAudioStream(s_synthStream);
    for (int i=0;i<20;i++) { MIX_DestroyAudio(s_bgm[i]); s_bgm[i]=NULL; }
    for (int i=0;i<256;i++) { MIX_DestroyAudio(s_sfx[i]); s_sfx[i]=NULL; }
    SDL_DestroyProperties(s_bgmTrackProp);
    SDL_DestroyProperties(s_synthTrackProp);
    s_synthStream=NULL;
    s_currentBgmId=s_queuedBgmId=0;
    MIX_DestroyMixer(s_mixer);
    s_mixer = NULL;
    MIX_Quit();
}

static char s_musicPaths[17][FILE_PATH_SIZE];
static SDL_EnumerationResult SDLCALL FindMusic(void* data, const char* directory, const char* name)
{
    (void)data;
    if (strlen(name)<4 || name[0]<'0' || name[0]>'9' || name[1]<'0' || name[1]>'9' ||
        (name[2]>='0' && name[2]<='9')) return SDL_ENUM_CONTINUE;
    int id=(name[0]-'0')*10+name[1]-'0';
    const char* extension=strrchr(name,'.');
    if (id<1 || id>16 || !extension ||
        (SDL_strcasecmp(extension,".mp3") && SDL_strcasecmp(extension,".ogg") &&
         SDL_strcasecmp(extension,".wav") && SDL_strcasecmp(extension,".flac"))) return SDL_ENUM_CONTINUE;
    char path[FILE_PATH_SIZE];
    if (SDL_snprintf(path,sizeof(path),"%s/%s",directory,name)>=(int)sizeof(path)) return SDL_ENUM_CONTINUE;
    /* Stable choice if several formats or recordings occupy the same slot. */
    if (!s_musicPaths[id][0] || strcmp(path,s_musicPaths[id])<0)
        SDL_strlcpy(s_musicPaths[id],path,sizeof(s_musicPaths[id]));
    return SDL_ENUM_CONTINUE;
}

static void AUDIO_SDL_LoadBgmTable(void)
{
    char directory[FILE_PATH_SIZE];
    memset(s_musicPaths,0,sizeof(s_musicPaths));
    if (ResolveAudioPath("Music",directory,sizeof(directory)))
        SDL_EnumerateDirectory(directory,FindMusic,NULL);
    for (int i=1;i<=16;i++) {
        if (s_musicPaths[i][0]) {
            s_bgm[i]=AUDIO_SDL_LoadAudio(s_musicPaths[i],false);
            if (!s_bgm[i]) fprintf(stderr,"Cannot load music %s: %s\n",s_musicPaths[i],SDL_GetError());
        }
        if (!s_bgm[i] && i<=15) {
            char fileName[32];
            SDL_snprintf(fileName,sizeof(fileName),"BGM/%02d.ogg",i);
            s_bgm[i]=AUDIO_SDL_LoadAudio(fileName,false);
        }
    }
}

static void AUDIO_SDL_LoadSfxTable(void)
{
    for (int i = 0; i < SFX_RULE_COUNT; i++)
    {
        const SfxRule* r = &g_sfxRules[i];
        char fileName[256] = {0,};
        if (s_sfx[r->sfxId] != NULL)
        {
            continue;
        }
        snprintf(fileName, sizeof(fileName), "Sound/%s", r->fileName);
        s_sfx[r->sfxId] = AUDIO_SDL_LoadAudio(fileName, true);
        if (!s_sfx[r->sfxId]) {
            snprintf(fileName,sizeof(fileName),"SFX/%s",r->fileName);
            s_sfx[r->sfxId]=AUDIO_SDL_LoadAudio(fileName,true);
        }
    }
}

bool AUDIO_SDL_HasSfx(int id)
{
    return id>=0 && id<256 && s_sfx[id]!=NULL;
}

static void AUDIO_SDL_PlaySfx(int id)
{
    debug("AUDIO_SDL_PlaySfx(%d)", id);

    if (!AUDIO_SDL_HasSfx(id))
        return;

    if (!MIX_SetTrackAudio(s_sfxTrack, s_sfx[id]) || !MIX_PlayTrack(s_sfxTrack, 0)) PrintError();
}

static void AUDIO_SDL_PlayTitle1Sfx(void)
{
    if (AUDIO_SDL_HasSfx(SFX_ID_TITLE1)) AUDIO_SDL_PlaySfx(SFX_ID_TITLE1);
    else PlaySpeaker(PCSPK_NOISE,1,16000,16000,0,0);
}

static void AUDIO_SDL_PlayTitle2Sfx(void)
{
    if (AUDIO_SDL_HasSfx(SFX_ID_TITLE2)) AUDIO_SDL_PlaySfx(SFX_ID_TITLE2);
    else PlaySpeaker(PCSPK_SWEEP,500,8000,8,800,0);
}

static void AUDIO_SDL_StopSfx(void)
{
    if (!s_mixer) return;
    SDL_ClearAudioStream(s_synthStream);
    MIX_StopTrack(s_synthTrack,0);
    Sint64 frames = MIX_MSToFrames(s_mixerSpec.freq, 10);
    MIX_StopTrack(s_sfxTrack, frames);
    PrintError();
}

static void AUDIO_SDL_PlayBgmSub(int id)
{
    s_currentBgmId = id;

    if (!s_mixer || id<1 || id>=20 || s_bgm[id] == NULL)
        return;

    MIX_SetTrackAudio(s_bgmTrack, s_bgm[id]);
    MIX_PlayTrack(s_bgmTrack, s_bgmTrackProp);
    PrintError();
}

static void AUDIO_SDL_PlayBgm(int id)
{
    debug("AUDIO_SDL_PlayBgm(%d)", id);

    s_queuedBgmId = 0;

    if (s_currentBgmId == id)
    {
        MIX_SetTrackLoops(s_bgmTrack, -1);
        return;
    }

    AUDIO_SDL_PlayBgmSub(id);
}

static void AUDIO_SDL_QueueBgm(int id)
{
    debug("AUDIO_SDL_QueueBgm(%d)", id);

    if (!s_mixer || id<1 || id>=20 || s_bgm[id] == NULL)
        return;

    if (s_currentBgmId == 0 || !MIX_TrackPlaying(s_bgmTrack))
    {
        s_queuedBgmId = 0;
        AUDIO_SDL_PlayBgmSub(id);
        return;
    }

    s_queuedBgmId = id;
    MIX_SetTrackLoops(s_bgmTrack, 0);
}

void SDLCALL AUDIO_SDL_OnBgmStopped(void* userdata, MIX_Track* track)
{
    int queuedBgmId = s_queuedBgmId;

    s_queuedBgmId = 0;
    s_currentBgmId = 0;

    if (queuedBgmId == 0)
    {
        return;
    }

    AUDIO_SDL_PlayBgmSub(queuedBgmId);
}

void AUDIO_SDL_StopBgm(void)
{
    debug("AUDIO_SDL_StopBgm()");

    s_currentBgmId = 0;
    s_queuedBgmId = 0;
    if (!s_mixer) return;

    Sint64 frames = MIX_MSToFrames(s_mixerSpec.freq, 500);
    MIX_StopTrack(s_bgmTrack, frames);
    PrintError();
}

void AUDIO_SDL_Noop(void)
{
    // no-op
}

static int AUDIO_SDL_GetSfxType(void)
{
    return SFX_TYPE_PCM;
}

static void PlaySpeaker(int kind, int a,int b,int c,int d,int e)
{
    if (!s_synthStream) return;
    size_t frames;
    float* pcm=PCSPK_Render(kind,a,b,c,d,e,&frames);
    if (!pcm) return;
    /* Queue consecutive calls so paired footsteps and spell phrases survive. */
    if (SDL_GetAudioStreamQueued(s_synthStream)<48000*4*10) {
        SDL_PutAudioStreamData(s_synthStream,pcm,(int)(frames*sizeof(float)));
        if (!MIX_TrackPlaying(s_synthTrack)) MIX_PlayTrack(s_synthTrack,s_synthTrackProp);
    }
    SDL_free(pcm);
}
static void AUDIO_SDL_PlaySynthPulse(int freq,int delay,int dur,int width,int inc)
{ PlaySpeaker(PCSPK_PULSE,freq,delay,dur,width,inc); }
static void AUDIO_SDL_PlaySynthNoise(int rate,int dur,int limit)
{ PlaySpeaker(PCSPK_NOISE,rate,dur,limit,0,0); }
static void AUDIO_SDL_PlaySynthTone(int freq,int dur)
{ PlaySpeaker(PCSPK_TONE,freq,dur,0,0,0); }
static void AUDIO_SDL_PlaySynthSweepTone(int start,int end,int step,int dur)
{ PlaySpeaker(PCSPK_SWEEP,start,end,step,dur,0); }

static AudioMusicDriverOps s_musicOps =
{
    .Initialize = AUDIO_SDL_Init,
    .Cleanup = AUDIO_SDL_Cleanup,
    .PlayBgm = AUDIO_SDL_PlayBgm,
    .QueueBgm = AUDIO_SDL_QueueBgm,
    .StopBgm = AUDIO_SDL_StopBgm
};

static AudioSfxDriverOps s_sfxOps =
{
    .Initialize = AUDIO_SDL_Noop,
    .Cleanup = AUDIO_SDL_Noop,
    .PlaySfx = AUDIO_SDL_PlaySfx,
    .PlayTitle1Sfx = AUDIO_SDL_PlayTitle1Sfx,
    .PlayTitle2Sfx = AUDIO_SDL_PlayTitle2Sfx,
    .StopSfx = AUDIO_SDL_StopSfx,
    .GetSfxType = AUDIO_SDL_GetSfxType,
    .PlaySynthPulse = AUDIO_SDL_PlaySynthPulse,
    .PlaySynthNoise = AUDIO_SDL_PlaySynthNoise,
    .PlaySynthTone = AUDIO_SDL_PlaySynthTone,
    .PlaySynthSweepTone = AUDIO_SDL_PlaySynthSweepTone
};

AudioMusicDriverOps* AUDIO_SDL_GetMusicOps(void)
{
    return &s_musicOps;
}

AudioSfxDriverOps* AUDIO_SDL_GetSfxOps(void)
{
    return &s_sfxOps;
}
