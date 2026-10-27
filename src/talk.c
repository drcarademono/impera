#include "common/common.h"
#include "funcs.h"
#include "vars.h"
#include "macros.h"
#include "tiles.h"

#include <string.h>
#include <stdlib.h>
#include "common/file.h"
#include "mod/dialogue_bank.h"
#include "mod/package.h"

#include "npc.h"
#include "shoppes.h"
#include "shoppes2.h"
#include "shoppes3.h"
#include "talk.h"
#include "text/text_window.h"
#include "text/dialogue_runtime.h"
#include "text/dialogue_game.h"
#include "town.h"

static unsigned char learnedNames[128][IMPERA_NPC_CAPACITY / 8];
void TALK_ResetKnowledge(void) { memset(learnedNames, 0, sizeof(learnedNames)); }
static void talkLearnName(void *host) {
    (void)host;
    if (D_5893_map_id >= 1 && D_5893_map_id <= 128 && D_bcdc >= 0 && D_bcdc < IMPERA_NPC_CAPACITY)
        learnedNames[D_5893_map_id - 1][D_bcdc / 8] |= 1u << (D_bcdc % 8);
}
int TALK_WriteKnowledge(FILE *f) {
    unsigned size = sizeof(learnedNames);
    const unsigned char *p = (const unsigned char *)learnedNames;
    while (size && !p[size - 1]) --size;
    if (!size) return 1;
    unsigned char header[16] = "IKNOW001";
    unsigned crc = MOD_Crc32(p, size);
    for (int i = 0; i < 4; ++i) { header[8+i] = size >> (8*i); header[12+i] = crc >> (8*i); }
    return fwrite(header, 1, 16, f) == 16 && fwrite(p, 1, size, f) == size;
}
int TALK_ReadKnowledge(FILE *f, int apply) {
    long start = ftell(f); unsigned char header[16];
    size_t n = fread(header, 1, 16, f);
    if (n < 8 || memcmp(header, "IKNOW001", 8)) {
        if (ferror(f) || fseek(f, start, SEEK_SET)) return 0;
        if (apply) TALK_ResetKnowledge();
        return 1;
    }
    if (n != 16) return 0;
    unsigned size = DIALOGUE_U32(header + 8);
    if (!size || size > sizeof(learnedNames)) return 0;
    unsigned char *p = malloc(size);
    if (!p) return 0;
    int ok = fread(p, 1, size, f) == size && MOD_Crc32(p, size) == DIALOGUE_U32(header + 12);
    if (ok && apply) { TALK_ResetKnowledge(); memcpy(learnedNames, p, size); }
    free(p); return ok;
}

#define TALK_KEY_NAME 0
#define TALK_KEY_DESC 1
#define TALK_KEY_2 2
#define TALK_KEY_3 3
#define TALK_KEY_4 4

static int TALK_0f32_ProcessChar(byte param_1);
static void TALK_127e_StartTalk(int a);

// CHECKED
static int TALK_0000_CompareStringNoCase(char* param_1, char* param_2)
{
    while (1)
    {
        if (ULTIMA_2032_ToUpper((*(byte*)param_1++) & 0x7f) != ULTIMA_2032_ToUpper((*(byte*)param_2++) & 0x7f))
        {
            return 0;
        }

        if ((*param_1 == 0) && (*param_2 == 0))
        {
            return 1;
        }
    }
}

static int TALK_0054(int param_1, int param_2)
{
    switch (*ULTIMA_4402_GetTileAddr(param_1, param_2))
    {
    case TILE_MAP_29:
    case TILE_MAP_TABLE_94:
    case TILE_MAP_TABLE_95:
    case TILE_MAP_TABLE_96:
    case TILE_MAP_97:
    case TILE_MAP_98:
    case TILE_MAP_99:
    case TILE_MAP_TABLE_9A:
    case TILE_MAP_TABLE_9B:
    case TILE_MAP_TABLE_9C:
    case TILE_MAP_DESK:
    case TILE_MAP_AE:
    case TILE_MAP_DOOR_BA:
    case TILE_MAP_DOOR_BB:
    case TILE_MAP_TABLE_BE:
    case TILE_MAP_CA:
    case TILE_MAP_CB:
        return 1;
    }

    return 0;
}

