#ifndef _NPC_H
#define _NPC_H
#include <stdio.h>
void NPC_LoadExtra(const char *, unsigned town);
void NPC_ResetExtra(void);
int NPC_ObjectKind(unsigned slot);
int NPC_ConsumeObject(unsigned actor);
int NPC_BloodAt(int x, int y, int floor);
void NPC_ApplyObjectTriggers(int x, int y, int floor);
int NPC_ReadExtraState(FILE *, int apply);
int NPC_WriteExtraState(FILE *);

void NPC_0000_LoadNpcFile(void);
/* Refresh a new settlement game from mounted NPC definitions, without entering gameplay. */
int NPC_InitializeNewGame(void);
void NPC_00d6(int param_1);
void NPC_0db4(int param_1);
int NPC_12e0(int param_1, char param_2);

#endif
