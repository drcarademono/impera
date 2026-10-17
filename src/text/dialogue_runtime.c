/* Extracted from native talk.c. Keep execution quirks; validate against the independent oracle. */
#include "dialogue_runtime.h"
#include "text_window.h"
#include <string.h>
#define _STATIC_TEXT(address, text) text
const char *const IMPERA_TalkDictionary[128] = {
#include "dialogue_dictionary.inc"
};
const char *const IMPERA_TalkKeywords[35] = {
#include "dialogue_keywords.inc"
};
#undef _STATIC_TEXT
#define _TEXT(address, literal) (s->text ? s->text(s->host, address, literal) : literal)
#define TALK_KEY_NAME 0
#define TALK_KEY_DESC 1
#define TALK_KEY_2 2
#define TALK_KEY_3 3
#define TALK_KEY_4 4
static int stopped(ImperaTalk *s) { return s->error || (s->cancelled && s->cancelled(s->host)); }
static uint8_t peek(ImperaTalk *s) {
    if (stopped(s))
        return 0;
    if (*s->cursor < s->data || *s->cursor >= s->data + s->size) {
        s->error = 1;
        s->errorText = "Conversation reads past its boundary";
        return 0;
    }
    return **s->cursor;
}
static uint8_t readByte(ImperaTalk *s) {
    uint8_t ch = peek(s);
    if (!stopped(s))
        ++*s->cursor;
    return ch;
}
static int TALK_0f32_ProcessChar(ImperaTalk *, uint8_t);
static int TALK_0b04(ImperaTalk *);
int IMPERA_TalkMatch(const uint8_t *param_1, const char *param_2) {
    uint8_t bVar1;
    int iVar2;
    int iVar3;
    int local_18;
    char local_14[10];

    local_18 = -1;
    memset(local_14, 0, 10);

    for (iVar2 = 0; iVar2 < 9 && (iVar3 = iVar2, param_1[iVar2] != 0); iVar2++) {
        bVar1 = param_1[iVar2] & 0x7f;
        if (bVar1 >= 'a' && bVar1 <= 'z') {
            bVar1 = (param_1[iVar2] & 0x7fU) - 0x20;
        }
        local_14[iVar2] = (char)bVar1;
    }

    iVar2 = strncmp(local_14, param_2, iVar3);
    if (iVar2 == 0) {
        local_18 = 0;
    }

    return local_18;
}

static void TALK_04d2_PrintCr(ImperaTalk *s) { TALK_0f32_ProcessChar(s, 0x80 | 0xd); }

static void TALK_04da_PrintQuoteMark(ImperaTalk *s) {
    if (!s->explicitQuotes)
        TALK_0f32_ProcessChar(s, 0x80 | 0x22);
}

static int explicitAt(ImperaTalk *s, const uint8_t *cursor) {
    return cursor >= s->data && cursor <= s->data + s->size &&
           s->size - (size_t)(cursor - s->data) >= IMPERA_TALK_EXPLICIT_QUOTES_SIZE &&
           !memcmp(cursor, IMPERA_TALK_EXPLICIT_QUOTES, IMPERA_TALK_EXPLICIT_QUOTES_SIZE);
}
static void startResponse(ImperaTalk *s) {
    const uint8_t *cursor = *s->cursor;
    // Aliases use the final response's quotation policy, not the alias keyword.
    while (cursor < s->data + s->size && (size_t)(s->data + s->size - cursor) >= 2 &&
           cursor[0] == 0x87 && cursor[1] == 0) {
        cursor += 2;
        while (cursor < s->data + s->size && !*cursor)
            ++cursor;
        while (cursor < s->data + s->size && *cursor)
            ++cursor;
        if (cursor < s->data + s->size)
            ++cursor;
    }
    s->explicitQuotes = explicitAt(s, cursor);
    // A bare transfer must not leave an empty speech quote ahead of narration.
    if (!s->explicitQuotes && cursor < s->data + s->size) {
        int label = *cursor >= 0x91 && *cursor <= 0x9f ? *cursor : 0;
        if (*cursor == 0x8c && (size_t)(s->data + s->size - cursor) >= 2 && s->introduced(s->host))
            label = cursor[1];
        if (*cursor == 0xfe && (size_t)(s->data + s->size - cursor) >= 3 &&
            s->karma(s->host) >= cursor[1])
            label = cursor[2];
        for (size_t i = 0;
             label >= 0x91 && label <= 0x9f && i + 2 + IMPERA_TALK_EXPLICIT_QUOTES_SIZE <= s->size;
             ++i)
            if (s->data[i] == 0x90 && s->data[i + 1] == label && explicitAt(s, s->data + i + 2)) {
                s->explicitQuotes = 1;
                break;
            }
    }
    TALK_04da_PrintQuoteMark(s);
}

