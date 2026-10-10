#ifndef IMPERA_MOD_RUNTIME_H
#define IMPERA_MOD_RUNTIME_H
#include <stdio.h>
void MOD_MountDirectory(const char *directory);
FILE *MOD_OpenResource(const char *name, int *found);
unsigned MOD_LoadedCount(void);
#endif
