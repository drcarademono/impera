#include "types_game.h"
#include "actors.h"
#include "types_state.h"
#include "contents.h"
#include "contents_game.h"
#include "combat.h"
#include "comsubs.h"
#include "funcs.h"
#include "graphics/grap_buf.h"
#include "graphics/tileset.h"
#include "graphics/widescreen.h"
#include "macros.h"
#include "vars.h"
#include <string.h>
/* Native map bytes remain the DOS boundary; type identities and mutations live
 * in TYPES' authoritative sparse state. Legacy interactions see their profile. */
TypeCell TYPES_GAME_Cell(int x, int y) {
    if (!D_5893_map_id) {
        x = MAPS_Wrap(x, MAP_WIDTH);
        y = MAPS_Wrap(y, MAP_HEIGHT);
    }
    TypeCell c = {0};
    c.kind = D_5893_map_id >= 128 ? CONTENTS_EncounterKind() : 0;
    c.map = c.kind ? CONTENTS_EncounterMap() : D_5893_map_id;
    c.floor = c.kind ? 0 : (int8_t)D_5895_map_level;
    c.x = x;
    c.y = y;
    return c;
}
byte *ULTIMA_4402_BaseAddr(int, int);
uint32_t TYPES_GAME_At(int x, int y) {
    if (!MAPS_Outside(D_5893_map_id) && D_5893_map_id < 128)
        return *ULTIMA_4402_BaseAddr(x, y);
    if (D_5893_map_id && (x < 0 || y < 0 || x >= (MAP_WIDTH) || y >= (MAP_HEIGHT)))
        return 255;
    uint32_t base;
    if (!D_5893_map_id && (((x - D_589b) & 255) >= 32 || ((y - D_589c) & 255) >= 32))
        base = WIDE_MapTile(x - D_5896_map_x, y - D_5897_map_y);
    else
        base = *ULTIMA_4402_BaseAddr(x, y);
    return TYPES_At(TYPES_GAME_Cell(x, y), base);
}
byte *TYPES_GAME_Address(int x, int y, byte *base) {
    static byte profiles[256];
    uint32_t id = TYPES_At(TYPES_GAME_Cell(x, y), *base);
    TypeDef d;
    if ((id >= 512 || TYPES_Defined(id) || id != *base) && TYPES_Get(id, &d)) {
        profiles[d.base] = d.base;
        return &profiles[d.base];
    }
    return base;
}
static void writeCell(int x, int y, TypeCell c, uint32_t id) {
    TypeDef d;
    if (!TYPES_Get(id, &d) || d.kind)
        return;
    TypeCell previous;
    uint32_t current = TYPES_GAME_At(x, y);
    TypeDef source;
    int tracked = TYPES_State(c, &previous) || current >= 512 || id >= 512 ||
                  c.restoreType ||
                  (TYPES_Defined(current) && TYPES_Get(current, &source) &&
                   source.base != current);
    if (tracked && !(!MAPS_Outside(D_5893_map_id) && D_5893_map_id < 128) && !TYPES_Set(c, id))
        return;
    *ULTIMA_4402_BaseAddr(x, y) = d.base;
}
void TYPES_GAME_Change(int x, int y, unsigned state, uint32_t fallback) {
    TypeCell c = TYPES_GAME_Cell(x, y);
    TYPES_State(c, &c);
    uint32_t current = TYPES_GAME_At(x, y);
    TypeDef d;
    uint32_t next = fallback;
    if (TYPES_Defined(current) && TYPES_Get(current, &d) && state < 4 && d.result[state])
        next = d.result[state];
    if (state == 0 && (current >= 512 || next >= 512))
        c.restoreType = current;
    if (state == 1 && c.restoreType) {
        if (!TYPES_Defined(current) || !d.result[1])
            next = c.restoreType;
        c.restoreType = 0;
    }
    if (state == 3)
        c.restoreType = 0;
    writeCell(x, y, c, next);
}
void TYPES_GAME_Write(int x, int y, uint32_t id) {
    uint32_t current = TYPES_GAME_At(x, y);
    TypeDef d, v;
    if (TYPES_Defined(current) && TYPES_Get(current, &d) && id < 256) {
        TYPES_Vanilla(0, d.base, &v);
        for (unsigned i = 0; i < 4; i++)
            if (d.result[i] && v.result[i] && v.result[i] == id) {
                TYPES_GAME_Change(x, y, i, id);
                return;
            }
    }
    writeCell(x, y, TYPES_GAME_Cell(x, y), id);
}
int TYPES_GAME_Walkable(int actor, int x, int y) {
    uint32_t id = TYPES_GAME_At(x, y);
    TypeDef d;
    if (!TYPES_Get(id, &d))
        return 0;
    if (id < 512 && !TYPES_Defined(id))
        return ULTIMA_2c4c_IsWalkableTile(actor, d.base);
    return TYPES_DefWalkable(actor, &d);
}
int TYPES_GAME_Property(int x, int y, int property, int fallback) {
    uint32_t id = TYPES_GAME_At(x, y);
    TypeDef d;
    if (!TYPES_Get(id, &d) || !TYPES_Defined(id))
        return fallback;
    return property == 0   ? d.sight
           : property == 1 ? d.projectile
           : property == 2 ? !!(d.render & TYPES_MASONRY)
           : property == 3 ? d.travel
           : property == 4 ? d.combatMap
                           : d.base;
}
void TYPES_GAME_Effects(void) {
    TypeDef d;
    uint32_t id = TYPES_GAME_At(D_5896_map_x, D_5897_map_y);
    if (!TYPES_Defined(id) || !TYPES_Get(id, &d))
        return;
    TypeDef movement;
    TYPES_Vanilla(1, D_587c_partyTile, &movement);
    if (movement.moveProfile > 10 || !(d.effectModes & (1u << movement.moveProfile)))
        return;
    int changed = 0;
    if (d.effects & TYPES_POISON)
        for (int i = 0; i < D_585b; i++)
            if (D_55a8_party[i].status != 'D' && D_55a8_party[i].status != 'P' &&
                D_55a8_party[i].dex <
                    ULTIMA_2092_RandomRange(D_5893_map_id ? 0 : 1, D_5893_map_id ? 29 : 30)) {
                D_55a8_party[i].status = 'P';
                changed = 1;
            }
    if (changed)
        ULTIMA_1850_PrintString("Poisoned!\n");
    if (d.effects & TYPES_BURN) {
        ULTIMA_1850_PrintString("Burning!\n");
        for (int i = 0; i < D_585b; i++)
            if (D_55a8_party[i].status != 'D')
                ULTIMA_2a52(i, ULTIMA_2092_RandomRange(d.damageMin, d.damageMax));
        changed = 1;
    }
    if (changed) {
        D_a9fa = 1;
        ULTIMA_2900_UpdateVitalsDisplay();
    }
}
int TYPES_GAME_CombatEffects(unsigned entity) {
    if (entity >= 32)
        return 0;
    CombatEntity *actor = &D_ba14[entity];
    if (actor->actorIdx >= 32)
        return 0;
    TypeDef d = {0}, move;
    if (actor->flags & COMBAT_FLAGS_MONSTER)
        move = ACTORS_Def(actor->entityIdx);
    else
        TYPES_Vanilla(1, D_5c5a[actor->actorIdx]._0_tile, &move);
    unsigned w, h;
    ACTORS_Size(actor->actorIdx, &w, &h);
    int handled = 0;
    for (unsigned oy = 0; oy < h; ++oy)
        for (unsigned ox = 0; ox < w; ++ox) {
            uint32_t id = TYPES_GAME_At(actor->x + ox, actor->y + oy);
            TypeDef tile;
            if (w == 1 && h == 1 && !TYPES_Defined(id))
                continue;
            if (!TYPES_Get(id, &tile))
                continue;
            handled = 1;
            if (move.moveProfile > 10 || !(tile.effectModes & (1u << move.moveProfile)))
                continue;
            d.effects |= tile.effects;
            if (tile.effects & TYPES_BURN) {
                if (tile.damageMin > d.damageMin)
                    d.damageMin = tile.damageMin;
                if (tile.damageMax > d.damageMax)
                    d.damageMax = tile.damageMax;
            }
        }
    if (!handled)
        return 0;
    if (d.effects & TYPES_POISON)
        COMBAT_18ba(entity, -1);
    if ((d.effects & TYPES_BURN) && !(actor->flags & COMBAT_FLAGS_DEAD))
        COMBAT_1574(entity, ULTIMA_2092_RandomRange(d.damageMin, d.damageMax));
    if (d.effects) {
        D_a9fa = 1;
        COMSUBS_0312(entity, 0xff);
    }
    return 1;
}

