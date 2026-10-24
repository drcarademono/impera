#include "mod/actors.h"
#include "mod/types_game.h"
#include "common/common.h"
#include "funcs.h"
#include "vars.h"
#include "macros.h"
#include "tiles.h"

#include "common/file.h"
#include <string.h>
#include "npc.h"
#include "town.h"
#include "mainout.h"
#include "mod/package.h"
#include <stdlib.h>

static int NPC_032c(int a, int b, int c, int d, int e, int f);
static int NPC_06a0(int param_1, int param_2, int param_3, int param_4);
static int NPC_0adc(int param_1, int param_2, int param_3, int param_4, int param_5);
static int NPC_0b9e(int param_1, int param_2, int param_3, int param_4);

int NPC_ObjectKind(unsigned slot) {
    return slot < (unsigned)LOCATION_NPC_COUNT ? NPCS_ObjectKind(&D_5d5e[slot], D_659e[slot]) : -1;
}
int NPC_PlacedShadowLord(void) {
    for (int slot = 1; slot < LOCATION_NPC_COUNT; slot++)
        if (NPC_ObjectKind(slot) == NPCS_OBJECT_SHADOWLORD && D_5d5e[slot].z[0] == D_5895_map_level)
            return D_5d5e[slot].time[3];
    return -1;
}
static int NPC_Behavior(int slot, int schedule) {
    return NPC_ObjectKind(slot) == NPCS_OBJECT_SHADOWLORD ? 6 : D_5d5e[slot].type[schedule];
}
int NPC_ConsumeObject(unsigned actor) {
    if (!MAPS_Town(D_5893_map_id) || actor == 0 || actor >= (unsigned)MAP_ACTOR_COUNT)
        return 0;
    int slot = TOWN_011e(actor);
    if (slot < 1 || NPC_ObjectKind(slot) < 0)
        return 0;
    TOWN_0052_SetNpcKilled(slot);
    TOWN_00b0_DespawnNpc(slot);
    return 1;
}
int NPC_BloodAt(int x, int y, int floor) {
    int blood = 0;
    for (int i = 1; i < MAP_ACTOR_COUNT; i++) {
        if (!D_5c5a[i]._0_tile || !ACTORS_Contains(i, x, y) ||
            (D_5893_map_id < 128 && D_5c5a[i]._4_z != floor))
            continue;
        if (NPCS_Blood(D_5c5a[i]._0_tile, D_5c5a[i]._5, D_5c5a[i]._7))
            blood = 1;
        else
            return 0;
    }
    return blood;
}
void NPC_ApplyObjectTriggers(int x, int y, int floor) {
    for (int i = 1; i < LOCATION_NPC_COUNT; i++) {
        NpcScheduleFmt *s = &D_5d5e[i];
        if (NPC_ObjectKind(i) != NPCS_OBJECT_TRIGGER || !D_5f5e[i]._0 || s->x[0] != x ||
            s->y[0] != y || s->z[0] != (unsigned char)floor)
            continue;
        D_5f5e[i]._0 = 0;
        for (int j = 1; j < 3; j++)
            if (s->x[j] < MAP_WIDTH && s->y[j] < MAP_HEIGHT && s->z[j] == (unsigned char)floor)
                TYPES_GAME_Write(s->x[j], s->y[j], D_5f5e[i]._a & 255);
        D_24e6 |= 1;
    }
}

void NPC_0000_LoadNpcFile(void)
{
    int local_4;
    char* local_6;
    byte local_26[32];
    int local_28;

#if !defined(MATCHING_BUILD)
    local_6 = NULL;
#endif

    char customName[32];
    unsigned map = D_5893_map_id;
    if (MAPS_NewLocation(map)) {
        snprintf(customName, sizeof(customName), "LOC%03u.NPC", map);
        local_6 = customName;
        local_28 = 0;
    } else {
        static char *files[] = {"TOWNE.NPC", "DWELLING.NPC", "CASTLE.NPC", "KEEP.NPC"};
        local_6 = files[(map - 1) / 8];
        local_28 = (map - 1) % 8;
    }
    ULTIMA_256e_ReadFileFromDisk(local_6, D_5d5e, 0x200, local_28 * 0x240);
    ULTIMA_256e_ReadFileFromDisk(local_6, D_659e, 0x20, local_28 * 0x240 + 0x200);
    ULTIMA_256e_ReadFileFromDisk(local_6, local_26, 0x20, local_28 * 0x240 + 0x220);

    for (local_4 = 0; local_4 < 0x20; local_4++)
    {
        D_5f5e[local_4]._a = (u16)local_26[local_4];
    }

    NPC_LoadExtra(local_6, local_28);
}

int NPC_InitializeNewGame(void)
{
    static const char *files[] = {"TOWNE.NPC", "DWELLING.NPC", "CASTLE.NPC", "KEEP.NPC"};
    byte record[0x240];
    if (D_5893_map_id == 0 || !MAPS_Outside(D_5893_map_id))
        return 1;
    int location = D_5893_map_id - 1;
    char customName[32];
    const char *resource = files[MAPS_NewLocation(D_5893_map_id) ? 0 : location / 8];
    if (MAPS_NewLocation(D_5893_map_id)) {
        snprintf(customName, sizeof(customName), "LOC%03u.NPC", D_5893_map_id);
        resource = customName;
        location = 0;
    }
    FILE *file = FILE_Open(resource, "rb");
    if (!file) {
        DEBUG_Error("Cannot initialize starting NPCs from %s", resource);
        return 0;
    }
    int ok = fseek(file, (location % 8) * sizeof(record), SEEK_SET) == 0 &&
             fread(record, 1, sizeof(record), file) == sizeof(record) && !ferror(file);
    fclose(file);
    if (!ok) {
        DEBUG_Error("Starting location NPC definitions are incomplete in %s", resource);
        return 0;
    }
    memcpy(D_5d5e, record, 0x200);
    memcpy(D_659e, record + 0x200, 0x20);
    for (int i = 0; i < 32; ++i) {
        D_5f5e[i]._a = record[0x220 + i];
        if (i && TOWN_0000_CheckNpcKilled(i)) D_659e[i] = 0;
    }
    NPC_LoadExtra(resource, location % 8);
    NPC_00d6(D_587f);
    TOWN_1694();
    debug("Initialized starting settlement NPCs: map=%u floor=%u hour=%u", D_5893_map_id,
          D_5895_map_level, D_587f);
    return 1;
}