static ImperaWords nativeWords(ImperaTalk *s) {
    ImperaWords w = {0};
    w.buffer = (uint8_t *)s->word;
    w.length = s->length;
    w.lastColumn = s->lastColumn;
    w.matching = s->matching;
    w.host = s->host;
    w.column = s->column;
    w.font = s->font;
    w.print = s->print;
    return w;
}
static void TALK_04e2_PrintWord(ImperaTalk *s) {
    ImperaWords w = nativeWords(s);
    IMPERA_WordFlush(&w);
    if(s->wordBoundary) s->wordBoundary(s->host);
}
static void TALK_0574_BuildOrPrintWord(ImperaTalk *s, uint8_t ch) {
    ImperaWords w = nativeWords(s);
    IMPERA_WordAppend(&w, ch);
    if(s->wordBoundary && ((ch&127)==' ' || (ch&127)=='\n')) s->wordBoundary(s->host);
}
static int TALK_0b04(ImperaTalk *s);

static int TALK_05b6(ImperaTalk *s) {
    int amount = (s->operands[0] & 127) * 100 + (s->operands[1] & 127) * 10 +
                 (s->operands[2] & 127) - 0x14d0;
    if (s->pay(s->host, amount) == 0)
        return 0;
    (*s->length) = 0;
    s->print(s->host, '"');
    s->string(s->host, _TEXT(0x9328, "Thou hast not enough gold!"));
    s->print(s->host, '"');
    s->string(s->host, _TEXT(0x9344, "\n\n"));
    (*s->operandCount) = (*s->pending) = 0;
    return TALK_0b04(s);
}

static void TALK_0682(ImperaTalk *s, uint8_t code) {
    if (code >= 48 && code < 64) {
        s->error = 1;
        s->errorText = "Invalid equipment item";
        return;
    }
    s->give(s->host, code);
}

// skip one talk string
static int TALK_0728_SkipOneTalkString(ImperaTalk *s, uint8_t param_1, uint8_t param_2) {
    uint8_t local_4;

    do {
        local_4 = readByte(s);
        if (local_4 == param_1) {
            return 1;
        }

        if (local_4 == param_2) {
            return 0;
        }
    } while (!stopped(s));
    return 0;
}

// CHECKED
// select nth talk string
static void TALK_075a_SelectNthTalkString(ImperaTalk *s, int param_1) {
    (*s->cursor) = s->data;

    if (param_1 != 0) {
        for (; param_1 != 0; param_1--) {
            TALK_0728_SkipOneTalkString(s, 0, 0x90);
        }
    }
}

static int TALK_0f32_ProcessChar(ImperaTalk *s, uint8_t param_1);

