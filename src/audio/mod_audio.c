#include "mod_audio.h"
#include "mod/runtime.h"
#include <stdlib.h>
static unsigned revision = ~0u, count, assignments;
static unsigned char *bytes;
static const unsigned char *routes;
static ModAudioClip clips[1024];
void AUDIO_MOD_Load(void) {
    if (revision == MOD_Revision())
        return;
    revision = MOD_Revision();
    free(bytes);
    bytes = NULL;
    count = assignments = 0;
    routes = NULL;
    int found;
    FILE *f = MOD_OpenResource("AUDIO.OVR", &found);
    if (!f)
        return;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return;
    }
    long n = ftell(f);
    if (n < 24 || n > MOD_MAX_AUDIO || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return;
    }
    unsigned char *b = malloc(n);
    if (!b) {
        fclose(f);
        return;
    }
    int valid = fread(b, 1, n, f) == (size_t)n && MA_Validate(b, n);
    fclose(f);
    if (!valid) {
        free(b);
        return;
    }
    bytes = b;
    count = MA_U32(b + 8);
    assignments = MA_U32(b + 12);
    size_t at = 24;
    for (unsigned i = 0; i < count; i++)
        at = MA_Clip(b, at, clips + i);
    routes = b + at;
}
const ModAudioClip *AUDIO_MOD_Clip(unsigned kind, unsigned id) {
    AUDIO_MOD_Load();
    for (unsigned i = 0; i < count; i++)
        if (clips[i].kind == kind && clips[i].id == id)
            return clips + i;
    return NULL;
}
unsigned AUDIO_MOD_Assignment(unsigned scope, unsigned key, unsigned level, unsigned fallback) {
    AUDIO_MOD_Load();
    for (unsigned i = 0; i < assignments; i++) {
        const unsigned char *a = routes + i * 16;
        if (MA_U32(a) == scope && MA_U32(a + 4) == key && MA_U32(a + 8) == level)
            return MA_U32(a + 12);
    }
    return fallback;
}

int AUDIO_MOD_HasMusic(void) {
    AUDIO_MOD_Load();
    for (unsigned i = 0; i < count; i++)
        if (!clips[i].kind && clips[i].size)
            return 1;
    return 0;
}