void NPC_00d6(int param_1)
{
    int local_4;
    int local_6;

    for (local_4 = 1; local_4 < LOCATION_NPC_COUNT; local_4++) {
        if (D_659e[local_4] == 0)
        {
            D_5f5e[local_4]._0 = 0;
        }
        else
        {
            local_6 = NPC_12e0(local_4, param_1);
            D_5f5e[local_4]._0 = 1;

            D_5f5e[local_4]._2_x = D_5d5e[local_4].x[local_6];
            D_5f5e[local_4]._4_y = D_5d5e[local_4].y[local_6];
            D_5f5e[local_4]._6_z = D_5d5e[local_4].z[local_6];

            D_5f5e[local_4]._8 = D_659e[local_4];
            D_5f5e[local_4]._e = local_6;
            D_5f5e[local_4]._c = 0;

            D_655e[local_4] = -1;
            D_615e[local_4].data[0] = 0;
            D_65c2[local_4] = 0;
        }
    }
}

static int NPC_01a0(int param_1, int param_2, int param_3, int param_4)
{
    int local_4;

    if (param_3 != 0)
    {
        local_4 = -1;
    }
    else
    {
        local_4 = -2;
    }

    return NPC_032c(param_1, param_2, 0, 0, local_4, param_4);
}

// CHECKED (loop)
static void NPC_01d2(int param_1, int param_2, int param_3, int param_4, int param_5, int param_6)
{
    int local_a;
    int local_8;
    int local_6;
    byte local_e;
    NpcFmt* local_4;
    ActorFmt* local_c;

    if (param_5 == -1)
    {
        local_e = 0xc8;
    }

    if (param_5 == -2)
    {
        local_e = 0xc9;
    }

    local_a = NPC_12e0(param_6, D_587f);
    for (local_6 = 0; local_6 < 0x20; local_6 = local_6 + 1)
    {
        for (local_8 = 0; local_8 < 0x20; local_8 = local_8 + 1)
        {
            if (NPC_0adc(local_8, local_6, D_5895_map_level, param_6, local_a) != 0)
            {
                D_b11c[local_8 + local_6 * 0x20] = 0;
            }
            else
            {
                D_b11c[local_8 + local_6 * 0x20] = 0x90;
            }

            if (param_5 < 0 && GetMap(local_8, local_6) == local_e)
            {
                D_b11c[local_8 + local_6 * 0x20] = 5;
            }
        }
    }

    // 0272
    local_4 = &D_5f5e[param_6];

    for (local_6 = MAP_ACTOR_COUNT - 1; local_6 > 0; local_6--) // NOT MATCHING
    {
        local_c = &D_5c5a[local_6];
        if (local_c->_0_tile > TILE_ACTOR_WIZARD - 1)
        {
            if (NPC_06a0(local_c->_2_x, local_c->_3_y, local_4->_2_x, local_4->_4_y) < 4)
            {
                D_b11c[local_c->_3_y * 0x20 + local_c->_2_x] = 0x90;
            }
        }
    }

    local_c = &D_5c5a[0];
    if (NPC_06a0(local_c->_2_x, local_c->_3_y, local_4->_2_x, local_4->_4_y) < 4)
    {
        D_b11c[local_c->_3_y * 0x20 + local_c->_2_x] = 0x90;
    }

    if (param_5 > -1)
    {
        D_b11c[param_4 * 0x20 + param_3] = 5;
    }

    D_b11c[param_2 * 0x20 + param_1] = 0x46;
}

static int NPC_ExtendedPath(int sx, int sy, int tx, int ty, int mode, int npc) {
    int w = MAP_WIDTH, h = MAP_HEIGHT, n = w * h;
    if (sx < 0 || sy < 0 || sx >= w || sy >= h || npc < 1 || npc >= LOCATION_NPC_COUNT)
        return 0;
    int *parent = malloc((size_t)n * sizeof(int)), *queue = malloc((size_t)n * sizeof(int));
    if (!parent || !queue) {
        free(parent);
        free(queue);
        return 0;
    }
    for (int i = 0; i < n; i++)
        parent[i] = -1;
    int start = sy * w + sx, read = 0, write = 1, goal = -1;
    queue[0] = start;
    parent[start] = start;
    const int dx[] = {1, 0, -1, 0}, dy[] = {0, -1, 0, 1};
    int schedule = NPC_12e0(npc, D_587f);
    while (read < write) {
        int at = queue[read++], x = at % w, y = at / w;
        if (mode < 0 ? GetMap(x, y) == (mode == -1 ? TILE_MAP_LADDER_UP : TILE_MAP_LADDER_DOWN)
                     : x == tx && y == ty) {
            goal = at;
            break;
        }
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            int next = ny * w + nx;
            if (parent[next] >= 0)
                continue;
            if (!(nx == tx && ny == ty && mode >= 0) &&
                !NPC_0adc(nx, ny, D_5895_map_level, npc, schedule))
                continue;
            int occupant = ULTIMA_368e_FindActorTileAtPos(nx, ny, D_5895_map_level);
            if (occupant && !(nx == tx && ny == ty))
                continue;
            parent[next] = at;
            queue[write++] = next;
        }
    }
    if (goal >= 0 && mode < 0) {
        D_5876 = goal % w;
        D_5878 = goal / w;
    }
    if (goal >= 0 && mode >= 0) {
        int length = 0, at = goal;
        while (at != start && length < n) {
            queue[length++] = at;
            at = parent[at];
        }
        memset(D_615e[npc].data, 0, 32);
        D_655e[npc] = 0;
        int used = 0, last = -1, count = 0, previous = start;
        for (int i = length - 1; i >= 0; i--) {
            int next = queue[i], direction = next == previous + 1   ? 1
                                             : next == previous - w ? 2
                                             : next == previous - 1 ? 3
                                                                    : 4;
            if (direction != last || count == 255) {
                if (count) {
                    D_615e[npc].data[used++] = count;
                    D_615e[npc].data[used++] = last;
                }
                if (used >= 30) {
                    count = 0;
                    break;
                }
                last = direction;
                count = 0;
            }
            count++;
            previous = next;
        }
        if (count && used < 30) {
            D_615e[npc].data[used++] = count;
            D_615e[npc].data[used] = last;
        }
    }
    free(parent);
    free(queue);
    return goal >= 0;
}

