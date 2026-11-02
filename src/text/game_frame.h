#ifndef IMPERA_GAME_FRAME_H
#define IMPERA_GAME_FRAME_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ImperaGameFrame {
    void *host;
    uint8_t blue, white;
    void (*fill)(void *, uint8_t, int, int, int, int);
    void (*line)(void *, uint8_t, int, int, int, int);
    void (*corner)(void *, uint8_t, int, int, uint8_t);
    unsigned partyRows; /* Zero retains the original six-row layout. */
} ImperaGameFrame;
void IMPERA_DrawGameFrame(const ImperaGameFrame *);
/* Exact conversation crop of the original 320x200 frame, with no invented edges. */
enum {
    IMPERA_TALK_FRAME_X = 185,
    IMPERA_TALK_FRAME_Y = 80,
    IMPERA_TALK_FRAME_WIDTH = 135,
    IMPERA_TALK_FRAME_HEIGHT = 112,
    IMPERA_TALK_FRAME_TEXT_X = 7,
    IMPERA_TALK_FRAME_TEXT_Y = 8
};
#ifdef __cplusplus
}
#endif
#endif
