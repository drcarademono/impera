#include "common/common.h"
#include "common/file.h"
#include "common/data_setup.h"
#include <errno.h>
#include <string.h>

#include "audio.h"
#include "aud_ops.h"
#include "sfx_map.h"
#include "pcspeaker.h"
#include "ambient_loop.h"
#include <limits.h>
#include <math.h>
#include "audio_catalog.h"
#include "mod_audio.h"
#include "mod/runtime.h"
#include "vars.h"

#include <stdio.h>

// TODO: clean up

#include <SDL3_mixer/SDL_mixer.h>

static MIX_Mixer* s_mixer;

static MIX_Track* s_bgmTrack;
static MIX_Track* s_sfxTrack;
static MIX_Track* s_synthTrack;
static MIX_Track* s_sfxTail;
static float s_tailGain=1.0f;
static MIX_Track* s_ambient[2];
static float s_ambientGain[2];
static int s_ambientId,s_ambientSlot;
static MIX_Audio* s_ambientLoops[4096];
static MIX_Audio* s_smoothedSfx[4096];
/* Two complete environmental notes plus a brief outgoing tail. Never loop notes. */
static MIX_Track* s_accents[3];
static float s_accentGain[3];
static int s_accentSlot;
static SDL_AudioStream* s_synthStream;
static float s_speakerGain=1.0f, s_recordingGain=1.0f, s_activeSpeakerGain=1.0f;
static PCSPK_State s_speakerState;
static unsigned s_rawFrequency;static Uint64 s_rawSince;static bool s_rawReload;

static SDL_PropertiesID s_bgmTrackProp;
static SDL_PropertiesID s_synthTrackProp;

static SDL_AudioSpec s_mixerSpec;

static MIX_Audio* s_bgm[4096];
static MIX_Audio* s_sfx[4096];

static unsigned s_audioRevision=~0u;
static struct {unsigned gain,start,end,loop;int present;} s_bgmSettings[4096];
static int s_currentBgmId;
static int s_queuedBgmId;