// process talk string
static int TALK_0788_ProcessTalkString(ImperaTalk *s) {
    if (explicitAt(s, *s->cursor)) {
        s->explicitQuotes = 1;
        *s->cursor += IMPERA_TALK_EXPLICIT_QUOTES_SIZE;
    }
    if (++s->depth > 128) {
        s->error = 1;
        s->errorText = "Conversation nesting limit exceeded";
        --s->depth;
        return 1;
    }
    while (!stopped(s) && peek(s) != 0) {
        if (s->observe)
            s->observe(s->host, *s->cursor);
        if (TALK_0f32_ProcessChar(s, readByte(s)) != 0) {
            --s->depth;
            return 1;
        }
    }

    --s->depth;
    return stopped(s) ? 1 : 0;
}

static int TALK_07aa_ProcessNthTalkString(ImperaTalk *s, uint8_t param_1) {
    TALK_075a_SelectNthTalkString(s, param_1);
    return TALK_0788_ProcessTalkString(s);
}

static int spokenNth(ImperaTalk *s, uint8_t index) {
    TALK_075a_SelectNthTalkString(s, index);
    startResponse(s);
    return TALK_0788_ProcessTalkString(s);
}

static int TALK_07be(ImperaTalk *s) {
    (*s->cursor)++;

    while (!stopped(s) && peek(s) == 0) {
        (*s->cursor)++;
    }

    while (!stopped(s) && readByte(s) != 0) {
        // empty
    }

    return TALK_0788_ProcessTalkString(s);
}

static void TALK_07e4_PrintAvatarName(ImperaTalk *s) {
    const uint8_t *local_4;
    local_4 = s->name(s->host, 0);

    while (*local_4 != 0) {
        TALK_0f32_ProcessChar(s, *local_4++ | 0x80);
    }
}

// CHECKED
static int TALK_080a_Join(ImperaTalk *s) { return s->join(s->host); }

static int TALK_093a(ImperaTalk *s) {
    (*s->cursor) = s->data;
    while (!stopped(s) && peek(s) != (*s->label)) {
        TALK_0728_SkipOneTalkString(s, 0x90, 0x9f);
    }
    (*s->cursor)++;
    startResponse(s);
    return TALK_0788_ProcessTalkString(s);
}

static int TALK_0960(ImperaTalk *s) {
    TALK_0728_SkipOneTalkString(s, 0, 0x90);
    startResponse(s);
    return TALK_0788_ProcessTalkString(s);
}

static int TALK_096e(ImperaTalk *s) {
    (*s->cursor) = s->data;
    while (!stopped(s) && peek(s) != (*s->label)) {
        TALK_0728_SkipOneTalkString(s, 0x90, 0x9f);
    }
    TALK_0728_SkipOneTalkString(s, 0, 0x9f);
    startResponse(s);
    return TALK_0788_ProcessTalkString(s);
}

static int TALK_099a(ImperaTalk *s, int param_1) {
    (*s->cursor) = s->data;
    param_1 = param_1 * 2 + 5;

    while (param_1 != 0) {
        if (TALK_0728_SkipOneTalkString(s, 0, 0x90) == 0)
            return 0;

        param_1--;
    }

    return 1;
}

/* Match exactly what the ordinary topic prompt accepts, without running a
 * response or changing the live conversation cursor. Question answers and
 * profanity commands are intentionally not advertised as topic links. */
int IMPERA_TalkIsTopic(const uint8_t *data,size_t size,const char *word) {
    if(!data || !size || size>1024 || !word || !*word) return 0;
    char answer[65];size_t n=0;
    for(;word[n] && n<64;n++) answer[n]=word[n]>='a'&&word[n]<='z'?word[n]-32:word[n];
    if(word[n]) return 0;
    answer[n]=0;
    for(int i=0;i<34;i++) if(IMPERA_TalkMatch((const uint8_t*)IMPERA_TalkKeywords[i],answer)==0) return i<5;
    uint8_t *cursor=(uint8_t*)data;
    ImperaTalk s={0};s.data=(uint8_t*)data;s.size=size;s.cursor=&cursor;
    for(int topic=0;topic<128 && !s.error;topic++) {
        if(!TALK_099a(&s,topic) || s.error) break;
        uint8_t key[10]={0};size_t k=0;
        while(k<9 && cursor+k<data+size && cursor[k]) {key[k]=cursor[k];++k;}
        if(!k || cursor+k>=data+size || cursor[k]) return 0;
        if(IMPERA_TalkMatch(key,answer)==0) return 1;
    }
    return 0;
}