/* Shared keyboard/mouse target resolution, including talking across furniture. */
int TALK_Target(int dx,int dy,int* x,int* y)
{
    *x=D_5896_map_x+dx;*y=D_5897_map_y+dy;
    if (D_5893_map_id && (*x < 0 || *y < 0 || *x >= MAP_WIDTH || *y >= MAP_HEIGHT))
        return 0;
    int actor=ULTIMA_368e_FindActorTileAtPos(*x,*y,D_5895_map_level);
    if(!actor && TALK_0054(*x,*y)) {
        *x+=dx;*y+=dy;
        if (D_5893_map_id && (*x < 0 || *y < 0 || *x >= MAP_WIDTH || *y >= MAP_HEIGHT))
            return 0;
        actor=ULTIMA_368e_FindActorTileAtPos(*x,*y,D_5895_map_level);
    }
    return actor;
}

// ask pay
static int TALK_00ac_AskPay(void)
{
    char ch;

    ULTIMA_1850_PrintString(_TEXT(0x9052, "\n\nDost thou pay?\n\n:"));

    do
    {
        ch = ULTIMA_2032_ToUpper(ULTIMA_266c_GetChar());
        if (ch == 'Y')
        {
            ULTIMA_1850_PrintString(_TEXT(0x9066, "Yes\n"));
            return 0;
        }
    } while (ch != 'N');

    ULTIMA_1850_PrintString(_TEXT(0x906c, "No!\n"));
    return 1;
}

// CHECKED
// talk to merchant (shoppe)
static void TALK_00e6_TalkToMerchant(int param_1)
{
    int local_4;

    if ((D_587c_partyTile & 0xfe) == TILE_ACTOR_RIDING_HORSE && param_1 != 0x83)
    {
        ULTIMA_1850_PrintString(_TEXT(0x9072, "A merchant says:\n\"GET THAT HORSE OUT OF HERE!\"\n"));
    }
    else
    {
        if (D_587b != 0xff)
        {
            local_4 = D_587b;
        }
        else
        {
            ULTIMA_39fc_GetFirstActivePartyMember();
            local_4 = D_5876;
        }

        D_b116 = param_1 - 0x81;

        for (D_b114 = 0; D_b114 < 0x10 && D_23ca[D_b116][D_b114] != D_5893_map_id; D_b114++) {
        }

        if (D_b114 >= 16) {
            ULTIMA_1850_PrintString("No service is available here.\n");
            return;
        }
        D_aafc = D_21ca[D_b116][D_b114];
        D_aafe = D_22ca[D_b116][D_b114];

        switch (D_b116)
        {
        case 0:
            SHOPPES_12b2(local_4);
            break;
        case 1:
            SHOPPES2_066c(local_4);
            break;
        case 2:
            SHOPPES_07be(local_4);
            break;
        case 3:
            SHOPPES2_0abc(local_4);
            break;
        case 4:
            SHOPPES_075e(local_4);
            break;
        case 5:
            SHOPPES_04a2(local_4);
            break;
        case 6:
            SHOPPES_14f8(local_4);
            break;
        case 7:
            SHOPPES3_08b4(local_4);
        }
    }
}