// CHECKED
static int NPC_032c(int param_1, int param_2, int param_3, int param_4, int param_5, int param_6)
{
    if (MAPS_Record(1, D_5893_map_id, (int8_t)D_5895_map_level))
        return NPC_ExtendedPath(param_1, param_2, param_3, param_4, param_5, param_6);
    byte local_50[32];
    byte local_2e[32];
    int local_30;
    int local_e;
    int local_c;
    int local_a;
    int local_8;
    int local_6;
    int local_4;

    local_e = 0;
    local_c = 0;
    local_4 = 1;

    NPC_01d2(param_1, param_2, param_3, param_4, param_5, param_6);

    local_2e[0] = param_1;
    local_50[0] = param_2;

    //while (1)
    do
    {
        if (local_e != 0)
            break;

        local_a = local_2e[local_c];
        local_8 = local_50[local_c];
        local_30 = D_b11c[local_8 * 0x20 + local_a] >> 4;

        for (local_6 = 0; local_6 < 4; local_6++)
        {
            switch (local_30)
            {
            case 1:
                local_a--;
                if (local_a < 0)
                {
                    local_e = 2;
                }
                break;

            case 2:
                local_8++;
                if (local_8 > 0x20)
                {
                    local_e = 2;
                }
                break;

            case 3:
                local_a++;
                if (local_a > 0x20)
                {
                    local_e = 2;
                }
                break;

            case 4:
                local_8--;
                if (local_8 < 0)
                {
                    local_e = 2;
                }
                break;

            default:
                break;
            }

            if (local_e != 2)
            {
                if (D_b11c[local_8 * 0x20 + local_a] < 0x10)
                {
                    local_e = D_b11c[local_8 * 0x20 + local_a] & 0xf;
                    D_b11c[local_8 * 0x20 + local_a] = (byte)(local_30 << 4);
                    if (local_e == 5)
                    {
                        local_e = 1;
                        D_5876 = local_a;
                        D_5878 = local_8;
                        break;
                    }

                    if (local_4 != local_c)
                    {
                        local_2e[local_4] = local_a;
                        local_50[local_4] = local_8;
                        local_4++;
                    }

                    if (local_4 >= 0x20)
                    {
                        local_4 = 0;
                    }
                }
            }

            local_e = 0;
            local_30 = (local_30 & 3) + 1;
            local_a = local_2e[local_c];
            local_8 = local_50[local_c];
        }

        if (local_e == 1)
        {
            return 1;
        }

        local_c++;
        if (local_c == 0x20)
        {
            local_c = 0;
        }

        //if (local_c == local_4)
        //    break;
    } while (local_c != local_4);

    return local_e;
}

// CHECKED
static int NPC_04ac(int param_1, int param_2, int param_3, int param_4)
{
    if (MAPS_Record(1, D_5893_map_id, (int8_t)D_5895_map_level))
        return 0;
    int local_c;
    int local_a;
    int local_8;
    int local_6;
    int local_4;

    local_6 = 0;
    local_a = 0;

    D_655e[param_1] = 0;

    local_8 = D_b11c[param_4 * 0x20 + param_3] & 0xf;
    local_4 = D_b11c[param_4 * 0x20 + param_3] >> 4;

    local_c = local_4;

    do
    {
        switch (local_4)
        {
        case 1:
            param_3++;
            break;
        case 2:
            param_4--;
            break;
        case 3:
            param_3--;
            break;
        case 4:
            param_4++;
            break;
        }

        if (local_c == local_4 && local_8 != 6)
        {
            local_6++;
        }

        if (local_c != local_4 || local_8 == 6)
        {
            D_615e[param_1].data[local_a] = local_6;
            local_a++;
            D_615e[param_1].data[local_a] = local_c;
            local_a++;

            if (local_8 == 6)
                break;

            local_c = local_4;
            local_6 = 1;
        }

        local_4 = D_b11c[param_4 * 0x20 + param_3] >> 4;
        local_8 = D_b11c[param_4 * 0x20 + param_3] & 0xf;
    } while (local_a < 0x20);

    local_8 = local_a - 2;

    if (local_8 >= 0)
    {
        local_6 = 0;
        do
        {
            // swap
            local_c = D_615e[param_1].data[local_6];
            D_615e[param_1].data[local_6] = D_615e[param_1].data[local_8];
            D_615e[param_1].data[local_8] = local_c;

            local_8++;
            local_6++;

            // swap*
            local_c = (D_615e[param_1].data[local_6] + 1 & 3) + 1;
            D_615e[param_1].data[local_6] = (D_615e[param_1].data[local_8] + 1 & 3) + 1;
            D_615e[param_1].data[local_8] = local_c;

            local_6++;
            local_8 -= 3;
        } while (local_8 >= local_6);
    }

    return local_a;
}

// set walk direction
static void NPC_0632(int param_1)
{
    switch (param_1)
    {
    case 1:
        D_5876++;
        if (D_5878 >= MAP_HEIGHT) {
            D_5876 = MAP_WIDTH - 1;
        }
        break;

    case 2:
        D_5878--;
        if (D_5876 < 0)
        {
            D_5878 = 0;
        }
        break;

    case 3:
        D_5876--;
        if (D_5878 < 0)
        {
            D_5876 = 0;
        }
        break;

    case 4:
        D_5878++;
        if (D_5876 >= MAP_WIDTH) {
            D_5878 = MAP_HEIGHT - 1;
        }
        break;
    }
}

// Calculate manhattan distance
static int NPC_06a0(int param_1, int param_2, int param_3, int param_4)
{
    int local_6;
    int local_4;

    local_4 = param_1 - param_3;
    local_6 = param_2 - param_4;

    if (local_4 < 0)
    {
        local_4 = -local_4;
    }

    if (local_6 < 0)
    {
        local_6 = -local_6;
    }

    local_4 += local_6;
    return local_4;
}

