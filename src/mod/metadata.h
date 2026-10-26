#ifndef IMPERA_MOD_METADATA_H
#define IMPERA_MOD_METADATA_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define MODINFO_MAX_TEXT 4096
#define MODINFO_MAX_IMAGE (256u * 1024u)
static inline unsigned MODINFO_U32(const unsigned char *p) {
    return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}
/* Header, UTF-8 description (no embedded NUL), optional 320x200 PNG. */
static inline int MODINFO_Validate(const void *data, size_t size) {
    const unsigned char *p = (const unsigned char *)data;
    if (!p || size < 16 || memcmp(p, "IMETA001", 8)) return 0;
    unsigned text = MODINFO_U32(p + 8), image = MODINFO_U32(p + 12);
    if (text > MODINFO_MAX_TEXT || image > MODINFO_MAX_IMAGE || size != 16u + text + image ||
        memchr(p + 16, 0, text)) return 0;
    /* Reject malformed UTF-8 and control characters except line breaks/tabs. */
    for (unsigned i = 0; i < text;) {
        unsigned c = p[16 + i++], n = 0, min = 0;
        if (c < 128) { if (c < 32 && c != 9 && c != 10 && c != 13) return 0; continue; }
        if (c >= 0xc2 && c <= 0xdf) { c &= 31; n = 1; min = 128; }
        else if (c >= 0xe0 && c <= 0xef) { c &= 15; n = 2; min = 2048; }
        else if (c >= 0xf0 && c <= 0xf4) { c &= 7; n = 3; min = 65536; }
        else return 0;
        if (i + n > text) return 0;
        while (n--) { unsigned b = p[16 + i++]; if ((b & 192) != 128) return 0; c = (c << 6) | (b & 63); }
        if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return 0;
    }
    if (image) {
        const unsigned char *png = p + 16 + text;
        static const unsigned char signature[] = {137,80,78,71,13,10,26,10};
        if (image < 33 || memcmp(png, signature, 8) || memcmp(png + 12, "IHDR", 4) ||
            png[16] || png[17] || png[18] != 1 || png[19] != 64 ||
            png[20] || png[21] || png[22] || png[23] != 200) return 0;
    }
    return 1;
}
#endif
