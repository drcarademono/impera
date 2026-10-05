#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "common/common.h"
#include "common/settings.h"
#include "savegame.h"
#include "vars.h"

int main(void)
{
    char value[3];
    FILE* stream;
    int i;

    errno = ERANGE;
    assert(SETTINGS_GetInt("window", "width", 1280) == 1280);
    assert(SETTINGS_GetInt("window", "height", 960) == 960);
    SETTINGS_GetString("test", "key", "abcd", value, sizeof(value));
    assert(strcmp(value, "ab") == 0);

    D_55a6 = 1234;
    strcpy(D_55a8_party[0].name, "Test");
    D_55a8_party[0].hp = 100;
    D_55a8_party[0].maxHp = 100;
    assert(FILE_ReadSavegameFile("missing.gam") == -1);
    assert(D_55a6 == 1234 && D_55a8_party[0].hp == 100);

    stream = fopen("truncated.gam", "wb");
    assert(stream);
    for (i = 0; i < 0x105f; i++)
        assert(fputc(0, stream) != EOF);
    assert(fclose(stream) == 0);
    assert(FILE_ReadSavegameFile("truncated.gam") == -1);
    assert(D_55a6 == 1234 && D_55a8_party[0].hp == 100);
    assert(strcmp(D_55a8_party[0].name, "Test") == 0);
    remove("truncated.gam");

    assert(FILE_WriteSavegameFile("SAVED.GAM") == 0);
    stream = fopen("SAVEGAME/SAVED.GAM", "rb");
    assert(stream);
    assert(fseek(stream, 0, SEEK_END) == 0);
    assert(ftell(stream) == 0x1060);
    fclose(stream);
    D_55a6 = 0;
    memset(D_55a8_party, 0, sizeof(S_55a8) * 16);
    assert(FILE_ReadSavegameFile("SAVED.GAM") == 0);
    assert(D_55a6 == 1234 && D_55a8_party[0].hp == 100);
    assert(strcmp(D_55a8_party[0].name, "Test") == 0);
    remove("SAVEGAME/SAVED.GAM");

    puts("Settings, rejected input, and save/load tests passed.");
    return 0;
}