static void AUDIO_SDL_LoadBgmTable(void);
static void AUDIO_SDL_LoadSfxTable(void);
static void PlaySpeaker(int kind,int a,int b,int c,int d,int e);
static void stopAmbience(int milliseconds) {
    s_ambientId=0;
    for(int i=0;i<2;i++)if(s_ambient[i])MIX_StopTrack(s_ambient[i],MIX_MSToFrames(s_mixerSpec.freq,milliseconds));
}
static void clearAmbientLoops(void) {
    stopAmbience(0);
    for(int i=0;i<2;i++)if(s_ambient[i])MIX_SetTrackAudio(s_ambient[i],NULL);
    for(int i=0;i<4096;i++){
        MIX_DestroyAudio(s_ambientLoops[i]);s_ambientLoops[i]=NULL;
        MIX_DestroyAudio(s_smoothedSfx[i]);s_smoothedSfx[i]=NULL;
    }
}
static bool ResolveAudioPath(const char* name,char* resolved,size_t capacity)
{
    if(FILE_NameEqual(name,"Music") && *SETUP_MusicDirectory())
        return FILE_ResolvePath(SETUP_MusicDirectory(),resolved,capacity,0)==0;
    if (FILE_ResolvePath(name,resolved,capacity,0)==0) return true;
    if(FILE_DataPath(name,resolved,capacity)==0) return true;
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
    MIX_Audio *audio=MIX_LoadAudio(s_mixer, resolved, predecode);
    if(audio)SDL_SetStringProperty(MIX_GetAudioProperties(audio),"impera.source",resolved);
    return audio;
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

void AUDIO_SDL_ApplyVolumes(void) {
    if(s_bgmTrack)MIX_SetTrackGain(s_bgmTrack,0.3f*AUDIO_MusicVolume()*
        (s_currentBgmId>0 && s_currentBgmId<4096?s_bgmSettings[s_currentBgmId].gain/100.0f:1.0f));
    if(s_sfxTrack)MIX_SetTrackGain(s_sfxTrack,0.2f*AUDIO_SoundVolume()*s_recordingGain);
    if(s_synthTrack)MIX_SetTrackGain(s_synthTrack,0.2f*AUDIO_SoundVolume()*s_activeSpeakerGain);
    if(s_sfxTail)MIX_SetTrackGain(s_sfxTail,0.2f*AUDIO_SoundVolume()*s_tailGain);
    for(int i=0;i<2;i++)if(s_ambient[i])MIX_SetTrackGain(s_ambient[i],0.2f*AUDIO_SoundVolume()*s_ambientGain[i]);
    for(int i=0;i<3;i++)if(s_accents[i])MIX_SetTrackGain(s_accents[i],0.2f*AUDIO_SoundVolume()*s_accentGain[i]);
}
void AUDIO_SDL_SetSoundSpeed(float speed) {
    if(s_sfxTrack)MIX_SetTrackFrequencyRatio(s_sfxTrack,speed);
    if(s_synthTrack)MIX_SetTrackFrequencyRatio(s_synthTrack,speed);
    if(s_sfxTail)MIX_SetTrackFrequencyRatio(s_sfxTail,speed);
    for(int i=0;i<2;i++)if(s_ambient[i])MIX_SetTrackFrequencyRatio(s_ambient[i],speed);
    for(int i=0;i<3;i++)if(s_accents[i])MIX_SetTrackFrequencyRatio(s_accents[i],speed);
}

static void AUDIO_SDL_Init(void)
{
    s_rawFrequency=0;
    s_recordingGain=s_activeSpeakerGain=1.0f;
    PCSPK_Init(&s_speakerState,PCSPK_NOISE_SEED);
    MIX_Init();
    s_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!s_mixer) { DEBUG_Error("Audio device unavailable: %s", SDL_GetError()); return; }
    MIX_GetMixerFormat(s_mixer, &s_mixerSpec);
    debug("Audio mixer frequency=%d channels=%d format=%u",s_mixerSpec.freq,s_mixerSpec.channels,(unsigned)s_mixerSpec.format);

    s_bgmTrack = MIX_CreateTrack(s_mixer);
    s_sfxTrack = MIX_CreateTrack(s_mixer);
    s_synthTrack = MIX_CreateTrack(s_mixer);
    s_sfxTail=MIX_CreateTrack(s_mixer);s_tailGain=1;
    s_ambientId=s_ambientSlot=0;
    for(int i=0;i<2;i++){s_ambient[i]=MIX_CreateTrack(s_mixer);s_ambientGain[i]=1;}
    s_accentSlot=0;
    for(int i=0;i<3;i++){s_accents[i]=MIX_CreateTrack(s_mixer);s_accentGain[i]=1;}
    AUDIO_SDL_SetSoundSpeed(AUDIO_SoundSpeed());
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, 48000 };
    s_synthStream = SDL_CreateAudioStream(&spec, &spec);
    MIX_SetTrackAudioStream(s_synthTrack, s_synthStream);
    MIX_SetTrackGain(s_synthTrack, 0.2f*AUDIO_SoundVolume());
    s_synthTrackProp = SDL_CreateProperties();
    SDL_SetBooleanProperty(s_synthTrackProp,MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN,false);

    MIX_SetTrackGain(s_bgmTrack, 0.3f*AUDIO_MusicVolume());
    MIX_SetTrackGain(s_sfxTrack, 0.2f*AUDIO_SoundVolume());

    s_bgmTrackProp = SDL_CreateProperties();
    SDL_SetNumberProperty(s_bgmTrackProp, MIX_PROP_PLAY_LOOPS_NUMBER, -1);

    MIX_SetTrackStoppedCallback(s_bgmTrack, AUDIO_SDL_OnBgmStopped, NULL);

    AUDIO_MOD_Load();s_audioRevision=MOD_Revision();
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
    MIX_DestroyTrack(s_sfxTail);s_sfxTail=NULL;
    clearAmbientLoops();
    for(int i=0;i<3;i++){MIX_DestroyTrack(s_accents[i]);s_accents[i]=NULL;}
    for(int i=0;i<2;i++){MIX_DestroyTrack(s_ambient[i]);s_ambient[i]=NULL;}
    SDL_DestroyAudioStream(s_synthStream);
    for (int i=0;i<4096;i++) { MIX_DestroyAudio(s_bgm[i]); s_bgm[i]=NULL; }
    for (int i=0;i<4096;i++) { MIX_DestroyAudio(s_sfx[i]); s_sfx[i]=NULL; }
    SDL_DestroyProperties(s_bgmTrackProp);
    SDL_DestroyProperties(s_synthTrackProp);
    s_synthStream=NULL;
    s_bgmTrack=s_sfxTrack=s_synthTrack=NULL;
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

