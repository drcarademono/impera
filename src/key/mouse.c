#include "common/common.h"
#include "common/movement.h"
#include "vars.h"
#include "funcs.h"
#include "macros.h"
#include "tiles.h"
#include "comsubs.h"
#include "talk.h"
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
static bool s_pointerMode;
void MOUSE_SetPointerMode(bool enabled)
{
    s_pointerMode = enabled;
    MOUSE_UpdateCursor();
}
static bool s_input, s_right, s_single;
static int s_cursorWidth, s_cursorHeight;
static void LoadCursors(void);
static int DirectionOctant(float dx, float dy);
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
    if (s_pointerMode || !D_58a4 || (D_5893_map_id > 32 && D_5893_map_id < 128) ||
        !GRAP_SDL_MouseMapPoint(x, y, &dx, &dy, &rx, &ry)) return 0;
    /* Combat sprites occupy fixed coordinates in the original 11x11 map. */
    if (D_5893_map_id >= 128) { rx -= D_5896_map_x - 5; ry -= D_5897_map_y - 5; }
    /* Only the exact origin has no direction; every other point belongs
     * to an octant, including points within the player sprite. */
    int direction = (rx == 0 && ry == 0) ? 0 : DirectionOctant(rx, ry);
    if (direction) s_lastCursorDirection = direction;
    if (!MOVEMENT_Diagonal() && s_lastCursorDirection >= U5_KEY_HOME)
        s_lastCursorDirection = (s_lastCursorDirection == U5_KEY_HOME || s_lastCursorDirection == U5_KEY_END) ? U5_KEY_LEFT : U5_KEY_RIGHT;
    return s_lastCursorDirection;
}

