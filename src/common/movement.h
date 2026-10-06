#ifndef U5D_MOVEMENT_H
#define U5D_MOVEMENT_H
/* Off by default, independent of mouse and animation options. */
void MOVEMENT_SetDiagonal(bool enabled);
bool MOVEMENT_Diagonal(void);
bool MOVEMENT_AttackAllowed(int dx, int dy);
bool MOVEMENT_Adjacent(int dx, int dy);
#endif
