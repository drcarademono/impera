#ifndef _COMMON_FILE_H
#define _COMMON_FILE_H

#include <stdio.h>

#define FILE_PATH_SIZE 4096
int FILE_NameEqual(const char* left, const char* right);
/* Exact spelling wins; otherwise require a unique ASCII case-insensitive match.
 * allowMissingLeaf is for creating files, never for creating parent directories. */
int FILE_ResolvePath(const char* path, char* resolved, size_t capacity, int allowMissingLeaf);
FILE* FILE_Open(const char* path, const char* mode);

int FILE_ReadU32LE(FILE* fp, u32* out);
int FILE_ReadU16LE(FILE* fp, u16* out);
int FILE_ReadU8(FILE* fp, u8* out);

int FILE_WriteU32LE(FILE* fp, u32 in);
int FILE_WriteU16LE(FILE* fp, u16 in);
int FILE_WriteU8(FILE* fp, u8 in);

int FILE_ReadFile(char* fileName, void* buffer, uint size, int offset);
int FILE_WriteFile(char* fileName, void* buffer, uint size, int offset);

#endif
