#include "common/common.h"
#include "vars.h"
#include "funcs.h"
#include "macros.h"
#include "tiles.h"
#include "mouse.h"
#include "graphics/grap_sdl.h"
#include <SDL3/SDL.h>
#include "common/file.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_PNG
#include "third_party/stb_image.h"

static bool s_enabled;
static bool s_input, s_right, s_single;
static int s_cursorWidth, s_cursorHeight;
static void LoadCursors(void);
static bool s_menu, s_menuClick;
static int s_menuX, s_menuY, s_menuWidth, s_menuCount, s_menuSelected, s_menuTarget = -1;
static float s_hoverX = -1, s_hoverY = -1;

void MOUSE_MenuSet(int x, int y, int width, int count, int selected)
{
    if (x != s_menuX || y != s_menuY || width != s_menuWidth || count != s_menuCount) {
        s_menuTarget = -1; s_menuClick = false; s_hoverX = s_hoverY = -1;
    }
    s_menuX=x; s_menuY=y; s_menuWidth=width; s_menuCount=count; s_menuSelected=selected;
    s_menu = s_enabled && count > 0;
    if (s_menu) { s_right = false; s_single = false; }
}
void MOUSE_MenuEnd(void) { s_menu = false; }
int MOUSE_MenuRead(int x, int y, int width, int count, int selected)
{
    MOUSE_MenuSet(x,y,width,count,selected);
    int key = ULTIMA_266c_GetChar();
    MOUSE_MenuEnd();
    return key;
}
static int MenuItem(float x, float y)
{
    float ux, uy;
    if (!GRAP_SDL_MouseUIPoint(x,y,&ux,&uy) || ux < s_menuX || ux >= s_menuX+s_menuWidth ||
        uy < s_menuY || uy >= s_menuY+s_menuCount*8) return -1;
    return (int)((uy-s_menuY)/8);
}
static int PollMenu(void)
{
    float x,y; SDL_GetMouseState(&x,&y);
    if (!s_menuClick && (x != s_hoverX || y != s_hoverY)) {
        s_hoverX=x; s_hoverY=y; s_menuTarget=MenuItem(x,y);
    }
    if (s_menuTarget < 0) return 0;
    if (s_menuTarget < s_menuSelected) return U5_KEY_UP;
    if (s_menuTarget > s_menuSelected) return U5_KEY_DOWN;
    if (s_menuClick) { s_menuClick=false; s_menuTarget=-1; return U5_KEY_ENTER; }
    return 0;
}

static SDL_Cursor* s_cursors[9];
static SDL_Cursor* s_currentCursor;
static int s_lastCursorDirection = U5_KEY_UP;
static const int s_cursorDirections[9] = {0, U5_KEY_UP, U5_KEY_PGUP, U5_KEY_RIGHT, U5_KEY_PGDN, U5_KEY_DOWN, U5_KEY_END, U5_KEY_LEFT, U5_KEY_HOME};
static const char* s_cursorNames[9] = {"pointer", "direction-n", "diag-ne", "direction-e", "diag-se", "direction-s", "diag-sw", "direction-w", "diag-nw"};

int MOUSE_CursorDirection(float x, float y)
{
    int dx, dy; float rx, ry;
    if (!D_58a4 || (D_5893_map_id > 32 && D_5893_map_id < 128) ||
        !GRAP_SDL_MouseMapPoint(x, y, &dx, &dy, &rx, &ry)) return 0;
    /* Combat sprites occupy fixed coordinates in the original 11x11 map. */
    if (D_5893_map_id >= 128) { rx -= D_5896_map_x - 5; ry -= D_5897_map_y - 5; }
    int direction = MOUSE_Direction(rx, ry);
    if (direction) s_lastCursorDirection = direction;
    return s_lastCursorDirection;
}

void MOUSE_UpdateCursor(void)
{
    if (!s_enabled) return;
    int width,height; GRAP_SDL_CursorSize(&width,&height);
    if (width != s_cursorWidth || height != s_cursorHeight) {
        MOUSE_Cleanup(); LoadCursors();
    }
    float x, y;
    SDL_GetMouseState(&x, &y);
    int direction = s_menu ? 0 : MOUSE_CursorDirection(x, y), index = 0;
    for (int i = 1; i < 9; ++i) if (s_cursorDirections[i] == direction) index = i;
    SDL_Cursor* cursor = s_cursors[index] ? s_cursors[index] : s_cursors[0];
    if (!cursor) cursor = SDL_GetDefaultCursor();
    if (cursor && cursor != s_currentCursor && SDL_SetCursor(cursor)) s_currentCursor = cursor;
}

