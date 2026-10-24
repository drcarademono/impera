#ifndef IMPERA_NPCS_H
#define IMPERA_NPCS_H
#include <stddef.h>
#include <stdint.h>
#define IMPERA_NPC_CAPACITY 32768
#define IMPERA_NPC_BASE_SIZE 4608
#define IMPERA_NPC_EXTRA_START 4624
/* Original 4608 bytes followed by INPC0001, count, reserved, then sorted
 * 24-byte entries: settlement, reserved, slot LE16, schedule[16], shape,
 * dialogue, reserved[2]. Slot zero remains reserved for the party. */
static inline unsigned NPCS_Word(const unsigned char *p) { return p[0] | ((unsigned)p[1] << 8); }
/* Authored stationary objects reuse location records. The three behavior bytes
 * are 0x80|kind, the dialogue byte carries property 5, and time[3] property 7.
 * Native records are unchanged; kinds match Workshop's stable placement IDs. */
#define NPCS_OBJECT_MARKER 0x80u
#define NPCS_OBJECT_TRIGGER 9u
#define NPCS_OBJECT_BLOOD 22u
static inline int NPCS_ObjectKind(const void *v, unsigned shape) {
    const unsigned char *p = (const unsigned char *)v;
    return shape > 0 && shape < 64 && p[0] == p[1] && p[1] == p[2] &&
                   (p[0] & 0xe0) == NPCS_OBJECT_MARKER && (p[0] & 31) >= 5 && (p[0] & 31) <= 23
               ? (p[0] & 31)
               : -1;
}
static inline int NPCS_Blood(unsigned shape, unsigned value, unsigned aux) {
    return shape == 31 && value == 255 && aux == 0xb1;
}
#ifdef __cplusplus
extern "C" {
#endif
unsigned NPCS_Count(const void *, size_t, unsigned town);
size_t NPCS_Offset(const void *, size_t, unsigned town, unsigned slot);
size_t NPCS_ShapeOffset(const void *, size_t, unsigned town, unsigned slot);
size_t NPCS_DialogueOffset(const void *, size_t, unsigned town, unsigned slot);
int NPCS_Validate(const void *, size_t);
#ifdef __cplusplus
}
#endif
#endif
