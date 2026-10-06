#ifndef _EVENT_EVENT_H
#define _EVENT_EVENT_H

typedef void EVT_Callback(void);

#if defined(TARGET_SDL)
/* Returns the previous mode so presentation screens can restore it. */
extern int EVT_SetImmediateExit(int enabled);
extern int EVT_ImmediateExitEnabled(void);
#endif

extern void EVT_Initialize(void);
extern void EVT_Cleanup(void);
extern void EVT_Yield(void);
extern void EVT_RegisterCallback(EVT_Callback* callback);

#endif
