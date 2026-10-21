#include "location_names.h"
#include "npcs.h"
#include "package.h"
#include "dialogue_bank.h"
#include "contents.h"
#include "creation_scene.h"
#include "cutscenes.h"
#include "engine_tables.h"
#include "narrative.h"
#include "tileset_resources.h"
#include "types.h"
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
    if (MOD_TilesetSheet(name))
        return 1;
    if (!strcmp(name, "LOCATIONS.OVR") || !strcmp(name, "TYPES.OVR") ||
        !strcmp(name, "TYPEMAP.OVR"))
        return 1;
    if (!strcmp(name, "CUTSCENE.OVR") || !strcmp(name, "TEXT.OVR") || !strcmp(name, "RULES.TBL") || !strcmp(name, "CONTENTS.OVR"))
        return 1;
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
    if (size >= 8 && memcmp(bytes, "IMOD0001", 8) && memcmp(bytes, "IMOD0002", 8) &&
        memcmp(bytes, "IMOD0003", 8) && memcmp(bytes, "IMOD0004", 8) &&
        memcmp(bytes, "IMOD0005", 8) && memcmp(bytes, "IMOD0006", 8) &&
        memcmp(bytes, "IMOD0007", 8) && memcmp(bytes, "IMOD0008", 8) &&
        (memcmp(bytes, "IMOD0009", 8) &&
         (memcmp(bytes, "IMOD0010", 8) &&
          (memcmp(bytes, "IMOD0011", 8) &&
           (memcmp(bytes, "IMOD0012", 8) &&
            (memcmp(bytes, "IMOD0013", 8) &&
             (memcmp(bytes, "IMOD0014", 8) &&
              (memcmp(bytes, "IMOD0015", 8) &&
               (memcmp(bytes, "IMOD0016", 8) &&
                (memcmp(bytes, "IMOD0017", 8) && memcmp(bytes, "IMOD0018", 8))))))))))) {
        reason = "Unsupported mod package version; update Impera";
        goto fail;
    }
    if (size > MOD_MAX_PACKAGE || !reader || !take(&c, magic, 8) ||
        (memcmp(magic, "IMOD0001", 8) && memcmp(magic, "IMOD0002", 8) &&
         memcmp(magic, "IMOD0003", 8) && memcmp(magic, "IMOD0004", 8) &&
         memcmp(magic, "IMOD0005", 8) && memcmp(magic, "IMOD0006", 8) &&
         memcmp(magic, "IMOD0007", 8) && memcmp(magic, "IMOD0008", 8) &&
         (memcmp(magic, "IMOD0009", 8) &&
          (memcmp(magic, "IMOD0010", 8) &&
           (memcmp(magic, "IMOD0011", 8) &&
            (memcmp(magic, "IMOD0012", 8) &&
             (memcmp(magic, "IMOD0013", 8) &&
              (memcmp(magic, "IMOD0014", 8) &&
               (memcmp(magic, "IMOD0015", 8) &&
                (memcmp(magic, "IMOD0016", 8) &&
                 (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8))))))))))) ||
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
        if (!strcmp(entry->name, "LOCATIONS.OVR")) {
            if ((memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8)) || baseSize ||
                baseCrc) {
                reason = "Location names require package version 17 and an empty base";
                goto fail;
            }
            base = calloc(1, 1);
            if (!base)
                goto fail;
        } else if (!strcmp(entry->name, "TYPES.OVR") || !strcmp(entry->name, "TYPEMAP.OVR")) {
            if ((memcmp(magic, "IMOD0009", 8) &&
                 (memcmp(magic, "IMOD0010", 8) &&
                  (memcmp(magic, "IMOD0011", 8) &&
                   (memcmp(magic, "IMOD0012", 8) &&
                    (memcmp(magic, "IMOD0013", 8) &&
                     (memcmp(magic, "IMOD0014", 8) &&
                      (memcmp(magic, "IMOD0015", 8) &&
                       (memcmp(magic, "IMOD0016", 8) &&
                        (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8)))))))))) ||
                baseSize || baseCrc) {
                reason = "Custom types require package version 9 and an empty base";
                goto fail;
            }
            base = calloc(1, 1);
            if (!base)
                goto fail;
        } else if (MOD_TilesetSheet(entry->name)) {
            if (baseSize || baseCrc) {
                reason = "Tileset PNG overrides require an empty base";
                goto fail;
            }
            base = calloc(1, 1);
            if (!base) {
                reason = "Out of memory loading tileset override";
                goto fail;
            }
        } else if (!strcmp(entry->name, "CUTSCENE.OVR") || !strcmp(entry->name, "TEXT.OVR") ||
                   !strcmp(entry->name, "RULES.TBL") || !strcmp(entry->name, "CONTENTS.OVR")) {
            if ((memcmp(magic, "IMOD0002", 8) && memcmp(magic, "IMOD0003", 8) &&
                 memcmp(magic, "IMOD0004", 8) && memcmp(magic, "IMOD0005", 8) &&
                 memcmp(magic, "IMOD0006", 8) && memcmp(magic, "IMOD0007", 8) &&
                 memcmp(magic, "IMOD0008", 8) &&
                 (memcmp(magic, "IMOD0009", 8) &&
                  (memcmp(magic, "IMOD0010", 8) &&
                   (memcmp(magic, "IMOD0011", 8) &&
                    (memcmp(magic, "IMOD0012", 8) &&
                     (memcmp(magic, "IMOD0013", 8) &&
                      (memcmp(magic, "IMOD0014", 8) &&
                       (memcmp(magic, "IMOD0015", 8) &&
                        (memcmp(magic, "IMOD0016", 8) &&
                         (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8))))))))))) ||
                (!strcmp(entry->name, "RULES.TBL") && memcmp(magic, "IMOD0003", 8) &&
                 memcmp(magic, "IMOD0004", 8) && memcmp(magic, "IMOD0005", 8) &&
                 memcmp(magic, "IMOD0006", 8) && memcmp(magic, "IMOD0007", 8) &&
                 memcmp(magic, "IMOD0008", 8) &&
                 (memcmp(magic, "IMOD0009", 8) &&
                  (memcmp(magic, "IMOD0010", 8) &&
                   (memcmp(magic, "IMOD0011", 8) &&
                    (memcmp(magic, "IMOD0012", 8) &&
                     (memcmp(magic, "IMOD0013", 8) &&
                      (memcmp(magic, "IMOD0014", 8) &&
                       (memcmp(magic, "IMOD0015", 8) &&
                        (memcmp(magic, "IMOD0016", 8) &&
                         (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8))))))))))) ||
                (!strcmp(entry->name, "CONTENTS.OVR") && memcmp(magic, "IMOD0004", 8) &&
                 memcmp(magic, "IMOD0005", 8) && memcmp(magic, "IMOD0006", 8) &&
                 memcmp(magic, "IMOD0007", 8) && memcmp(magic, "IMOD0008", 8) &&
                 (memcmp(magic, "IMOD0009", 8) &&
                  (memcmp(magic, "IMOD0010", 8) &&
                   (memcmp(magic, "IMOD0011", 8) &&
                    (memcmp(magic, "IMOD0012", 8) &&
                     (memcmp(magic, "IMOD0013", 8) &&
                      (memcmp(magic, "IMOD0014", 8) &&
                       (memcmp(magic, "IMOD0015", 8) &&
                        (memcmp(magic, "IMOD0016", 8) &&
                         (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8))))))))))) ||
                (!strcmp(entry->name, "CUTSCENE.OVR") && memcmp(magic, "IMOD0006", 8) &&
                 memcmp(magic, "IMOD0007", 8) && memcmp(magic, "IMOD0008", 8) &&
                 (memcmp(magic, "IMOD0009", 8) &&
                  (memcmp(magic, "IMOD0010", 8) &&
                   (memcmp(magic, "IMOD0011", 8) &&
                    (memcmp(magic, "IMOD0012", 8) &&
                     (memcmp(magic, "IMOD0013", 8) &&
                      (memcmp(magic, "IMOD0014", 8) &&
                       (memcmp(magic, "IMOD0015", 8) &&
                        (memcmp(magic, "IMOD0016", 8) &&
                         (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8))))))))))) ||
                baseSize || baseCrc) {
                reason =
                    "Companion overrides require a supported package version and an empty base";
                goto fail;
            }
            base = calloc(1, 1);
            if (!base) {
                reason = "Out of memory loading companion overrides";
                goto fail;
            }
        } else if (!reader(context, entry->name, &base, &actualSize)) {
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
        const ModTilesetSheet *sheet = MOD_TilesetSheet(entry->name);
        if (sheet && !MOD_TilesetPngHeader(sheet, entry->data, outSize)) {
            reason = "Tileset PNG has an invalid header or source dimensions";
            goto fail;
        }
        if (MOD_Crc32(entry->data, outSize) != outCrc) {
            reason = "Mod output checksum mismatch";
            goto fail;
        }
        if ((!strcmp(entry->name, "IBM.CH") || !strcmp(entry->name, "RUNES.CH")) &&
            entry->size != 1024) {
            reason = "Invalid color font dimensions";
            goto fail;
        }
        if ((!strcmp(entry->name, "IBM.HCS") || !strcmp(entry->name, "RUNES.HCS")) &&
            entry->size != 3072) {
            reason = "Invalid Hercules font dimensions";
            goto fail;
        }
        if (!strcmp(entry->name, "LOCATIONS.OVR") &&
            !LOCATIONS_Validate(entry->data, entry->size)) {
            reason = "Invalid location names";
            goto fail;
        }
        if (!strcmp(entry->name, "CUTSCENE.OVR") &&
            (!U5_ValidateCutscenes(entry->data, entry->size) ||
             (!memcmp(entry->data, "ICUT0002", 8) && memcmp(magic, "IMOD0007", 8) &&
              memcmp(magic, "IMOD0008", 8) &&
              (memcmp(magic, "IMOD0009", 8) &&
               (memcmp(magic, "IMOD0010", 8) &&
                (memcmp(magic, "IMOD0011", 8) &&
                 (memcmp(magic, "IMOD0012", 8) &&
                  (memcmp(magic, "IMOD0013", 8) &&
                   (memcmp(magic, "IMOD0014", 8) &&
                    (memcmp(magic, "IMOD0015", 8) &&
                     (memcmp(magic, "IMOD0016", 8) &&
                      (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8))))))))))) ||
             (!memcmp(entry->data, "ICUT0003", 8) && memcmp(magic, "IMOD0008", 8) &&
              (memcmp(magic, "IMOD0009", 8) &&
               (memcmp(magic, "IMOD0010", 8) &&
                (memcmp(magic, "IMOD0011", 8) &&
                 (memcmp(magic, "IMOD0012", 8) &&
                  (memcmp(magic, "IMOD0013", 8) &&
                   (memcmp(magic, "IMOD0014", 8) &&
                    (memcmp(magic, "IMOD0015", 8) &&
                     (memcmp(magic, "IMOD0016", 8) &&
                      (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8))))))))))))) {
            reason = "Unsupported or invalid cutscene pages";
            goto fail;
        }
        const char *extension = strrchr(entry->name, '.');
        if (extension && !strcmp(extension, ".TLK") &&
            (!DIALOGUE_ValidateBank(entry->data, entry->size) ||
             (DIALOGUE_Extended(entry->data, entry->size) &&
              (memcmp(magic, "IMOD0014", 8) &&
               (memcmp(magic, "IMOD0015", 8) &&
                (memcmp(magic, "IMOD0016", 8) &&
                 (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8)))))))) {
            reason = "Invalid conversation bank or extended dialogue requires package version 14";
            goto fail;
        }
        if (extension && !strcmp(extension, ".TLK") &&
            (memcmp(magic, "IMOD0015", 8) &&
             (memcmp(magic, "IMOD0016", 8) &&
              (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8))))) {
            unsigned count = DIALOGUE_Extended(entry->data, entry->size)
                                 ? DIALOGUE_U32(entry->data + 8)
                                 : DIALOGUE_U16(entry->data);
            for (unsigned c = 0; c < count; ++c) {
                unsigned id;
                size_t offset, length;
                if (DIALOGUE_BankEntry(entry->data, entry->size, c, &id, &offset, &length) &&
                    DIALOGUE_SkipsOpening(entry->data + offset, length)) {
                    reason =
                        "Conversation opening settings require package version 15; update Impera";
                    goto fail;
                }
            }
        }
        if (extension && !strcmp(extension, ".TLK") &&
            (memcmp(magic, "IMOD0016", 8) &&
             (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8)))) {
            unsigned count = DIALOGUE_Extended(entry->data, entry->size)
                                 ? DIALOGUE_U32(entry->data + 8)
                                 : DIALOGUE_U16(entry->data);
            for (unsigned c = 0; c < count; ++c) {
                unsigned id;
                size_t offset, length;
                if (DIALOGUE_BankEntry(entry->data, entry->size, c, &id, &offset, &length) &&
                    DIALOGUE_GargoyleSpeech(entry->data + offset, length)) {
                    reason = "Gargoyle speech requires package version 16; update Impera";
                    goto fail;
                }
            }
        }
        if (extension && !strcmp(extension, ".NPC") &&
            (!NPCS_Validate(entry->data, entry->size) ||
             (entry->size > IMPERA_NPC_BASE_SIZE &&
              (memcmp(magic, "IMOD0012", 8) &&
               (memcmp(magic, "IMOD0013", 8) &&
                (memcmp(magic, "IMOD0014", 8) &&
                 (memcmp(magic, "IMOD0015", 8) &&
                  (memcmp(magic, "IMOD0016", 8) &&
                   (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8)))))))))) {
            reason = "Extended NPC records require package version 12 and valid schedules; update "
                     "Impera";
            goto fail;
        }
        if (!strcmp(entry->name, "CONTENTS.OVR") && !CONTENTS_Validate(entry->data, entry->size)) {
            reason = "Unsupported or invalid map contents schema";
            goto fail;
        }
        if (!strcmp(entry->name, "RULES.TBL") && !U5_ValidateTables(entry->data, entry->size)) {
            reason = "Unsupported or invalid engine table schema/value";
            goto fail;
        }
        if (!strcmp(entry->name, "MISCMAPS.DAT")) {
            U5CreationPreview state;
            if (entry->size <= 1216 || entry->size > 2704 ||
                !U5_CreationPreview(entry->data + 704, entry->size - 704, ~0u, &state)) {
                reason = "Invalid character-creation scene sequence";
                goto fail;
            }
        }
        if (!strcmp(entry->name, "TEXT.OVR")) {
            int valid;
            U5_FindNarrativeText(entry->data, entry->size, NULL, &valid);
            if (!valid) {
                reason = "Unsupported or invalid narrative override schema/entry/token";
                goto fail;
            }
        }
    }
    const ModResource *types = NULL, *mapTypes = NULL;
    for (unsigned i = 0; i < result->count; i++) {
        if (!strcmp(result->entries[i].name, "TYPES.OVR"))
            types = &result->entries[i];
        if (!strcmp(result->entries[i].name, "TYPEMAP.OVR"))
            mapTypes = &result->entries[i];
    }
    if (types && (memcmp(magic, "IMOD0010", 8) &&
                  (memcmp(magic, "IMOD0011", 8) &&
                   (memcmp(magic, "IMOD0012", 8) &&
                    (memcmp(magic, "IMOD0013", 8) &&
                     (memcmp(magic, "IMOD0014", 8) &&
                      (memcmp(magic, "IMOD0015", 8) &&
                       (memcmp(magic, "IMOD0016", 8) &&
                        (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8)))))))))) {
        size_t off = 32;
        if (TYPES_Validate(types->data, types->size))
            for (unsigned i = 0; i < TYPES_U32(types->data + 24); i++) {
                if (types->data[off + 8]) {
                    reason = "Actor types require package version 10; update Impera";
                    goto fail;
                }
                off += TYPES_U32(types->data + off);
            }
    }
    if (types &&
        (memcmp(magic, "IMOD0011", 8) &&
         (memcmp(magic, "IMOD0012", 8) &&
          (memcmp(magic, "IMOD0013", 8) &&
           (memcmp(magic, "IMOD0014", 8) &&
            (memcmp(magic, "IMOD0015", 8) &&
             (memcmp(magic, "IMOD0016", 8) &&
              (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8)))))))) &&
        TYPES_Validate(types->data, types->size)) {
        size_t off = 32;
        for (unsigned i = 0; i < TYPES_U32(types->data + 24); ++i) {
            const unsigned char *r = types->data + off;
            size_t frame = 256;
            if (r[253] || r[254]) {
                reason = "Multi-tile actors require package version 11; update Impera";
                goto fail;
            }
            for (unsigned j = 0; j < (r[248] | (unsigned)r[249] << 8); ++j) {
                if (r[frame] >= 6) {
                    reason = "World actor artwork requires package version 11; update Impera";
                    goto fail;
                }
                frame += TYPES_FrameBytes(r, frame);
            }
            off += TYPES_U32(r);
        }
    }
    if (types && TYPES_Validate(types->data, types->size) && memcmp(magic, "IMOD0018", 8)) {
        for (size_t off = 32; off < types->size; off += TYPES_U32(types->data + off))
            if (TYPES_RecordLook(types->data + off, TYPES_U32(types->data + off))) {
                reason = "Actor Look descriptions require package version 18; update Impera";
                goto fail;
            }
    }
    if (types && TYPES_Validate(types->data, types->size) &&
        (memcmp(magic, "IMOD0013", 8) &&
         (memcmp(magic, "IMOD0014", 8) &&
          (memcmp(magic, "IMOD0015", 8) &&
           (memcmp(magic, "IMOD0016", 8) &&
            (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8))))))) {
        for (size_t off = 32; off < types->size; off += TYPES_U32(types->data + off))
            if (types->data[off + 255]) {
                reason = "Custom actor animation options require package version 13; update Impera";
                goto fail;
            }
    }
    if (mapTypes &&
        (memcmp(magic, "IMOD0012", 8) &&
         (memcmp(magic, "IMOD0013", 8) &&
          (memcmp(magic, "IMOD0014", 8) &&
           (memcmp(magic, "IMOD0015", 8) &&
            (memcmp(magic, "IMOD0016", 8) &&
             (memcmp(magic, "IMOD0017", 8) && memcmp(magic, "IMOD0018", 8))))))) &&
        mapTypes->size >= 32) {
        for (size_t off = 32; off + 16 <= mapTypes->size; off += 16)
            if (mapTypes->data[off] == 3 &&
                (mapTypes->data[off + 3] >= 32 || mapTypes->data[off + 4])) {
                reason = "Extended NPC type bindings require package version 12; update Impera";
                goto fail;
            }
    }
    if ((types && !TYPES_Validate(types->data, types->size)) ||
        (mapTypes && (!types || !TYPES_ValidateMap(mapTypes->data, mapTypes->size, types->data,
                                                   types->size)))) {
        reason = "Invalid custom tile definitions, artwork or map references";
        goto fail;
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