// CHECKED
static void NPC_06e4(int param_1, int param_2)
{
    int local_4;
    int local_6;
    NpcFmt* local_8;
    int local_10[4];
    int local_12;
    int local_14;
    int local_16;
    ActorFmt* local_18;
    int local_1a;
    int local_1c;

    CHECK_PARAM_RANGE(param_1, 0, LOCATION_NPC_COUNT);
    CHECK_PARAM_RANGE(param_2, 0, 3);

    local_18 = &D_5c5a[0];
    local_8 = &D_5f5e[param_1];
    local_4 = NPC_Behavior(param_1, param_2);
    local_6 = NPC_06a0(D_5c5a[0]._2_x, D_5c5a[0]._3_y, local_8->_2_x, local_8->_4_y);
    if (local_6 == 1 && local_4 > 3)
    {
        if (local_4 == 4 || local_4 == 5)
        {
            // a9cd
            if (local_8->_a != 0)
            {
                // a9d6
                D_65be = 0x74;
                // a9db
                D_65bf = param_1;
                local_4 = 1;
            }
        }
        else
        {
            // aa4e: ??
            D_65be = 0x61;
            // -> a9db
            D_65bf = param_1;
            local_4 = 1;
        }
    }
    else
    {
        // a9ea
        for (local_16 = 1; local_16 < 5; local_16++)
        {
            // aa09
            D_5876 = local_8->_2_x;
            D_5878 = local_8->_4_y;
            NPC_0632(local_16);
            local_12 = D_5876;
            local_14 = D_5878;
            if (NPC_0b9e(D_5876, D_5878, param_1, -1) != 0)
            {
                local_10[local_16 - 1] = NPC_06a0(local_18->_2_x, local_18->_3_y, local_12, local_14);
            }
            else
            {
                // aa56 (OK)
                local_10[local_16 - 1] = 99;
            }
        }

        // aa6a (OK)
        local_1a = -1; // di
        local_6 = NPC_06a0(local_18->_2_x, local_18->_3_y, local_8->_2_x, local_8->_4_y);
        // aa8c
        for (local_16 = 1; local_16 < 8; local_16++)
        {
            // aaa7
            if (local_16 < 5 && local_10[local_16 - 1] != 99)
            {
                if (local_4 == 3)
                {
                    if (local_6 < local_10[local_16 - 1])
                    {
                        if (local_1a == -1 || ULTIMA_2092_RandomRange(0, 1) != 0)
                        {
                            local_1a = local_16;
                        }

                        if (local_16 == 4)
                            break;
                    }
                }
                else
                {
                    // ab14
                    if (local_10[local_16 - 1] < local_6)
                    {
                        local_1a = local_16;
                        break;
                    }
                }
            }

#if defined(MATCHING_BUILD)
            // aae4 (OK)
            if (local_16 > 4 && local_10[local_16 - 1] != 99) // BUG? (OOB, invalid index)
#else
            if (local_16 > 4 && local_10[local_16 - 5] != 99) // BUGFIX?
#endif
            {
                if (local_10[local_16 - 5] == local_6)
                {
                    local_1a = local_16;
                    break;
                }
            }
        }

        // ab22 (OK below)
        if ((local_4 == 5 || local_4 == 7) && ULTIMA_2092_RandomRange(0, 0x3f) < 0x10)
        {
            // ab3d
            local_1c = local_1a;
            for (local_16 = 1; local_16 < 5; local_16++)
            {
                // ab4b
                if (local_1a != local_16 && local_10[local_16 - 1] != 99 &&
                    (local_1c == local_1a || ULTIMA_2092_RandomRange(0, 0x3f) < 0x10))
                {
                    local_1c = local_16;
                }
            }
            local_1a = local_1c;
        }

        if (local_1a > -1)
        {
            D_5876 = local_8->_2_x;
            D_5878 = local_8->_4_y;
            NPC_0632(local_1a);

            unsigned w, h;
            ACTORS_Size(local_8->_c, &w, &h);
            if ((w > 1 || h > 1) && !ACTORS_CanMove(local_8->_c, D_5876, D_5878))
                return;
            D_5c5a[local_8->_c]._2_x = local_8->_2_x = D_5876;
            D_5c5a[local_8->_c]._3_y = local_8->_4_y = D_5878;
            D_24e6 |= 2;
        }
    }
}

// CHECKED
static int NPC_0938(int param_1, int param_2)
{
    NpcFmt* local_8;
    int local_a;
    int local_6;
    NpcScheduleFmt* local_4;

    if (NPC_ObjectKind(param_1) == NPCS_OBJECT_SHADOWLORD)
        return 0;
    local_a = -1;
    local_8 = &D_5f5e[param_1];
    local_4 = &D_5d5e[param_1];

    for (local_6 = 0; local_6 < 4; local_6++)
    {
        if (local_4->time[local_6] == param_2)
        {
            local_a = NPC_12e0(param_1, param_2);
            if (local_a == local_8->_e) // NOT MATCHING: di vs ax
            {
                local_8->_0 = 1;
                break;
            }

            if (local_8->_6_z != D_5895_map_level && local_4->z[local_a] != D_5895_map_level)
            {
                local_8->_0 = 8;
            }
            else
            {
                if (local_8->_6_z == D_5895_map_level)
                {
                    if (local_4->z[local_a] == D_5895_map_level)
                    {
                        local_8->_0 = 2;
                    }
                    else if ((char)D_5895_map_level < (char)local_4->z[local_a])
                    {
                        local_8->_0 = 6;
                    }
                    else
                    {
                        local_8->_0 = 7;
                    }
                }
                else if ((char)D_5895_map_level < (char)local_8->_6_z)
                {
                    local_8->_0 = 4;
                }
                else
                {
                    local_8->_0 = 5;
                }
            }
            break;
        }
    }

    // ac1b
    if (-1 < local_a && local_8->_2_x == (uint)local_4->x[local_a] &&
        local_8->_4_y == (uint)local_4->y[local_a] &&
        local_8->_6_z == (uint)local_4->z[local_a])
    {
        local_a = 0;
        local_8->_0 = 1;
    }

    if (local_a == -1)
    {
        local_a = 0;
    }
    else
    {
        local_a++;
    }

    return local_a;
}