// CHECKED
static int TALK_01e2(void)
{
    int local_14;
    int local_16;
    char local_12[0xf];

    if (D_5893_map_id != 0x12)
    {
        // 01f3
        if (D_5893_map_id == 5)
        {
            ULTIMA_16ba_PrintChar('\"');
            ULTIMA_1850_PrintString(_TEXT(0x90a2, "Thou wilt give\nhalf thy gold to\ncharity!"));
            ULTIMA_16ba_PrintChar('\"');

            if (TALK_00ac_AskPay() != 0)
            {
                // 0216
                return 1;
            }

            // 021c
            D_57aa /= 2;

            // 0228
            ULTIMA_2900_UpdateVitalsDisplay();
            // 022b
            return 0;
        }
        else
        {
            // 0230
            local_16 = 0;

            // 0243 (NOT MATCHING: loop structure)
            for (local_14 = 0; local_14 < D_585b; local_14++)
            {
                // 025a
                if (D_55a8_party[local_14].status != STATUS_DEAD)
                {
                    local_16 += 10;
                }
            }

            // 026c
            ULTIMA_1850_PrintString(_TEXT(0x90cc, "A guard demands\na "));
            ULTIMA_1a3e_PrintNumber(local_16, 2, 0x20);
            ULTIMA_1850_PrintString(_TEXT(0x90e0, " gp tribute\nto Blackthorn!"));

            if (TALK_00ac_AskPay() != 0)
            {
                // 028d -> 0216
                return 1;
            }

            // 028f
            if (D_57aa >= local_16)
            {
                // 029a
                D_57aa -= local_16;

                ULTIMA_2900_UpdateVitalsDisplay();
                return 0;
            }

            // 0297 -> 0216
            return 1;
        }
    }
    else
    {
        // 02a4
        if (D_587a == 0x1d)
        {
            // 02ae
            ULTIMA_16ba_PrintChar('\"');
            ULTIMA_1850_PrintString(_TEXT(0x90fc, "Give now the\npassword, bearer\nof the Badge!"));
            ULTIMA_16ba_PrintChar('\"');
            ULTIMA_1850_PrintString(_TEXT(0x9128, "\n\nYour response?\n"));
            ULTIMA_3b1c_GetString(local_12, 0xe);
            ULTIMA_16ba_PrintChar('\n');
            local_12[4] = 0;

            if (TALK_0000_CompareStringNoCase(_TEXT(0x4a9a, "IMPE"), local_12) != 0) // TODO: D_4a9a = "IMPE"?
            {
                // 02f2
                ULTIMA_16ba_PrintChar('\n');
                ULTIMA_16ba_PrintChar('\"');
                ULTIMA_1850_PrintString(_TEXT(0x913a, "Pass, friend!"));
                ULTIMA_16ba_PrintChar('\"');
                ULTIMA_16ba_PrintChar('\n');
                // 0315 -> 022b
                return 0;
            }
        }

        // 02ef -> 0216
        return 1;
    }
}

// CHECKED
// talk to npc
int TALK_031e_TalkToNpc(int param_1)
{
    int local_4;
    NpcFmt* local_6;
    NpcScheduleFmt* local_8;

    ULTIMA_16ba_PrintChar('\n');

    D_bcdc = param_1;
    local_6 = &D_5f5e[param_1];
    local_8 = &D_5d5e[param_1];

    if (local_8->type[local_6->_e] == 4)
    {
        local_8->type[local_6->_e] = 1;
    }
    // c2ea NOT MATCHING
    else if (D_5c5a[local_6->_c]._0_tile == TILE_ACTOR_GUARD && ((local_6->_e & 1) == 0 || (local_6->_a) == 0))
    {
        ULTIMA_1850_PrintString(_TEXT(0x9148, "The guard offers\nno response!\n"));
        return 0;
    }

    local_4 = local_6->_a;
    if (local_4 == 0)
    {
        ULTIMA_1850_PrintString(_TEXT(0x9168, "No response!\n"));
        return 0;
    }
    else
    {
        // c316
        if (local_4 < 0x80)
        {
            TALK_127e_StartTalk(local_4);
            return 0;
        }

        if (local_4 == 0xfd)
        {
            ULTIMA_16ba_PrintChar('\"');
            ULTIMA_1850_PrintString(_TEXT(0x9176, "Don't hurt me!\nPlease go away!"));
            ULTIMA_16ba_PrintChar('\"');
            ULTIMA_16ba_PrintChar('\n');
            return 0;
        }

        // c34c
        if (local_4 == 0xfe)
        {
#if !defined(MATCHING_BUILD)
            TOWN_10da(param_1); // TODO: validate param (reference apple?)
#else
            TOWN_10da(); // BUG: TOWN_10da(unaff_SI)
#endif
            return 0;
        }

        if (local_4 == 0xff)
        {
            return TALK_01e2();
        }

        if ((local_6->_e & 1) != 0 && (NPC_12e0(D_bcdc, D_587f) & 1) != 0)
        {
            TALK_00e6_TalkToMerchant(local_4);
            return 0;
        }
        else
        {
            ULTIMA_1850_PrintString(_TEXT(0x9196, "A merchant says:\n\"Come see me at\nmy shoppe, "));
            ULTIMA_1850_PrintString(_TEXT(0x91c4, "when\nit's open!\"\n"));
            return 0;
        }
    }
}