static MIX_Audio *LoadModAudio(unsigned kind,unsigned id) {
    const ModAudioClip *c=AUDIO_MOD_Clip(kind,id);
    if(!c || !c->size) return NULL;
    SDL_IOStream *io=SDL_IOFromConstMem(c->data,c->size);
    return io?MIX_LoadAudio_IO(s_mixer,io,kind!=0,true):NULL;
}
static void AUDIO_SDL_LoadBgmTable(void)
{
    for(unsigned i=0;i<4096;i++) {
        const ModAudioClip *c=AUDIO_MOD_Clip(0,i);s_bgmSettings[i].present=c!=NULL;
        s_bgmSettings[i].gain=c?c->gain:100;s_bgmSettings[i].loop=c?c->loop:1;
        s_bgmSettings[i].start=c?c->start:0;s_bgmSettings[i].end=c?c->end:0;
    }
    char directory[FILE_PATH_SIZE];
    memset(s_musicPaths,0,sizeof(s_musicPaths));
    if (ResolveAudioPath("Music",directory,sizeof(directory)))
        SDL_EnumerateDirectory(directory,FindMusic,NULL);
    for (int i=1;i<=16;i++) {
        s_bgm[i]=LoadModAudio(0,i);
        if (!s_bgm[i] && s_musicPaths[i][0]) {
            s_bgm[i]=AUDIO_SDL_LoadAudio(s_musicPaths[i],false);
            if (!s_bgm[i]) DEBUG_Error("Cannot load music %s: %s",s_musicPaths[i],SDL_GetError());
        }
        if (!s_bgm[i] && i<=15) {
            char fileName[32];
            SDL_snprintf(fileName,sizeof(fileName),"BGM/%02d.ogg",i);
            s_bgm[i]=AUDIO_SDL_LoadAudio(fileName,false);
        }
        debug("Music track=%d available=%d source=%s",i,s_bgm[i]!=NULL,s_musicPaths[i][0]?s_musicPaths[i]:"legacy BGM or absent");
    }
    for(unsigned i=17;i<4096;i++) s_bgm[i]=LoadModAudio(0,i);
}

