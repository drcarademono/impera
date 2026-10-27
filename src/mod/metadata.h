#ifndef IMPERA_MOD_METADATA_H
#define IMPERA_MOD_METADATA_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define MODINFO_MAX_TEXT 4096
#define MODINFO_MAX_AUTHOR 256
#define MODINFO_MAX_IMAGE (256u * 1024u)
static inline unsigned MODINFO_U32(const unsigned char *p) {
    return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}
static inline unsigned MODINFO_Header(const unsigned char *p) { return !memcmp(p, "IMETA002", 8) ? 20 : 16; }
static inline unsigned MODINFO_AuthorSize(const unsigned char *p) { return MODINFO_Header(p) == 20 ? MODINFO_U32(p + 16) : 0; }
static inline unsigned MODINFO_ImageOffset(const unsigned char *p) {
    return MODINFO_Header(p) + MODINFO_U32(p + 8) + MODINFO_AuthorSize(p);
}
/* Version 2 adds a single-line UTF-8 author after the description. */
static inline int MODINFO_Validate(const void *data, size_t size) {
    const unsigned char *p = (const unsigned char *)data;
    if (!p || size < 16 || (memcmp(p, "IMETA001", 8) && memcmp(p, "IMETA002", 8))) return 0;
    unsigned header = MODINFO_Header(p);
    if (size < header) return 0;
    unsigned author = MODINFO_AuthorSize(p);
    unsigned text = MODINFO_U32(p + 8), image = MODINFO_U32(p + 12);
    if (text > MODINFO_MAX_TEXT || image > MODINFO_MAX_IMAGE || author > MODINFO_MAX_AUTHOR || size != header + text + author + image ||
        memchr(p + header, 0, text + author)) return 0;
    /* Reject malformed UTF-8 and control characters except line breaks/tabs. */
    for (unsigned i = 0; i < text + author;) {
        unsigned c = p[header + i++], n = 0, min = 0;
        if (c < 128) { if (c < 32 && (i > text || (c != 9 && c != 10 && c != 13))) return 0; continue; }
        if (c >= 0xc2 && c <= 0xdf) { c &= 31; n = 1; min = 128; }
        else if (c >= 0xe0 && c <= 0xef) { c &= 15; n = 2; min = 2048; }
        else if (c >= 0xf0 && c <= 0xf4) { c &= 7; n = 3; min = 65536; }
        else return 0;
        if (i + n > (i <= text ? text : text + author)) return 0;
        while (n--) { unsigned b = p[header + i++]; if ((b & 192) != 128) return 0; c = (c << 6) | (b & 63); }
        if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return 0;
    }
    if (image) {
        const unsigned char *png = p + header + text + author;
        static const unsigned char signature[] = {137,80,78,71,13,10,26,10};
        if (image < 33 || memcmp(png, signature, 8) || memcmp(png + 12, "IHDR", 4) ||
            png[16] || png[17] || png[18] != 1 || png[19] != 64 ||
            png[20] || png[21] || png[22] || png[23] != 200) return 0;
    }
    return 1;
}
#endif