void TYPES_GAME_Draw(byte *pixels, int stride, int x, int y, int dx, int dy, int top, int bottom) {
    int wx = D_5896_map_x + dx, wy = D_5897_map_y + dy;
    uint32_t id = TYPES_GAME_At(wx, wy);
    TypeDef d;
    if (!TYPES_Get(id, &d) || !TYPES_ArtSlot(id))
        return;
    uint32_t ground = d.ground;
    if (ground == TYPES_INFER_GROUND) {
        int offsets[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}}, best = 0;
        ground = TYPES_INFER_GROUND;
        for (unsigned i = 0; i < 4; i++) {
            uint32_t candidate = TYPES_GAME_At(wx + offsets[i][0], wy + offsets[i][1]);
            TypeDef floor;
            if (!TYPES_Get(candidate, &floor) ||
                !((candidate >= 512 && !(floor.render & TYPES_SPRITE)) || candidate == 5 ||
                  candidate == 0x40 || candidate == 0x44 || candidate == 0x45))
                continue;
            int votes = 0;
            for (int j = 0; j < 4; j++)
                votes += TYPES_GAME_At(wx + offsets[j][0], wy + offsets[j][1]) == candidate;
            if (votes > best) {
                best = votes;
                ground = candidate;
            }
        }
    }
    /* The ground pass is complete before any actors are composited. */
    for (int py = 0; py < 16; py++)
        for (int px = 0; px < 16; px++) {
            if (y + py < top || y + py >= bottom)
                continue;
            byte *dest = pixels + (y + py) * stride + x + px;
            if (d.render & TYPES_SPRITE) {
                int c;
                if (ground == TYPES_INFER_GROUND) {
                    TILESET_SetSample(0);
                    c = 0;
                } else
                    c = GRAP_BUF_TilePixel(ground, px, py);
                TILESET_Record(dest, c);
                *dest = c;
            }
            int c = GRAP_BUF_TilePixel(id, px, py);
            TILESET_RecordSprite(dest, c, !!(d.render & TYPES_SPRITE));
            *dest = c;
        }
}
int TYPES_GAME_Look(int x, int y) {
    TypeDef d;
    uint32_t id = TYPES_GAME_At(x, y);
    if (!TYPES_Defined(id) || !TYPES_Get(id, &d))
        return 0;
    ULTIMA_1850_PrintString(d.description[0] ? d.description : d.name);
    ULTIMA_16ba_PrintChar('\n');
    return 1;
}