void AUDIO_SDL_ReloadMusic(void)
{
    if(!s_mixer) return;
    /* A music-folder change leaves effects intact; a mod change reloads both banks. */
    bool reloadEffects=s_audioRevision!=MOD_Revision();
    MIX_SetTrackStoppedCallback(s_bgmTrack,NULL,NULL);
    MIX_StopTrack(s_bgmTrack,0);
    MIX_SetTrackAudio(s_bgmTrack,NULL);
    s_currentBgmId=s_queuedBgmId=0;
    for(int i=0;i<4096;i++) { MIX_DestroyAudio(s_bgm[i]);s_bgm[i]=NULL; }
    if(reloadEffects) {
        for(int i=0;i<3;i++){MIX_StopTrack(s_accents[i],0);MIX_SetTrackAudio(s_accents[i],NULL);}
        clearAmbientLoops();
        MIX_StopTrack(s_sfxTail,0);MIX_SetTrackAudio(s_sfxTail,NULL);
        MIX_SetTrackAudio(s_sfxTrack,NULL);MIX_StopTrack(s_sfxTrack,0);
        SDL_ClearAudioStream(s_synthStream);MIX_StopTrack(s_synthTrack,0);
        for(int i=0;i<4096;i++) {MIX_DestroyAudio(s_sfx[i]);s_sfx[i]=NULL;}
    }
    AUDIO_MOD_Load();s_audioRevision=MOD_Revision();
    AUDIO_SDL_LoadBgmTable();if(reloadEffects) AUDIO_SDL_LoadSfxTable();
    MIX_SetTrackStoppedCallback(s_bgmTrack,AUDIO_SDL_OnBgmStopped,NULL);
    SDL_ClearError();
}

static void AUDIO_SDL_LoadSfxTable(void)
{
    for(unsigned i=1;i<4096;i++) s_sfx[i]=LoadModAudio(1,i);
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
        debug("Sound effect=%d source=%s",r->sfxId,s_sfx[r->sfxId]?"external recording":"PC-speaker synthesis fallback");
    }
}

bool AUDIO_SDL_HasSfx(int id)
{
    if(s_audioRevision!=MOD_Revision()) AUDIO_SDL_ReloadMusic();
    return id>=0 && id<4096 && s_sfx[id]!=NULL;
}

/* Decode once into a shared format for either loop joins or finite-file edge ramps. */
static float *decodeRecording(int id,size_t *frames) {
    SDL_AudioSpec source,spec={SDL_AUDIO_F32,2,48000};
    Sint64 duration=MIX_GetAudioDuration(s_sfx[id]);
    if(!MIX_GetAudioFormat(s_sfx[id],&source) || source.freq<=0 || duration<=0)return NULL;
    double estimated=ceil((double)duration*spec.freq/source.freq)+1;
    if(estimated>INT_MAX/(2*sizeof(float)))return NULL;
    int capacity=(int)estimated*2*(int)sizeof(float);
    const ModAudioClip *clip=AUDIO_MOD_Clip(1,id);
    MIX_AudioDecoder *decoder=NULL;
    if(clip && clip->size) {
        SDL_IOStream *io=SDL_IOFromConstMem(clip->data,clip->size);
        if(io)decoder=MIX_CreateAudioDecoder_IO(io,true,0);
    } else {
        const char *path=SDL_GetStringProperty(MIX_GetAudioProperties(s_sfx[id]),"impera.source","");
        if(*path)decoder=MIX_CreateAudioDecoder(path,0);
    }
    if(!decoder)return NULL;
    float *pcm=SDL_malloc(capacity);int used=0,got=0;
    if(pcm)while(used<capacity && (got=MIX_DecodeAudio(decoder,(char*)pcm+used,capacity-used,&spec))>0)used+=got;
    MIX_DestroyAudioDecoder(decoder);
    if(!pcm || got<0 || used>=capacity || used%(2*sizeof(float))) {SDL_free(pcm);return NULL;}
    *frames=used/(2*sizeof(float));return pcm;
}
static MIX_Audio *recording(int id,bool loop) {
    MIX_Audio **cache=loop?s_ambientLoops:s_smoothedSfx;
    if(cache[id])return cache[id];
    size_t frames=0;float *pcm=decodeRecording(id,&frames);
    if(!pcm)return loop?NULL:s_sfx[id];
    SDL_AudioSpec spec={SDL_AUDIO_F32,2,48000};
    if(loop)frames=AUDIO_CrossfadeLoop(pcm,frames,2,spec.freq);
    else AUDIO_FadeRecordingEdges(pcm,frames,2,spec.freq);
    if(frames)cache[id]=MIX_LoadRawAudio(s_mixer,pcm,frames*2*sizeof(float),&spec);
    SDL_free(pcm);return cache[id]?cache[id]:loop?NULL:s_sfx[id];
}
/* Recorded clock ticks, instrument notes, and drips retain their scheduled attacks.
 * Foreground effects must not replace them; bounded voices preserve natural tails. */
