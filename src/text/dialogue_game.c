#include "dialogue_game.h"
#include <string.h>
static unsigned read16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static void write16(uint8_t *p, unsigned n) {
    p[0] = n;
    p[1] = n >> 8;
}
static void increment(uint8_t *p, int n, int cap) { *p = *p + n < cap ? *p + n : cap; }
static void vitals(ImperaDialogueGame *g) {
    if (g->vitals)
        g->vitals(g->host);
}
static unsigned npcCount(ImperaDialogueGame *g) { return g->npcCount ? g->npcCount : 32; }
static unsigned actorCount(ImperaDialogueGame *g) { return g->actorCount ? g->actorCount : 32; }
static uint8_t *flag(ImperaDialogueGame *g, uint8_t *flags) {
    if (*g->map < 1 || *g->map > (g->mapCount ? g->mapCount : 32) || g->npc < 0 ||
        (unsigned)g->npc >= npcCount(g))
        return 0;
    if (g->npc >= 32 || *g->map > 32) {
        uint8_t *extra = flags == g->met ? g->extraMet : g->extraKilled;
        return extra ? extra + (*g->map - 1) * 4096 + g->npc / 8 : 0;
    }
    return flags + (*g->map - 1) * 4 + g->npc / 8;
}
int IMPERA_GameMet(ImperaDialogueGame *g) {
    uint8_t *p = flag(g, g->met);
    return p && (*p & (1u << (g->npc % 8)));
}
void IMPERA_GameIntroduce(ImperaDialogueGame *g) {
    uint8_t *p = flag(g, g->met);
    if (p)
        *p |= 1u << (g->npc % 8);
}
int IMPERA_GamePay(ImperaDialogueGame *g, int amount) {
    int gold = (int16_t)read16(g->gold);
    if (gold < amount)
        return 1;
    write16(g->gold, gold - amount);
    vitals(g);
    if (g->npc >= 0 && (unsigned)g->npc < npcCount(g)) {
        unsigned actor = read16(g->npcs + g->npc * 16 + 12);
        if (actor < actorCount(g) && (g->actors[actor * 8] & 0xfc) == 0x6c && *g->turn >= 100) {
            *g->turn = 0;
            increment(g->karma, 1, 99);
            if (gold == amount)
                increment(g->karma, 2, 99);
        }
    }
    return 0;
}
int IMPERA_GameGive(ImperaDialogueGame *g, uint8_t code) {
    if (code < 48) {
        increment(g->equipment + code, 1, 99);
        return 1;
    }
    /* The old 48..63 equipment access escaped the 48-byte array. No portable meaning. */
    if (code < 64)
        return 0;
    if (code == 65 || code == 66) {
        uint8_t *p = code == 65 ? g->food : g->gold;
        int value = (int16_t)read16(p);
        write16(p, value + 1 < 9999 ? value + 1 : 9999);
        vitals(g);
    } else if (code >= 67 && code <= 75) {
        uint8_t *p = g->supplies[code - 65];
        if (code >= 72 && code <= 74)
            *p = 255;
        else
            increment(p, 1, 99);
    }
    return 1;
}
static const char *text(ImperaTalk *s, uint16_t address, const char *literal) {
    return s->text ? s->text(s->host, address, literal) : literal;
}
int IMPERA_GameJoin(ImperaDialogueGame *g, ImperaTalk *s) {
    uint8_t *saved = *s->cursor;
    int candidate = 15;
    char name[4] = {0}, other[4] = {0};
    uint8_t swap[32];
    if (*g->partyCount == 6) {
        s->getChar(s->host);
        s->string(s->host, text(s, 0x9348, "\"Thou hast no room for me in thy party! "));
        s->string(s->host,
                  text(s, 0x9372, "Seek me again if one of thy members doth leave\nthee."));
        return 0;
    }
    IMPERA_TalkSelect(s, 0);
    if (s->error || s->size < 3 || (size_t)(*s->cursor - s->data) > s->size - 3)
        return 1;
    memcpy(name, *s->cursor, 3);
    *s->cursor += 3;
    for (;;) {
        memcpy(other, g->party + candidate * 32, 3);
        if (IMPERA_TalkCompare(name, other))
            break;
        if (--candidate == 0) {
            IMPERA_TalkAppend(s, 0x22);
            IMPERA_TalkAppend(s, 10);
            s->string(s->host, text(s, 0x93a8, "\nSystem Error -\nNo Match!"));
            *s->cursor = saved;
            return 1;
        }
    }
    g->party[candidate * 32 + 31] = 0;
    memcpy(swap, g->party + candidate * 32, 32);
    memcpy(g->party + candidate * 32, g->party + *g->partyCount * 32, 32);
    memcpy(g->party + *g->partyCount * 32, swap, 32);
    ++*g->partyCount;
    if (g->npc >= 0 && (unsigned)g->npc < npcCount(g)) {
        int shape = g->shapes[g->npc] & 0xfc;
        uint8_t *p = flag(g, g->killed);
        if (p && ((shape != 0x70 && shape < 0x80) || shape == 0xb4))
            *p |= 1u << (g->npc % 8);
        IMPERA_GameDespawn(g, g->npc);
    }
    vitals(g);
    IMPERA_TalkFlush(s);
    *s->cursor = saved;
    return 1;
}
void IMPERA_GameDespawn(ImperaDialogueGame *g, int slot) {
    if (slot < 0 || (unsigned)slot >= npcCount(g))
        return;
    uint8_t *npc = g->npcs + slot * 16;
    unsigned actor = read16(npc + 12);
    if (actor < actorCount(g)) {
        uint8_t *p = g->actors + actor * 8;
        memset(p, 0, 5);
        p[6] = p[7] = 0;
    }
    memset(npc, 0, 8);
    npc[10] = npc[11] = 0;
    memset(g->schedules + slot * 16, 0, 3);
    g->shapes[slot] = 0;
    *g->dirty |= 2;
}
void IMPERA_GameGuards(ImperaDialogueGame *g) {
    int i;
    for (i = 0; i < (int)npcCount(g); ++i) {
        uint8_t *npc = g->npcs + i * 16, *schedule = g->schedules + i * 16;
        int shape = g->shapes[i];
        if (!read16(npc))
            continue;
        if (shape == 0xfc || shape == 0xd8 || shape == 0x70) {
            memset(schedule + 12, 0, 4);
            memset(schedule, shape >= 0x2f ? 7 : 6, 3);
        } else if (g->random(g->host, 0, 255) < 128 && shape >= 0x40 && shape < 0x74 &&
                   (read16(npc + 10) == 0xfe || schedule[12] || schedule[13] || schedule[14] ||
                    schedule[15])) {
            write16(npc + 10, 0xfd);
            memset(schedule, 3, 3);
        }
    }
}
int IMPERA_GameSchedule(const uint8_t *s, uint8_t hour) {
    uint8_t delta = hour - s[12], next;
    int result = 0;
    next = hour - s[13];
    if (next < delta) {
        delta = next;
        result = 1;
    }
    next = hour - s[14];
    if (next < delta) {
        delta = next;
        result = 2;
    }
    next = hour - s[15];
    if (next < delta)
        result = 1;
    return result;
}
