#ifndef IMPERA_TYPES_H
#define IMPERA_TYPES_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TYPES_MAX 1024
#define TYPES_CELLS 65536
#define TYPES_FIRST_CUSTOM 512u
#define TYPES_LAST_CUSTOM 65535u
#define TYPES_HEADER 32u
#define TYPES_RECORD 256u
#define TYPES_MAP_RECORD 16u
#define TYPES_INFER_GROUND UINT32_MAX
#define TYPES_SPRITE 1u
#define TYPES_MASONRY 2u
#define TYPES_POISON 1u
#define TYPES_BURN 2u
/* Portable wire format; fields are explicitly encoded, never memcpy a struct.
 * ITYPES01 header: UUID[16] at 8, count:u32 at 24, reserved[4].
 * Each record: length:u32, id:u32, kind:u8 (0 tile,1 NPC,2 monster), base:u8,
 * filters:u8, render:u8, movement:u16, sight:u8, projectile:u8, travel:u8,
 * effects:u8, damageMin:u8, damageMax:u8, ground:u32,
 * result[4]:u32 (open,closed,locked,broken), stats[8], actorFlags:u16,
 * moveProfile:u8,range:u8,projectileAppearance:u8,companion:u8, effectModes:u16,
 * name[64],description[128],frames:u16,skiffDirections:u8,humanoidSeat:u8,combatMap:u8,widthMinus1:u8,heightMinus1:u8,animation:u8
 * (0 base pattern,1 loop). Frames: variant:u8 (0..5 local tilesets,6..11 world
 * tilesets),cellWidth:u8 (14/16/32),duration:u16 (10..60000 ms), followed by RGBA pixels. Local
 * frames multiply the cell dimensions by the actor footprint; world frames are one cell. Cell width
 * 14 means Apple II 14x16; 16 and 32 are square cells. Variants in each appearance group share a
 * frame count; the two groups animate independently. ITYMAP01 header: UUID[16],
 * count:u32,reserved[4]. Sorted cells: kind:u8 (0 map,1 BRIT.CBT,2 DUNGEON.CBT),
 * map:u8,floor:i8,x:u8,y:u8, kinds 3 NPC slots, 4 world actor slots, 5/6 encounter actor slots;
 * x=slot,y=0. mapHigh:u8,reserved[2],type:u32,reserved[4]. Save records reuse the final word for a temporary
 * door’s restore type. */
typedef struct TypeDef {
    uint32_t id, ground, result[4];
    uint16_t movement, actorFlags, frames, effectModes;
    uint8_t kind, base, filters, render, sight, projectile, travel, effects, damageMin, damageMax;
    uint8_t stats[8], moveProfile, range, projectileAppearance, companion;
    uint8_t skiffDirections, humanoidSeat, combatMap, width, height, animation;
    char name[64], description[128];
} TypeDef;
typedef struct TypeCell {
    uint8_t kind;
    uint16_t map;
    int8_t floor;
    uint8_t x, y;
    uint32_t type, restoreType;
} TypeCell;
uint32_t TYPES_U32(const void *);
void TYPES_Put32(void *, uint32_t);
void TYPES_Decode(const unsigned char *, TypeDef *);
void TYPES_Encode(unsigned char *, const TypeDef *);
void TYPES_Vanilla(unsigned kind, unsigned id, TypeDef *);
int TYPES_Walkable(unsigned actorShape, unsigned terrain);
int TYPES_DefWalkable(unsigned actorShape, const TypeDef *);
int TYPES_Validate(const void *, size_t);
int TYPES_ValidateMap(const void *, size_t, const void *, size_t);
const unsigned char *TYPES_Find(const void *, size_t, uint32_t);
static inline unsigned TYPES_FrameHeight(unsigned width) { return width == 14 ? 16 : width; }
static inline unsigned TYPES_Width(const TypeDef *d) { return d->width ? d->width : 1; }
static inline unsigned TYPES_Height(const TypeDef *d) { return d->height ? d->height : 1; }
static inline unsigned TYPES_FrameWidth(const unsigned char *p, unsigned variant, unsigned cell) {
    return cell * (variant < 6 ? 1u + p[253] : 1u);
}
static inline unsigned TYPES_FrameRows(const unsigned char *p, unsigned variant, unsigned cell) {
    return TYPES_FrameHeight(cell) * (variant < 6 ? 1u + p[254] : 1u);
}
static inline size_t TYPES_FrameBytes(const unsigned char *p, size_t off) {
    return 4 + (size_t)TYPES_FrameWidth(p, p[off], p[off + 1]) *
                   TYPES_FrameRows(p, p[off], p[off + 1]) * 4;
}
/* Shared native animation interpreter. Random callback returns a byte. */
void TYPES_AdvanceAnimation(unsigned base, unsigned char *tile, unsigned char *state,
                            unsigned (*randomByte)(void));
const unsigned char *TYPES_FrameAt(const unsigned char *, unsigned, unsigned, unsigned *,
                                   unsigned *);
void TYPES_ArtSlotSize(unsigned slot, int world, unsigned *, unsigned *);
static inline unsigned TYPES_ArtworkType(unsigned tile) { return tile & 65535u; }
const unsigned char *TYPES_FrameImage(const unsigned char *, unsigned, uint64_t, unsigned *,
                                      unsigned *);
const unsigned char *TYPES_ArtImage(uint32_t, unsigned, uint64_t, unsigned *, unsigned *);
const unsigned char *TYPES_SampleArtImage(unsigned, unsigned, uint64_t, unsigned *, unsigned *);
void TYPES_ArtSize(uint32_t, int world, unsigned *, unsigned *);
const unsigned char *TYPES_Frame(const unsigned char *, unsigned variant, uint64_t ms,
                                 unsigned *side);
void TYPES_CellDecode(const unsigned char *, TypeCell *);
void TYPES_CellEncode(unsigned char *, const TypeCell *);
uint64_t TYPES_CellKey(const TypeCell *);
/* Runtime snapshot and mutable terrain state, shared with save validation. */
void TYPES_Mount(const void *, size_t, const void *, size_t);
void TYPES_Reset(void);
void TYPES_BeginEncounter(unsigned kind, unsigned map);
int TYPES_Get(uint32_t, TypeDef *);
uint32_t TYPES_At(TypeCell, uint32_t fallback);
int TYPES_State(TypeCell, TypeCell *);
int TYPES_Defined(uint32_t);
int TYPES_Set(TypeCell, uint32_t);
void TYPES_Remove(TypeCell);
int TYPES_WriteState(FILE *);
int TYPES_ReadState(FILE *, int apply);
unsigned TYPES_ArtSlot(uint32_t);
/* Resolve fixed native actor frame overrides without replacing engine animation. */
unsigned TYPES_ActorArt(uint32_t type, unsigned nativeTile);
const unsigned char *TYPES_SampleArt(unsigned slot, unsigned variant, uint64_t ms, unsigned *side);
const unsigned char *TYPES_Art(uint32_t, unsigned variant, uint64_t ms, unsigned *side);
#ifdef __cplusplus
}
#endif
#endif
