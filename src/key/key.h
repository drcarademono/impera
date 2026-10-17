#ifndef _KEY_KEY_H
#define _KEY_KEY_H

extern void KEY_Initialize(void);
extern void KEY_Cleanup(void);

#if defined(TARGET_SDL)
extern void KEY_SDL_SetGameplayInput(int enabled);
extern void KEY_SDL_SubmitText(const char *text);
#endif
extern int KEY_PollKey(void);

#endif
