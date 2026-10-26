#ifndef _TALK_H
#define _TALK_H
#include <stdio.h>
void TALK_ResetKnowledge(void);
int TALK_ReadKnowledge(FILE *, int apply);
int TALK_WriteKnowledge(FILE *);

int TALK_LookNpc(unsigned actor);
int TALK_031e_TalkToNpc(int param_1);
int TALK_Target(int dx,int dy,int* x,int* y);
int TALK_041c_TalkCmd(void);

#endif