static int TALK_09d8(ImperaTalk *s) {
    int local_4;

    (*s->topic) = 0;
    while (!stopped(s)) {
        if (TALK_099a(s, (*s->topic)) == 0) {
            return 0;
        }

        local_4 = IMPERA_TalkMatch((*s->cursor), s->answer);
        if (local_4 != -1 && (local_4 == 0 || s->answer[local_4 - 1] == ' ')) {
            return 1;
        }

        (*s->cursor)++;
        (*s->topic)++;
    }

    return 1;
}

static void TALK_0a2c_GetString(ImperaTalk *s) {
    TALK_04e2_PrintWord(s);
    s->getString(s->host, s->answer, 0xf);
}

static int TALK_0a3c(ImperaTalk *s) {
    if (spokenNth(s, TALK_KEY_4) == 0) {
        TALK_04da_PrintQuoteMark(s);
        TALK_04d2_PrintCr(s);
    }
    return 1;
}

static int TALK_0a54(ImperaTalk *s, uint8_t param_1) {
    int local_4;

    switch (param_1) {
    case 0:
        // ca1c
        if ((*s->questionMode) != 0) {
            return 2;
        }

        s->string(s->host, _TEXT(0x93c2, "\"My name is "));
        if (TALK_07aa_ProcessNthTalkString(s, TALK_KEY_NAME) != 0) {
            return 1;
        }

        TALK_04da_PrintQuoteMark(s);
        TALK_04d2_PrintCr(s);
        TALK_04d2_PrintCr(s);
        return 0;

    case 1:
    case 2:
        // ca3e
        if ((*s->questionMode) != 0) {
            return 2;
        }

        if (spokenNth(s, TALK_KEY_3) != 0) {
            return 1;
        }

        TALK_04da_PrintQuoteMark(s);
        TALK_04d2_PrintCr(s);
        TALK_04d2_PrintCr(s);
        return 0;

    case 3:
    case 4:
        // ca60
        if ((*s->questionMode) != 0) {
            return 2;
        }

        return TALK_0a3c(s);

    default:
        // c9f8
        s->string(s->host,
                  _TEXT(0x93d0, "\"With language like that, how did you become an Avatar?"));
        TALK_04da_PrintQuoteMark(s);
        TALK_04d2_PrintCr(s);
        TALK_04d2_PrintCr(s);

        for (local_4 = 0; local_4 < 0x1c; local_4++) {
            s->frame(s->host);
            if (s->poll(s->host) != 0)
                break;

            s->waitTicks(s->host, 1);
        }

        s->clearKeys(s->host);
        return 0;
    }
}

static int TALK_0b04(ImperaTalk *s) {
    int local_8;
    int local_6;
    uint8_t local_4;

    while (!stopped(s)) {
        s->explicitQuotes = 0;
        (*s->questionMode) = 0;
        s->string(s->host, _TEXT(0x9408, "Your interest?\n:"));
        if(s->topicPrompt) s->topicPrompt(s->host,1);
        TALK_0a2c_GetString(s);
        if(s->topicPrompt) s->topicPrompt(s->host,0);
        if (s->answer[0] == 0) {
            s->string(s->host, _TEXT(0x941a, "BYE\n\n"));
            return TALK_0a3c(s);
        }
        TALK_04d2_PrintCr(s);
        TALK_04d2_PrintCr(s);
        for (local_4 = 0, local_6 = -1; local_4 < 0x22; local_4++) {
            local_8 = IMPERA_TalkMatch((uint8_t *)s->keywords[local_4], s->answer);
            if (local_8 != -1 && (local_8 == 0 || s->answer[local_8 - 1] == ' ')) {
                local_6 = TALK_0a54(s, local_4);
                if (local_6 == 0) {
                    break;
                }

                if (local_6 == 1) {
                    return 1;
                }
            }
        }

        if (local_6 != 0) {
            if (TALK_09d8(s) == 0) {
                s->string(s->host, _TEXT(0x9420, "\"I cannot help thee with that."));
                TALK_04da_PrintQuoteMark(s);
                TALK_04d2_PrintCr(s);
                TALK_04d2_PrintCr(s);
            } else {
                if (spokenNth(s, (*s->topic) * 2 + 6) != 0) {
                    return 1;
                }
                TALK_04da_PrintQuoteMark(s);
                TALK_04d2_PrintCr(s);
                TALK_04d2_PrintCr(s);
            }
        }
    }
    return 1;
}