int AUDIO_SDL_PlayAccentSfx(int id) {
    if(!s_mixer || !D_a9ce || id<1 || id>=4096 || !AUDIO_SDL_HasSfx(id))return 0;
    int slot=(s_accentSlot+1)%3,oldest=(slot+1)%3;
    MIX_StopTrack(s_accents[slot],0);
    if(MIX_TrackPlaying(s_accents[oldest]))
        MIX_StopTrack(s_accents[oldest],MIX_MSToFrames(s_mixerSpec.freq,2));
    const ModAudioClip *clip=AUDIO_MOD_Clip(1,id);
    s_accentGain[slot]=clip?clip->gain/100.0f:1;
    MIX_SetTrackGain(s_accents[slot],0.2f*AUDIO_SoundVolume()*s_accentGain[slot]);
    bool ok=MIX_SetTrackAudio(s_accents[slot],recording(id,false)) && MIX_PlayTrack(s_accents[slot],0);
    s_accentSlot=slot;if(!ok)PrintError();return ok;
}
int AUDIO_SDL_UpdateAmbience(int id) {
    if(!s_mixer)return 0;
    if(id<1 || id>=4096 || !AUDIO_SDL_HasSfx(id)) {
        if(s_ambientId)stopAmbience(50);
        return id==0;
    }
    if(s_ambientId==id && MIX_TrackPlaying(s_ambient[s_ambientSlot]))return 1;
    MIX_Audio *loop=recording(id,true);
    if(!loop) {if(s_ambientId)stopAmbience(50);return 0;}
    int next=1-s_ambientSlot;
    MIX_StopTrack(s_ambient[next],0);
    if(s_ambientId)MIX_StopTrack(s_ambient[s_ambientSlot],MIX_MSToFrames(s_mixerSpec.freq,50));
    const ModAudioClip *clip=AUDIO_MOD_Clip(1,id);
    s_ambientGain[next]=clip?clip->gain/100.0f:1;
    MIX_SetTrackGain(s_ambient[next],0.2f*AUDIO_SoundVolume()*s_ambientGain[next]);
    SDL_PropertiesID options=SDL_CreateProperties();
    SDL_SetNumberProperty(options,MIX_PROP_PLAY_LOOPS_NUMBER,-1);
    SDL_SetNumberProperty(options,MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER,50);
    bool ok=MIX_SetTrackAudio(s_ambient[next],loop) && MIX_PlayTrack(s_ambient[next],options);
    SDL_DestroyProperties(options);
    s_ambientSlot=next;s_ambientId=ok?id:0;
    if(!ok)PrintError();return ok;
}

static void AUDIO_SDL_PlaySfx(int id)
{
    debug("AUDIO_SDL_PlaySfx(%d)", id);

    if (!D_a9ce || id<1 || id>=4096) return;
    if(!AUDIO_SDL_HasSfx(id)) {
        const ModAudioClip *clip=AUDIO_MOD_Clip(1,id);s_speakerGain=clip?clip->gain/100.0f:1.0f;
        if(id<=115) {const AudioCatalog *c=&audioCatalog[id-1];for(unsigned i=0;i<c->count;i++) {
            const AudioRecipe *r=&audioRecipes[c->first+i];PlaySpeaker(r->kind,r->p[0],r->p[1],r->p[2],r->p[3],r->p[4]);}}
        s_speakerGain=1.0f;return;
    }

    if(MIX_TrackPlaying(s_sfxTrack)) {
        MIX_StopTrack(s_sfxTail,0);
        MIX_Track *old=s_sfxTrack;s_sfxTrack=s_sfxTail;s_sfxTail=old;
        s_tailGain=s_recordingGain;
        MIX_StopTrack(s_sfxTail,MIX_MSToFrames(s_mixerSpec.freq,2));
    }
    const ModAudioClip *clip=AUDIO_MOD_Clip(1,id);
    s_recordingGain=clip?clip->gain/100.0f:1.0f;
    MIX_SetTrackGain(s_sfxTrack,0.2f*AUDIO_SoundVolume()*s_recordingGain);
    SDL_AudioSpec spec;Sint64 length=MIX_GetAudioDuration(s_sfx[id]);
    Sint64 fade=0;
    if(MIX_GetAudioFormat(s_sfx[id],&spec) && spec.freq>0 && length>0)
        fade=SDL_min(2,(length*1000/spec.freq)/8);
    SDL_PropertiesID options=SDL_CreateProperties();
    SDL_SetNumberProperty(options,MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER,fade);
    if (!MIX_SetTrackAudio(s_sfxTrack, recording(id,false)) || !MIX_PlayTrack(s_sfxTrack, options)) PrintError();
    SDL_DestroyProperties(options);
}