int TALK_041c_TalkCmd(void)
{
    int local_a;
    int local_8;
    int local_c;
    int local_4;
    int local_6;

    if (ULTIMA_SelectTalkDirection() == 0)
    {
        return 0;
    }

    local_8 = D_5876;
    local_c = D_5878;
    if (TALK_Target(local_8,local_c,&local_6,&local_a) == 0)
    {
        ULTIMA_1850_PrintString(_TEXT(0x91d6, "\nNobody's here!\n"));
    }
    else
    {
        local_4 = TOWN_011e(D_5876);
        switch (*ULTIMA_4402_GetTileAddr(local_6, local_a))
        {
        case TILE_MAP_BED:
            ULTIMA_1850_PrintString(_TEXT(0x91e8, "\n\"Zzzzzz...\"\n"));
            break;
        case TILE_MAP_MIRROR:
            ULTIMA_1850_PrintString(_TEXT(0x91f6, "\nNo response!\n"));
            break;
        default:
            return TALK_031e_TalkToNpc(local_4);
        }
    }

    return 0;
}

static ImperaTalk nativeTalk(void);
static ImperaDialogueGame dialogueGame(void);
static int TALK_0f32_ProcessChar(byte ch) {
    ImperaTalk s = nativeTalk();
    return IMPERA_TalkCharacter(&s, ch);
}
static void TALK_04da_PrintQuoteMark(void) { TALK_0f32_ProcessChar(0xa2); }
static void TALK_04d2_PrintCr(void) { TALK_0f32_ProcessChar(0x8d); }
static void TALK_04e2_PrintWord(void) {
    ImperaTalk s = nativeTalk();
    IMPERA_TalkFlush(&s);
}
static void TALK_0574_BuildOrPrintWord(byte ch) {
    ImperaTalk s = nativeTalk();
    IMPERA_TalkAppend(&s, ch);
}
static void TALK_075a_SelectNthTalkString(int entry) {
    ImperaTalk s = nativeTalk();
    IMPERA_TalkSelect(&s, entry);
}
static int TALK_111c(void) {
    ImperaTalk s = nativeTalk();
    return IMPERA_TalkDescription(&s);
}
static int TALK_0b04(void) {
    ImperaTalk s = nativeTalk();
    return IMPERA_TalkTopics(&s);
}
static int TALK_0a3c(void) {
    ImperaTalk s = nativeTalk();
    return IMPERA_TalkFarewell(&s);
}
static void talkPrint(void *host, uint16_t ch) {
    (void)host;
    ULTIMA_16ba_PrintChar(ch);
}
static void talkString(void *host, const char *str) {
    (void)host;
    ULTIMA_1850_PrintString((char *)str);
}
static const char *talkText(void *host, uint16_t address, const char *str) {
    (void)host;
    (void)address;
    return _TEXT(address, str);
}
static int talkColumn(void *host) {
    (void)host;
    return ULTIMA_1f12_GetCurrentTextX();
}
static void talkFont(void *host, int index) {
    (void)host;
    ULTIMA_1c9e_SelectCharset(index);
}
static int talkChar(void *host) {
    (void)host;
    return ULTIMA_266c_GetChar();
}
static void talkInput(void *host, char *str, int size) {
    (void)host;
    ULTIMA_3b1c_GetString(str, size);
}
static int talkPoll(void *host) {
    (void)host;
    return ULTIMA_1d5e_PollKey();
}
static void talkWait(void *host, int ticks) {
    (void)host;
    ULTIMA_20fa_WaitTicks(ticks);
}
static void talkFrame(void *host) {
    (void)host;
    ULTIMA_5910_UpdateFrame();
}
static void talkClear(void *host) {
    (void)host;
    ULTIMA_1b16_ClearKbdBuffer();
}
static int talkIntroduced(void *host) {
    (void)host;
    ImperaDialogueGame g = dialogueGame();
    return IMPERA_GameMet(&g);
}
static void talkIntroduce(void *host) {
    (void)host;
    ImperaDialogueGame g = dialogueGame();
    IMPERA_GameIntroduce(&g);
}
static int talkParty(void *host) {
    (void)host;
    return D_585b;
}
static const uint8_t *talkName(void *host, int index) {
    (void)host;
    return D_55a8_party[index].name;
}
static int talkKarma(void *host) {
    (void)host;
    return D_5888;
}
static void talkChangeKarma(void *host, int amount) {
    (void)host;
    if (amount > 0)
        ULTIMA_3ef0_IncreaseByte(&D_5888, amount, 99);
    else
        ULTIMA_3f36_DecreaseByte(&D_5888, -amount);
}
static int gameRandom(void *h, int low, int high) {
    (void)h;
    return ULTIMA_2092_RandomRange(low, high);
}
static void gameVitals(void *h) {
    (void)h;
    ULTIMA_2900_UpdateVitalsDisplay();
}
static ImperaDialogueGame dialogueGame(void) {
    ImperaDialogueGame g = {0};
    g.party = (uint8_t *)D_55a8_party;
    g.partyCount = &D_585b;
    g.food = (uint8_t *)&D_57a8;
    g.gold = (uint8_t *)&D_57aa;
    g.equipment = D_57c0;
    g.supplies[2] = &D_57ac;
    g.supplies[3] = &D_57ad;
    g.supplies[4] = &D_57ae;
    g.supplies[5] = &D_57af;
    g.supplies[6] = &D_57b0;
    g.supplies[7] = &D_57bc;
    g.supplies[8] = &D_57ba;
    g.supplies[9] = &D_57be;
    g.supplies[10] = &D_57b1;
    g.karma = &D_5888;
    g.turn = &D_588b;
    g.map = &D_5893_map_id;
    g.killed = D_5b5a;
    g.met = D_5bda;
    g.actors = (uint8_t *)D_5c5a;
    g.npcs = (uint8_t *)D_5f5e;
    g.schedules = (uint8_t *)D_5d5e;
    g.shapes = D_659e;
    g.dirty = &D_24e6;
    g.npc = D_bcdc;
    g.npcCount = LOCATION_NPC_COUNT;
    g.actorCount = MAP_ACTOR_COUNT;
    g.extraKilled = D_5b5a + 128;
    g.extraMet = D_5bda + 128;
    g.mapCount = 128;
    g.random = gameRandom;
    g.vitals = gameVitals;
    return g;
}
static int talkPay(void *host, int amount) {
    (void)host;
    ImperaDialogueGame g = dialogueGame();
    return IMPERA_GamePay(&g, amount);
}
static void talkGive(void *host, uint8_t code) {
    (void)host;
    ImperaDialogueGame g = dialogueGame();
    IMPERA_GameGive(&g, code);
}
static int talkJoin(void *host) {
    (void)host;
    ImperaDialogueGame g = dialogueGame();
    ImperaTalk s = nativeTalk();
    return IMPERA_GameJoin(&g, &s);
}
static void talkGuards(void *host) {
    (void)host;
    ImperaDialogueGame g = dialogueGame();
    IMPERA_GameGuards(&g);
}
static int talkGreeting(void *host) {
    (void)host;
    ULTIMA_207e_srand(ULTIMA_2056_GetTime());
    return ULTIMA_2092_RandomRange(0, 1);
}
/* Observe source bytes separately from rendered game text. */
static void talkObserve(void *host, const uint8_t *cursor) {
    (void)host;
    (void)cursor;
#if defined(IMPERA_REFERENCE_OBSERVER)
    REFERENCE_TraceOpcode(cursor);
#endif
}
#if !defined(TARGET_DOS16)
static uint8_t *s_conversation;
static size_t s_conversationSize;
#endif
static ImperaTalk nativeTalk(void) {
    ImperaTalk s = {0};
    s.data = D_b21e;
    s.size = 1024;
#if !defined(TARGET_DOS16)
    if (s_conversation) {
        s.data = s_conversation;
        s.size = s_conversationSize;
    }
#endif
    s.cursor = &D_bcde;
    s.operands = D_bce0;
    s.pending = &D_4aee;
    s.operandCount = &D_4aef;
    s.mask = &D_4af0;
    s.length = &D_4af1;
    s.questionMode = &D_4af2;
    s.lastColumn = &D_4af3;
    s.lastLiteral = &D_4af4;
    s.dictionaryBoundary = &D_4af5;
    s.label = &D_bcf4;
    s.topic = &D_bcf6;
    s.word = D_bce4;
    s.answer = D_bcf8;
    s.dictionary = (const char *const *)D_24ea;
    s.keywords = (const char *const *)D_4aa8;
    s.text = talkText;
#if defined(MATCHING_BUILD)
    s.matching = 1;
#endif
    s.print = talkPrint;
    s.string = talkString;
    s.column = talkColumn;
    s.font = talkFont;
    s.getChar = talkChar;
    s.getString = talkInput;
    s.poll = talkPoll;
    s.waitTicks = talkWait;
    s.frame = talkFrame;
    s.clearKeys = talkClear;
    s.introduced = talkIntroduced;
    s.introduce = talkIntroduce;
    s.learnName = talkLearnName;
    s.partySize = talkParty;
    s.name = talkName;
    s.karma = talkKarma;
    s.changeKarma = talkChangeKarma;
    s.pay = talkPay;
    s.give = talkGive;
    s.join = talkJoin;
    s.guards = talkGuards;
    s.greetingRoll = talkGreeting;
    s.observe = talkObserve;
    return s;
}

