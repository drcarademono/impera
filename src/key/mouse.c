#include "common/common.h"
#include "vars.h"
#include "funcs.h"
#include "macros.h"
#include "tiles.h"
#include "mouse.h"
#include "graphics/grap_sdl.h"
#include <SDL3/SDL.h>

static bool s_input, s_right, s_single;
static int s_dx, s_dy, s_command, s_direction;
static int s_map, s_level, s_x, s_y;
static Uint64 s_singleTime, s_moveTime;

void MOUSE_Initialize(void)
{
    SDL_SetHint(SDL_HINT_MOUSE_DOUBLE_CLICK_TIME, "300");
    MOUSE_Cancel();
    s_input = false;
}
void MOUSE_Cancel(void)
{
    s_right = s_single = false;
    s_command = s_direction = 0;
}
void MOUSE_SetCommandInput(bool enabled)
{
    s_input = enabled;
    if (enabled) s_direction = 0;
}
int MOUSE_Direction(float dx, float dy)
{
    float ax = SDL_fabsf(dx), ay = SDL_fabsf(dy);
    if (ax < 0.5f && ay < 0.5f) return 0;
    if (ay < ax * 0.41421356f) return dx < 0 ? U5_KEY_LEFT : U5_KEY_RIGHT;
    if (ax < ay * 0.41421356f) return dy < 0 ? U5_KEY_UP : U5_KEY_DOWN;
    if (dy < 0) return dx < 0 ? U5_KEY_HOME : U5_KEY_PGUP;
    return dx < 0 ? U5_KEY_END : U5_KEY_PGDN;
}
static int AdjacentDirection(int dx, int dy)
{
    if (abs(dx) + abs(dy) != 1) return 0;
    return dx ? (dx < 0 ? U5_KEY_LEFT : U5_KEY_RIGHT) : (dy < 0 ? U5_KEY_UP : U5_KEY_DOWN);
}
int MOUSE_Action(int dx, int dy, bool mainAction)
{
    if (D_5893_map_id > 32) return 0;
    if (!mainAction) return AdjacentDirection(dx, dy) ? 'L' : 0;
    int x = D_5896_map_x + dx, y = D_5897_map_y + dy;
    if (!dx && !dy) {
        int tile = *ULTIMA_4402_GetTileAddr(x, y);
        for (int i = 1; i < 32; i++) {
            ActorFmt* a = &D_5c5a[i];
            if (a->_2_x == x && a->_3_y == y && a->_4_z == D_5895_map_level &&
                ((a->_0_tile & 0xfc) == TILE_ACTOR_HORSE ||
                 (a->_0_tile & 0xfc) == TILE_ACTOR_SKIFF ||
                 (a->_0_tile & 0xf8) == TILE_ACTOR_FRIGATE_20 || a->_0_tile == TILE_ACTOR_CARPET))
                return 'B';
        }
        if (!D_5893_map_id && (tile == TILE_MAP_TOWNE || tile == TILE_MAP_CASTLE ||
            tile == TILE_MAP_CASTLELB || (tile >= 0x16 && tile <= 0x1a))) return 'E';
        if (tile == TILE_MAP_LADDER_UP || tile == TILE_MAP_LADDER_DOWN) return 'K';
        if (tile == TILE_MAP_BED) return 'H';
        return 0;
    }
    if (!AdjacentDirection(dx, dy)) return 0;
    for (int i = 1; i < 32; i++) {
        ActorFmt* a = &D_5c5a[i];
        if (a->_0_tile && a->_2_x == (byte)x && a->_3_y == (byte)y && a->_4_z == D_5895_map_level) {
            if (a->_0_tile == TILE_ACTOR_CHEST) return 'O';
            if (a->_0_tile < TILE_ACTOR_HORSE || a->_0_tile == TILE_ACTOR_MOONSTONE ||
                a->_0_tile == TILE_ACTOR_CARPET || (a->_0_tile & 0xfc) == TILE_ACTOR_SHARD) return 'G';
            if (D_5893_map_id && a->_0_tile >= 0x30) return 'T';
        }
    }
    int tile = *ULTIMA_4402_GetTileAddr(x, y);
    if ((tile >= TILE_MAP_DOOR_B8 && tile <= TILE_MAP_DOOR_BB) ||
        tile == TILE_MAP_TRUNK || (tile >= 0x97 && tile <= 0x99)) return 'O';
    if (tile == TILE_MAP_CROPS || tile == TILE_MAP_B0 || tile == TILE_MAP_B1) return 'G';
    return 'L';
}
static void Snapshot(void)
{
    s_map = D_5893_map_id; s_level = D_5895_map_level;
    s_x = D_5896_map_x; s_y = D_5897_map_y;
}
static bool SamePosition(void)
{
    return s_map == D_5893_map_id && s_level == D_5895_map_level &&
           s_x == D_5896_map_x && s_y == D_5897_map_y;
}
void MOUSE_Button(float x, float y, int button, bool down, int clicks)
{
    if (button == SDL_BUTTON_RIGHT) {
        s_right = down && s_input;
        debug("Mouse right button: down=%d command_input=%d\n", down, s_input);
        if (down) s_moveTime = 0;
        return;
    }
    if (!s_input || !down || button != SDL_BUTTON_LEFT) return;
    int dx, dy;
    float rx, ry;
    if (!GRAP_SDL_MouseMapPoint(x, y, &dx, &dy, &rx, &ry)) { s_single = false; return; }
    if (clicks >= 2 && s_single && SamePosition() && dx == s_dx && dy == s_dy) {
        s_single = false;
        s_command = MOUSE_Action(dx, dy, true);
    } else {
        s_dx = dx; s_dy = dy; s_single = true;
        s_singleTime = SDL_GetTicks();
        Snapshot();
    }
}
int MOUSE_PollCommand(void)
{
    if (!s_input || D_5893_map_id > 32) return 0;
    if (s_single && SDL_GetTicks() - s_singleTime >= 300) {
        s_single = false;
        if (SamePosition()) s_command = MOUSE_Action(s_dx, s_dy, false);
    }
    if (s_command) {
        if (!SamePosition()) { s_command = s_direction = 0; return 0; }
        int command = s_command;
        s_command = 0;
        s_right = false;
        s_direction = AdjacentDirection(s_dx, s_dy);
        debug("Mouse action: %c target_offset=%d,%d\n", command, s_dx, s_dy);
        return command;
    }
    if (s_right && SDL_GetTicks() - s_moveTime >= 160) {
        float x, y, rx, ry;
        int dx, dy;
        SDL_GetMouseState(&x, &y);
        if (!GRAP_SDL_MouseMapPoint(x, y, &dx, &dy, &rx, &ry)) return 0;
        if (s_single) s_single = false;
        s_moveTime = SDL_GetTicks();
        int direction = MOUSE_Direction(rx, ry);
        debug("Mouse move: direction=%d cursor_offset=%f,%f\n", direction, rx, ry);
        return direction;
    }
    return 0;
}
int MOUSE_TakeDirection(void)
{
    int direction = SamePosition() ? s_direction : 0;
    s_direction = 0;
    return direction;
}
