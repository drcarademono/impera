#include "contents_game.h"
#include "common/common.h"
#include "contents.h"
#include "funcs.h"
#include "macros.h"
#include "tiles.h"
#include "vars.h"
#include <stdio.h>
static const ContentsDef *here(int x, int y) {
    unsigned kind = D_5893_map_id >= 128 ? CONTENTS_EncounterKind() : 0;
    unsigned map = kind ? CONTENTS_EncounterMap() : D_5893_map_id;
    if (!kind && !map) {
        x &= 255;
        y &= 255;
    }
    return CONTENTS_Find(kind, map, kind ? 0 : (int8_t)D_5895_map_level, x, y);
}
static uint32_t today(void) { return CONTENTS_GameDay(D_5874, D_587d, D_587e); }
static int pending(const ContentsDef *d, const ContentsState *s) {
    for (unsigned i = 0; i < d->count; i++)
        if (s->remaining[i])
            return 1;
    return 0;
}
static int visibleShape(const ContentsDef *d) {
    if (!d || !d->id)
        return 0;
    unsigned kind = d->kind;
    int shape = CONTENTS_PendingShape(kind, d->map, d->floor, d->x, d->y);
    if (shape)
        return shape;
    /* A placed container remains visible until opened; hidden discoveries have no sprite. */
    const ContentsState *s = CONTENTS_Peek(d);
    return d->action == 1 && s && !(s->flags & 1) ? TILE_ACTOR_CHEST : 0;
}
int CONTENTS_GAME_Shape(int x, int y) { return visibleShape(here(x, y)); }
int CONTENTS_GAME_LocationShape(unsigned map, int floor, int x, int y) {
    return visibleShape(CONTENTS_Find(0, map, floor, x, y));
}
int CONTENTS_GAME_DungeonTile(int tile, int x, int y) {
    const ContentsDef *d = here(x, y);
    if (!d || !d->id || d->action != 1)
        return tile;
    const ContentsState *s = CONTENTS_Peek(d);
    return (tile & 8) | (!(s->flags & 1) ? 0x40 : pending(d, s) ? 0x70 : 0);
}
int CONTENTS_GAME_Command(int x, int y) {
    const ContentsDef *d = here(x, y);
    if (!d)
        return 0;
    if (!d->id)
        return 'S';
    const ContentsState *s = CONTENTS_Peek(d);
    return s && pending(d, s) ? 'G' : d->action ? 'O' : 'S';
}
unsigned CONTENTS_GAME_Grant(const ContentsItem *t, unsigned quantity) {
    if (!t || t->type < 1 || t->type > 9 ||
        t->variant >= (t->type == 7   ? 48
                       : t->type >= 8 ? 8
                                      : 1))
        return 0;
    unsigned max = t->type <= 2 ? 9999 : 99, current = 0;
    byte *b = NULL;
    u16 *w = NULL;
    switch (t->type) {
    case 1:
        w = &D_57aa;
        break;
    case 2:
        w = &D_57a8;
        break;
    case 3:
        b = &D_57ae;
        break;
    case 4:
        b = &D_57ad;
        break;
    case 5:
        b = &D_57ac;
        break;
    case 6:
        b = &D_57b1;
        break;
    case 7:
        b = &D_57c0[t->variant];
        break;
    case 8:
        b = &D_5820[t->variant];
        break;
    case 9:
        b = &D_5828[t->variant];
        break;
    default:
        return 0;
    }
    current = b ? *b : *w;
    unsigned take = current >= max ? 0 : max - current;
    if (take > quantity)
        take = quantity;
    if (!take)
        return 0;
    if (b)
        *b = (byte)(current + take);
    else
        *w = (u16)(current + take);
    D_a9fa = 1;
    return take;
}
static void describe(const ContentsItem *t, unsigned take) {
    const char *name = t->type == 7   ? D_17f6[t->variant]
                       : t->type == 8 ? D_41ac[t->variant]
                       : t->type == 9 ? D_419c[t->variant]
                       : t->type == 1 ? "gold"
                       : t->type == 2 ? "food"
                       : t->type == 3 ? "torches"
                       : t->type == 4 ? "gems"
                       : t->type == 5 ? "keys"
                                      : "skull keys";
    ULTIMA_1a3e_PrintNumber((int)take, 1, 0x20);
    ULTIMA_1850_PrintString(" ");
    ULTIMA_1850_PrintString((char *)name);
    ULTIMA_1850_PrintString("!\n");
}
int CONTENTS_GAME_Interact(int action, int x, int y, int actor) {
    const ContentsDef *d = here(x, y);
    if (!d)
        return 0;
    if (!d->id) {
        if (action == 0 || action == 2) {
            ULTIMA_1850_PrintString("Nothing of note.\n");
            return 1;
        }
        return 0;
    }
    ContentsState *s = CONTENTS_State(d);
    if (!s) {
        ULTIMA_1850_PrintString("Cannot open this container.\n");
        return 1;
    }
    int actorRequired = action == 0 || action == 1 || action == 3;
    if (actorRequired && actor < 0) {
        actor = ULTIMA_4988();
        if (actor < 0)
            return 1;
    }
    if (action == 3 || action == 4) {
        if (!d->action)
            return 0;
        if (action == 4) {
            s->flags |= 2 | 4;
            ULTIMA_1850_PrintString("Unlocked and disarmed!\n");
            return 1;
        }
        if (!(d->flags & 1) || (s->flags & 2)) {
            ULTIMA_1850_PrintString("No lock!\n");
            return 1;
        }
        if (!D_57ac) {
            ULTIMA_1850_PrintString("No keys!\n");
            return 1;
        }
        int threshold = ((int)d->lockDifficulty - D_55a8_party[actor].dex + 30) / 2;
        if (threshold < ULTIMA_2092_RandomRange(1, 30)) {
            s->flags |= 2;
            ULTIMA_1850_PrintString("Unlocked!\n");
        } else {
            D_57ac--;
            ULTIMA_1850_PrintString("Key broke!\n");
        }
        return 1;
    }
    if (action == 2) {
        if (!(s->flags & 1)) {
            ULTIMA_1850_PrintString(d->action ? "Open it first!\n" : "Search it first!\n");
            return 1;
        }
        int any = 0;
        for (unsigned i = 0; i < d->count; i++)
            if (s->remaining[i]) {
                unsigned taken = CONTENTS_GAME_Grant(&d->items[i], s->remaining[i]);
                if (taken)
                    describe(&d->items[i], taken);
                s->remaining[i] -= (uint16_t)taken;
                any |= taken != 0;
            }
        if (!any)
            ULTIMA_1850_PrintString(pending(d, s) ? "Thou canst carry no more.\n"
                                                  : "Nothing to get!\n");
        D_24e6 |= 2;
        return 1;
    }
    if (action == 0 && d->action) {
        if ((d->flags & 2) && !(s->flags & 4)) {
            int threshold = ((int)d->trapDifficulty - D_55a8_party[actor].intel + 30) / 2;
            if (threshold <= ULTIMA_2092_RandomRange(1, 30))
                ULTIMA_1850_PrintString(d->trapDifficulty < 10   ? "A simple trap!\n"
                                        : d->trapDifficulty > 20 ? "A complex trap!\n"
                                                                 : "A trap!\n");
            else
                ULTIMA_1850_PrintString("No trap!\n");
        } else
            ULTIMA_1850_PrintString("No trap!\n");
        return 1;
    }
    if (action != d->action) {
        ULTIMA_1850_PrintString(d->action ? "Open it first!\n" : "Search here!\n");
        return 1;
    }
    if (d->action && (d->flags & 1) && !(s->flags & 2)) {
        ULTIMA_1850_PrintString("Locked!\n");
        return 1;
    }
    if (pending(d, s)) {
        ULTIMA_1850_PrintString("Treasure remains.\n");
        return 1;
    }
    if (!CONTENTS_Available(d, s, today(), D_585c, D_57b2, D_57ac, D_57c0[0x27])) {
        ULTIMA_1850_PrintString("Nothing of note.\n");
        return 1;
    }
    if ((d->flags & 2) && !(s->flags & 4)) {
        ULTIMA_1850_PrintString("Trapped!\n");
        s->flags |= 4;
        ULTIMA_2fd0(actor);
        if (D_55a8_party[actor].status == STATUS_DEAD)
            return 1;
    }
    CONTENTS_Reveal(d, s, today());
    if (d->action)
        for (int i = 1; i < 32; i++)
            if (D_5c5a[i]._0_tile == TILE_ACTOR_CHEST && D_5c5a[i]._2_x == d->x &&
                D_5c5a[i]._3_y == d->y && (d->slot == 255 || i == d->slot) &&
                (D_5893_map_id >= 128 || D_5c5a[i]._4_z == D_5895_map_level)) {
                D_5c5a[i]._0_tile = D_5c5a[i]._1_animTile = 0;
            }
    if (d->original == 14)
        D_57b2 = D_587e;
    if (d->original != 65535 && d->original != 13 && d->original != 14 && d->original != 15)
        D_585c[d->original / 8] |= (byte)(1u << (d->original % 8));
    ULTIMA_1850_PrintString(d->count ? "Treasure!\n" : "Empty!\n");
    D_24e6 |= 2;
    return 1;
}