static int NPC_0a4a(int param_1, int param_2)
{
    byte local_8;
    int local_6;
    NpcFmt* local_4;

    local_4 = &D_5f5e[param_1];

    // 0a4a
    local_8 = *ULTIMA_4402_GetTileAddr(D_5c5a[local_4->_c]._2_x, D_5c5a[local_4->_c]._3_y);
    if ((char)D_5d5e[param_1].z[param_2] < (char)D_5895_map_level)
    {
        if (local_8 != TILE_MAP_LADDER_DOWN)
        {
            // 0a92
            if ((local_8 & 0xf4) == TILE_MAP_STAIR)
            {
                local_6 = 1;
            }
            else
            {
                local_6 = 0;
            }
        }
        else
        {
            // 0a9b
            local_6 = 1;
        }
    }
    else
    {
        // 0aa2
        local_8 = *ULTIMA_4402_GetTileAddr(D_5c5a[local_4->_c]._2_x, D_5c5a[local_4->_c]._3_y);
        if (local_8 == TILE_MAP_LADDER_UP)
        {
            // -> 0a9b
            local_6 = 1;
        }
        else
        {
            if ((local_8 & 0xf4) == TILE_MAP_STAIR)
            {
                local_6 = 1;
            }
            else
            {
                local_6 = 0;
            }
        }
    }

    return local_6;
}

static int NPC_0adc(int param_1, int param_2, int param_3, int param_4, int param_5)
{
    byte local_8;
    int local_6;
    NpcScheduleFmt* local_4;

    local_6 = 0;
    local_4 = &D_5d5e[param_4];
    unsigned actor = D_5f5e[param_4]._c, width, height;
    if (actor < (unsigned)MAP_ACTOR_COUNT && param_3 == D_5895_map_level) {
        ACTORS_Size(actor, &width, &height);
        if (width > 1 || height > 1)
            return ACTORS_CanMove(actor, param_1, param_2);
    }

    if (param_5 > -1 && local_4->x[param_5] == param_1 && local_4->y[param_5] == param_2 && local_4->z[param_5] == param_3)
    {
        local_6 = 2;
    }
    else
    {
        if (param_1 < 0 || param_2 < 0 || param_1 >= MAP_WIDTH || param_2 >= MAP_HEIGHT) {
            local_8 = D_6a07;
        } else {
            local_8 = GetMap(param_1, param_2);
        }

        if (D_5f5e[param_4]._0 == 3 && (local_8 == TILE_MAP_LADDER_UP || local_8 == TILE_MAP_LADDER_DOWN))
        {
            local_6 = 1;
        }
        else
        {
            local_6 = ((uint)D_367e[local_8 >> 3] & 0x80 >> (local_8 & 7)) == 0;
            if (param_1 >= 0 && param_2 >= 0 && param_1 < MAP_WIDTH && param_2 < MAP_HEIGHT) {
                TypeCell key = {3, D_5893_map_id, 0, param_4 & 255, (unsigned)param_4 >> 8, 0, 0};
                unsigned actorType = TYPES_At(key, 0), tileType = TYPES_GAME_At(param_1, param_2);
                unsigned originalType = 256 + D_659e[param_4];
                if (!actorType && TYPES_Defined(originalType)) actorType = originalType;
                if (actorType || TYPES_Defined(tileType)) {
                    TypeDef terrain, actor;
                    if (actorType)
                        TYPES_Get(actorType, &actor);
                    else
                        TYPES_Vanilla(1, D_659e[param_4], &actor);
                    if (TYPES_Get(tileType, &terrain))
                        local_6 = !!(terrain.movement & (1u << actor.moveProfile));
                }
            }
        }
    }

    return local_6;
}

static int NPC_0b9e(int param_1, int param_2, int param_3, int param_4)
{
    unsigned actor = D_5f5e[param_3]._c, w, h;
    if (actor < (unsigned)MAP_ACTOR_COUNT) {
        ACTORS_Size(actor, &w, &h);
        if (w > 1 || h > 1)
            return ACTORS_CanMove(actor, param_1, param_2);
    }
    int local_8;
    int local_6;
    int local_4;

    local_6 = 0;
    if (param_1 < 0 || param_1 >= MAP_WIDTH || param_2 < 0 || param_2 >= MAP_HEIGHT) {
        return 0;
    }

    local_8 = *ULTIMA_4402_GetTileAddr(param_1, param_2) & 0xfc;
    if (D_659e[param_3] == 0xfc)
    {
        local_4 = 1;
    }
    else if (local_8 == TILE_MAP_30)
    {
        local_4 = 1;
    }
    else if (local_8 == TILE_MAP_CHAIR_90 && D_5f5e[param_3]._0 != 2)
    {
        local_4 = 0;
    }
    else
    {
        local_4 = NPC_0adc(param_1, param_2, D_5895_map_level, param_3, param_4);
    }

    local_6 = ULTIMA_3702(param_1, param_2, D_5895_map_level);
    if (local_4 == 0 || local_6 != 0)
    {
        local_4 = 0;
    }

    return local_4;
}

// move npc randomly
static void NPC_0c50(NpcFmt* param_1, int param_2, int param_3, int param_4, NpcScheduleFmt* param_5)
{
    int local_8;
    int local_6;
    int local_4;

    if ((ULTIMA_2092_RandomRange(0, 0xff) & 8) != 0)
    {
        local_4 = ULTIMA_3aae_Random(0x40);
        local_4 = (local_4 & 3) + 1;
        D_5876 = param_1->_2_x;
        D_5878 = param_1->_4_y;
        NPC_0632(local_4);
        local_6 = D_5876;
        local_8 = D_5878;

        if ((param_2 == 0 || NPC_06a0(param_5->x[param_3], param_5->y[param_3], D_5876, D_5878) <= param_2) &&
            NPC_0b9e(D_5876, D_5878, param_4, param_3) != 0)
        {
            D_5c5a[param_1->_c]._2_x = param_1->_2_x = local_6;
            D_5c5a[param_1->_c]._3_y = param_1->_4_y = local_8;

            D_24e6 |= 2;
        }
    }
}