int TYPES_GAME_Sight(int x, int y, int distance, int fallback) {
    int sight = TYPES_GAME_Property(x, y, 0, -1);
    return sight < 0 ? fallback : sight == 0 ? 1 : sight == 1 ? 0 : distance != 1;
}

int TYPES_GAME_WalkableView(int actor, int vx, int vy) {
    int x = D_5893_map_id >= 128 ? vx : D_5896_map_x + vx - 5,
        y = D_5893_map_id >= 128 ? vy : D_5897_map_y + vy - 5;
    uint32_t id = TYPES_GAME_At(x, y);
    if (TYPES_Defined(id))
        return TYPES_GAME_Walkable(actor, x, y);
    return ULTIMA_2c4c_IsWalkableTile(actor, GetMapViewport(vx, vy));
}

int TYPES_GAME_Push(int sx, int sy, int dx, int dy) {
    uint32_t source = TYPES_GAME_At(sx, sy), destination = TYPES_GAME_At(dx, dy);
    TypeDef sourceDef, destinationDef;
    int extended = source >= 512 || destination >= 512 ||
                   (TYPES_Defined(source) && TYPES_Get(source, &sourceDef) &&
                    sourceDef.base != source) ||
                   (TYPES_Defined(destination) && TYPES_Get(destination, &destinationDef) &&
                    destinationDef.base != destination);
    if (!extended)
        return 0;
    /* Scenery contents have stable map-cell identities. Workshop can move their
     * definitions and terrain together; gameplay must not orphan those contents. */
    if (CONTENTS_GAME_Command(sx, sy) || CONTENTS_GAME_Command(dx, dy)) {
        ULTIMA_1850_PrintString("It won't move.\n");
        return 1;
    }
    if (!TYPES_SwapCells(TYPES_GAME_Cell(sx, sy), TYPES_GAME_Cell(dx, dy), source, destination)) {
        ULTIMA_1850_PrintString("It won't move.\n");
        return 1;
    }
    TypeDef d;
    TYPES_Get(source, &d);
    *ULTIMA_4402_BaseAddr(dx, dy) = d.base;
    TYPES_Get(destination, &d);
    *ULTIMA_4402_BaseAddr(sx, sy) = d.base;
    ULTIMA_1850_PrintString("Pushed!\n");
    return 1;
}