void MOUSE_Cleanup(void)
{
    SDL_Cursor* cursor = SDL_GetDefaultCursor();
    if (cursor) SDL_SetCursor(cursor);
    for (int i = 0; i < 9; ++i) { SDL_DestroyCursor(s_cursors[i]); s_cursors[i] = NULL; }
    s_currentCursor = NULL;
}

static void LoadCursors(void)
{
    GRAP_SDL_CursorSize(&s_cursorWidth,&s_cursorHeight);
    for (int i = 0; i < 9; ++i) {
        char path[128];
        snprintf(path, sizeof(path), "textures/cursors/cursor-%s.png", s_cursorNames[i]);
        FILE* fp = FILE_Open(path, "rb");
        if (!fp) { debug("Cannot load mouse cursor: %s\n", path); continue; }
        int w, h, channels;
        unsigned char* pixels = stbi_load_from_file(fp, &w, &h, &channels, 4);
        fclose(fp);
        if (!pixels) { debug("Invalid mouse cursor PNG: %s\n", path); continue; }
        SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);
        if (surface) {
            SDL_Surface* scaled = SDL_ScaleSurface(surface,s_cursorWidth,s_cursorHeight,SDL_SCALEMODE_NEAREST);
            if (scaled) {
                /* SDL mouse coordinates refer to this hotspot. Keep hit testing
                 * unchanged so the visible arrow tip identifies the target. */
                static const int tipX[9] = {0, 1, 2, 2, 2, 1, 0, 0, 0};
                static const int tipY[9] = {0, 0, 0, 1, 2, 2, 2, 1, 0};
                int hotspotX = tipX[i] == 2 ? s_cursorWidth - 1 : tipX[i] * (s_cursorWidth / 2);
                int hotspotY = tipY[i] == 2 ? s_cursorHeight - 1 : tipY[i] * (s_cursorHeight / 2);
                s_cursors[i] = SDL_CreateColorCursor(scaled, hotspotX, hotspotY);
                SDL_DestroySurface(scaled);
            }
            SDL_DestroySurface(surface);
        }
        stbi_image_free(pixels);
    }
}

void MOUSE_SetEnabled(bool enabled)
{
    s_enabled = enabled;
    MOUSE_Cancel();
}
static int s_dx, s_dy, s_command, s_direction;
static int s_map, s_level, s_x, s_y;
static Uint64 s_singleTime, s_moveTime;

void MOUSE_Initialize(void)
{
    SDL_SetHint(SDL_HINT_MOUSE_DOUBLE_CLICK_TIME, "300");
    MOUSE_Cancel();
    s_input = false;
    if (s_enabled) { MOUSE_Cleanup(); LoadCursors(); MOUSE_UpdateCursor(); }
}
void MOUSE_Cancel(void)
{
    s_menuTarget = -1; s_menuClick = false;
    SDL_GetMouseState(&s_hoverX,&s_hoverY);
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
    if (abs(dx) > 1 || abs(dy) > 1 || (!dx && !dy)) return 0;
    return MOUSE_Direction((float)dx, (float)dy);
}
int MOUSE_Action(int dx, int dy, bool mainAction)
{
    if (D_5893_map_id > 32) return 0;
    if (D_5893_map_id && (D_5896_map_x + dx < 0 || D_5896_map_x + dx >= 32 ||
        D_5897_map_y + dy < 0 || D_5897_map_y + dy >= 32)) return 0;
    if (!mainAction) return 'L';
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
    if (!s_enabled) return;
    if (s_menu) {
        if (down && button == SDL_BUTTON_LEFT) {
            s_menuTarget = MenuItem(x,y);
            s_menuClick = s_menuTarget >= 0;
        }
        return;
    }
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
    if (s_enabled && s_menu) return PollMenu();
    if (!s_enabled || !s_input || D_5893_map_id > 32) return 0;
    if (s_single && SDL_GetTicks() - s_singleTime >= 300) {
        s_single = false;
        if (SamePosition()) s_command = MOUSE_Action(s_dx, s_dy, false);
    }
    if (s_command) {
        if (!SamePosition()) { s_command = s_direction = 0; return 0; }
        int command = s_command;
        s_command = 0;
        s_right = false;
        s_direction = MOUSE_Direction((float)s_dx, (float)s_dy);
        if (!s_direction && command == 'L') s_direction = U5_KEY_SPACE;
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

bool MOUSE_TakeTarget(int* dx, int* dy)
{
    if (!s_direction || !SamePosition()) { s_direction = 0; return false; }
    *dx = s_dx; *dy = s_dy;
    s_direction = 0;
    return true;
}