// CHECKED (switch order)
static void NPC_0d00(int param_1, int param_2)
{
    NpcFmt* local_4;
    NpcScheduleFmt* local_6;

    local_4 = &D_5f5e[param_1];
    local_6 = &D_5d5e[param_1];

    switch (NPC_Behavior(param_1, param_2)) {
    case 0:
        break;

    case 2:
        NPC_0c50(local_4, 0, param_2, param_1, local_6);
        break;

    case 4:
        if (NPC_06a0(D_5896_map_x, D_5897_map_y, local_6->x[param_2], local_6->y[param_2]) < 4)
        {
            NPC_06e4(param_1, param_2);
        }

        break;

    case 1:
        NPC_0c50(local_4, 3, param_2, param_1, local_6);
        break;

    case 3:
    case 6:
        if (NPC_06a0(D_5896_map_x, D_5897_map_y, local_4->_2_x, local_4->_4_y) < 4)
        {
            NPC_06e4(param_1, param_2);
        }

        break;

    case 5:
    case 7:
        NPC_06e4(param_1, param_2);
        break;
    }
}

// NOT MATCHING (checked)
// move npc
void NPC_0db4(int param_1)
{
    byte local_12;
    int local_10;
    int local_e;
    int local_14;
    int local_c;
    int local_a;
    int local_6;
    int local_4;
    NpcFmt* local_8;

    local_14 = 0;
    D_65be = 0;
    D_65bf = 0;

    // 0dd9
    for (local_4 = 1; /*1267*/ local_4 < LOCATION_NPC_COUNT; /*1264*/ local_4++) {
        // 126d
        int kind = NPC_ObjectKind(local_4);
        if (kind == NPCS_OBJECT_PIRATE || kind == NPCS_OBJECT_WHIRLPOOL) {
            unsigned actor = D_5f5e[local_4]._c;
            if (actor && D_5c5a[actor]._0_tile && D_5c5a[actor]._4_z == D_5895_map_level) {
                MAINOUT_UpdateLocationHazard(actor);
                if (!D_5893_map_id)
                    return;
                if (D_659e[local_4] && D_5f5e[local_4]._c) {
                    actor = D_5f5e[local_4]._c;
                    D_5f5e[local_4]._2_x = D_5c5a[actor]._2_x;
                    D_5f5e[local_4]._4_y = D_5c5a[actor]._3_y;
                }
            }
            continue;
        }
        if (D_659e[local_4] == 0 || (kind >= 0 && kind != NPCS_OBJECT_SHADOWLORD))
            continue;

        // 1277
        local_e = NPC_12e0(local_4, param_1);
        // 1281
        local_8 = &D_5f5e[local_4];

        // 128e
        if (local_8->_0 <= 1 && NPC_0938(local_4, param_1) == 0)
        {
            // 124e
            if (local_8->_6_z == D_5895_map_level)
            {
                // 1261
                NPC_0d00(local_4, local_e);
            }
        }
        // 12a2
        else if (local_8->_0 > 3)
        {
            if (local_8->_0 == 4 || local_8->_0 == 5)
            {
                // 12bc
                if (local_14 != 1)
                {
                    local_14 = 1;
                    if (local_8->_0 == 4)
                    {
                        // 12cf
                        local_6 = 3;
                        // -> 0dd9
                    }
                    else
                    {
                        // 0dd4
                        local_6 = 4;
                    }

                    // 0dd9 (ok)
                    local_10 =
                        NPC_01a0(D_5d5e[local_4].x[local_e], D_5d5e[local_4].y[local_e], local_6 == 3, local_4);
                    if (local_10 != 0)
                    {
                        local_a = D_5876;
                        local_c = D_5878;
                        local_10 = NPC_032c(local_a, local_c, D_5d5e[local_4].x[local_e],
                                            D_5d5e[local_4].y[local_e], local_6, local_4);
                    }

                    // 0e32 (ok)
                    if (local_10 != 0)
                    {
                        // 0e3b (ok)
                        NPC_04ac(local_4, local_6, D_5d5e[local_4].x[local_e], D_5d5e[local_4].y[local_e]);

                        // 0e5a (ok)
                        local_12 = GetMap(local_a, local_c);
                        if (/*0e6b*/ (local_6 == 3 && local_12 == TILE_MAP_LADDER_UP) ||
                            /*0e75*/ (local_6 == 4 && local_12 == TILE_MAP_LADDER_DOWN) ||
                            /*0e81*/ (local_12 & 0xfc) == TILE_MAP_STAIR)
                        {
                            // 0e8a (ok)
                            TOWN_1726(local_4, local_a, local_c, D_5895_map_level);
                        }

                        // 0e9c (ok)
                        local_8->_0 = 2;
                    }
                }
            }
            else if (local_8->_0 == 6 || local_8->_0 == 7)
            {
                // 0eb2
                if (NPC_0a4a(local_4, local_e) != 0)
                {
                    // 0ebf
                    TOWN_1726(local_4, D_5d5e[local_4].x[local_e], D_5d5e[local_4].y[local_e],
                              D_5d5e[local_4].z[local_e]);

                    local_8->_e = local_e;
                    D_655e[local_4] = -1;
                    local_8->_0 = 1;
                }
                else
                {
                    // 0efe
                    if (local_8->_0 != 2 && local_14 != 1)
                    {
                        local_14 = 1;
                        if (local_8->_0 == 6)
                        {
                            local_6 = 1;
                        }
                        else
                        {
                            local_6 = 2;
                        }
                        // 0f2b
                        local_10 = NPC_01a0(local_8->_2_x, local_8->_4_y, local_6 == 1, local_4);
                        if (local_10 != 0)
                        {
                            local_a = D_5876;
                            local_c = D_5878;
                            local_10 = NPC_032c(local_8->_2_x, local_8->_4_y, local_a, local_c, local_6, local_4);
                        }
                        // 0f71
                        if (local_10 != 0)
                        {
                            // 0f7a
                            NPC_04ac(local_4, local_6, local_a, local_c);
                            local_8->_0 = 3;
                        }
                    }
                }
            }
        }
        else
        {
            // 0f94
            if (D_655e[local_4] > -1 && D_615e[local_4].data[D_655e[local_4]] != 0)
            {
                // 0fb8
                D_5876 = local_8->_2_x;
                D_5878 = local_8->_4_y;
                NPC_0632(D_615e[local_4].data[D_655e[local_4] + 1]);
                local_a = D_5876;
                local_c = D_5878;
                local_10 = NPC_0b9e(D_5876, local_c, local_4, local_e);
                if (local_10 != 0)
                {
                    // 0ff9
                    D_5c5a[local_8->_c]._2_x = (u8)local_a;
                    local_8->_2_x = local_a & 0xff;
                    D_5c5a[local_8->_c]._3_y = (u8)local_c;
                    local_8->_4_y = local_c & 0xff;
                    D_24e6 |= 2;

                    D_615e[local_4].data[D_655e[local_4]]--;
                    D_65c2[local_4] = 0;
                    if (D_615e[local_4].data[D_655e[local_4]] == 0)
                    {
                        // 104a
                        D_655e[local_4]++;
                        D_615e[local_4].data[D_655e[local_4]++] = 0;
                        if (D_655e[local_4] >= 0x20 || D_615e[local_4].data[D_655e[local_4]] < 1)
                        {
                            D_655e[local_4] = -1;
                        }

                        if (local_10 == 2)
                        {
                            // 1083
                            local_8->_e = local_e;
                            local_8->_0 = 1;
                            D_655e[local_4] = -1;
                        }
                    }
                }
                else
                {
                    D_65c2[local_4]++;
                    NPC_0c50(&D_5f5e[local_4], 0, local_e, local_4, &D_5d5e[local_4]);
                    if (D_65c2[local_4] > 3)
                    {
                        // 10d0
                        D_655e[local_4] = -1;
                        // 10d6
                        D_65c2[local_4] = 0;
                    }
                }
            }
            else
            {
                // 10e0
                if (D_655e[local_4] == -1 && local_8->_0 == 3)
                {
                    local_e = NPC_12e0(local_4, param_1);
                    if (D_5895_map_level < D_5d5e[local_4].z[local_e])
                    {
                        local_8->_0 = 6;
                        return; // 12d8
                    }
                    local_8->_0 = 7;
                    return; // 12d8
                }

                // 1124
                if (local_14 < 1)
                {
                    // 112d
                    if (local_8->_0 != 1)
                    {
                        // 1138
                        if (D_65c2[local_4] < 200 && (D_65c2[local_4] == 0 || ULTIMA_2092_RandomRange(0, 2) == 1))
                        {
                            // 1160
                            if (D_655e[local_4] == -1)
                            {
                                local_14 = local_14 + 1;
                                if (NPC_032c(local_8->_2_x, local_8->_4_y, D_5d5e[local_4].x[local_e],
                                             D_5d5e[local_4].y[local_e], 0, local_4) != 0)
                                {
                                    NPC_04ac(local_4, 0, D_5d5e[local_4].x[local_e],
                                             D_5d5e[local_4].y[local_e]);

                                    D_65c2[local_4] = 0;
                                    continue;
                                }
                                // 11ba
                                D_65c2[local_4] = 200;
                            }

                            // 11c5
                            NPC_0c50(local_8, 0, local_e, local_4, &D_5d5e[local_4]);
                        }
                        else
                        {
                            // 11e8
                            if (D_65c2[local_4] >= 200)
                            {
                                D_65c2[local_4]++;
                            }

                            // 11f9
                            if (D_65c2[local_4] > 204)
                            {
                                D_65c2[local_4] = 0;
                            }
                        }
                    }
                }
                // 120e
                else if (local_8->_6_z == (uint)D_5895_map_level)
                {
                    // 1243 -> 1261
                    NPC_0d00(local_4, local_8->_e);
                }
                else if (local_e == 1 || local_e == 2)
                {
                    // 1227
                    local_10 = 0;
                    // -> 1243
                    NPC_0d00(local_4, local_8->_e);
                }
                else if (local_e == 0)
                {
                    local_10 = 2;
                    // -> 1243
                    NPC_0d00(local_4, local_8->_e);
                }
                else
                {
                    // 123c
                    local_10 = local_e - 1;
                    // 1243
                    NPC_0d00(local_4, local_8->_e);
                }
                // 1264
            }
        }
    }

    // 12d8
}

