#include "funcs.h"
#include "mod/contents.h"
#include "mod/actors.h"
#include "comsubs.h"
#include "combat.h"
#include "mod/package.h"
#include "mod/types.h"
#include "mod/types_game.h"
#include "savegame.h"
#include "tiles.h"
static void customTypeGame(void) {
    const unsigned recordSize = 256 + 4 + 32 * 32 * 4, size = 32 + recordSize * 4;
    unsigned char *defs = calloc(1, size);
    assert(defs);
    memcpy(defs, "ITYPES01", 8);
    defs[8] = 17;
    TYPES_Put32(defs + 24, 4);
    for (unsigned i = 0; i < 4; i++) {
        TypeDef d;
        TYPES_Vanilla(i < 2 ? 0 : 1, i < 2 ? (i ? 0x44 : 0xb8) : 144, &d);
        d.id = 40000 + i;
        d.frames = 1;
        d.ground = 5;
        d.render = TYPES_SPRITE | TYPES_MASONRY;
        if (i < 2) {
            d.result[0] = 40001;
            d.result[1] = 40000;
        } else {
            d.kind = 2;
            d.stats[5] = i == 2 ? 30 : 90;
            d.stats[6] = 1;
            d.actorFlags = i == 2 ? 0x410 : 0x2000;
            d.result[1] = d.result[2] = d.result[3] = 40003;
        }
        strcpy(d.name, i ? "Open crystal door" : "Closed crystal door");
        unsigned char *r = defs + 32 + i * recordSize;
        TYPES_Encode(r, &d);
        TYPES_Put32(r, recordSize);
        r[257] = 32;
        r[258] = 100;
        for (unsigned p = 0; p < 32 * 32; p++) {
            r[260 + p * 4] = p % 32;
            r[261 + p * 4] = p / 32;
            r[262 + p * 4] = 200;
            r[263 + p * 4] = 255;
        }
        /* Opaque black and a transparent pixel are deliberately distinct. */
        memset(r + 260, 0, 4);
        r[263] = 255;
        memset(r + 264, 0, 4);
    }
    assert(TYPES_Validate(defs, size));
    unsigned char map[48] = {0};
    memcpy(map, "ITYMAP01", 8);
    map[8] = 17;
    TYPES_Put32(map + 24, 1);
    TypeCell c = {0, 13, 0, 10, 11, 40000};
    TYPES_CellEncode(map + 32, &c);
    assert(TYPES_ValidateMap(map, 48, defs, size));
    byte oldMap = D_5893_map_id, oldFloor = D_5895_map_level, oldX = D_5896_map_x,
         oldY = D_5897_map_y;
    unsigned char base[1024];
    memcpy(base, D_6608, 1024);
    D_5893_map_id = 13;
    D_5895_map_level = 0;
    D_5896_map_x = 10;
    D_5897_map_y = 11;
    memset(D_6608, 5, 1024);
    TYPES_Mount(defs, size, map, 48);
    assert(TYPES_GAME_At(10, 11) == 40000);
    assert(ACTORS_Stats(40002).maxHp == 30 && ACTORS_Stats(40003).maxHp == 90);
    assert(ACTORS_Def(40002).base == ACTORS_Def(40003).base);
    assert(GRAP_BUF_SpritePixel(40002, -1, 0) == -1);
    assert(GRAP_BUF_SpritePixel(40002, 16, 0) == -1);
    assert((ACTORS_Flags(40002) & 0x410) == 0x410 && ACTORS_Flags(40003) == 0x2000);
    assert(ACTORS_Target(40002, 1, 0x26) == 40003);
    assert(ACTORS_Target(40002, 2, 0) == 40003);
    TypeCell npcType = {3, 13, 0, 5, 0, 40002, 0};
    assert(TYPES_Set(npcType, 40002));
    NpcFmt oldNpc = D_5f5e[5];
    ActorFmt oldTypedActor = D_5c5a[12];
    D_5f5e[5]._0 = 1;
    D_5f5e[5]._c = 12;
    D_5c5a[12]._0_tile = D_5c5a[12]._1_animTile = 144;
    assert(ACTORS_Instance(12) == 40002);
    assert(ACTORS_Sprite(12, 400) == 40002);
    D_5f5e[5] = oldNpc;
    D_5c5a[12] = oldTypedActor;

    assert(*ULTIMA_4402_GetTileAddr(10, 11) == 0xb8);
    assert(!TYPES_GAME_Walkable(0x1c, 10, 11));
    assert(TYPES_GAME_Property(10, 11, 2, 0));
    TYPES_GAME_Change(10, 11, 0, 0x44);
    assert(TYPES_GAME_At(10, 11) == 40001);
    assert(TYPES_GAME_Walkable(0x1c, 10, 11));
    TYPES_GAME_Change(10, 11, 1, 0xb8);
    assert(TYPES_GAME_At(10, 11) == 40000);
    assert(TILESET_Select(TILESET_DOS));
    unsigned char pixel = 0;
    TILESET_Register(&pixel, 1);
    int color = GRAP_BUF_TilePixel(40000, 0, 0);
    assert(color == 16);
    assert(TILESET_LastColor(color, 0, 0, ega) == 0xff000000u);
    assert(TILESET_LastSpriteColor(color, 0, 0, ega, true) == 0);
    assert(TILESET_LastSpriteColor(color, 0, 1, ega, true) >> 24);
    TILESET_RecordSprite(&pixel, color, true);
    pixel = color;
    assert(TILESET_BufferColor(&pixel, 0, 0, ega) == 0xff000000u);
    assert(TILESET_BufferColor(&pixel, 1, 0, ega) == ega[0]);
    color = GRAP_BUF_TilePixel(40000, 1, 1);
    TILESET_Record(&pixel, color);
    pixel = color;
    assert(TILESET_BufferColor(&pixel, 0, 0, ega) == 0xff0202c8u);
    assert(TILESET_BufferColor(&pixel, 1, 1, ega) == 0xff0303c8u);
    TILESET_Unregister(&pixel);
    /* Loading a library must not make ordinary vanilla terrain persistent. */
    TYPES_GAME_Write(20, 20, 0x44);
    TypeCell ordinary = TYPES_GAME_Cell(20, 20), ordinaryState;
    assert(!TYPES_State(ordinary, &ordinaryState));
    assert(*ULTIMA_4402_GetTileAddr(20, 20) == 0x44);
    assert(FILE_WriteSavegameFile("custom-types.gam") == 0);
    TYPES_GAME_Write(10, 11, 5);
    assert(TYPES_GAME_At(10, 11) == 5);
    assert(FILE_ReadSavegameFile("custom-types.gam") == 0);
    assert(TYPES_GAME_At(10, 11) == 40000);
    TypeCell loadedNpc;
    assert(TYPES_State(npcType, &loadedNpc) && loadedNpc.type == 40002);
    remove("custom-types.gam");
    /* Vanilla doors temporarily become floor; the original custom type must
     * survive save/load even without a separately authored open-door tile. */
    TYPES_Put32(defs + 32 + 24, 0x44);
    TYPES_GAME_Change(10, 11, 0, 0x44);
    assert(TYPES_GAME_At(10, 11) == 0x44);
    assert(FILE_WriteSavegameFile("custom-open-door.gam") == 0);
    TYPES_GAME_Change(10, 11, 1, 0xb8);
    assert(TYPES_GAME_At(10, 11) == 40000);
    assert(FILE_ReadSavegameFile("custom-open-door.gam") == 0);
    assert(TYPES_GAME_At(10, 11) == 0x44);
    TYPES_GAME_Change(10, 11, 1, 0xb8);
    assert(TYPES_GAME_At(10, 11) == 40000);
    remove("custom-open-door.gam");
    /* Combat uses the same authored damage range, not the proxy terrain byte. */
    TypeCell combat = {1, 0, 0, 2, 3, 40001, 0};
    CONTENTS_BeginEncounter(1, 0);
    assert(TYPES_Set(combat, 40001));
    assert(TYPES_State(npcType, &loadedNpc) && loadedNpc.type == 40002);

    defs[32 + recordSize + 17] = TYPES_BURN;
    defs[32 + recordSize + 18] = 2;
    defs[32 + recordSize + 19] = 2;
    CombatEntity oldCombat = D_ba14[0];
    ActorFmt oldActor = D_5c5a[0];
    S_55a8 oldParty = D_55a8_party[0];
    TextWindow oldWindows[4];
    memcpy(oldWindows, D_535e_textWindows, sizeof(oldWindows));
    int oldAdvance = D_538e;
    D_538e = 1;
    for (int i = 0; i < 4; i++)
        D_535e_textWindows[i] = (TextWindow){0, 0, 39, 24, 0, 0, 15, 0};
    byte *oldCharset = D_5398_currentCharset;
    TextWindow *oldWindow = D_539a_currentTextWindow;
    byte charset[1024] = {0};
    D_5398_currentCharset = charset;
    D_539a_currentTextWindow = &D_535e_textWindows[2];
    byte sound = D_a9ce;
    D_a9ce = 0;
    D_5893_map_id = 128;
    D_ba14[0] = (CombatEntity){0};
    D_ba14[0].flags = COMBAT_FLAGS_PLAYER;
    D_ba14[0].x = 2;
    D_ba14[0].y = 3;
    D_5c5a[0]._0_tile = TILE_ACTOR_AVATAR;
    D_55a8_party[0].hp = 100;
    D_55a8_party[0].status = STATUS_GOOD;
    assert(TYPES_GAME_CombatEffects(0));
    assert(D_55a8_party[0].hp == 98);
    D_ba14[0] = oldCombat;
    D_5c5a[0] = oldActor;
    D_55a8_party[0] = oldParty;
    D_a9ce = sound;
    D_538e = oldAdvance;
    D_5398_currentCharset = oldCharset;
    D_539a_currentTextWindow = oldWindow;
    memcpy(D_535e_textWindows, oldWindows, sizeof(oldWindows));

    CombatEntity oldEntities[32];
    ActorFmt oldActors[32];
    memcpy(oldEntities, D_ba14, sizeof(oldEntities));
    memcpy(oldActors, D_5c5a, sizeof(oldActors));
    memset(D_ba14, 0, sizeof(oldEntities));
    memset(D_5c5a, 0, sizeof(oldActors));
    D_5893_map_id = 128;
    int first = ULTIMA_6506(40002, 0, 2, 2, 0);
    int second = ULTIMA_6506(40003, 0, 3, 2, 0);
    assert(first >= 6 && second >= 6 && first != second);
    assert(D_ba14[first].entityIdx == 40002 && D_ba14[first].hp == 30);
    assert(D_ba14[second].entityIdx == 40003 && D_ba14[second].hp == 90);
    assert(ACTORS_Instance(D_ba14[first].actorIdx) == 40002);
    byte savedCombatMap[352]; memcpy(savedCombatMap, D_ad14, sizeof(savedCombatMap));
    memset(D_ad14, 0x44, sizeof(savedCombatMap));
    D_538e = 1;
    for (int i = 0; i < 4; i++) D_535e_textWindows[i] = (TextWindow){0,0,39,24,0,0,15,0};
    D_5398_currentCharset = charset; D_539a_currentTextWindow = &D_535e_textWindows[2];
    D_a9ce = 0;
    D_ba14[first].x = D_5c5a[D_ba14[first].actorIdx]._2_x = 5;
    D_ba14[first].y = D_5c5a[D_ba14[first].actorIdx]._3_y = 5;
    COMBAT_1574(first, 1);
    int divided = 0;
    for (int i = 6; i < 32; i++) if (i != second && D_ba14[i].entityIdx == 40003 && D_ba14[i].flags) divided++;
    assert(divided > 0);
    byte oldNegation = D_587a; D_587a = 'A';
    ULTIMA_207e_srand(1234);
    int summoned = 0;
    for (int attempt = 0; attempt < 256 && !summoned; attempt++) summoned = COMSUBS_00f4(first);
    assert(summoned);
    int customChildren = 0;
    for (int i = 6; i < 32; i++) if (i != second && D_ba14[i].entityIdx == 40003 && D_ba14[i].flags) customChildren++;
    assert(customChildren > divided);
    D_587a = oldNegation;
    memcpy(D_ad14, savedCombatMap, sizeof(savedCombatMap));
    D_538e = oldAdvance; D_5398_currentCharset = oldCharset; D_539a_currentTextWindow = oldWindow;
    memcpy(D_535e_textWindows, oldWindows, sizeof(oldWindows)); D_a9ce = sound;

    TYPES_Put32(defs + 32 + 2 * recordSize + 24, 40003);
    assert(ACTORS_Transform(first));
    assert(D_ba14[first].entityIdx == 40003 && D_ba14[first].hp == 90);
    D_5893_map_id = 0;
    TypeCell oldWorldSlot = {4, 0, 0, 15, 0, 40002, 0};
    assert(TYPES_Set(oldWorldSlot, 40002));
    D_5c5a[15]._0_tile = 0;
    ULTIMA_3a74(144, 144, 20, 20, 0, 0, 15);
    assert(TYPES_At(oldWorldSlot, 0) == 0);
    memcpy(D_ba14, oldEntities, sizeof(oldEntities));
    memcpy(D_5c5a, oldActors, sizeof(oldActors));
    D_5893_map_id = 13;
    byte stale = 0;
    assert(TILESET_Select(TILESET_AMIGA));
    TILESET_Register(&stale, 1);
    int staleColor = GRAP_BUF_TilePixel(40000, 2, 2);
    TILESET_Record(&stale, staleColor);
    stale = staleColor;
    TYPES_Mount(NULL, 0, NULL, 0);
    assert(TILESET_BufferColor(&stale, 0, 0, ega) == ega[0]);
    TILESET_Unregister(&stale);
    assert(TILESET_Select(TILESET_DOS));
    free(defs);
    memcpy(D_6608, base, 1024);
    D_5893_map_id = oldMap;
    D_5895_map_level = oldFloor;
    D_5896_map_x = oldX;
    D_5897_map_y = oldY;
    puts("PASS custom terrain native rules, door transitions, 32px art, opaque black/alpha and "
         "native save/load");
}

