#ifndef IMPERA_PARTY_H
#define IMPERA_PARTY_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#define IMPERA_PARTY_CAPACITY 256
#define PARTY_RECORD_SIZE 36
int PARTY_Validate(const void *, size_t);
int PARTY_NameEqual(const char *, const char *);
unsigned PARTY_Count(const void *, size_t);
/* Runtime roster includes inactive recruitable companions; active party is configurable up to eight. */
unsigned PARTY_RosterCount(void);
unsigned PARTY_MaxActive(void);
unsigned PARTY_CombatType(unsigned member);
int PARTY_Configure(void);
void PARTY_InitExtras(void);
int PARTY_ReadState(FILE *, int apply);
int PARTY_WriteState(FILE *);
#ifdef __cplusplus
}
#endif
#endif