// ASM
// use literal conversion from FMT
int NPC_12e0(int param_1, char param_2)
{
    int i;
    int ret;
    byte local_8[4];

    for (i = 0; i < 4; i++)
    {
        local_8[i] = param_2 - D_5d5e[param_1].time[i];
    }

    ret = 0;
    if (local_8[1] < local_8[0])
    {
        local_8[0] = local_8[1];
        ret = 1;
    }

    if (local_8[2] < local_8[0])
    {
        local_8[0] = local_8[2];
        ret = 2;
    }

    if (local_8[3] < local_8[0])
    {
        ret = 1;
    }

    return ret;
}

void NPC_ResetExtra(void) {
    D_imperaNpcCount = 32;
    memset(D_5b5a + 128, 0, sizeof(D_5b5a) - 128);
    memset(D_5bda + 128, 0, sizeof(D_5bda) - 128);
    memset(D_5c5a + 32, 0, sizeof(D_5c5a) - 32 * sizeof(*D_5c5a));
}
void NPC_LoadExtra(const char *name, unsigned town) {
    unsigned previous = LOCATION_NPC_COUNT;
    memset(D_5c5a + 32, 0, (previous - 32) * sizeof(*D_5c5a));
    memset(D_659e + 32, 0, previous - 32);
    memset(D_5f5e + 32, 0, (previous - 32) * sizeof(*D_5f5e));
    D_imperaNpcCount = 32;
    FILE *f = FILE_Open(name, "rb");
    if (!f)
        return;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return;
    }
    long size = ftell(f);
    if (size <= 4608 || size > MOD_MAX_RESOURCE || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return;
    }
    unsigned char *bytes = malloc(size);
    int ok = bytes && fread(bytes, 1, size, f) == (size_t)size && NPCS_Validate(bytes, size);
    fclose(f);
    if (!ok) {
        free(bytes);
        DEBUG_Error("Invalid extended NPC records in %s", name);
        return;
    }
    D_imperaNpcCount = NPCS_Count(bytes, size, town);
    memset(D_5f5e + 32, 0, (D_imperaNpcCount - 32) * sizeof(*D_5f5e));
    memset(D_5c5a + 32, 0, (D_imperaNpcCount - 32) * sizeof(*D_5c5a));
    memset(D_659e + 32, 0, D_imperaNpcCount - 32);
    for (size_t off = IMPERA_NPC_EXTRA_START; off < (size_t)size; off += 24) {
        if (bytes[off] != town)
            continue;
        unsigned slot = NPCS_Word(bytes + off + 2);
        memcpy(&D_5d5e[slot], bytes + off + 4, 16);
        D_659e[slot] = bytes[off + 20];
        D_5f5e[slot]._a = bytes[off + 21];
        if (TOWN_0000_CheckNpcKilled(slot))
            D_659e[slot] = 0;
    }
    free(bytes);
}
/* Prefix before the existing contents/type trailers; vanilla saves have no prefix.
 * Records use explicit little-endian words, never compiler structure layouts. */