static void TALK_1180(void)
{
    int local_4;

    if (D_5958 == 0)
    {
        ULTIMA_1850_PrintString(_TEXT(0x94dc, "\nSomething was stolen!\n"));
        ULTIMA_43ae_AudioSweepTone(800, 2000, 1, 0x32);
        ULTIMA_207e_srand(ULTIMA_2056_GetTime());

        if ((D_57ac | D_57ad | D_57ae) != 0)
        {
            do
            {
                while (1)
                {
                    switch (ULTIMA_2092_RandomRange(0, 2))
                    {
                    case 0:
                        if (D_57ac != 0)
                        {
                            ULTIMA_3f36_DecreaseByte(&D_57ac, 1);
                            return;
                        }
                        break;

                    case 1:
                        if (D_57ad != 0)
                        {
                            ULTIMA_3f36_DecreaseByte(&D_57ad, 1);
                            return;
                        }
                        break;

                    case 2:
                        if (D_57ae != 0)
                        {
                            ULTIMA_3f36_DecreaseByte(&D_57ae, 1);
                            return;
                        }
                        break;
                    default:
                        return;
                    }
                }
            } while (D_57ad == 0);

            ULTIMA_3f36_DecreaseByte(&D_57ad, 1); // BUG?
        }
        else
        {
            for (local_4 = 0x2f; local_4 >= 0; local_4--)
            {
                if (D_57c0[local_4] != 0)
                {
                    ULTIMA_3f36_DecreaseByte(&D_57c0[local_4], 1);
                    return;
                }
            }

            for (local_4 = 7; local_4 >= 0; local_4--)
            {
                if (D_5828[local_4] != 0)
                {
                    ULTIMA_3f36_DecreaseByte(&D_5828[local_4], 1);
                    return;
                }
            }

            for (local_4 = 7; local_4 >= 0; local_4--)
            {
                if (D_5820[local_4] != 0)
                {
                    ULTIMA_3f36_DecreaseByte(&D_5820[local_4], 1);
                    return;
                }
            }

            ULTIMA_3f54_DecreaseInt(&D_57aa, ULTIMA_2092_RandomRange(1, 0xf));
            ULTIMA_2900_UpdateVitalsDisplay();
        }
    }
}

