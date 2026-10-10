/* Exercise the actual engine decoder with Python-generated resource files.
 * File I/O stubs replace only the engine's data-directory resolution. */
#include "common/common.h"
#include "common/file.h"
#include "common/lzw.h"

#include <stdarg.h>
#include <stdio.h>

int FILE_ReadU8(FILE* fp, u8* out)
{
    return fread(out, 1, 1, fp) == 1;
}

int FILE_ReadU32LE(FILE* fp, u32* out)
{
    u8 b[4];
    if (fread(b, 1, 4, fp) != 4)
        return 0;
    *out = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
    return 1;
}

void debug(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    FILE* file = fopen(argv[1], "rb");
    if (!file)
        return 2;
    u8* buffer = NULL;
    u32 size = 0;
    int result = LzwDecompressFile(file, &buffer, &size);
    fclose(file);
    if (result)
        return 1;
    file = fopen(argv[2], "wb");
    if (!file)
    {
        free(buffer);
        return 2;
    }
    result = fwrite(buffer, 1, size, file) == size ? 0 : 2;
    if (fclose(file))
        result = 2;
    free(buffer);
    return result;
}