void MOUSE_UpdateCursor(void)
{
    if (!s_enabled && !s_pointerMode) return;
    int width,height; GRAP_SDL_CursorSize(&width,&height);
    if (width != s_cursorWidth || height != s_cursorHeight) {
        MOUSE_Cleanup(); LoadCursors();
    }
    float x, y;
    SDL_GetMouseState(&x, &y);
    int direction = (s_menu || s_pointerMode) ? 0 : MOUSE_CursorDirection(x, y), index = 0;
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
    s_cursorWidth=s_cursorHeight=0;
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
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        MOUSE_Cleanup(); /* Rebuild lazily at the current window scale. */
    }
}
bool MOUSE_Enabled(void) { return s_enabled; }
static int s_dx, s_dy, s_command, s_direction, s_targetDx, s_targetDy;
static int s_map, s_level, s_x, s_y, s_combatEntity;
static bool s_combatAttack;
static int s_attackX, s_attackY, s_attackEntity;
void MOUSE_ClearCombatAttack(void) { s_combatAttack=false; }
static int CombatRange(int entity)
{
    if (!(D_ba14[entity].flags & COMBAT_FLAGS_PLAYER)) return D_159c[D_ba14[entity].entityIdx];
    int range=1, party=D_ba14[entity].entityIdx;
    const int slots[3]={0,2,3};
    for (int i=0;i<3;i++) {
        int weapon=D_55a8_party[party].equips[slots[i]];
        if (weapon!=0xff && D_15fc[weapon] && D_1664[weapon]>range) range=D_1664[weapon];
    }
    return range;
}
static bool s_aimInput;
static int s_aimEntity, s_aimRange, s_aimKey;
void MOUSE_SetCombatAimInput(int entity, int range)
{
    s_aimInput=s_enabled;
    s_aimEntity=entity; s_aimRange=range; s_aimKey=0;
    s_single=s_right=false;
}
void MOUSE_EndCombatAimInput(void) { s_aimInput=false; s_aimKey=0; }
bool MOUSE_CombatAttackTarget(int entity, int range, int* distance)
{
    if (!s_combatAttack || entity!=s_attackEntity) return false;
    int target=COMSUBS_0748(s_attackX,s_attackY);
    *distance=COMSUBS_048a(D_ba14[entity].x,D_ba14[entity].y,s_attackX,s_attackY);
    if (target<0 || ULTIMA_5646(target)==ULTIMA_5646(entity) ||
        (D_ba14[target].flags & (COMBAT_FLAGS_DEAD|COMBAT_FLAGS_INVISIBLE|COMBAT_FLAGS_4)) ||
        !MOVEMENT_AttackAllowed(s_attackX-D_ba14[entity].x,s_attackY-D_ba14[entity].y) ||
        *distance>range) *distance=0;
    D_5899=s_attackX; D_589a=s_attackY;
    return true;
}
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
    s_aimKey=0;
    MOUSE_ClearCombatAttack();
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
static int DirectionOctant(float dx, float dy)
{
    float ax = SDL_fabsf(dx), ay = SDL_fabsf(dy);
    if (!MOVEMENT_Diagonal())
        return ax >= ay ? (dx < 0 ? U5_KEY_LEFT : U5_KEY_RIGHT) : (dy < 0 ? U5_KEY_UP : U5_KEY_DOWN);
    if (ay < ax * 0.41421356f) return dx < 0 ? U5_KEY_LEFT : U5_KEY_RIGHT;
    if (ax < ay * 0.41421356f) return dy < 0 ? U5_KEY_UP : U5_KEY_DOWN;
    if (dy < 0) return dx < 0 ? U5_KEY_HOME : U5_KEY_PGUP;
    return dx < 0 ? U5_KEY_END : U5_KEY_PGDN;
}
int MOUSE_Direction(float dx, float dy)
{
    if (dx == 0 && dy == 0) return 0;
    return DirectionOctant(dx, dy);
}
static int AdjacentDirection(int dx, int dy)
{
    if (!MOVEMENT_Adjacent(dx,dy) || (!dx && !dy)) return 0;
    return MOUSE_Direction((float)dx, (float)dy);
}
static int TalkTarget(int dx,int dy,int* x,int* y)
{
    /* The original actor lookup uses D_5876 as an output actor index. */
    int saved=D_5876;int actor=TALK_Target(dx,dy,x,y);D_5876=saved;
    return actor>=0x30 && (actor&0xfc)!=TILE_ACTOR_SHARD?actor:0;
}
int MOUSE_Action(int dx, int dy, bool mainAction)
{
    if (D_5893_map_id >= 128) {
        if (D_589e >= 32) return 0;
        int x=dx+5, y=dy+5;
        if (x<0 || y<0 || x>10 || y>10) return 0;
        if (!mainAction) return 0;
        int target=COMSUBS_0748(x,y);
        if (target<0 || target==D_589e || ULTIMA_5646(target)==ULTIMA_5646(D_589e) ||
            (D_ba14[target].flags & (COMBAT_FLAGS_DEAD|COMBAT_FLAGS_INVISIBLE|COMBAT_FLAGS_4)) ||
            !MOVEMENT_AttackAllowed(x-D_ba14[D_589e].x,y-D_ba14[D_589e].y) ||
            COMSUBS_048a(D_ba14[D_589e].x,D_ba14[D_589e].y,x,y)>CombatRange(D_589e)) return 0;
        return 'A';
    }
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
            tile == TILE_MAP_CASTLELB || tile == TILE_MAP_PALACEBT || (tile >= 0x16 && tile <= 0x1a))) return 'E';
        if (tile == TILE_MAP_LADDER_UP || tile == TILE_MAP_LADDER_DOWN) return 'K';
        if (tile == TILE_MAP_BED) return 'H';
        return 0;
    }
    if (!AdjacentDirection(dx, dy)) {
        /* A two-tile Talk target must lie exactly along a legal direction. */
        if(D_5893_map_id && dx%2==0 && dy%2==0 && AdjacentDirection(dx/2,dy/2)) {
            int tx,ty;int actor=TalkTarget(dx/2,dy/2,&tx,&ty);
            if(actor>=0x30 && tx==x && ty==y) return 'T';
        }
        return 0;
    }
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
    if (tile == TILE_MAP_CROPS || tile == TILE_MAP_B0 || tile == TILE_MAP_B1 ||
        (tile==TILE_MAP_TABLE_9A && dy==1) || (tile==TILE_MAP_TABLE_9B && dy==-1) ||
        (tile==TILE_MAP_TABLE_9C && dx==0)) return 'G';
    if(D_5893_map_id) {
        int tx,ty;
        if(TalkTarget(dx,dy,&tx,&ty)) return 'T';
    }
    return 'L';
}
static void Snapshot(void)
{
    s_combatEntity=D_589e;
    s_map = D_5893_map_id; s_level = D_5895_map_level;
    s_x = D_5896_map_x; s_y = D_5897_map_y;
}
static bool SamePosition(void)
{
    return (D_5893_map_id<128 || s_combatEntity==D_589e) &&
           s_map == D_5893_map_id && s_level == D_5895_map_level &&
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
    if (s_aimInput) {
        int dx,dy; float rx,ry;
        if (down && button==SDL_BUTTON_LEFT && s_aimEntity==D_589e && D_5893_map_id>=128 &&
            GRAP_SDL_MouseMapPoint(x,y,&dx,&dy,&rx,&ry)) {
            int tx=dx+5, ty=dy+5;
            int distance=COMSUBS_048a(D_ba14[s_aimEntity].x,D_ba14[s_aimEntity].y,tx,ty);
            if (tx>=0 && tx<11 && ty>=0 && ty<11 && distance>0 && distance<=s_aimRange &&
                MOVEMENT_AttackAllowed(tx-D_ba14[s_aimEntity].x,ty-D_ba14[s_aimEntity].y)) {
                D_5899=tx; D_589a=ty; s_aimKey=U5_KEY_ENTER;
            }
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
    if (s_enabled && s_aimInput) {
        int key=s_aimKey; s_aimKey=0;
        if (s_aimEntity!=D_589e || D_5893_map_id<128) return 0;
        return key;
    }
    if (s_enabled && s_menu) return PollMenu();
    if (!s_enabled || !s_input || (D_5893_map_id > 32 && D_5893_map_id < 128)) return 0;
    if (s_single && SDL_GetTicks() - s_singleTime >= 300) {
        s_single = false;
        if (SamePosition()) s_command = MOUSE_Action(s_dx, s_dy, false);
    }
    if (s_command) {
        if (!SamePosition()) { s_command = s_direction = 0; return 0; }
        int command = s_command;
        s_command = 0;
        s_right = false;
        if (D_5893_map_id>=128) {
            s_direction=0;
            if (command=='A') {
                s_combatAttack=true; s_attackEntity=D_589e;
                s_attackX=s_dx+5; s_attackY=s_dy+5;
            }
            return command;
        }
        s_targetDx=command=='T'?(s_dx>0)-(s_dx<0):s_dx;
        s_targetDy=command=='T'?(s_dy>0)-(s_dy<0):s_dy;
        s_direction = MOUSE_Direction((float)s_dx, (float)s_dy);
        if (!s_direction && command == 'L') s_direction = U5_KEY_SPACE;
        debug("Mouse action: %c target_offset=%d,%d\n", command, s_dx, s_dy);
        return command;
    }
    /* Pace held movement independently of the smooth animation duration. */
    Uint64 moveInterval = GRAP_SDL_MovementInterval();
    if (s_right && SDL_GetTicks() - s_moveTime >= moveInterval) {
        float x, y, rx, ry;
        int dx, dy;
        SDL_GetMouseState(&x, &y);
        if (!GRAP_SDL_MouseMapPoint(x, y, &dx, &dy, &rx, &ry)) return 0;
        if (s_single) s_single = false;
        s_moveTime = SDL_GetTicks();
        int direction = MOUSE_CursorDirection(x, y);
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
    *dx = s_targetDx; *dy = s_targetDy;
    s_direction = 0;
    return true;
}