static void AUDIO_SDL_PlayTitle1Sfx(void)
{
    AUDIO_SDL_PlaySfx(SFX_ID_TITLE1);
}

static void AUDIO_SDL_PlayTitle2Sfx(void)
{
    AUDIO_SDL_PlaySfx(SFX_ID_TITLE2);
}

static void AUDIO_SDL_StopSfx(void)
{
    s_rawFrequency=0;
    stopAmbience(10);
    for(int i=0;i<3;i++)if(s_accents[i])MIX_StopTrack(s_accents[i],MIX_MSToFrames(s_mixerSpec.freq,10));
    if(s_sfxTail)MIX_StopTrack(s_sfxTail,MIX_MSToFrames(s_mixerSpec.freq,10));
    uint16_t noise=s_speakerState.noise;PCSPK_Init(&s_speakerState,noise);
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

    if (!s_mixer || id<1 || id>=4096 || s_bgm[id] == NULL)
        return;

    MIX_SetTrackAudio(s_bgmTrack, s_bgm[id]);
    MIX_SetTrackGain(s_bgmTrack,0.3f*AUDIO_MusicVolume()*s_bgmSettings[id].gain/100.0f);
    SDL_SetNumberProperty(s_bgmTrackProp,MIX_PROP_PLAY_LOOPS_NUMBER,s_bgmSettings[id].loop?-1:0);
    SDL_SetNumberProperty(s_bgmTrackProp,MIX_PROP_PLAY_LOOP_START_MILLISECOND_NUMBER,s_bgmSettings[id].start);
    SDL_SetNumberProperty(s_bgmTrackProp,MIX_PROP_PLAY_MAX_MILLISECONDS_NUMBER,s_bgmSettings[id].end?(Sint64)s_bgmSettings[id].end:-1);
    MIX_PlayTrack(s_bgmTrack, s_bgmTrackProp);
    PrintError();
}

static void AUDIO_SDL_PlayBgm(int id)
{
    if(s_audioRevision!=MOD_Revision()) AUDIO_SDL_ReloadMusic();
    if(id<1 || id>=4096) return;
    debug("AUDIO_SDL_PlayBgm(%d)", id);

    s_queuedBgmId = 0;

    if (s_currentBgmId == id)
    {
        MIX_SetTrackLoops(s_bgmTrack, s_bgmSettings[id].loop?-1:0);
        return;
    }

    AUDIO_SDL_PlayBgmSub(id);
}