static int TALK_0bd4(ImperaTalk *s) {
    int local_4;

    (*s->cursor) = s->data;
    while (!stopped(s) && peek(s) != (*s->label)) {
        TALK_0728_SkipOneTalkString(s, 0x90, 0x9f);
    }

    TALK_0728_SkipOneTalkString(s, 0, 0x9f);
    TALK_0728_SkipOneTalkString(s, 0, 0x9f);

    while (!stopped(s)) {
        local_4 = IMPERA_TalkMatch((*s->cursor), s->answer);
        if (local_4 != -1 && (local_4 == 0 || s->answer[local_4 - 1] == ' ')) {
            return 1;
        }

        if (TALK_0728_SkipOneTalkString(s, 0, 0x90) == 0) {
            return 0;
        }

        if (TALK_0728_SkipOneTalkString(s, 0, 0x90) == 0) {
            return 0;
        }
    }
    return 0;
}

// CHECKED
// process label?
static int TALK_0c5c(ImperaTalk *s) {
    int local_6;
    int local_8;
    uint8_t local_4;

    do {
        if (TALK_093a(s) != 0) {
            return 1;
        }

        TALK_04da_PrintQuoteMark(s);

        do {
            s->explicitQuotes = 0;
            TALK_04d2_PrintCr(s);
            TALK_04d2_PrintCr(s);
            (*s->questionMode) = 0xff;
            s->string(s->host, _TEXT(0x9440, "You respond-\n:"));
            TALK_0a2c_GetString(s);
            if (s->answer[0] == 0) {
                s->string(s->host, _TEXT(0x9450, "\n\n\"What didst thou say?"));
            }
        } while (!stopped(s) && s->answer[0] == 0);

        TALK_04d2_PrintCr(s);
        TALK_04d2_PrintCr(s);
        for (local_4 = 0, local_6 = -1; local_4 < 0x22; local_4++) {
            local_8 = IMPERA_TalkMatch((const uint8_t *)s->keywords[local_4], s->answer);
            if (local_8 != -1 && (local_8 == 0 || s->answer[local_8 - 1] == ' ')) {
                local_6 = TALK_0a54(s, local_4);
                if (local_6 == 0) {
                    break;
                }

                if (local_6 == 1) {
                    return 1;
                }
            }
        }
    } while (!stopped(s) && local_6 == 0);

    if (TALK_0bd4(s) == 0) {
        local_6 = TALK_096e(s);
    } else {
        local_6 = TALK_0960(s);
    }

    if (local_6 == 0) {
        TALK_04da_PrintQuoteMark(s);
        TALK_04d2_PrintCr(s);
        TALK_04d2_PrintCr(s);
        return TALK_0b04(s);
    } else {
        return 1;
    }
}

// NOT MATCHING (u32 operation)
// set npc met flag
static int TALK_0d42(ImperaTalk *s, int slot) {
    (void)slot;
    s->introduce(s->host);
    return 0;
}

// NOT MATCHING (u32 operation)
// check npc met flag
static int TALK_0d7a(ImperaTalk *s, int slot) {
    (void)slot;
    return s->introduced(s->host);
}