static void vanillaActorOverrideIdentity(void) {
    const unsigned ids[] = {368, 369, 440};
    const unsigned recordSize = 256 + 4 + 16 * 16 * 4;
    const unsigned size = 32 + 3 * recordSize;
    unsigned char *defs = calloc(1, size);
    assert(defs);
    memcpy(defs, "ITYPES01", 8);
    defs[8] = 17;
    TYPES_Put32(defs + 24, 3);
    for (unsigned i = 0; i < 3; ++i) {
        TypeDef d;
        TYPES_Vanilla(1, ids[i] - 256, &d);
        d.kind = i == 2 ? 2 : 1;
        d.frames = 1;
        if (i == 2) strcpy(d.name, "Stone Golem");
        unsigned char *record = defs + 32 + i * recordSize;
        TYPES_Encode(record, &d);
        TYPES_Put32(record, recordSize);
        record[257] = 16;
        record[258] = 100;
        memset(record + 260, 255, 16 * 16 * 4);
    }
    assert(TYPES_Validate(defs, size));
    TYPES_Mount(defs, size, NULL, 0);
    /* These are native tile-type IDs, rather than combat indices.
     * Editing artwork must not redirect them to entity zero (Mage). */
    assert(!strcmp(ACTORS_Name(368, 0), "Guard"));
    assert(!strcmp(ACTORS_Name(369, 0), "Guard"));
    assert(!strcmp(ACTORS_Name(440, 0), "Stone Golem"));
    assert(!strcmp(ACTORS_Name(12, 0), "Guard"));
    assert(!strcmp(ACTORS_Name(30, 0), "Stone Golem"));
    TypeDef direct = ACTORS_Def(368), combat = ACTORS_Def(12);
    assert(direct.base == 112 && !memcmp(direct.stats, combat.stats, sizeof(direct.stats)));
    assert(!strcmp(ACTORS_Name(368, 1), ACTORS_Name(12, 1)));
    /* Explicitly authored names still take precedence over the vanilla label. */
    strcpy((char *)defs + 32 + 56, "Royal Guard");
    TYPES_Mount(defs, size, NULL, 0);
    assert(!strcmp(ACTORS_Name(368, 0), "Royal Guard"));
    assert(!strcmp(ACTORS_Name(12, 0), "Royal Guard"));
    TYPES_Mount(NULL, 0, NULL, 0);
    free(defs);
    puts("PASS vanilla actor artwork overrides preserve type names and combat identity");
}

