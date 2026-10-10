#include "package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Cursor {
    const unsigned char *p;
    size_t left;
} Cursor;
static int take(Cursor *c, void *dest, size_t size) {
    if (size > c->left)
        return 0;
    if (dest)
        memcpy(dest, c->p, size);
    c->p += size;
    c->left -= size;
    return 1;
}
static int number(Cursor *c, uint32_t *value) {
    unsigned char b[4];
    if (!take(c, b, 4))
        return 0;
    *value = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}
uint32_t MOD_Crc32(const void *data, size_t size) {
    const unsigned char *p = data;
    uint32_t crc = ~0u;
    while (size--) {
        crc ^= *p++;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return ~crc;
}
int MOD_AllowedResource(const char *name) {
    size_t n = name ? strlen(name) : 0;
    if (n < 4 || n > 32)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (!((name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= '0' && name[i] <= '9') || name[i] == '.' ||
              name[i] == '_'))
            return 0;
    if (strstr(name, ".."))
        return 0;
    if (!strcmp(name, "DATA.OVL") || !strcmp(name, "INIT.GAM") || !strcmp(name, "INIT.OOL") ||
        !strcmp(name, "BRIT.OOL") || !strcmp(name, "UNDER.OOL"))
        return 1;
    const char *ext = strrchr(name, '.');
    return ext &&
           (!strcmp(ext, ".DAT") || !strcmp(ext, ".NPC") || !strcmp(ext, ".TLK") || !strcmp(ext, ".16") ||
            !strcmp(ext, ".CH") || !strcmp(ext, ".HCS") || !strcmp(ext, ".CBT"));
}
void MOD_Free(ModPackage *package) {
    if (!package)
        return;
    for (unsigned i = 0; i < package->count; i++)
        free(package->entries[i].data);
    memset(package, 0, sizeof(*package));
}
int MOD_Decode(const void *bytes, size_t size, ModBaseReader reader, void *context, ModPackage *result,
               char *error, size_t errorSize) {
    Cursor c = {(const unsigned char *)bytes, size};
    uint32_t count, titleLength;
    size_t totalOutput = 0;
    const char *reason = "Malformed or truncated mod package";
    unsigned char magic[8];
    memset(result, 0, sizeof(*result));
    if (size > MOD_MAX_PACKAGE || !reader || !take(&c, magic, 8) || memcmp(magic, "IMOD0001", 8) ||
        !number(&c, &titleLength) || titleLength == 0 || titleLength > 256 || !number(&c, &count) ||
        count == 0 || count > MOD_MAX_ENTRIES || !take(&c, result->title, titleLength) ||
        memchr(result->title, 0, titleLength))
        goto fail;
    for (unsigned i = 0; i < count; i++) {
        ModResource *entry = &result->entries[i];
        uint32_t nameLength, baseSize, outSize, baseCrc, outCrc, spans;
        unsigned char *base = NULL;
        size_t actualSize = 0;
        if (!number(&c, &nameLength) || nameLength == 0 || nameLength > 32 ||
            !take(&c, entry->name, nameLength) || memchr(entry->name, 0, nameLength) ||
            !MOD_AllowedResource(entry->name) || !number(&c, &baseSize) || !number(&c, &outSize) ||
            !number(&c, &baseCrc) || !number(&c, &outCrc) || !number(&c, &spans) ||
            baseSize > MOD_MAX_RESOURCE || outSize == 0 || outSize > MOD_MAX_RESOURCE ||
            spans > MOD_MAX_RESOURCE / 8)
            goto fail;
        if (outSize > MOD_MAX_PACKAGE - totalOutput) {
            reason = "Decoded package exceeds memory limit";
            goto fail;
        }
        totalOutput += outSize;
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(entry->name, result->entries[j].name)) {
                reason = "Duplicate resource in mod package";
                goto fail;
            }
        if (!reader(context, entry->name, &base, &actualSize)) {
            reason = "Required original resource is missing";
            free(base);
            goto fail;
        }
        if (actualSize != baseSize || MOD_Crc32(base, actualSize) != baseCrc) {
            reason = "Mod requires a different original resource (size/checksum mismatch)";
            free(base);
            goto fail;
        }
        entry->data = calloc(1, outSize);
        if (!entry->data) {
            free(base);
            reason = "Out of memory loading mod";
            goto fail;
        }
        entry->size = outSize;
        result->count = i + 1;
        memcpy(entry->data, base, baseSize < outSize ? baseSize : outSize);
        free(base);
        uint32_t end = 0;
        for (unsigned j = 0; j < spans; j++) {
            uint32_t offset, length;
            if (!number(&c, &offset) || !number(&c, &length) || length == 0 || offset < end ||
                offset > outSize || length > outSize - offset || !take(&c, entry->data + offset, length))
                goto fail;
            end = offset + length;
        }
        if (MOD_Crc32(entry->data, outSize) != outCrc) {
            reason = "Mod output checksum mismatch";
            goto fail;
        }
    }
    if (c.left) {
        reason = "Unexpected data after mod package";
        goto fail;
    }
    if (errorSize)
        error[0] = 0;
    return 1;
fail:
    if (errorSize)
        snprintf(error, errorSize, "%s", reason);
    MOD_Free(result);
    return 0;
}
