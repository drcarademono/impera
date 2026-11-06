#include "soundfont.h"
#include "audio_catalog.h"
#include "pcspeaker.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <math.h>
#define TSF_IMPLEMENTATION
#include "third_party/tsf.h"

static tsf *font;
static char path[4096];
static int presets[2]; /* harpsichord, lute */
static unsigned revision;
static bool enabled = true;
bool SF_Load(const char *filename) {
    if (!filename || strlen(filename) >= sizeof(path) || strpbrk(filename, "\r\n"))
        return false;
    size_t bytes = 0;
    void *data = *filename ? SDL_LoadFile(filename, &bytes) : NULL;
    tsf *next = data && bytes <= INT_MAX ? tsf_load_memory(data, (int)bytes) : NULL;
    SDL_free(data);
    if (*filename && (!next || tsf_get_presetcount(next) < 1)) {
        if (next)
            tsf_close(next);
        return false;
    }
    if (font)
        tsf_close(font);
    font = next;
    SDL_strlcpy(path, filename, sizeof(path));
    for (int i = 0; i < 2; i++) {
        int index = font ? tsf_get_presetindex(font, 0, i ? 24 : 6) : -1;
        presets[i] = index < 0 ? 0 : index;
    }
    ++revision;
    return true;
}
const char *SF_Path(void) { return path; }
int SF_Count(void) { return font ? tsf_get_presetcount(font) : 0; }
const char *SF_Name(int index) {
    return font && index >= 0 && index < SF_Count() ? tsf_get_presetname(font, index)
                                                    : "PC Speaker";
}
int SF_Preset(int instrument) {
    return instrument >= 0 && instrument < 2 ? presets[instrument] : 0;
}
void SF_SetPreset(int instrument, int preset) {
    if (instrument < 0 || instrument > 1 || preset < 0 || preset >= SF_Count())
        return;
    if (presets[instrument] != preset) {
        presets[instrument] = preset;
        ++revision;
    }
}
bool SF_Enabled(void) { return enabled; }
void SF_SetEnabled(bool value) {
    if (enabled != value) {
        enabled = value;
        ++revision;
    }
}
unsigned SF_Revision(void) { return revision; }
float *SF_Render(int effect, float speed, size_t *frames) {
    *frames = 0;
    if (!font || !enabled || effect < 20 || effect > 38 || !isfinite(speed) || speed < 0.1f ||
        speed > 10)
        return NULL;
    const AudioRecipe *r = &audioRecipes[audioCatalog[effect - 1].first];
    /* Accumulator wraps define pitch; the exact sequence includes gate-dependent
       instruction costs. Duration can change without transposing that pitch. */
    uint64_t clocks = PCSPK_Clocks(r->kind, r->p[0], r->p[1], r->p[2], r->p[3], r->p[4]);
    double hz = (double)r->p[0] * r->p[2] * PCSPK_CLOCK / (65536.0 * clocks);
    double midi = 69 + 12 * log2(hz / 440.0);
    int key = (int)lround(midi);
    if (key < 0 || key > 127)
        return NULL;
    size_t held = (size_t)(clocks * 48000.0 / PCSPK_CLOCK / speed);
    size_t release = 24000;
    float *pcm = SDL_calloc((held + release) * 2, sizeof(float));
    if (!pcm)
        return NULL;
    tsf *voice = tsf_copy(font);
    if (!voice) {
        SDL_free(pcm);
        return NULL;
    }
    tsf_set_output(voice, TSF_STEREO_INTERLEAVED, 48000, -6);
    tsf_channel_set_presetindex(voice, 0, presets[effect >= 30]);
    tsf_channel_set_tuning(voice, 0, (float)(midi - key));
    if (!tsf_channel_note_on(voice, 0, key, 0.8f) || !tsf_active_voice_count(voice)) {
        tsf_close(voice);
        SDL_free(pcm);
        return NULL;
    }
    tsf_render_float(voice, pcm, (int)held, 0);
    tsf_channel_note_off(voice, 0, key);
    tsf_render_float(voice, pcm + held * 2, (int)release, 0);
    tsf_close(voice);
    /* Bound long SF2 releases without a discontinuity at the truncation. */
    for (size_t i = 0; i < 240; i++)
        for (int c = 0; c < 2; c++)
            pcm[(held + release - 240 + i) * 2 + c] *= (239 - i) / 240.0f;
    *frames = held + release;
    return pcm;
}
