#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "common/common.h"
#include "vars.h"
#include "funcs.h"
#include "macros.h"

/* Run the real map-return handler, replacing only combat and presentation. */
void __wrap_DNGLOOK_117e(int a, int b) { (void)a; (void)b; }
void __wrap_AUDIO_PlayBgmPerMap(void) {}
void __wrap_ULTIMA_2900_UpdateVitalsDisplay(void) {}
int __wrap_COMBAT_0b94_MainLoop(void)
{
    D_5896_map_x = 1;
    D_5897_map_y = 2;
    D_5895_map_level = 3;
    D_5c5a[0]._0_tile = 0;
    return 0;
}

static void check_return(unsigned char active, unsigned char status, unsigned char expected)
{
    D_587b = active;
    if (active < 16)
        D_55a8_party[active].status = status;
    D_5893_map_id = 13;
    D_5896_map_x = 15;
    D_5897_map_y = 16;
    D_5895_map_level = 0;
    D_5c5a[0]._0_tile = 42;
    D_bb16 = 0;
    ULTIMA_5f86_SpecialMapHandler(2, 0, 0);
    assert(D_587b == expected);
    assert(D_5893_map_id == 13);
    assert(D_5896_map_x == 15 && D_5897_map_y == 16);
    assert(D_5895_map_level == 0);
    assert(D_5c5a[0]._0_tile == 42);
}

int main(void)
{
    check_return(255, 'G', 255);
    check_return(16, 'G', 255);
    check_return(0, 'G', 0);
    check_return(5, 'G', 5);
    check_return(0, STATUS_DEAD, 255);
    check_return(0, STATUS_SLEEP, 255);
    puts("Combat map restoration passed.");
    return 0;
}