static void AUDIO_SDL_QueueBgm(int id)
{
    debug("AUDIO_SDL_QueueBgm(%d)", id);

    if (!s_mixer || id<1 || id>=4096 || s_bgm[id] == NULL)
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
    if (!s_synthStream || !D_a9ce) return;
    s_activeSpeakerGain=s_speakerGain;
    MIX_SetTrackGain(s_synthTrack,0.2f*AUDIO_SoundVolume()*s_activeSpeakerGain);
    size_t frames;
    float* pcm=PCSPK_RenderState(&s_speakerState,kind,a,b,c,d,e,&frames);
    if (!pcm) return;
    /* Queue consecutive calls so paired footsteps and spell phrases survive. */
    if (!SDL_PutAudioStreamData(s_synthStream,pcm,(int)(frames*sizeof(float)))) PrintError();
    else if (!MIX_TrackPlaying(s_synthTrack) && !MIX_PlayTrack(s_synthTrack,s_synthTrackProp)) PrintError();
    SDL_free(pcm);
}
static void rawSpan(uint64_t clocks) {
    if(!s_synthStream || !s_rawFrequency || !clocks) return;
    size_t frames;float *pcm=PCSPK_RenderSpan(&s_speakerState,s_rawFrequency,clocks,s_rawReload,&frames);
    s_rawReload=false;
    if(pcm) {
        SDL_PutAudioStreamData(s_synthStream,pcm,(int)(frames*sizeof(float)));SDL_free(pcm);
        if(!MIX_TrackPlaying(s_synthTrack))MIX_PlayTrack(s_synthTrack,s_synthTrackProp);
    }
}
static void flushRaw(void) {
    Uint64 now=SDL_GetTicksNS();if(!s_rawFrequency || now<=s_rawSince)return;
    /* Presentation should not accumulate seconds of delayed sound. */
    Uint64 elapsed=now-s_rawSince;if(elapsed>100000000u)elapsed=100000000u;
    uint64_t clocks=(uint64_t)(elapsed*(double)AUDIO_SoundSpeed()*PCSPK_CLOCK/1000000000u);
    rawSpan(clocks);s_rawSince=now;
}
void AUDIO_SDL_SpeakerOn(unsigned frequency) {
    flushRaw();if(!s_rawFrequency)s_rawSince=SDL_GetTicksNS();
    s_rawFrequency=D_a9ce?frequency:0;s_rawReload=true;
}
void AUDIO_SDL_SpeakerOff(void) {
    flushRaw();s_rawFrequency=0;size_t frames;
    float *pcm=PCSPK_RenderSpan(&s_speakerState,0,0,false,&frames);SDL_free(pcm);
}
int AUDIO_SDL_SpeakerDelay(unsigned units) {
    if(!s_rawFrequency || !D_a9ce)return -1;
    flushRaw();uint64_t clocks=PCSPK_Clocks(PCSPK_TONE,s_rawFrequency,units,0,0,0);
    rawSpan(clocks);Uint64 ns=(Uint64)(clocks*1000000000.0/(PCSPK_CLOCK*AUDIO_SoundSpeed()));
    s_rawSince=SDL_GetTicksNS()+ns;return (int)((ns+999999u)/1000000u);
}
int AUDIO_SDL_PlaySpeakerFootstep(void) {
    if(s_audioRevision!=MOD_Revision())AUDIO_SDL_ReloadMusic();
    /* Retain authored/external replacements and their existing playback path. */
    if(s_sfx[73] || s_sfx[74] || AUDIO_MOD_Assignment(4,73,0,73)!=73 ||
       AUDIO_MOD_Assignment(4,74,0,74)!=74)return 0;
    if(!s_synthStream)return 1;
    AUDIO_SDL_PlaySfx(73);
    /* DOS 433e calls SomeDelay(1,20) BETWEEN the two noise bursts.
     * This belongs in the sound timeline, independently of movement pacing. */
    size_t frames;float *gap=PCSPK_RenderSpan(&s_speakerState,0,
        PCSPK_Clocks(PCSPK_TONE,1,20,0,0,0),false,&frames);
    if(gap){
        if(!SDL_PutAudioStreamData(s_synthStream,gap,(int)(frames*sizeof(float))))PrintError();
        SDL_free(gap);
    }
    AUDIO_SDL_PlaySfx(74);
    return 1;
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
