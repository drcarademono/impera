#include "common/common.h"

#include "vars.h"
#include "macros.h"
#include "mouse.h"
#include "common/engine_settings.h"
#include "common/save_slots.h"
#include "event/event.h"
#include "graphics/grap_sdl.h"

#include <SDL3/SDL.h>

#define KBD_HOME   0x47
#define KBD_UP     0x48
#define KBD_PGUP   0x49
#define KBD_LEFT   0x4B
#define KBD_RIGHT  0x4D
#define KBD_END    0x4f
#define KBD_DOWN   0x50
#define KBD_PGDN   0x51

void KEY_Initialize(void)
{ MOUSE_Initialize(); }

void KEY_Cleanup(void)
{ MOUSE_Cleanup(); }

static int s_gameplayInput;
void KEY_SDL_SetGameplayInput(int enabled) { s_gameplayInput=enabled; }
static u16 s_lastDownKeycode = 0;
static u16 s_heldMovementKey;
static SDL_Keycode s_heldSDLKey;
static Uint64 s_nextMovement;

void KEY_SDL_ReleaseKey(SDL_Keycode key)
{
    if (!key || key == s_heldSDLKey) {
        s_heldMovementKey = 0;
        s_heldSDLKey = 0;
    }
}

void KEY_SDL_ClearInput(void)
{
    s_lastDownKeycode = 0;
    KEY_SDL_ReleaseKey(0);
}

static u16 KeyboardEventToUltimaKeycode(SDL_KeyboardEvent ev)
{
    if (ev.mod & SDL_KMOD_CTRL)
    {
        switch (ev.key)
        {
        case SDLK_B: return 2;
        case SDLK_E: return U5_KEY_CTRL_E;
        case SDLK_K: return U5_KEY_CTRL_K;
        case SDLK_L: return s_gameplayInput && !EVT_ImmediateExitEnabled()?U5_KEY_CTRL_L:0x1a;
        case SDLK_W: return s_gameplayInput && !EVT_ImmediateExitEnabled()?U5_KEY_CTRL_W:0x1a;
        case SDLK_O: return U5_KEY_CTRL_O;
        case SDLK_M: return U5_KEY_CTRL_M; // same as CR
        case SDLK_S: return U5_KEY_CTRL_S;
        case SDLK_V: return U5_KEY_CTRL_V;
        }

        if (!(ev.key & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK)))
            return 0x1a; // return not used keycode
    }

    if (!(ev.key & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK)))
    {
        // ascii/unicode keypoint

        bool shift = (ev.mod & SDL_KMOD_SHIFT) != 0;
        bool caps = (ev.mod & SDL_KMOD_CAPS) != 0;

        if (shift ^ caps)
        {
            // TODO: symbols
            return SDL_toupper((u8)ev.key);
        }

        return (u8)ev.key;
    }

    u8 extendedKeycode;
    switch (ev.key)
    {
    case SDLK_HOME: extendedKeycode = KBD_HOME; break;
    case SDLK_UP: extendedKeycode = KBD_UP; break;
    case SDLK_PAGEUP: extendedKeycode = KBD_PGUP; break;
    case SDLK_LEFT: extendedKeycode = KBD_LEFT; break;
    case SDLK_RIGHT: extendedKeycode = KBD_RIGHT; break;
    case SDLK_END: extendedKeycode = KBD_END; break;
    case SDLK_DOWN: extendedKeycode = KBD_DOWN; break;
    case SDLK_PAGEDOWN: extendedKeycode = KBD_PGDN; break;
    default:
        return 0;
    }

    // based on ULTIMA_1d5e

    if (extendedKeycode <= 0x44)
    {
        // F1..F10 -> c9..d2
        return extendedKeycode + 0x8e;
    }

    // Arrow keys
    // l, r, d, u, home, end, pgup, pgdn -> 1, 2, 3, 4, d3, d4, d5, d6
    int i;
    for (i = 0; i < 8; i++)
    {
        if (extendedKeycode == D_540e[i])
            break;
    }

    if (i == 8)
    {
        return 0;
    }

    u16 key = D_5416[i];
    key |= 0x100; // special keystroke
    return key;
}

void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent ev)
{
    u16 key = KeyboardEventToUltimaKeycode(ev);
    if ((key == U5_KEY_CTRL_O || key == U5_KEY_CTRL_W || key == U5_KEY_CTRL_L) && ev.repeat) return;
    bool direction = (key >= (0x100 | U5_KEY_LEFT) && key <= (0x100 | U5_KEY_DOWN)) ||
                     (key >= (0x100 | U5_KEY_HOME) && key <= (0x100 | U5_KEY_PGDN));
    if ((GRAP_SDL_CustomMovementSpeed() || GRAP_SDL_SmoothMovementEnabled()) && direction) {
        if (ev.repeat) return; /* use our timer rather than OS keyboard repeat */
        s_heldMovementKey = key;
        s_heldSDLKey = ev.key;
        s_nextMovement = SDL_GetTicks() + GRAP_SDL_MovementInterval();
    } else if (!ev.repeat) KEY_SDL_ReleaseKey(0);
    s_lastDownKeycode = key;
}

extern void EVT_Yield(void);

int KEY_PollKey(void)
{
	int ret;

	D_538a = 0;

	EVT_Yield();

    if (!s_lastDownKeycode && s_heldMovementKey && SDL_GetTicks() >= s_nextMovement) {
        s_lastDownKeycode = s_heldMovementKey;
        s_nextMovement = SDL_GetTicks() + GRAP_SDL_MovementInterval();
    }

    // special keystroke
    if (s_lastDownKeycode & 0x100)
    {
        s_lastDownKeycode &= 0xff;
        D_538a = 1;
    }

    if(s_gameplayInput && !EVT_ImmediateExitEnabled() &&
       (s_lastDownKeycode==U5_KEY_CTRL_W || s_lastDownKeycode==U5_KEY_CTRL_L)) {
        bool loading=s_lastDownKeycode==U5_KEY_CTRL_L;
        KEY_SDL_ClearInput();
        /* Disable the shortcuts while the browser owns keyboard input. */
        s_gameplayInput=0;
        if(loading) {
            if(SLOTS_ShowLoadInGame()) SLOTS_RequestReload();
        } else SLOTS_ShowSave();
        s_gameplayInput=1;
        return 0;
    }
    if (s_lastDownKeycode == U5_KEY_CTRL_O) {
        /* Open at an input boundary, preserving the suspended game screen. */
        ENGINE_ShowOptions(!EVT_ImmediateExitEnabled());
        return 0;
    }

    if(s_lastDownKeycode && s_gameplayInput) debug("Gameplay key=%d repeat=%d",s_lastDownKeycode,s_heldMovementKey!=0);
    ret = s_lastDownKeycode;
    s_lastDownKeycode = 0;
    if (!ret) {
        ret = MOUSE_PollCommand();
        if (ret) D_538a = 1;
    }

	return ret;
}
