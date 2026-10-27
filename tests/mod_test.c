#include "mod/metadata.h"
#include "mod/interface.h"
#include "mod/location_names.h"
#undef NDEBUG
#include "common/common.h"
#include "common/file.h"
#include "mod/contents.h"
#include "mod/cutscenes.h"
#include "mod/creation_scene.h"
#include "mod/engine_tables.h"
#include "mod/narrative.h"
#include "mod/package.h"
#include "mod/runtime.h"
#include "mod/world_resources.h"
#include <assert.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

typedef struct Buffer {
    unsigned char bytes[131072];
    size_t size;
} Buffer;
static void bytes(Buffer *b, const void *p, size_t n) {
    assert(b->size + n <= sizeof(b->bytes));
    memcpy(b->bytes + b->size, p, n);
    b->size += n;
}
static void number(Buffer *b, uint32_t v) {
    unsigned char p[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16),
                          (unsigned char)(v >> 24)};
    bytes(b, p, 4);
}
static void header(Buffer *b, unsigned count) {
    b->size = 0;
    bytes(b, "IMOD0001", 8);
    number(b, 4);
    number(b, count);
    bytes(b, "Test", 4);
}
static void rawEntry(Buffer *b, const char *name, const void *base, size_t baseSize,
                     const void *changed, size_t changedSize) {
    number(b, (uint32_t)strlen(name));
    bytes(b, name, strlen(name));
    number(b, (uint32_t)baseSize);
    number(b, (uint32_t)changedSize);
    number(b, MOD_Crc32(base, baseSize));
    number(b, MOD_Crc32(changed, changedSize));
    number(b, 1);
    number(b, 0);
    number(b, (uint32_t)changedSize);
    bytes(b, changed, changedSize);
}
static void entry(Buffer *b, const char *name, const char *base, const char *changed) {
    rawEntry(b, name, base, strlen(base), changed, strlen(changed));
}
static void put(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(data, 1, size, f) == size);
    assert(!fclose(f));
}
static int readBase(void *context, const char *name, unsigned char **data, size_t *size) {
    (void)context;
    (void)name;
    *size = 4;
    *data = malloc(4);
    assert(*data);
    memcpy(*data, "base", 4);
    return 1;
}
static void value(const char *path, const char *expected, int useMod) {
    FILE *f = useMod ? FILE_Open(path, "rb") : fopen(path, "rb");
    assert(f);
    char b[32] = {0};
    assert(fread(b, 1, sizeof(b) - 1, f) == strlen(expected));
    assert(!strcmp(b, expected));
    fclose(f);
}
static void locationNames(void) {
    unsigned char names[LOCATION_NAMES_SIZE] = "ILOC0001";
    strcpy((char *)names + 8 + 2 * LOCATION_NAME_BYTES, "New Britain");
    strcpy((char *)names + 8 + 17 * LOCATION_NAME_BYTES, "Avatar's Castle");
    strcpy((char *)names + 8 + 33 * LOCATION_NAME_BYTES, "Abyss");
    assert(LOCATIONS_Validate(names, sizeof(names)));
    assert(!LOCATIONS_Name(names, sizeof(names), 1));
    assert(!LOCATIONS_Name(names, sizeof(names), 42));
    Buffer package;
    header(&package, 1);
    memcpy(package.bytes, "IMOD0017", 8);
    rawEntry(&package, "LOCATIONS.OVR", "", 0, names, sizeof(names));
    ModPackage decoded;
    char error[256];
    assert(MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
    MOD_Free(&decoded);
    memcpy(package.bytes, "IMOD0016", 8);
    assert(
        !MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
    memcpy(package.bytes, "IMOD0017", 8);
    put("application/Mods/locations.imperamod", package.bytes, package.size);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    assert(!strcmp(MOD_LocationName(2, "BRITAIN"), "New Britain"));
    assert(!strcmp(MOD_LocationName(17, NULL), "Avatar's Castle"));
    assert(!strcmp(MOD_LocationName(33, NULL), "Abyss"));
    assert(!strcmp(MOD_LocationName(1, "MOONGLOW"), "MOONGLOW"));
    assert(!MOD_LocationName(42, NULL));
    FILE_SetDataDirectory(NULL);
    assert(!MOD_LocationName(2, NULL));
    remove("application/Mods/locations.imperamod");
    names[8 + 63] = 'x';
    assert(!LOCATIONS_Validate(names, sizeof(names)));
    names[8 + 63] = 0;
    names[8] = 1;
    assert(!LOCATIONS_Validate(names, sizeof(names)));
    names[8] = 0;
    assert(!LOCATIONS_Validate(names, sizeof(names) - 1));
}
static void narrativeResources(void) {
    Buffer text = {0}, package;
    bytes(&text, "ITXT0001", 8);
    number(&text, 4);
    const char *ids[] = {"ending/page/1", "shop/D_3c10/0", "scene/blackthorn/0",
                         "scene/final rescue/0"};
    const char *messages[] = {"Extended ending text", "A reagent costs % gold",
                              "Extended interrogation", "Extended rescue"};
    for (unsigned i = 0; i < 4; ++i) {
        number(&text, (uint32_t)strlen(ids[i]));
        bytes(&text, ids[i], strlen(ids[i]));
        number(&text, (uint32_t)strlen(messages[i]) + 1);
        bytes(&text, messages[i], strlen(messages[i]) + 1);
    }
    int valid;
    assert(U5_FindNarrativeText(text.bytes, text.size, ids[0], &valid));
    assert(valid);
    for (size_t n=0;n<text.size;++n) {
        U5_FindNarrativeText(text.bytes,n,ids[0],&valid);assert(!valid);
    }
    header(&package, 1);
    memcpy(package.bytes, "IMOD0002", 8);
    rawEntry(&package, "TEXT.OVR", "", 0, text.bytes, text.size);
    put("application/Mods/20-narrative.imperamod", package.bytes, package.size);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    assert(!strcmp(MOD_NarrativeText("END.DAT", 0), messages[0]));
    assert(!strcmp(MOD_NarrativeText("SHOPPE.DAT", 0x1c1c), messages[1]));
    assert(!strcmp(MOD_SceneMessage("MISCMSG.DAT", 0, "old"), messages[2]));
    assert(!strcmp(MOD_SceneMessage("ENDMSG.DAT", 0, "old"), messages[3]));
    assert(!strcmp(MOD_SceneMessage("MISCMSG.DAT", 0x2d, "original"), "original"));
    assert(!MOD_NarrativeText("END.DAT", 0x1a8)); // Original fallback.
    assert(!MOD_NarrativeText("END.DAT", 1));     // Not a verified entry.
    header(&package, 1);
    memcpy(package.bytes, "IMOD0002", 8);
    rawEntry(&package, "TEXT.OVR", "", 0, "invalid", 7);
    put("application/Mods/21-invalid-narrative.imperamod", package.bytes, package.size);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    FILE_SetDataDirectory(NULL);
    assert(!MOD_NarrativeText("END.DAT", 0));
    remove("application/Mods/20-narrative.imperamod");
    remove("application/Mods/21-invalid-narrative.imperamod");
}
static void sceneResources(void) {
    unsigned char scene[1227] = {0};
    const unsigned char code[] = {6, 0, 12, 0, 0, 32, 5, 1, 3, 1, 9};
    memcpy(scene + 1216, code, sizeof(code));
    Buffer package;
    ModPackage decoded;
    char error[256];
    header(&package, 1);
    rawEntry(&package, "MISCMAPS.DAT", "base", 4, scene, sizeof(scene));
    assert(MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
    MOD_Free(&decoded);
    scene[1225] = 0; // A zero wait underflows the original interpreter.
    header(&package, 1);
    rawEntry(&package, "MISCMAPS.DAT", "base", 4, scene, sizeof(scene));
    assert(
        !MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
    assert(strstr(error, "creation"));
}
static void worldResources(void) {
    static unsigned char overlay[U5_WORLD_INDEX_OFFSET + 256], altered[sizeof(overlay)];
    static unsigned char map[65536], worlds[512], seed[256], brit[256], under[256];
    Buffer b;
    memset(map, 5, sizeof(map));
    memset(map + 256, 7, 256);
    memcpy(overlay + U5_WORLD_INDEX_OFFSET, U5_BritanniaDefaultIndex, 256);
    memcpy(altered, overlay, sizeof(overlay));
    memset(altered + U5_WORLD_INDEX_OFFSET, 255, 256);
    altered[U5_WORLD_INDEX_OFFSET] = 1;
    altered[U5_WORLD_INDEX_OFFSET + 255] = 0;
    put("game/DATA.OVL", overlay, sizeof(overlay));
    put("game/BRIT.DAT", map, sizeof(map));
    memset(seed, 0xab, 256); memset(brit, 0x23, 256); memset(under, 0x45, 256);
    put("game/INIT.OOL", seed, 256);
    put("game/BRIT.OOL", seed, 256); put("game/UNDER.OOL", seed, 256);
    put("SAVEGAME/BRIT.OOL", "old world", 9);
    put("SAVEGAME/UNDER.OOL", "old world", 9);
    header(&b, 1);rawEntry(&b, "DATA.OVL", overlay, sizeof(overlay), altered, sizeof(altered));
    put("application/Mods/10-index.imperamod", b.bytes, b.size);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    assert(MOD_BritanniaIndex()[0] == 1 && MOD_BritanniaIndex()[1] == 255 && MOD_BritanniaIndex()[255] == 0);
    assert(MOD_InitializeWorldObjects(worlds));
    for (unsigned i=0;i<512;++i) assert(worlds[i] == (i<256?0:0xab));
    value("SAVEGAME/BRIT.OOL", "old world", 0);
    /* A second package may supply the map paired with the earlier index. */
    header(&b, 1);rawEntry(&b, "BRIT.DAT", map, sizeof(map), map, 512);
    put("application/Mods/11-map.imperamod", b.bytes, b.size);
    header(&b, 2);
    rawEntry(&b, "BRIT.OOL", seed, 256, brit, 256);
    rawEntry(&b, "INIT.OOL", seed, 256, under, 256);
    put("application/Mods/12-objects.imperamod", b.bytes, b.size);
    FILE_SetDataDirectory("game");assert(MOD_LoadedCount()==3);
    assert(MOD_InitializeWorldObjects(worlds));
    assert(!memcmp(worlds,brit,256) && !memcmp(worlds+256,under,256));
    /* Explicit UNDER.OOL overrides INIT.OOL, regardless of existing save files. */
    header(&b, 1);rawEntry(&b, "UNDER.OOL", seed, 256, brit, 256);
    put("application/Mods/13-under.imperamod", b.bytes, b.size);
    FILE_SetDataDirectory("game");assert(MOD_LoadedCount()==4);
    assert(MOD_InitializeWorldObjects(worlds));assert(!memcmp(worlds+256,brit,256));
    remove("SAVEGAME/BRIT.OOL");remove("SAVEGAME/UNDER.OOL");
    assert(MOD_InitializeWorldObjects(worlds));assert(!memcmp(worlds,brit,256));
    /* Invalid object records reject their entire package. */
    remove("application/Mods/13-under.imperamod");
    header(&b, 2);rawEntry(&b, "UNDER.OOL", seed, 256, brit, 255);
    entry(&b,"STORY.DAT","base","partial");
    put("application/Mods/13-under.imperamod",b.bytes,b.size);
    FILE_SetDataDirectory("game");assert(MOD_LoadedCount()==3);value("STORY.DAT","base",1);
    remove("application/Mods/13-under.imperamod");remove("application/Mods/12-objects.imperamod");
    /* Invalid effective index/map pair rejects the map package atomically. */
    header(&b, 2);rawEntry(&b,"BRIT.DAT",map,sizeof(map),map,256);
    entry(&b,"STORY.DAT","base","partial");
    put("application/Mods/11-map.imperamod",b.bytes,b.size);
    FILE_SetDataDirectory("game");assert(MOD_LoadedCount()==1);value("STORY.DAT","base",1);
    remove("application/Mods/11-map.imperamod");
    header(&b, 1);rawEntry(&b,"DATA.OVL",overlay,sizeof(overlay),altered,256);
    put("application/Mods/10-index.imperamod",b.bytes,b.size);
    FILE_SetDataDirectory("game");assert(MOD_LoadedCount()==0);
    assert(!memcmp(MOD_BritanniaIndex(),U5_BritanniaDefaultIndex,256));
    remove("application/Mods/10-index.imperamod");
    /* Compiled defaults work without a base overlay. Failed initialization is atomic. */
    remove("game/DATA.OVL");FILE_SetDataDirectory("game");
    assert(!memcmp(MOD_BritanniaIndex(),U5_BritanniaDefaultIndex,256));
    memset(worlds,0xee,512);put("game/INIT.OOL",seed,255);
    assert(!MOD_InitializeWorldObjects(worlds));
    for(unsigned i=0;i<512;++i) assert(worlds[i]==0xee);
    FILE_SetDataDirectory(NULL);
    const char *files[]={"game/BRIT.DAT","game/INIT.OOL","game/BRIT.OOL","game/UNDER.OOL"};
    for(unsigned i=0;i<4;++i) remove(files[i]);
}
static void dungeonResources(void) {
    unsigned char base[4096], changed[4096], level[512], rooms[112 * 352], room[352];
    memset(base, 0xb0, sizeof(base));
    base[511] = 0x9a;
    memcpy(changed, base, sizeof(base));
    changed[1225] = 0x23;
    changed[1226] = 0xa5;
    memset(rooms, 0, sizeof(rooms));
    rooms[(16 + 5) * 352] = 7;
    put("game/DUNGEON.DAT", base, sizeof(base));
    put("game/DUNGEON.CBT", rooms, sizeof(rooms));
    Buffer package;
    header(&package, 1);
    rawEntry(&package, "DUNGEON.DAT", base, sizeof(base), changed, sizeof(changed));
    put("application/Mods/dungeon.imperamod", package.bytes, package.size);
    const char *exported = getenv("IMPERA_DUNGEON_TEST_PACKAGE");
    if (exported) {
        FILE *in = fopen(exported, "rb"), *out = fopen("application/Mods/dungeon.imperamod", "wb");
        assert(in && out);
        unsigned char bytes[4096];
        size_t count;
        while ((count = fread(bytes, 1, sizeof(bytes), in)))
            assert(fwrite(bytes, 1, count, out) == count);
        assert(!ferror(in));
        assert(!fclose(in));
        assert(!fclose(out));
    }
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    /* Same 512-byte block read as entering Destard in MAINOUT. */
    assert(!FILE_ReadFile("DUNGEON.DAT", level, 512, 2 * 512));
    assert(level[3 * 64 + 9] == 0x23 && level[3 * 64 + 10] == 0xa5);
    assert(!FILE_ReadFile("DUNGEON.CBT", room, 352, (16 + 5) * 352));
    assert(room[0] == 7);
    assert(!FILE_ReadFile("DUNGEON.DAT", level, 512, 0));
    assert(!memcmp(level, base, 512));
    remove("application/Mods/dungeon.imperamod");
    FILE_SetDataDirectory("game");
    assert(!FILE_ReadFile("DUNGEON.DAT", level, 512, 2 * 512));
    assert(!memcmp(level, base + 2 * 512, 512));
    FILE_SetDataDirectory(NULL);
    remove("game/DUNGEON.DAT");
    remove("game/DUNGEON.CBT");
}
static void engineTables(void) {
    unsigned char rules[50] = "IRULES01";
    rules[8] = 8;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned char *r = rules + 10 + i * 5;
        r[0] = i + 1;
        r[3] = i == 3 ? 12 : 42;
    }
    assert(U5_ValidateTables(rules, sizeof(rules)));
    Buffer b;
    ModPackage p;
    char error[256];
    header(&b, 1);
    memcpy(b.bytes, "IMOD0003", 8);
    rawEntry(&b, "RULES.TBL", NULL, 0, rules, sizeof(rules));
    assert(MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    MOD_Free(&p);
    put("application/Mods/01-rules.imperamod", b.bytes, b.size);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    for (unsigned id = 1; id <= 8; ++id) {
        assert(MOD_TableValue(id, 0, 123) == (id == 4 ? 12 : 42));
        assert(MOD_TableValue(id, 1, 123) == 123);
    }
    FILE_SetDataDirectory(NULL);
    assert(MOD_TableValue(1, 0, 123) == 123);
    remove("application/Mods/01-rules.imperamod");
    memcpy(b.bytes, "IMOD0002", 8);
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    /* A bad stock reference rejects the whole package, including unrelated entries. */
    rules[10 + 3 * 5 + 3] = 48;
    header(&b, 2);
    memcpy(b.bytes, "IMOD0003", 8);
    entry(&b, "INIT.GAM", "base", "partial");
    rawEntry(&b, "RULES.TBL", NULL, 0, rules, sizeof(rules));
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    assert(p.count == 0);
    put("application/Mods/01-rules.imperamod", b.bytes, b.size);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 0);
    assert(MOD_TableValue(4, 0, 123) == 123);
    FILE_SetDataDirectory(NULL);
    remove("application/Mods/01-rules.imperamod");
}
static void contentsResources(void)
{
    Buffer package;
    unsigned char resource[128] = {0};
    memcpy(resource, "ICONT001", 8);
    resource[8] = 1;
    CONTENTS_PutWord(resource + 24, 1);
    ContentsDef d = {0};
    d.id = 1;
    d.map = 5;
    d.x = 2;
    d.y = 2;
    d.original = 14;
    d.slot = 255;
    d.count = 1;
    d.refill = 2;
    d.items[0] = (ContentsItem){6, 0, 5};
    CONTENTS_EncodeRecord(resource + 32, &d);
    header(&package, 1);
    memcpy(package.bytes, "IMOD0004", 8);
    rawEntry(&package, "CONTENTS.OVR", NULL, 0, resource, sizeof(resource));
    put("application/Mods/contents-a.imperamod", package.bytes, package.size);
    resource[8] = 2;
    d.id = 2;
    d.map = 1;
    d.x = 1;
    d.y = 1;
    d.original = 65535;
    d.refill = 0;
    CONTENTS_EncodeRecord(resource + 32, &d);
    header(&package, 1);
    memcpy(package.bytes, "IMOD0004", 8);
    rawEntry(&package, "CONTENTS.OVR", NULL, 0, resource, sizeof(resource));
    put("application/Mods/contents-b.imperamod", package.bytes, package.size);
    resource[8] = 3;
    d.id = 3;
    d.map = 5;
    d.x = 2;
    d.y = 2;
    CONTENTS_EncodeRecord(resource + 32, &d);
    header(&package, 1);
    memcpy(package.bytes, "IMOD0004", 8);
    rawEntry(&package, "CONTENTS.OVR", NULL, 0, resource, sizeof(resource));
    put("application/Mods/contents-c-conflict.imperamod", package.bytes, package.size);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 2 && CONTENTS_Count() == 2);
    assert(CONTENTS_Find(0, 5, 0, 2, 2)->items[0].quantity == 5);
    assert(CONTENTS_Find(0, 1, 0, 1, 1));
    ModPackage decoded = {0};
    char error[256];
    memcpy(package.bytes, "IMOD0005", 8);
    assert(MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
    MOD_Free(&decoded);
    memcpy(package.bytes, "IMOD0003", 8);
    assert(
        !MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
    FILE_SetDataDirectory(NULL);
    assert(CONTENTS_Count() == 0);
    remove("application/Mods/contents-a.imperamod");
    remove("application/Mods/contents-b.imperamod");
    remove("application/Mods/contents-c-conflict.imperamod");
}
static void cutsceneResources(void) {
    Buffer data={0}, package;
    bytes(&data,"ICUT0001",8); number(&data,2); number(&data,1);
    const char *texts[]={"New first page","New second page","New ending"};
    for(unsigned i=0;i<3;i++) { number(&data,i); number(&data,(unsigned)strlen(texts[i])+1); bytes(&data,texts[i],strlen(texts[i])+1); }
    assert(U5_ValidateCutscenes(data.bytes,data.size));
    for(size_t n=0;n<data.size;n++) assert(!U5_ValidateCutscenes(data.bytes,n));
    header(&package,1); memcpy(package.bytes,"IMOD0006",8);
    rawEntry(&package,"CUTSCENE.OVR",NULL,0,data.bytes,data.size);
    ModPackage decoded={0}; char error[256];
    assert(MOD_Decode(package.bytes,package.size,readBase,NULL,&decoded,error,sizeof(error)));
    MOD_Free(&decoded);
    memcpy(package.bytes,"IMOD0005",8);
    assert(!MOD_Decode(package.bytes,package.size,readBase,NULL,&decoded,error,sizeof(error)));
    memcpy(package.bytes,"IMOD0006",8);
    put("application/Mods/cutscenes.imperamod",package.bytes,package.size);
    FILE_SetDataDirectory("game");
    assert(MOD_CutsceneCount(0)==2 && MOD_CutsceneCount(1)==1);
    unsigned layout=0;
    assert(!strcmp(MOD_CutscenePage(0,1,&layout),texts[1]) && layout==1);
    assert(!strcmp(MOD_CutscenePage(1,0,&layout),texts[2]) && layout==2);
    assert(!MOD_CutscenePage(1,1,&layout));
    FILE_SetDataDirectory(NULL); assert(!MOD_CutsceneCount(0));
    remove("application/Mods/cutscenes.imperamod");
    data.bytes[16]=21; assert(!U5_ValidateCutscenes(data.bytes,data.size));
    Buffer custom={0};
    bytes(&custom,"ICUT0002",8); number(&custom,1);number(&custom,0);
    number(&custom,0);number(&custom,6);
    const unsigned coordinates[]={1,30,40,0,320,0,320,200,200,10,20,180};
    for(unsigned i=0;i<12;i++) number(&custom,coordinates[i]);
    bytes(&custom,"Hello",6);
    assert(U5_ValidateCutscenes(custom.bytes,custom.size));
    for(size_t n=0;n<custom.size;n++) assert(!U5_ValidateCutscenes(custom.bytes,n));
    header(&package,1);memcpy(package.bytes,"IMOD0007",8);
    rawEntry(&package,"CUTSCENE.OVR",NULL,0,custom.bytes,custom.size);
    assert(MOD_Decode(package.bytes,package.size,readBase,NULL,&decoded,error,sizeof(error)));
    MOD_Free(&decoded);
    memcpy(package.bytes,"IMOD0006",8);
    assert(!MOD_Decode(package.bytes,package.size,readBase,NULL,&decoded,error,sizeof(error)));
    memcpy(package.bytes,"IMOD0007",8);
    put("application/Mods/cutscenes.imperamod",package.bytes,package.size);
    FILE_SetDataDirectory("game");
    U5CutscenePlacement position;
    MOD_CutscenePlacement(0,0,&position);
    assert(position.custom && position.imageX==30 && position.imageY==40 && position.x==10 && position.y==20);
    assert(position.right[0]==320 && position.secondY==180);
    FILE_SetDataDirectory(NULL); MOD_CutscenePlacement(0,0,&position);assert(!position.custom);
    remove("application/Mods/cutscenes.imperamod");
    memset(custom.bytes+24+4*4,0,4); /* A zero-width text area is invalid. */
    assert(!U5_ValidateCutscenes(custom.bytes,custom.size));
}
static void compositionResources(void) {
    Buffer data={0},package;
    bytes(&data,"ICUT0003",8);number(&data,1);number(&data,0);number(&data,1);
    number(&data,2);number(&data,1);number(&data,3);bytes(&data,"PNG",3);
    const unsigned char pixels[]={255,0,0,128,0,0,0,255};bytes(&data,pixels,sizeof(pixels));
    number(&data,1);number(&data,1);number(&data,0);number(&data,319);number(&data,199);
    const unsigned fields[]={1,2,100,30,0,0,0,0,0,0,0,0,0,6};
    for(unsigned i=0;i<14;i++)number(&data,fields[i]);bytes(&data,"Hello",6);
    assert(U5_ValidateCutscenes(data.bytes,data.size));
    for(size_t n=0;n<data.size;n++)assert(!U5_ValidateCutscenes(data.bytes,n));
    header(&package,1);memcpy(package.bytes,"IMOD0008",8);rawEntry(&package,"CUTSCENE.OVR",NULL,0,data.bytes,data.size);
    ModPackage decoded={0};char error[256];
    assert(MOD_Decode(package.bytes,package.size,readBase,NULL,&decoded,error,sizeof(error)));MOD_Free(&decoded);
    memcpy(package.bytes,"IMOD0007",8);assert(!MOD_Decode(package.bytes,package.size,readBase,NULL,&decoded,error,sizeof(error)));
    memcpy(package.bytes,"IMOD0008",8);put("application/Mods/composition.imperamod",package.bytes,package.size);
    FILE_SetDataDirectory("game");size_t size=0;const void *composition=MOD_CutsceneComposition(0,&size);
    assert(composition&&size==data.size&&MOD_CutsceneCount(0)==1&&!MOD_CutsceneComposition(1,&size));
    FILE_SetDataDirectory(NULL);remove("application/Mods/composition.imperamod");
}
static void interfaceColors(void) {
    Buffer package;
    ModPackage decoded;
    char error[256];
    unsigned char settings[9] = "IUI00001";
    assert(MOD_FrameColor() == 1);
    for (unsigned color = 0; color < 16; ++color) {
        settings[8] = color;
        assert(INTERFACE_Validate(settings, sizeof(settings)));
        header(&package, 1);
        memcpy(package.bytes, "IMOD0026", 8);
        rawEntry(&package, "INTERFACE.OVR", "", 0, settings, sizeof(settings));
        assert(MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
        MOD_Free(&decoded);
        put("application/Mods/interface.imperamod", package.bytes, package.size);
        FILE_SetDataDirectory("game");
        assert(MOD_FrameColor() == color);
        FILE_SetDataDirectory(NULL);
        assert(MOD_FrameColor() == 1);
        remove("application/Mods/interface.imperamod");
        memcpy(package.bytes, "IMOD0025", 8);
        assert(!MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
    }
    settings[8] = 16;
    assert(!INTERFACE_Validate(settings, sizeof(settings)));
    header(&package, 1); memcpy(package.bytes, "IMOD0026", 8);
    rawEntry(&package, "INTERFACE.OVR", "", 0, settings, sizeof(settings));
    assert(!MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
    settings[8] = 1; settings[0] = 'x';
    assert(!INTERFACE_Validate(settings, sizeof(settings)));
    assert(!INTERFACE_Validate(settings, 8));
}

static void modCatalog(void) {
    const char *description = "Explore a new Britannia.";
    unsigned char metadata[128] = "IMETA001";
    metadata[8] = (unsigned char)strlen(description);
    memcpy(metadata + 16, description, strlen(description));
    size_t metadataSize = 16 + strlen(description);
    assert(MODINFO_Validate(metadata, metadataSize));
    metadata[16] = 0xc0; assert(!MODINFO_Validate(metadata, metadataSize)); metadata[16] = 'E';
    assert(!MODINFO_Validate(metadata, metadataSize - 1));
    ModPackage decoded; char error[256]; Buffer package;
    header(&package, 1); memcpy(package.bytes, "IMOD0027", 8);
    rawEntry(&package, "MODINFO.DAT", "", 0, metadata, metadataSize);
    assert(MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
    MOD_Free(&decoded);
    memcpy(package.bytes, "IMOD0026", 8);
    assert(!MOD_Decode(package.bytes, package.size, readBase, NULL, &decoded, error, sizeof(error)));
    unsigned char by[128]="IMETA002"; by[8]=1; by[16]=11;
    memcpy(by+20,"XTest Author",12);
    assert(MODINFO_Validate(by,32));
    by[21]='\n'; assert(!MODINFO_Validate(by,32)); by[21]='T';
    header(&package,1); memcpy(package.bytes,"IMOD0027",8);
    rawEntry(&package,"MODINFO.DAT","",0,by,32);
    assert(!MOD_Decode(package.bytes,package.size,readBase,NULL,&decoded,error,sizeof(error)));
    const char *names[] = {"FIRST.DAT", "SECOND.DAT"};
    const char *paths[] = {"application/Mods/a.imperamod", "application/Mods/b.imperamod"};
    put("game/FIRST.DAT", "base", 4); put("game/SECOND.DAT", "base", 4);
    for (int i = 0; i < 2; ++i) {
        header(&package, 2); memcpy(package.bytes, i ? "IMOD0027" : "IMOD0028", 8);
        rawEntry(&package, "MODINFO.DAT", "", 0, i ? metadata : by, i ? metadataSize : 32);
        entry(&package, names[i], "base", "modded");
        put(paths[i], package.bytes, package.size);
    }
    put("application/Mods/c.imperamod", "invalid", 7);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 2 && MOD_AvailableCount() == 3);
    assert(!MOD_Available(3));
    assert(!strcmp(MOD_Available(0)->description,"X") && !strcmp(MOD_Available(0)->author,"Test Author"));
    assert(!*MOD_Available(1)->author && !strcmp(MOD_Available(1)->description,description));
    assert(MOD_Available(0)->active && MOD_Available(1)->active);
    assert(*MOD_Available(2)->error && !MOD_Available(2)->active);
    assert(SDL_CreateDirectory("application/Mods/mods.cfg.tmp"));
    assert(!MOD_SetEnabled(0, 0)); /* Failed save cannot alter selected or active mods. */
    assert(MOD_Available(0)->enabled && !MOD_ChangesPending());
    assert(SDL_RemovePath("application/Mods/mods.cfg.tmp"));
    unsigned revision = MOD_Revision();
    assert(MOD_SetEnabled(0, 0));
    assert(MOD_ChangesPending() && MOD_LoadedCount() == 2 && MOD_Revision() == revision);
    value("FIRST.DAT", "modded", 1); /* Active game keeps its snapshot. */
    FILE_ReloadMods();
    assert(!MOD_ChangesPending() && MOD_LoadedCount() == 1 && MOD_Revision() != revision);
    assert(!MOD_Available(0)->enabled && !MOD_Available(0)->active);
    assert(MOD_Available(1)->enabled && MOD_Available(1)->active);
    assert(!strcmp(MOD_Available(0)->description,"X") && !strcmp(MOD_Available(0)->author,"Test Author"));
    assert(!*MOD_Available(1)->author && !strcmp(MOD_Available(1)->description,description));
    value("FIRST.DAT", "base", 1);
    assert(MOD_SetEnabled(0, 1)); FILE_ReloadMods();
    assert(MOD_LoadedCount() == 2 && MOD_Available(0)->active);
    assert(!MOD_SetEnabled(99, 0));
    FILE_SetDataDirectory(NULL);
    assert(MOD_AvailableCount() == 0 && !MOD_ChangesPending());
    remove(paths[0]); remove(paths[1]); remove("application/Mods/c.imperamod");
    remove("application/Mods/mods.cfg"); remove("game/FIRST.DAT"); remove("game/SECOND.DAT");
}

int main(void) {
    assert(SDL_CreateDirectory("application/Mods"));
    assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "APPIMAGE", "application/Impera.AppImage", true));
    modCatalog();
    interfaceColors();
    assert(MOD_AllowedResource("INIT.GAM"));
    assert(MOD_AllowedResource("BRIT.CBT"));
    assert(!MOD_AllowedResource("../INIT.GAM"));
    assert(!MOD_AllowedResource("ENGINE.CFG"));
    assert(!MOD_AllowedResource("SAVED.GAM"));
    assert(!MOD_AllowedResource("ULTIMA.EXE"));
    assert(MOD_Crc32("123456789", 9) == 0xcbf43926u);
    Buffer b;
    header(&b, 1);
    entry(&b, "INIT.GAM", "base", "modded");
    ModPackage p;
    char error[256];
    assert(MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    assert(p.count == 1 && p.entries[0].size == 6 && !memcmp(p.entries[0].data, "modded", 6));
    MOD_Free(&p);
    for (size_t n = 0; n < b.size; n++) {
        assert(!MOD_Decode(b.bytes, n, readBase, NULL, &p, error, sizeof(error)));
        assert(p.count == 0 && error[0]);
    }
    b.bytes[b.size - 1] ^= 1;
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    b.bytes[b.size - 1] ^= 1;
    header(&b, 2);
    entry(&b, "INIT.GAM", "base", "good");
    entry(&b, "STORY.DAT", "xxxx", "bad");
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    assert(!p.count);
    header(&b, 2);
    entry(&b, "INIT.GAM", "base", "good");
    entry(&b, "INIT.GAM", "base", "bad");
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    header(&b, 1);
    entry(&b, "../INIT.GAM", "base", "good");
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    header(&b, 1);
    entry(&b, "INIT.GAM", "base", "grow");
    unsigned char saved = b.bytes[b.size - 12];
    b.bytes[b.size - 12] = 255;
    assert(!MOD_Decode(b.bytes, b.size, readBase, NULL, &p, error, sizeof(error)));
    b.bytes[b.size - 12] = saved;
    put("game/init.gam", "base", 4);
    put("game/story.dat", "base", 4);
    header(&b, 1);
    entry(&b, "INIT.GAM", "base", "first");
    put("application/Mods/01-first.imperamod", b.bytes, b.size);
    header(&b, 1);
    entry(&b, "INIT.GAM", "base", "second");
    put("application/Mods/02-conflict.imperamod", b.bytes, b.size);
    header(&b, 2);
    entry(&b, "INIT.GAM", "base", "third");
    entry(&b, "STORY.DAT", "base", "partial");
    put("application/Mods/03-paired-conflict.imperamod", b.bytes, b.size);
    header(&b, 1);
    entry(&b, "STORY.DAT", "wrong", "bad");
    put("application/Mods/04-invalid.IMPERAMOD", b.bytes, b.size);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    value("INIT.GAM", "first", 1);
    value("init.gam", "first", 1);
    value("STORY.DAT", "base", 1);
    value("game/init.gam", "base", 0);
    /* Relative writable saves and explicit paths never go through overlays. */
    put("SAVEGAME/INIT.GAM", "saved", 5);
    value("SAVEGAME/INIT.GAM", "saved", 1);
    value("game/init.gam", "base", 1);
    FILE *f = FILE_Open("INIT.GAM", "wb");
    assert(f);
    assert(fputs("local", f) >= 0);
    fclose(f);
    value("INIT.GAM", "first", 1);
    value("INIT.GAM", "local", 0);
    value("game/init.gam", "base", 0);
    FILE_SetDataDirectory(NULL);
    assert(MOD_LoadedCount() == 0);
    value("INIT.GAM", "local", 1);
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    remove("application/Mods/01-first.imperamod");
    FILE_SetDataDirectory("game");
    assert(MOD_LoadedCount() == 1);
    value("INIT.GAM", "second", 1);
    value("STORY.DAT", "base", 1);
    FILE_SetDataDirectory(NULL);
    remove("INIT.GAM");
    remove("application/Mods/02-conflict.imperamod");
    remove("application/Mods/03-paired-conflict.imperamod");
    remove("application/Mods/04-invalid.IMPERAMOD");
    remove("SAVEGAME/INIT.GAM");
    worldResources();
    dungeonResources();
    locationNames();
    narrativeResources();
    sceneResources();
    engineTables();
    contentsResources();
    cutsceneResources();
    compositionResources();
    puts("Mod package validation, atomic mounting, order, conflicts, reloads and original preservation "
         "passed");
    return 0;
}
