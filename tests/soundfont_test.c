#undef NDEBUG
#include "audio/soundfont.h"
#include "soundfont_fixture.h"
#include <SDL3/SDL.h>
#include <assert.h>
static double pitch(const float *pcm, size_t n) {
    unsigned crossings = 0;
    for (size_t i = 101; i < n; i++)
        if (pcm[i * 2] >= 0 && pcm[(i - 1) * 2] < 0)
            ++crossings;
    return crossings * 48000.0 / (n - 100);
}
int main(void) {
    size_t frames = 999;
    assert(!SF_Render(20, 1, &frames) && frames == 0);
    soundfontFixture("test.sf2");
    assert(SF_Load("test.sf2") && SF_Count() == 2);
    assert(SF_Preset(0) == 0 && SF_Preset(1) == 1);
    assert(!strcmp(SF_Name(1), "Test Lute"));
    unsigned revision = SF_Revision();
    assert(!SF_Load("absent.sf2") && SF_Revision() == revision && SF_Count() == 2);
    FILE *f = fopen("invalid.sf2", "wb");
    assert(f);
    fputs("not a font", f);
    fclose(f);
    assert(!SF_Load("invalid.sf2") && SF_Count() == 2);
    for (int id = 20; id <= 38; id++) {
        size_t normal, slow;
        float *a = SF_Render(id, 1, &normal), *b = SF_Render(id, 0.5f, &slow);
        assert(a && b && slow > normal);
        double energy = 0;
        for (size_t i = 0; i < normal * 2; i++) {
            assert(isfinite(a[i]));
            energy += fabs(a[i]);
        }
        assert(energy > 1);
        assert(fabs(pitch(a, 3000) - pitch(b, 3000)) < 8); /* Speed must not transpose notes. */
        SDL_free(a);
        SDL_free(b);
    }
    SF_SetPreset(1, 0);
    assert(SF_Preset(1) == 0 && SF_Revision() > revision);
    SF_SetPreset(1, 999);
    assert(SF_Preset(1) == 0);
    SF_SetEnabled(false);
    assert(!SF_Render(20, 1, &frames));
    SF_SetEnabled(true);
    assert(!SF_Render(39, 1, &frames));
    assert(SF_Load("") && SF_Count() == 0);
    remove("test.sf2");
    remove("invalid.sf2");
    return 0;
}