static int TALK_0dbe(ImperaTalk *s, uint8_t param_1) {
    switch ((*s->pending)) {
    default:
        // 0dda
        (*s->operandCount) = (*s->pending) = 0;
        // 0dfc ->
        // 0e74
        break;

    case 0x85:
        // 0de4
        s->operands[(*s->operandCount)++] = param_1;
        if ((*s->operandCount) != 3) {
            // 0dfc
            return 0;
        }

        // 0e02
        if (TALK_05b6(s) != 0) {
            // -> 0e74
            return 1;
        }

        // 0dda
        (*s->operandCount) = (*s->pending) = 0;
        break;

    case 0x86:
        // 0e0e
        TALK_0682(s, param_1 & 0x7f);

        // 0dda
        (*s->operandCount) = (*s->pending) = 0;
        break;

    case 0x8c:
        // 0e1c
        (*s->operandCount) = (*s->pending) = 0;

        if (TALK_0d7a(s, 0) == 0) {
            // 0dfc
            return 0;
        }

        if (param_1 == 0xff) {
            return TALK_0b04(s);
        }

        // 0e3a
        (*s->label) = param_1;

        // 0e3d
        return TALK_0c5c(s);

    case 0xfe:
        // 0e46
        s->operands[(*s->operandCount)++] = param_1;
        if ((*s->operandCount) != 2) {
            // 0dfc
            return 0;
        }

        (*s->operandCount) = (*s->pending) = 0;

        if (s->karma(s->host) < s->operands[0]) {
            // 0dfc
            return 0;
        }

        (*s->label) = s->operands[1];

        // 0e3d
        return TALK_0c5c(s);
    }

    return 0;
}

// CHECKED
static void TALK_0e78_AskName(ImperaTalk *s) {
    int local_e;
    int local_c;
    int local_a;
    char local_8[5];

    local_a = 0;
    TALK_04da_PrintQuoteMark(s);
    TALK_04e2_PrintWord(s);
    s->string(s->host, _TEXT(0x9468, "What is thy name?\"\n"));
    s->string(s->host, _TEXT(0x947c, "\nYou respond-\n:"));
    TALK_0a2c_GetString(s);

    (*s->pending) = (*s->operandCount) = 0;

    if (s->answer[0] == 0) {
        s->string(s->host, _TEXT(0x948c, "\n\n\"If you say so..."));
    } else {
        for (; local_a < s->partySize(s->host); local_a++) {
            for (local_c = 0; local_c < 4; local_c++) {
                local_8[local_c] = s->name(s->host, local_a)[local_c];
            }

            local_8[4] = 0;

            local_e = IMPERA_TalkMatch((const uint8_t *)local_8, s->answer);
            if (local_e != -1 && (local_e == 0 || (s->answer[local_e - 1] == ' '))) {
                TALK_0d42(s, 0);
                s->string(s->host, _TEXT(0x94a0, "\n\n\"A pleasure!"));
                return;
            }
        }

        s->string(s->host, _TEXT(0x948c, "\n\n\"If you say so..."));
    }
}

