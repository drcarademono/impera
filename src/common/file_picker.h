#ifndef IMPERA_FILE_PICKER_H
#define IMPERA_FILE_PICKER_H
#include "common.h"
/* Themed file browser. filename/suffix filters may be NULL; failed acceptance
 * keeps browsing, and cancellation leaves caller-owned settings unchanged. */
bool FILEPICKER_Select(const char* title,const char* prompt,const char* filename,const char* suffix,
                       const char* initial,const char* backLabel,const char* invalid,
                       bool (*accept)(const char*,void*),void* user);
#endif