static void multiTileActors(void) {
    const unsigned localBytes = 32 * 32 * 4, worldBytes = 16 * 16 * 4;
    const unsigned recordSize = 256 + 4 + localBytes + 32 * 32 + 4 + worldBytes + 16 * 16,
                   size = 32 + recordSize;
    unsigned char *defs = calloc(1, size);
    assert(defs);
    memcpy(defs, "ITYPES01", 8);
    defs[8] = 23;
    TYPES_Put32(defs + 24, 1);
    TypeDef d;
    TYPES_Vanilla(1, 144, &d);
    d.id = 60000;
    d.kind = 2;
    d.width = 2;
    d.height = 2;
    d.frames = 2;
    d.render = TYPES_SPRITE;
    d.stats[5] = 100;
    d.stats[6] = 1;
    unsigned char *r = defs + 32;
    TYPES_Encode(r, &d);
    TYPES_Put32(r, recordSize);
    r[256] = 0;
    r[257] = 16 | 128;
    r[258] = 100;
    for (unsigned pixel = 0; pixel < 32 * 32; ++pixel) {
        unsigned x = pixel % 32, y = pixel / 32;
        bool black = (x <= 10 && y <= 10) || (x >= 12 && x <= 23 && y >= 12 && y <= 23);
        r[260 + pixel * 4] = black ? 0 : 255;
        r[263 + pixel * 4] = 255;
    }
    r[260 + localBytes + 3 * 32 + 3] = 255;
    r[260 + localBytes + 19 * 32 + 19] = 255;
    r[260 + localBytes + 5 * 32 + 5] = 255;
    r[263 + (5 * 32 + 5) * 4] = 0; /* White never resurrects PNG transparency. */
    unsigned off = 260 + localBytes + 32 * 32;
    r[off] = 6;
    r[off + 1] = 16 | 128;
    r[off + 2] = 100;
    for (unsigned pixel = 0; pixel < 256; ++pixel) {
        r[off + 6 + pixel * 4] = 255;
        r[off + 7 + pixel * 4] = 255;
    }
    r[off + 6 + (3 * 16 + 3) * 4] = 0;
    r[off + 4 + worldBytes + 3 * 16 + 3] = 255;
    assert(TYPES_Validate(defs, size));
    byte oldMap = D_5893_map_id, oldFloor = D_5895_map_level, oldX = D_5896_map_x,
         oldY = D_5897_map_y;
    ActorFmt oldActors[32];
    CombatEntity oldEntities[32];
    NpcFmt oldNpcs[32];
    byte oldTerrain[1024], oldCombat[352], oldViewport[352];
    byte oldLight = D_58a5;
    D_58a5 = 99;
    memcpy(oldViewport, D_ab02, sizeof(oldViewport));
    memset(D_ab02, 0, sizeof(oldViewport));
    memcpy(oldActors, D_5c5a, sizeof(oldActors));
    memcpy(oldEntities, D_ba14, sizeof(oldEntities));
    memcpy(oldNpcs, D_5f5e, sizeof(oldNpcs));
    memcpy(oldTerrain, D_6608, sizeof(oldTerrain));
    memcpy(oldCombat, D_ad14, sizeof(oldCombat));
    memset(D_5c5a, 0, sizeof(oldActors));
    memset(D_ba14, 0, sizeof(oldEntities));
    memset(D_5f5e, 0, sizeof(oldNpcs));
    memset(D_6608, 5, sizeof(oldTerrain));
    memset(D_ad14, 5, sizeof(oldCombat));
    D_5893_map_id = 13;
    D_5895_map_level = 0;
    D_5896_map_x = D_5897_map_y = 20;
    TYPES_Mount(defs, size, NULL, 0);
    TypeCell npc = {3, 13, 0, 1, 0, 60000, 0};
    assert(TYPES_Set(npc, 60000));
    D_5f5e[1]._0 = 1;
    D_5f5e[1]._c = 12;
    D_5c5a[12] = (ActorFmt){0};
    D_5c5a[12]._0_tile = D_5c5a[12]._1_animTile = 144;
    D_5c5a[12]._2_x = D_5c5a[12]._3_y = 3;
    unsigned w, h;
    ACTORS_Size(12, &w, &h);
    assert(w == 2 && h == 2);
    assert(ACTORS_Contains(12, 4, 4) && !ACTORS_Contains(12, 5, 4));
    assert(ULTIMA_3702(4, 4, 0) == 144);
    assert(ACTORS_CanMove(12, 4, 3)); /* Own overlapping cells do not block movement. */
    assert(!ACTORS_CanMove(12, 31, 3));
    GetMap(5, 4) = 0x4f;
    assert(!ACTORS_CanMove(12, 4, 3));
    GetMap(5, 4) = 5;
    assert(TILESET_Select(TILESET_DOS));
    int color = GRAP_BUF_SpritePixel(60000, 31, 31);
    assert(color >= 0 && TILESET_LastSpriteColor(color, 0, 0, ega, true) == 0xffff0000u);
    assert(GRAP_BUF_SpritePixel(60000, 32, 31) == -1);
    GRAP_BUF_DrawSprite(g_linearEgaBuffer0, 320, 16, 16, 60000, 8, 8, 184, 184, 0, 0);
    assert(TILESET_BufferColor(g_linearEgaBuffer0 + 47 * 320 + 47, 0, 0, ega) == 0xffff0000u);
    /* Black-key transparency spans the whole body, including the seams between
     * its four cells. Ground must remain visible in both black regions. */
    bool oldTransparency = GRAP_BUF_TransparentSprites();
    GRAP_BUF_SetTransparentSprites(true);
    assert(GRAP_BUF_SpriteIsTransparent(60000));
    assert(GRAP_BUF_SpritePixel(60000, 0, 0) == -1);
    assert(GRAP_BUF_SpritePixel(60000, 17, 17) == -1);
    assert(GRAP_BUF_SpritePixel(60000, 5, 5) == -1);
    for (int coordinate = 3; coordinate <= 19; coordinate += 16) {
        int protectedColor = GRAP_BUF_SpritePixel(60000, coordinate, coordinate);
        assert(protectedColor > 0 &&
               TILESET_LastSpriteColor(protectedColor, 0, 0, ega, true) == 0xff000000u);
    }
    assert(GRAP_BUF_SpritePixel(60000, 10, 5) == 0);  /* Native-style black outline. */
    assert(GRAP_BUF_SpritePixel(60000, 32, 31) == 0); /* Outer body edge, not cell seams. */
    int blackPixel = GRAP_BUF_TilePixel(60000, 0, 0);
    assert(TILESET_LastColor(blackPixel, 0, 0, ega) == 0xff000000u);
    assert(TILESET_LastSpriteColor(blackPixel, 0, 0, ega, true) == 0);
    int points[] = {16 * 320 + 16, 33 * 320 + 33};
    for (unsigned i = 0; i < 2; i++) {
        byte *p = g_linearEgaBuffer0 + points[i];
        TILESET_SetSample(0);
        TILESET_Record(p, 2);
        *p = 2;
    }
    GRAP_BUF_DrawSprite(g_linearEgaBuffer0, 320, 16, 16, 60000, 8, 8, 184, 184, 0, 0);
    for (unsigned i = 0; i < 2; i++)
        assert(TILESET_BufferColor(g_linearEgaBuffer0 + points[i], 0, 0, ega) == ega[2]);
    GRAP_BUF_SetTransparentSprites(false);
    GRAP_BUF_DrawSprite(g_linearEgaBuffer0, 320, 16, 16, 60000, 8, 8, 184, 184, 0, 0);
    for (unsigned i = 0; i < 2; i++)
        assert(TILESET_BufferColor(g_linearEgaBuffer0 + points[i], 0, 0, ega) == 0xff000000u);
    GRAP_BUF_SetTransparentSprites(true);
    r[11] = 0; /* The characteristic is required even when the option is enabled. */
    assert(!GRAP_BUF_SpriteIsTransparent(60000));
    assert(GRAP_BUF_SpritePixel(60000, 0, 0) >= 0);
    r[11] = TYPES_SPRITE;
    /* A large northern actor's bottom outline must not cover the Avatar's
     * first pixel row when widescreen redraws artwork across the central seam. */
    GRAP_BUF_SetTransparentSprites(true);
    byte oldActorMap[sizeof(D_ac64)];
    memcpy(oldActorMap, D_ac64, sizeof(oldActorMap));
    byte oldActive = D_58a4;
    D_58a4 = 1;
    D_5c5a[12]._2_x = 20;
    D_5c5a[12]._3_y = 18;
    D_5c5a[0]._0_tile = D_5c5a[0]._1_animTile = 80;
    D_5c5a[0]._2_x = D_5c5a[0]._3_y = 20;
    memset(D_ab02, 255, sizeof(oldViewport));
    GetMapViewport(5, 3) = GetMapViewport(5, 5) = 0;
    GetActorMap(5, 3) = 144;
    GetActorMap(5, 5) = 80;
    GRAP_BUF_PutMapSprite(5, 3, 60000);
    GRAP_BUF_PutMapSprite(5, 5, 336);
    WideLayout layout = WIDE_Layout(1600, 900);
    byte *composed = calloc(layout.width * layout.height, 1);
    assert(composed && WIDE_Compose(composed, layout));
    int avatarPixel = (layout.mapY + layout.rows / 2 * 16) * layout.width + layout.mapX +
                      layout.columns / 2 * 16 + 8;
    assert(TILESET_BufferColor(composed + avatarPixel, 0, 0, ega) == ega[5]);
    /* Southern scenery clips the outline below the full two-row footprint. */
    GetMap(20, 20) = D_6a86[0];
    assert(WIDE_SpritePixel(60000, 0, -2, 8, 32) == -1);
    GetMap(20, 20) = 5;
    TILESET_Copy(composed, NULL, layout.width * layout.height);
    free(composed);
    D_58a4 = oldActive;
    memcpy(D_ac64, oldActorMap, sizeof(oldActorMap));
    memcpy(D_ab02, oldViewport, sizeof(oldViewport));
    D_5c5a[0] = (ActorFmt){0};
    D_5c5a[12]._2_x = D_5c5a[12]._3_y = 3;
    GRAP_BUF_SetTransparentSprites(oldTransparency);
    unsigned oldNpcCount = D_imperaNpcCount;
    D_imperaNpcCount = 258;
    TypeCell extendedNpc = {3, 13, 0, 1, 1, 60000, 0};
    assert(TYPES_Set(extendedNpc, 60000));
    D_5f5e[257] = (NpcFmt){0};
    D_5f5e[257]._0 = 1;
    D_5f5e[257]._c = 257;
    D_5c5a[257] = D_5c5a[12];
    D_5c5a[257]._2_x = 8;
    assert(ACTORS_Instance(257) == 60000);
    ACTORS_Size(257, &w, &h);
    assert(w == 2 && h == 2);
    assert(ACTORS_Contains(257, 9, 4) && ACTORS_AtCell(9, 4, 0) == 60000);
    D_5c5a[257] = (ActorFmt){0};
    D_5f5e[257] = (NpcFmt){0};
    D_imperaNpcCount = oldNpcCount;
    D_5893_map_id = 0;
    TypeCell world = {4, 0, 0, 12, 0, 60000, 0};
    assert(TYPES_Set(world, 60000));
    ACTORS_Size(12, &w, &h);
    assert(w == 1 && h == 1);
    assert(!ACTORS_Contains(12, 4, 4));
    color = GRAP_BUF_SpritePixel(60000, 15, 15);
    assert(color >= 0 && TILESET_LastSpriteColor(color, 0, 0, ega, true) == 0xff0000ffu);
    color = GRAP_BUF_SpritePixel(60000, 3, 3);
    assert(color > 0 && TILESET_LastSpriteColor(color, 0, 0, ega, true) == 0xff000000u);
    assert(GRAP_BUF_SpritePixel(60000, 16, 15) == -1);
    D_5893_map_id = 128;
    D_ba14[6].flags = COMBAT_FLAGS_MONSTER;
    D_ba14[6].entityIdx = 60000;
    D_ba14[6].actorIdx = 12;
    D_ba14[6].x = D_ba14[6].y = 3;
    D_ba14[7].flags = COMBAT_FLAGS_PLAYER;
    D_ba14[7].actorIdx = 13;
    D_ba14[7].x = 5;
    D_ba14[7].y = 4;
    D_5c5a[13]._0_tile = D_5c5a[13]._1_animTile = 0x1c;
    D_5c5a[13]._2_x = 5;
    D_5c5a[13]._3_y = 4;
    assert(COMSUBS_0748(4, 4) == 6 && COMSUBS_04d4(6, 7) == 1);
    /* Corpses under the lower-right footprint cell must not cover living art
     * or cause the same 2x2 actor to be redrawn from the corpse's anchor. */
    byte savedActorMap[sizeof(D_ac64)];
    memcpy(savedActorMap, D_ac64, sizeof(savedActorMap));
    byte savedSelection = D_589e, savedMapActive = D_58a4;
    byte savedX = D_5896_map_x, savedY = D_5897_map_y;
    D_5896_map_x = D_5897_map_y = 5;
    D_589e = 255;
    D_58a4 = 1;
    for (int transparent = 0; transparent < 2; ++transparent) {
        GRAP_BUF_SetTransparentSprites(transparent != 0);
        const byte groundShapes[] = {1,
                                     2,
                                     3,
                                     4,
                                     5,
                                     7,
                                     0x19,
                                     0x1a,
                                     0x1b,
                                     0xb4,
                                     0xb5,
                                     0xb6,
                                     0xb7,
                                     TILE_ACTOR_SLEEP,
                                     TILE_ACTOR_DEAD};
        for (int slot = 0; slot <= 31; slot += 31)
            for (unsigned kind = 0; kind < sizeof(groundShapes); ++kind) {
                D_5c5a[slot] = (ActorFmt){0};
                D_5c5a[slot]._0_tile = D_5c5a[slot]._1_animTile = groundShapes[kind];
                D_5c5a[slot]._2_x = D_5c5a[slot]._3_y = 4;
                memcpy(D_ab02, D_ad14, sizeof(D_ab02));
                ULTIMA_5394();
                assert(GetActorMap(4, 4) == groundShapes[kind]);
                ULTIMA_56ac_DrawMap();
                assert(TILESET_BufferColor(g_linearEgaBuffer0 + 80 * 320 + 80, 0, 0, ega) ==
                       0xffff0000u);
                assert(TILESET_BufferColor(g_linearEgaBuffer0 + 100 * 320 + 100, 0, 0, ega) ==
                       ega[5]);
                WideLayout combatLayout = WIDE_Layout(1600, 900);
                byte *combatPixels = calloc(combatLayout.width * combatLayout.height, 1);
                assert(combatPixels);
                TILESET_Register(combatPixels, combatLayout.width * combatLayout.height);
                assert(WIDE_Compose(combatPixels, combatLayout));
                int left = combatLayout.mapX + (combatLayout.columns / 2 - 5) * 16;
                int top = combatLayout.mapY + (combatLayout.rows / 2 - 5) * 16;
                assert(TILESET_BufferColor(combatPixels + (top + 72) * combatLayout.width + left + 72,
                                          0, 0, ega) == 0xffff0000u);
                TILESET_Unregister(combatPixels);
                free(combatPixels);
                /* A ground object at the actor's anchor must not replace the
                 * standing actor through the one-byte native actor map. */
                D_5c5a[slot]._2_x = D_5c5a[slot]._3_y = 3;
                memcpy(D_ab02, D_ad14, sizeof(D_ab02));
                ULTIMA_5394();
                ULTIMA_56ac_DrawMap();
                assert(TILESET_BufferColor(g_linearEgaBuffer0 + 80 * 320 + 80, 0, 0, ega) ==
                       0xffff0000u);
                D_5c5a[slot] = (ActorFmt){0};
            }
    }
    /* A taller eastern actor sharing the same foot row has priority over the
     * shorter western actor, even though its top-left cell is farther north. */
    GRAP_BUF_SetTransparentSprites(true);
    for (int slot = 0; slot <= 31; slot += 31) {
        D_5c5a[slot] = (ActorFmt){0};
        D_5c5a[slot]._0_tile = D_5c5a[slot]._1_animTile = 0x44;
        D_5c5a[slot]._2_x = 2;
        D_5c5a[slot]._3_y = 4;
        memcpy(D_ab02, D_ad14, sizeof(D_ab02));
        ULTIMA_5394();
        ULTIMA_56ac_DrawMap();
        assert(TILESET_BufferColor(g_linearEgaBuffer0 + 80 * 320 + 55, 0, 0, ega) == ega[0]);
        D_5c5a[slot]._2_x = 4;
        D_5c5a[slot]._3_y = 5;
        memcpy(D_ab02, D_ad14, sizeof(D_ab02));
        ULTIMA_5394();
        ULTIMA_56ac_DrawMap();
        assert(TILESET_BufferColor(g_linearEgaBuffer0 + 88 * 320 + 80, 0, 0, ega) == ega[5]);
        D_5c5a[slot] = (ActorFmt){0};
    }
    /* An authored NPC remains an actor even when its vanilla base looks like
     * an inventory object. Native hit effects must also survive custom art. */
    r[9] = 0xb4;
    D_5c5a[12]._0_tile = D_5c5a[12]._1_animTile = 0xb4;
    TYPES_Mount(defs, size, NULL, 0);
    D_5c5a[0] = (ActorFmt){0};
    D_5c5a[0]._0_tile = D_5c5a[0]._1_animTile = TILE_ACTOR_GOLD;
    D_5c5a[0]._2_x = D_5c5a[0]._3_y = 4;
    memcpy(D_ab02, D_ad14, sizeof(D_ab02));
    ULTIMA_5394();
    ULTIMA_56ac_DrawMap();
    assert(TILESET_BufferColor(g_linearEgaBuffer0 + 80 * 320 + 80, 0, 0, ega) == 0xffff0000u);
    D_5c5a[0] = (ActorFmt){0};
    D_5c5a[12]._1_animTile = TILE_ACTOR_1D;
    assert(ACTORS_Sprite(12, 256 + TILE_ACTOR_1D) == 256 + TILE_ACTOR_1D);
    r[9] = 144;
    D_5c5a[12]._0_tile = D_5c5a[12]._1_animTile = 144;
    TYPES_Mount(defs, size, NULL, 0);
    GRAP_BUF_SetTransparentSprites(oldTransparency);
    D_589e = savedSelection;
    D_58a4 = savedMapActive;
    D_5896_map_x = savedX;
    D_5897_map_y = savedY;
    memcpy(D_ac64, savedActorMap, sizeof(savedActorMap));

    assert(!ACTORS_CanMove(12, 4, 3) && ACTORS_CanMove(12, 2, 3));
    int ax, ay, bx, by;
    ACTORS_AttackCells(6, 7, &ax, &ay, &bx, &by);
    assert(ax == 4 && ay == 4 && bx == 5 && by == 4);
    assert(!ACTORS_CanPlace(60000, 10, 10, 32));
    int spawned = ULTIMA_6506(60000, 0, 10, 10, 0);
    assert(spawned >= 6 && D_ba14[spawned].x <= 9 && D_ba14[spawned].y <= 9);
    TYPES_Mount(NULL, 0, NULL, 0);
    free(defs);
    memcpy(D_5c5a, oldActors, sizeof(oldActors));
    memcpy(D_ba14, oldEntities, sizeof(oldEntities));
    memcpy(D_5f5e, oldNpcs, sizeof(oldNpcs));
    memcpy(D_6608, oldTerrain, sizeof(oldTerrain));
    memcpy(D_ad14, oldCombat, sizeof(oldCombat));
    memcpy(D_ab02, oldViewport, sizeof(oldViewport));
    D_58a5 = oldLight;
    D_5893_map_id = oldMap;
    D_5895_map_level = oldFloor;
    D_5896_map_x = oldX;
    D_5897_map_y = oldY;
    puts("PASS multi-tile occupancy, own-overlap movement, obstacles, targeting, range, large "
         "rendering and single-tile world art");
}

