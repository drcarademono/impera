#ifndef IMPERA_DIALOGUE_GAME_H
#define IMPERA_DIALOGUE_GAME_H
#include "dialogue_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Byte spans are native records, not pointers into a presumed contiguous globals block. */
typedef struct ImperaDialogueGame {
    uint8_t *party, *partyCount, *food, *gold, *equipment;
    uint8_t *supplies[11], *karma, *turn, *map, *killed, *met;
    uint8_t *actors, *npcs, *schedules, *shapes, *dirty;
    int npc;
    unsigned npcCount, actorCount,
        mapCount;                    /* Zero retains the original 32-record host contract. */
    uint8_t *extraKilled, *extraMet; /* 4096 flag bytes per location, slots >=32. */
    void *host;
    int (*random)(void *, int, int);
    void (*vitals)(void *);
} ImperaDialogueGame;
int IMPERA_GamePay(ImperaDialogueGame *, int);
int IMPERA_GameGive(ImperaDialogueGame *, uint8_t);
int IMPERA_GameJoin(ImperaDialogueGame *, ImperaTalk *);
void IMPERA_GameGuards(ImperaDialogueGame *);
void IMPERA_GameDespawn(ImperaDialogueGame *, int);
int IMPERA_GameSchedule(const uint8_t *, uint8_t);
int IMPERA_GameMet(ImperaDialogueGame *);
void IMPERA_GameIntroduce(ImperaDialogueGame *);
#ifdef __cplusplus
}
#endif
#endif
