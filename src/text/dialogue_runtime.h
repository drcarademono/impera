#ifndef IMPERA_DIALOGUE_RUNTIME_H
#define IMPERA_DIALOGUE_RUNTIME_H
#include <stdint.h>
#include "dialogue_format.h"
#include <stddef.h>
/* Entry-level extension: authored quotation marks, no implicit speech wrapper.
 * Recognized only at a response start; ordinary DOS text remains unchanged. */
#define IMPERA_TALK_EXPLICIT_QUOTES "\xfc\xfd\xfc\xfb"
#define IMPERA_TALK_EXPLICIT_QUOTES_SIZE 4
#ifdef __cplusplus
extern "C" {
#endif
/* Host operations involving the game remain explicit. No world success is invented. */
typedef struct ImperaTalk {
    uint8_t *data, **cursor, *operands, *pending, *operandCount, *mask;
    uint8_t *length, *questionMode, *lastColumn, *lastLiteral, *dictionaryBoundary, *label;
    uint16_t *topic;
    char *word, *answer;
    size_t size;
    int matching, error, depth, explicitQuotes;
    const char *errorText;
    const char *const *dictionary, *const *keywords;
    const char *(*text)(void *, uint16_t, const char *);
    void *host;
    int (*cancelled)(void *);
    void (*print)(void *, uint16_t);
    void (*string)(void *, const char *);
    int (*column)(void *);
    void (*font)(void *, int);
    int (*getChar)(void *);
    void (*getString)(void *, char *, int);
    int (*poll)(void *);
    void (*waitTicks)(void *, int);
    void (*frame)(void *);
    void (*clearKeys)(void *);
    int (*introduced)(void *);
    void (*introduce)(void *);
    int (*partySize)(void *);
    const uint8_t *(*name)(void *, int);
    int (*karma)(void *);
    void (*changeKarma)(void *, int);
    int (*pay)(void *, int);
    void (*give)(void *, uint8_t);
    int (*join)(void *);
    void (*guards)(void *);
    int (*greetingRoll)(void *);
    void (*observe)(void *, const uint8_t *);
    void (*learnName)(void *);
} ImperaTalk;
typedef struct ImperaTalkStorage {
    uint8_t data[1024], *cursor, operands[4], pending, operandCount, mask;
    uint8_t length, questionMode, lastColumn, lastLiteral, dictionaryBoundary, label;
    uint16_t topic;
    char word[16], answer[16];
} ImperaTalkStorage;
void IMPERA_TalkBind(ImperaTalk *, ImperaTalkStorage *, size_t);
int IMPERA_TalkRun(ImperaTalk *);
/* Pure, action-free name/description decoding. Returns required bytes including
 * NUL (zero for invalid/empty text); pass NULL to measure the output first. */
size_t IMPERA_TalkLookText(const uint8_t *, size_t, int known, const char *avatar, char *, size_t);
int IMPERA_TalkDescription(ImperaTalk *);
int IMPERA_TalkTopics(ImperaTalk *);
int IMPERA_TalkFarewell(ImperaTalk *);
int IMPERA_TalkCharacter(ImperaTalk *, uint8_t);
void IMPERA_TalkFlush(ImperaTalk *);
void IMPERA_TalkAppend(ImperaTalk *, uint8_t);
void IMPERA_TalkSelect(ImperaTalk *, int);
int IMPERA_TalkCompare(const char *, const char *);
int IMPERA_TalkMatch(const uint8_t *, const char *);
extern const char *const IMPERA_TalkDictionary[128];
extern const char *const IMPERA_TalkKeywords[35];
#ifdef __cplusplus
}
#endif
#endif