static void actorAnimationArtwork(void) {
    const unsigned length = 256 + 4 * (4 + 32 * 32 * 4) + 4 * (4 + 16 * 16 * 4);
    unsigned char *data = calloc(1, 32 + length);
    assert(data);
    memcpy(data, "ITYPES01", 8);
    data[8] = 19;
    TYPES_Put32(data + 24, 1);
    TypeDef definition;
    TYPES_Vanilla(1, 80, &definition);
    definition.id = 60001;
    definition.kind = 1;
    definition.stats[5] = 20;
    definition.stats[6] = 1;
    definition.width = definition.height = 2;
    definition.frames = 8;
    definition.render = TYPES_SPRITE;
    TYPES_Encode(data + 32, &definition);
    TYPES_Put32(data + 32, length);
    unsigned offset = 32 + 256;
    for (unsigned group = 0; group < 2; ++group)
        for (unsigned pose = 0; pose < 4; ++pose) {
            unsigned side = group ? 16 : 32;
            data[offset] = group ? 6 : 0;
            data[offset + 1] = 16;
            data[offset + 2] = 150;
            for (unsigned pixel = 0; pixel < side * side; ++pixel) {
                data[offset + 4 + pixel * 4 + group] = 50 * (pose + 1);
                data[offset + 7 + pixel * 4] = 255;
            }
            offset += 4 + side * side * 4;
        }
    assert(offset == 32 + length && TYPES_Validate(data, offset));
    TYPES_Mount(data, offset, NULL, 0);
    byte previousMap = D_5893_map_id;
    bool previousTransparency = GRAP_BUF_TransparentSprites();
    GRAP_BUF_SetTransparentSprites(true);
    assert(TILESET_Select(TILESET_DOS));
    for (unsigned group = 0; group < 2; ++group) {
        D_5893_map_id = group ? 0 : 13;
        for (unsigned pose = 0; pose < 4; ++pose) {
            unsigned tile = TYPES_ActorArt(60001, 336 + pose);
            assert(GRAP_BUF_SpriteIsTransparent(tile));
            int color = GRAP_BUF_SpritePixel(tile, group ? 15 : 31, group ? 15 : 31);
            unsigned expected = 0xff000000u | (50 * (pose + 1) << (group ? 8 : 16));
            assert(TILESET_LastSpriteColor(color, 0, 0, ega, true) == expected);
        }
    }
    D_5893_map_id = previousMap;
    GRAP_BUF_SetTransparentSprites(previousTransparency);
    TYPES_Mount(NULL, 0, NULL, 0);
    free(data);
}