static int TALK_0f32_ProcessChar(ImperaTalk *s, uint8_t param_1) {
    if (stopped(s))
        return 1;
    uint8_t *local_4;
    int local_6;
    const char *pbVar5;

    if ((*s->pending) != 0) {
        return TALK_0dbe(s, param_1);
    }

    if (!s->matching) {
        /* Controls can transfer/finish the response or replace the word buffer.
         * Workshop text need not end in a space: print it before the control runs.
         * Operand bytes were handled above, so they cannot trigger this flush. */
        if ((param_1 >= 0x81 && param_1 <= 0x8c) || param_1 == 0x8e || param_1 == 0x8f ||
            (param_1 >= 0x91 && param_1 <= 0x9f) || param_1 == 0xfe || param_1 == 0xff) {
            if ((*s->length) && s->column(s->host) + (uint16_t)(*s->length) >= 0x12)
                s->print(s->host, '\n');
            TALK_04e2_PrintWord(s);
        }
    }

    // 0f4c
    (*s->operandCount) = 0;
    if ((param_1 == 0xa2) && ((*s->lastLiteral) == 0xa2)) {
        // 0f5e
        return 0;
    }

    // 0f64
    switch (param_1) {
    case 0x81: // avatar
        // 0f86
        TALK_07e4_PrintAvatarName(s);
        break;
    case 0x82: // end conversation
        // 0f8c
        return 1;
    case 0x83: // pause
        // 0f92
        for (local_6 = 0; local_6 < 0x1c; local_6++) {
            s->frame(s->host);
            if (s->poll(s->host) != 0)
                break;
            s->waitTicks(s->host, 1);
        }
        s->clearKeys(s->host);
        return 0; // -> 0f5e
    case 0x84:    // join
        // 0fb6
        return TALK_080a_Join(s); // -> 1114
    case 0x87:                    // key_or
        // 0fbc
        local_4 = (*s->cursor);
        if (TALK_07be(s) != 0) {
            return 1;
        }
        (*s->cursor) = local_4;
        return 0; // -> 0f5e
    case 0x88:    // ask name
        // 0fd2
        local_6 = s->explicitQuotes;
        s->explicitQuotes = 0;
        TALK_0e78_AskName(s);
        s->explicitQuotes = local_6;
        if (s->explicitQuotes) {
            *s->lastLiteral = 0;
            TALK_0f32_ProcessChar(s, 0xa2);
        }
        return 0; // -> 0f5e
    case 0x89:    // karma +1
        // 0fd8
        s->changeKarma(s->host, 1);
        return 0; // -> 0f5e
    case 0x8a:    // karma -1
        // 0fea
        s->changeKarma(s->host, -1);
        return 0; // -> 0f5e
    case 0x8b:    // call guards
        // 0ff8
        s->guards(s->host);
        return 0; // -> 0f5e
    case 0x8e:    // rune
        // 0ffe
        (*s->mask) ^= 0x80;
        return 0; // -> 0f5e
    case 0xff:    // no selection
        // 1006
        TALK_04e2_PrintWord(s);
        return TALK_0b04(s); // -> 1114
    case 0x8f:               // wait
        // 1010
        s->getChar(s->host);
        return 0; // -> 0f5e
    case 0xfe:
    case 0x85: // gold-
    case 0x86: // change item 65
    case 0x8c: // if else name
        // 1016
        (*s->pending) = param_1;
        return 0;
    case 0x91: // label_0
    case 0x92: // label_1
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0x97:
    case 0x98:
    case 0x99:
    case 0x9a:
    case 0x9b: // label_10
    case 0x9c: // label?
    case 0x9d: // label?
    case 0x9e: // label?
    case 0x9f: // label?
        // 1020
        (*s->label) = param_1;
        return TALK_0c5c(s); // -> 1114
        // ----------
    default:
        // 10ce
        if (param_1 < 0x81) {
            TALK_0574_BuildOrPrintWord(s, 0xa0); // 0x20

            pbVar5 = param_1 ? s->dictionary[param_1 - 1] : NULL;
            if (!pbVar5) {
                s->error = 1;
                s->errorText = "Invalid conversation dictionary word";
                return 1;
            }
            while (*pbVar5 != 0) {
                TALK_0574_BuildOrPrintWord(s, (uint8_t)*pbVar5++ | 0x80);
            }

            if (*s->dictionary[param_1 - 1] == 0) {
                TALK_0574_BuildOrPrintWord(s, param_1);
                return 0; // -> 0f5e
            }

            (*s->dictionaryBoundary) = 1;
        } else {
            // 1062
            param_1 |= 0x80;
            if (param_1 == 0x8d) // 0xd newline
            {
                param_1 = 0x8a;
            }

            (*s->lastLiteral) = param_1 &= (*s->mask);

            if ((*s->dictionaryBoundary) != 0) {
                TALK_0574_BuildOrPrintWord(s, 0xa0); // 0x20
            }

            TALK_0574_BuildOrPrintWord(s, param_1);
            (*s->dictionaryBoundary) = 0;
        }
        break;
    }

    return 0;
}

