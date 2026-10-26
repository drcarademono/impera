/* The frozen vanilla dialogue oracle intentionally has no Enhanced Look or
 * learned-name save state. Keep new callers linkable without changing it. */
#include <stdio.h>
int TALK_LookNpc(unsigned actor) { (void)actor; return 0; }
void TALK_ResetKnowledge(void) {}
int TALK_ReadKnowledge(FILE *f, int apply) { (void)f; (void)apply; return 1; }
int TALK_WriteKnowledge(FILE *f) { (void)f; return 1; }