#if !defined(TARGET_DOS16)
static uint8_t *TALK_LoadConversation(const char *name, unsigned wanted) {
    s_conversation = NULL;
    s_conversationSize = 0;
    FILE *file = FILE_Open(name, "rb");
    uint8_t *bank = NULL;
    long size = 0;
    if (file && !fseek(file, 0, SEEK_END)) {
        size = ftell(file);
        if (size > 0 && size <= DIALOGUE_BANK_MAX && !fseek(file, 0, SEEK_SET)) {
            bank = malloc((size_t)size);
            if (bank && fread(bank, 1, size, file) != (size_t)size) {
                free(bank);
                bank = NULL;
            }
        }
    }
    if (file)
        fclose(file);
    if (!bank || !DIALOGUE_ValidateBank(bank, size)) {
        free(bank);
        DEBUG_Error("Cannot load conversation bank '%s'", name);
        return NULL;
    }
    unsigned count = DIALOGUE_Extended(bank, size) ? DIALOGUE_U32(bank + 8) : DIALOGUE_U16(bank);
    for (unsigned i = 0; i < count; ++i) {
        unsigned id;
        size_t offset, length;
        if (DIALOGUE_BankEntry(bank, size, i, &id, &offset, &length) && id == wanted) {
            s_conversation = bank + offset;
            s_conversationSize = length;
            break;
        }
    }
    if (!s_conversation) {
        free(bank);
        DEBUG_Error("Conversation %u missing from '%s'", wanted, name);
        return NULL;
    }
    return bank;
}
#endif

