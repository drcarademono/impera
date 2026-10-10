#ifndef IMPERA_MOD_PACKAGE_H
#define IMPERA_MOD_PACKAGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MOD_MAX_RESOURCE (8u * 1024u * 1024u)
#define MOD_MAX_PACKAGE (32u * 1024u * 1024u)
#define MOD_MAX_ENTRIES 128
/* Packages are data-only sparse patches. Reader owns returned base allocation. */
typedef int (*ModBaseReader)(void *context, const char *name, unsigned char **data, size_t *size);
typedef struct ModResource {
    char name[33];
    unsigned char *data;
    size_t size;
} ModResource;
typedef struct ModPackage {
    char title[257];
    ModResource entries[MOD_MAX_ENTRIES];
    unsigned count;
} ModPackage;
uint32_t MOD_Crc32(const void *data, size_t size);
int MOD_AllowedResource(const char *name);
/* All-or-nothing; error buffer always contains a diagnostic on failure. */
int MOD_Decode(const void *bytes, size_t size, ModBaseReader reader, void *context, ModPackage *result,
               char *error, size_t errorSize);
void MOD_Free(ModPackage *package);
#ifdef __cplusplus
}
#endif
#endif
