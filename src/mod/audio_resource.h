#ifndef IMPERA_AUDIO_RESOURCE_H
#define IMPERA_AUDIO_RESOURCE_H
#include "package.h"
#include <string.h>
#define MOD_MAX_AUDIO (64u * 1024u * 1024u)
#define AUDIO_ASSET_LIMIT 4095
/* IAUDIO01: header (counts, CRC), variable clips, then 16-byte assignments.
 * Clip fields: kind,id,loop,gain%,loop-start/end ms,name bytes,audio bytes.
 * Assignment scopes: 1 map (key=map, level=world plane), 2 event, 3 music slot, 4 sound effect
 * slot, 5 combat map (key=bank 1/2, level=page). */
typedef struct {
    unsigned kind, id, loop, gain, start, end, nameSize, size;
    const unsigned char *name, *data;
} ModAudioClip;
static inline unsigned MA_U32(const void *p) {
    const unsigned char *b = (const unsigned char *)p;
    return b[0] | (unsigned)b[1] << 8 | (unsigned)b[2] << 16 | (unsigned)b[3] << 24;
}
static inline void MA_Put(void *p, unsigned n) {
    unsigned char *b = (unsigned char *)p;
    for (int i = 0; i < 4; i++)
        b[i] = n >> (i * 8);
}
static inline size_t MA_Clip(const unsigned char *b, size_t at, ModAudioClip *c) {
    c->kind = MA_U32(b + at);
    c->id = MA_U32(b + at + 4);
    c->loop = MA_U32(b + at + 8);
    c->gain = MA_U32(b + at + 12);
    c->start = MA_U32(b + at + 16);
    c->end = MA_U32(b + at + 20);
    c->nameSize = MA_U32(b + at + 24);
    c->size = MA_U32(b + at + 28);
    c->name = b + at + 32;
    c->data = c->name + c->nameSize;
    return at + 32 + c->nameSize + c->size;
}
static inline int MA_Validate(const void *data, size_t n) {
    const unsigned char *b = (const unsigned char *)data;
    size_t at = 24;
    unsigned seen[2][128] = {0};
    if (!b || n < 24 || n > MOD_MAX_AUDIO || memcmp(b, "IAUDIO01", 8) || MA_U32(b + 20) ||
        MA_U32(b + 8) > 1024 || MA_U32(b + 12) > 4096 ||
        MOD_Crc32(b + 24, n - 24) != MA_U32(b + 16))
        return 0;
    for (unsigned i = 0; i < MA_U32(b + 8); i++) {
        if (n - at < 32)
            return 0;
        unsigned kind = MA_U32(b + at), id = MA_U32(b + at + 4), len = MA_U32(b + at + 24),
                 size = MA_U32(b + at + 28);
        if (kind > 1 || !id || id > AUDIO_ASSET_LIMIT || !len || len > 128 || size > n - at - 32 ||
            len > n - at - 32 - size || memchr(b + at + 32, 0, len))
            return 0;
        if (seen[kind][id / 32] & (1u << (id % 32)))
            return 0;
        seen[kind][id / 32] |= 1u << (id % 32);
        ModAudioClip c;
        at = MA_Clip(b, at, &c);
        if (c.loop > 1 || c.gain > 200 || (kind && (c.loop || c.start || c.end)) ||
            (c.end && c.start >= c.end) || c.start > 86400000 || c.end > 86400000)
            return 0;
        if (!size && ((!kind && id > 17) || (kind && id > 115)))
            return 0;
        if (size &&
            !((size >= 12 && !memcmp(c.data, "RIFF", 4) && !memcmp(c.data + 8, "WAVE", 4)) ||
              (size >= 4 && !memcmp(c.data, "OggS", 4))))
            return 0;
    }
    unsigned count = MA_U32(b + 12);
    if (n - at != (size_t)count * 16)
        return 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *a = b + at + i * 16;
        unsigned scope = MA_U32(a), key = MA_U32(a + 4), level = MA_U32(a + 8),
                 track = MA_U32(a + 12);
        if (scope < 1 || scope > 5 || track > AUDIO_ASSET_LIMIT ||
            (scope == 1 && (key > 255 || level > 255)) ||
            (scope == 2 && (!key || key > 11 || level)) ||
            (scope == 3 && (!key || key > 17 || level)))
            return 0;
        if (scope == 5 && (key < 1 || key > 2 || level > 65535))
            return 0;
        if (scope == 4 && (!key || key > 115 || level))
            return 0;
        if (track > (scope == 4 ? 115 : 17) &&
            !(seen[scope == 4][track / 32] & (1u << (track % 32))))
            return 0;
        for (unsigned j = 0; j < i; j++)
            if (!memcmp(a, b + at + j * 16, 12))
                return 0;
    }
    return 1;
}
#endif