int NPC_WriteExtraState(FILE *f) {
    unsigned count = LOCATION_NPC_COUNT;
    size_t flagBytes = 32u * (IMPERA_NPC_CAPACITY / 8);
    for (size_t i = 128 + flagBytes; i < sizeof(D_5b5a); ++i)
        if (D_5b5a[i] || D_5bda[i]) {
            flagBytes = sizeof(D_5b5a) - 128;
            break;
        }
    int flags = 0;
    for (size_t i = 0; i < flagBytes; i++)
        flags |= D_5b5a[128 + i] | D_5bda[128 + i];
    if (count == 32 && !flags)
        return 1;
    size_t size = 2 * flagBytes + (size_t)(count - 32) * 78;
    unsigned char *b = calloc(1, size), h[24] = {0};
    if (!b)
        return 0;
    memcpy(h, flagBytes == 32u * (IMPERA_NPC_CAPACITY / 8) ? "INPSAV01" : "INPSAV02", 8);
    TYPES_Put32(h + 8, size);
    TYPES_Put32(h + 12, count);
    memcpy(b, D_5b5a + 128, flagBytes);
    memcpy(b + flagBytes, D_5bda + 128, flagBytes);
    size_t off = 2 * flagBytes;
    for (unsigned i = 32; i < count; i++, off += 78) {
        memcpy(b + off, &D_5c5a[i], 8);
        memcpy(b + off + 8, &D_5d5e[i], 16);
        const unsigned short *words = (const unsigned short *)&D_5f5e[i];
        for (unsigned j = 0; j < 8; j++) {
            b[off + 24 + j * 2] = words[j];
            b[off + 25 + j * 2] = words[j] >> 8;
        }
        memcpy(b + off + 40, &D_615e[i], 32);
        b[off + 72] = D_655e[i];
        b[off + 73] = (unsigned short)D_655e[i] >> 8;
        b[off + 74] = D_659e[i];
        b[off + 75] = 0;
        b[off + 76] = D_65c2[i];
        b[off + 77] = (unsigned short)D_65c2[i] >> 8;
    }
    TYPES_Put32(h + 16, MOD_Crc32(b, size));
    int ok = fwrite(h, 1, 24, f) == 24 && fwrite(b, 1, size, f) == size;
    free(b);
    return ok;
}
int NPC_ReadExtraState(FILE *f, int apply) {
    long start = ftell(f);
    unsigned char h[24] = {0};
    size_t got = fread(h, 1, 24, f);
    if (got < 8 || (memcmp(h, "INPSAV01", 8) && memcmp(h, "INPSAV02", 8))) {
        if (ferror(f) || fseek(f, start, SEEK_SET))
            return 0;
        if (apply)
            NPC_ResetExtra();
        return 1;
    }
    unsigned count = TYPES_U32(h + 12), size = TYPES_U32(h + 8);
    size_t flagBytes =
        !memcmp(h, "INPSAV02", 8) ? sizeof(D_5b5a) - 128 : 32u * (IMPERA_NPC_CAPACITY / 8);
    if (got != 24 || count < 32 || count > IMPERA_NPC_CAPACITY || TYPES_U32(h + 20) ||
        size != 2 * flagBytes + (size_t)(count - 32) * 78)
        return 0;
    unsigned char *b = malloc(size);
    if (!b)
        return 0;
    int ok = fread(b, 1, size, f) == size && MOD_Crc32(b, size) == TYPES_U32(h + 16);
    for (unsigned i = 32; ok && i < count; i++) {
        size_t off = 2 * flagBytes + (size_t)(i - 32) * 78;
        unsigned actor = NPCS_Word(b + off + 24 + 12);
        int path = (short)NPCS_Word(b + off + 72);
        if (actor >= count || path < -1 || path > 30 || b[off + 75])
            ok = 0;
    }
    if (ok && apply) {
        NPC_ResetExtra();
        D_imperaNpcCount = count;
        memcpy(D_5b5a + 128, b, flagBytes);
        memcpy(D_5bda + 128, b + flagBytes, flagBytes);
        for (unsigned i = 32; i < count; i++) {
            size_t off = 2 * flagBytes + (size_t)(i - 32) * 78;
            memcpy(&D_5c5a[i], b + off, 8);
            memcpy(&D_5d5e[i], b + off + 8, 16);
            unsigned short *words = (unsigned short *)&D_5f5e[i];
            for (unsigned j = 0; j < 8; j++)
                words[j] = NPCS_Word(b + off + 24 + j * 2);
            memcpy(&D_615e[i], b + off + 40, 32);
            D_655e[i] = (short)NPCS_Word(b + off + 72);
            D_659e[i] = b[off + 74];
            D_65c2[i] = (short)NPCS_Word(b + off + 76);
        }
    }
    free(b);
    return ok;
}