int TALK_LookNpc(unsigned actor) {
#if !defined(TARGET_DOS16)
    if (D_5893_map_id < 1 || D_5893_map_id > 128 || !MAPS_Outside(D_5893_map_id)) return 0;
    int slot = TOWN_011e(actor);
    if (slot < 0 || NPC_ObjectKind(slot) >= 0) return 0;
    unsigned id = D_5f5e[slot]._a;
    if (!id || id >= 128) return 0; /* Shops and actors without conversations keep ordinary Look. */
    char customName[32];
    const char *resource;
    if (MAPS_NewLocation(D_5893_map_id)) {
        snprintf(customName, sizeof(customName), "LOC%03u.TLK", D_5893_map_id); resource = customName;
    } else resource = D_4aa0[(D_5893_map_id - 1) >> 3];
    uint8_t *saved = s_conversation; size_t savedSize = s_conversationSize;
    uint8_t *bank = TALK_LoadConversation(resource, id);
    if (!bank) { s_conversation = saved; s_conversationSize = savedSize; return 0; }
    int known = (learnedNames[D_5893_map_id - 1][slot / 8] & (1u << (slot % 8))) != 0;
    const char *avatar = (const char *)D_55a8_party[0].name;
    size_t length = IMPERA_TalkLookText(s_conversation, s_conversationSize, known, avatar, NULL, 0);
    /* Leave room for punctuation so the word wrapper sees the final word
     * and its period together, including names that exactly fill a row. */
    char *text = length ? malloc(length + 1) : NULL;
    if (text) IMPERA_TalkLookText(s_conversation, s_conversationSize, known, avatar, text, length);
    s_conversation = saved; s_conversationSize = savedSize; free(bank);
    if (!text) return 0;
    size_t n = strlen(text);
    if (n && text[n-1] != '.' && text[n-1] != '!' && text[n-1] != '?') {
        text[n] = '.'; text[n+1] = 0;
    }
    ULTIMA_1850_PrintString("\nYou see "); ULTIMA_1850_PrintString(text);
    ULTIMA_16ba_PrintChar('\n'); free(text); return 1;
#else
    (void)actor; return 0;
#endif
}

static void TALK_127e_StartTalk(int param_1)
{
#if !defined(TARGET_DOS16)
    char customName[32];
    const char *resource;
    if (MAPS_NewLocation(D_5893_map_id)) {
        snprintf(customName, sizeof(customName), "LOC%03u.TLK", D_5893_map_id);
        resource = customName;
    } else
        resource = D_4aa0[(D_5893_map_id - 1) >> 3];
    uint8_t *bank = TALK_LoadConversation(resource, param_1);
    if (!bank)
        return;
#else
    uint local_4;
    s16 *local_6;
    s16 local_8, local_a, local_c;
    local_4 = (D_5893_map_id - 1) >> 3;
    ULTIMA_256e_ReadFileFromDisk(D_4aa0[local_4], D_b21e, 0x200, 0);

    // 12a9
    local_6 = (s16*)D_b21e;
    local_c = *local_6++;

    // 12ad
    for (local_a = 1; *local_6++ != param_1 && local_a <= local_c; local_6++, local_a++)
    {
    }

    // 12d6
    local_8 = *local_6;
    ULTIMA_256e_ReadFileFromDisk(D_4aa0[local_4], D_b21e, 0x400, local_8);

#endif
    ImperaTalk session = nativeTalk();
    IMPERA_TalkRun(&session);
#if !defined(TARGET_DOS16)
    D_bcde = NULL;
    s_conversation = NULL;
    s_conversationSize = 0;
    free(bank);
#endif

    TALK_1180();
}
