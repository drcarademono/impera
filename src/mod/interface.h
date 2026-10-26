#ifndef IMPERA_MOD_INTERFACE_H
#define IMPERA_MOD_INTERFACE_H
#include <stddef.h>
#include <string.h>
/* Eight-byte signature, followed by the EGA palette index for frame fills. */
static inline int INTERFACE_Validate(const void *data, size_t size) {
    const unsigned char *p = (const unsigned char *)data;
    return p && size == 9 && !memcmp(p, "IUI00001", 8) && p[8] < 16;
}
#endif
