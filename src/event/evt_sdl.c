#include "common/common.h"
#include "graphics/grap.h"

#include "event.h"
#include "vars.h"
#include "key/mouse.h"
#include "graphics/crt.h"

#include <SDL3/SDL.h>

static EVT_Callback* s_callbacks[16];
static int s_registeredCallbackCount;
static int s_immediateExit;

int EVT_ImmediateExitEnabled(void)
{
    return s_immediateExit;
}

int EVT_SetImmediateExit(int enabled)
{
    int previous = s_immediateExit;
    s_immediateExit = enabled;
    return previous;
}

void EVT_Initialize(void)
{
}

void EVT_Cleanup(void)
{
}

// TODO
void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent ev);
void KEY_SDL_ReleaseKey(SDL_Keycode key);

void EVT_PollMessages(void)
{
	SDL_Event ev;
	while (SDL_PollEvent(&ev))
	{
		switch (ev.type)
		{
		case SDL_EVENT_QUIT:
            debug("Window close requested");
			exit(0);
			break;

		case SDL_EVENT_KEY_DOWN:
            /* Handle exit before menus or cutscenes can consume the key. */
            if (s_immediateExit && ev.key.key == SDLK_E && (ev.key.mod & SDL_KMOD_CTRL))
                exit(0);
			MOUSE_Cancel();
			KEY_SDL_ProcessKeyDown(ev.key);
			break;
        case SDL_EVENT_KEY_UP:
            KEY_SDL_ReleaseKey(ev.key.key);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            CRT_RefreshCursor();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            MOUSE_Button(ev.button.x, ev.button.y, ev.button.button,
                         ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN, ev.button.clicks);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            debug("Window focus lost; releasing held input");
            MOUSE_Cancel();
            KEY_SDL_ReleaseKey(0);
            break;
		}
	}
}

void EVT_Yield(void)
{
    static int map=-1,level=-1,x=-1,y=-1,entity=-1;
    if(map!=D_5893_map_id || level!=D_5895_map_level || x!=D_5896_map_x || y!=D_5897_map_y || entity!=D_589e) {
        map=D_5893_map_id;level=D_5895_map_level;x=D_5896_map_x;y=D_5897_map_y;entity=D_589e;
        debug("Game state map=%d level=%d position=%d,%d active=%d command=%d",map,level,x,y,entity,D_587a);
    }
	GRAP_FlushPendingPresent();
	EVT_PollMessages();
    MOUSE_UpdateCursor();

	for (int i = 0; i < s_registeredCallbackCount; i++)
	{
		(*s_callbacks[i])();
	}
}

void EVT_RegisterCallback(EVT_Callback* callback)
{
	ASSERT(s_registeredCallbackCount < ARRAYSIZE(s_callbacks));

	s_callbacks[s_registeredCallbackCount++] = callback;
}