static int TALK_111c(ImperaTalk *s) {
    s->string(s->host, _TEXT(0x94c4, "You see "));

    if (TALK_07aa_ProcessNthTalkString(s, TALK_KEY_DESC) != 0) {
        return 1;
    } else {
        TALK_04d2_PrintCr(s);
        TALK_04d2_PrintCr(s);
        if (TALK_0d7a(s, 0) == 0) {
            s->explicitQuotes = 0;
            if (s->greetingRoll(s->host) != 0) {
                // "I am called\nNAME"\n\n
                s->string(s->host, _TEXT(0x94ce, "\"I am called "));
                if (TALK_07aa_ProcessNthTalkString(s, TALK_KEY_NAME) == 0) {
                    TALK_04da_PrintQuoteMark(s);
                    TALK_04d2_PrintCr(s);
                    TALK_04d2_PrintCr(s);
                } else {
                    return 1;
                }
            }
        } else {
            if (spokenNth(s, TALK_KEY_2) == 0) {
                TALK_04da_PrintQuoteMark(s);
                TALK_04d2_PrintCr(s);
                TALK_04d2_PrintCr(s);
            } else {
                return 1;
            }
        }

        return 0;
    }
}

void IMPERA_TalkBind(ImperaTalk *s, ImperaTalkStorage *v, size_t size) {
    memset(s, 0, sizeof(*s));
    memset(v, 0, sizeof(*v));
    v->mask = 255;
    v->cursor = v->data;
    s->data = v->data;
    s->cursor = &v->cursor;
    s->operands = v->operands;
    s->pending = &v->pending;
    s->operandCount = &v->operandCount;
    s->mask = &v->mask;
    s->length = &v->length;
    s->questionMode = &v->questionMode;
    s->lastColumn = &v->lastColumn;
    s->lastLiteral = &v->lastLiteral;
    s->dictionaryBoundary = &v->dictionaryBoundary;
    s->label = &v->label;
    s->topic = &v->topic;
    s->word = v->word;
    s->answer = v->answer;
    s->size = size;
    s->dictionary = IMPERA_TalkDictionary;
    s->keywords = IMPERA_TalkKeywords;
}
int IMPERA_TalkCharacter(ImperaTalk *s, uint8_t ch) { return TALK_0f32_ProcessChar(s, ch); }
void IMPERA_TalkFlush(ImperaTalk *s) { TALK_04e2_PrintWord(s); }
void IMPERA_TalkAppend(ImperaTalk *s, uint8_t ch) { TALK_0574_BuildOrPrintWord(s, ch); }
void IMPERA_TalkSelect(ImperaTalk *s, int entry) { TALK_075a_SelectNthTalkString(s, entry); }
int IMPERA_TalkDescription(ImperaTalk *s) { return TALK_111c(s); }
int IMPERA_TalkTopics(ImperaTalk *s) { return TALK_0b04(s); }
int IMPERA_TalkFarewell(ImperaTalk *s) { return TALK_0a3c(s); }
int IMPERA_TalkRun(ImperaTalk *s) {
    if (!stopped(s) && TALK_111c(s) == 0 && !stopped(s) && TALK_0b04(s) == 0 && !stopped(s))
        TALK_0a3c(s);
    if (!stopped(s))
        TALK_04e2_PrintWord(s);
    return stopped(s) ? (s->error ? -1 : 1) : 0;
}
int IMPERA_TalkCompare(const char *a, const char *b) {
    while (1) {
        int x = ((uint8_t)*a++) & 127, y = ((uint8_t)*b++) & 127;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return 0;
        if (!*a && !*b)
            return 1;
    }
}
