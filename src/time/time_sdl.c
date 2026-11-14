#include <SDL3/SDL_timer.h>

#include "common/common.h"

#include "event/event.h"

static Uint64 s_pausedTicks, s_pauseStart;
static bool s_paused;
void TIME_PauseGameClock(bool paused)
{
    if(paused == s_paused) return;
    if(paused) s_pauseStart = SDL_GetTicks();
    else s_pausedTicks += SDL_GetTicks() - s_pauseStart;
    s_paused = paused;
}
static Uint64 gameTicks(void)
{
    return (s_paused ? s_pauseStart : SDL_GetTicks()) - s_pausedTicks;
}

void TIME_Initialize(void)
{}

void TIME_Cleanup(void)
{}

u32 TIME_GetTicksMs(void)
{
	return (u32)gameTicks();
}

void TIME_SleepMs(int ms)
{
	u32 ticks = (u32)SDL_GetTicks() + (u32)ms;

	EVT_Yield();
	while (ticks > SDL_GetTicks())
	{
		EVT_Yield();
		SDL_Delay(1);
	}
}

u32 TIME_CurrentFrame(void)
{
	// 62.5 hz
	return gameTicks() / 16;
}
