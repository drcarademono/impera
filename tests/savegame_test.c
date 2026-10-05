#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "common/common.h"
#include "common/settings.h"
#include "savegame.h"
#include "vars.h"
#include "npc.h"
#include "funcs.h"

int main(int argc, char** argv)
{
    char value[3];
    FILE* stream;
    int i;
    static const char* npcFiles[] = { "TOWNE.NPC", "DWELLING.NPC", "CASTLE.NPC", "KEEP.NPC" };
    static const int maps[] = { 1, 13, 17, 25 };

    if (argc > 1 && strcmp(argv[1], "--missing-required") == 0)
    {
        unsigned char buffer[32];
        DEBUG_Initialize();
        ULTIMA_256e_ReadFileFromDisk("missing-required.dat", buffer, sizeof(buffer), 0);
        return 0;
    }

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

    /* Entering a town reloads NPC schedules. Use uppercase-only original assets
     * to catch filename case errors, including Iolo's Hut (map 13). */
    for (i = 0; i < 4; i++)
    {
        unsigned char expected[0x240];
        int npc;
        int offset = ((maps[i] - 1) % 8) * 0x240;
        stream = fopen(npcFiles[i], "rb");
        assert(stream);
        assert(fseek(stream, offset, SEEK_SET) == 0);
        assert(fread(expected, 1, sizeof(expected), stream) == sizeof(expected));
        fclose(stream);
        D_5893_map_id = maps[i];
        NPC_0000_LoadNpcFile();
        assert(D_5893_map_id == maps[i]);
        assert(memcmp(D_5d5e, expected, 0x200) == 0);
        assert(memcmp(D_659e, expected + 0x200, 0x20) == 0);
        for (npc = 0; npc < 32; npc++)
            assert(D_5f5e[npc]._a == expected[0x220 + npc]);
    }

    puts("Settings, save/load, and NPC town-entry tests passed.");
    return 0;
}
