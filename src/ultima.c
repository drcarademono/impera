#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include "common/common.h"
#include "common/movement.h"
#include "backend/backend.h"
#include "vars.h"
#include "funcs.h"

#if defined(ENABLE_TRANSLATION)
#include "translate.h"
#endif

#include "dungeon.h"
#include "intro.h"
#include "mainout.h"
#include "outsubs.h"
#include "town.h"
#if defined(TARGET_SDL)
#include <setjmp.h>
#if defined(OS_WINDOWS)
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "common/save_slots.h"
#include "common/data_setup.h"
#include "key/key.h"
#include "key/mouse.h"
static jmp_buf s_reloadPoint;
static void ReloadFromSlot(void) { longjmp(s_reloadPoint,1); }
#endif

#if !defined(TARGET_DOS16)
extern int g_enableDebugOverlay;
#endif

#if defined(TARGET_SDL)
#include <SDL3/SDL_main.h>
#include "graphics/grap_sdl.h"
#include "graphics/crt.h"
#include "graphics/grap_buf.h"
#include "graphics/animate.h"
#include "key/mouse.h"
#include "common/engine_settings.h"
#include "graphics/widescreen.h"
#endif

// 0000
int CDECL main(int argc, char** argv/*, char** envp*/)
{
    u16 local_8;
    u8 local_6; // hard drive letter (0xff for floppy)
    u8 local_4;
    u16 local_2;

    local_4 = 0x20;

#if defined(TARGET_SDL)
    /* Workshop test sessions must isolate writes before logs/settings are opened. */
    const char* runtimeDirectory = getenv("U5D_RUNTIME_DIR");
    if (runtimeDirectory && *runtimeDirectory) {
#if defined(OS_WINDOWS)
        int changed = _chdir(runtimeDirectory);
#else
        int changed = chdir(runtimeDirectory);
#endif
        if (changed != 0) {
            fprintf(stderr, "Cannot enter Impera runtime directory '%s': %s\n", runtimeDirectory, strerror(errno));
            return EXIT_FAILURE;
        }
    }
    DEBUG_Initialize();
    debug("Startup argc=%d",argc);
    ENGINE_Load();
    for (int arg = 1; arg < argc; arg++)
    {
        if (strcmp(argv[arg], "--fullscreen-4:3") == 0)
        { GRAP_SDL_SetVideoMode(GRAP_VIDEO_FULLSCREEN_43);continue; }
        if (strcmp(argv[arg], "--fullscreen") == 0)
            GRAP_SDL_SetFullscreen(true);
        else if (strcmp(argv[arg], "--diagonal-movement") == 0)
            MOVEMENT_SetDiagonal(true);
        else if (strcmp(argv[arg], "--mouse") == 0)
            MOUSE_SetEnabled(true);
        else if (strcmp(argv[arg], "--smooth-movement") == 0)
            GRAP_SDL_SetSmoothMovement(true);
        else if (strcmp(argv[arg], "--movement-speed") == 0 || strcmp(argv[arg], "--animation-speed") == 0)
        {
            bool movement = strcmp(argv[arg], "--movement-speed") == 0;
            const char* option = argv[arg];
            char* end;
            if (++arg >= argc) {
                fprintf(stderr, "%s requires a multiplier from 0.1 to 10\n", option);
                return EXIT_FAILURE;
            }
            errno = 0;
            float speed = strtof(argv[arg], &end);
            if (errno || end == argv[arg] || *end || !isfinite(speed) || speed < 0.1f || speed > 10.0f) {
                fprintf(stderr, "%s requires a multiplier from 0.1 to 10\n", option);
                return EXIT_FAILURE;
            }
            if (movement) GRAP_SDL_SetMovementSpeed(speed);
            else ANIMATION_SetSpeed(speed);
        }
        else if (strcmp(argv[arg], "--transparent-sprites") == 0)
            GRAP_BUF_SetTransparentSprites(true);
        else if (strcmp(argv[arg], "--dithered-darkness") == 0)
            WIDE_SetDitheredDarkness(true);
        else if (strcmp(argv[arg], "--crt-filter") == 0)
            CRT_SetEnabled(true);
        else if (strcmp(argv[arg], "--legacy-save") == 0)
            SLOTS_SetLegacyEnabled(true);
        else if (strcmp(argv[arg], "--help") == 0)
        {
            puts("Usage: ultima5 [--fullscreen | --fullscreen-4:3] [--mouse] [--smooth-movement] [--movement-speed N] [--animation-speed N] [--diagonal-movement] [--transparent-sprites] [--dithered-darkness] [--crt-filter] [--legacy-save] [C|H|T|E]\n"
                 "  --fullscreen-4:3   Center the fullscreen map in a 4:3 viewport.\n"
                 "  --fullscreen       Expand the overhead map with uniform integer pixel scaling.\n"
                 "  --mouse            Enable mouse movement and contextual actions.\n"
                 "  --diagonal-movement Enable diagonal movement, actions, cursors and combat.\n"
                 "  --smooth-movement  Animate overhead movement with a scrolling camera.\n"
                 "  --movement-speed N Held movement speed multiplier (0.1 to 10; default 1).\n"
                 "  --animation-speed N Animated sprite speed multiplier (0.1 to 10; default 1).\n"
                 "  --transparent-sprites Show ground through black sprite backgrounds with a one-pixel outline.\n"
                 "  --dithered-darkness Fade visibility edges with a retro pixel pattern.\n"
                 "  --crt-filter       Simulate a late-1980s VGA CRT monitor.\n"
                 "  --legacy-save      Show Legacy Save in the load menu when original save files are valid.");
            return EXIT_SUCCESS;
        }
        else if (argv[arg][0] == '-')
        {
            fprintf(stderr, "Unknown option: %s (use --help)\n", argv[arg]);
            return EXIT_FAILURE;
        }
        else
            local_4 = ULTIMA_2032_ToUpper((unsigned char)argv[arg][0]);
    }
#else
    if (argc > 1)
        local_4 = ULTIMA_2032_ToUpper((unsigned char)argv[1][0]);
#endif

#if defined(TARGET_SDL)
    if(!SETUP_Run()) { debug("Startup setup cancelled or failed");return EXIT_SUCCESS; }
#endif

#if defined(ENABLE_TRANSLATION)
    TRS_Initialize();
#endif

#if !defined(TARGET_DOS16)
    if(!BACKEND_Initialize()) return EXIT_FAILURE;
    atexit(BACKEND_Cleanup);
#endif

    // 0021
    D_52ba_vdp._52ba_forceCga = local_4 == 'C';
    D_52f3_forceHerc = local_4 == 'H';
    D_52f1_forceTandy = local_4 == 'T';
    D_52ef_forceEga = local_4 == 'E';

    // 0061
    D_5394_fn = ULTIMA_2322_DiskSwapMessage;

    D_a9bd[0] = 0;
    D_a9bd[1] = 0;
    D_a9c2 = 1;

    if ((D_a9c8[0] = ULTIMA_16a6_GetDefaultDrive()) >= 'C')
        local_6 = D_a9c8[0];
    else
        local_6 = 0xff;

    // 0090
    D_a9cd = local_6;
    D_a9ca = local_6;
    D_a9c9 = local_6;

    D_a9cb = 0xff;
    D_b11c = D_b21e;
    D_538c = 1;

    INTRO_0986_Main(); // 00ad
    ULTIMA_2900_UpdateVitalsDisplay();

#if !defined(TARGET_DOS16)
    g_enableDebugOverlay = 1;
#endif

    local_8 = 0;
#if defined(TARGET_SDL)
    if(setjmp(s_reloadPoint)) {
        KEY_SDL_SetGameplayInput(0);
        MOUSE_SetCommandInput(false);
        MOUSE_Cancel();
        SLOTS_ReloadActiveGame();
        local_8=0;
    }
    SLOTS_SetReloadCallback(ReloadFromSlot);
#endif

    // 00b8
    // main game loop
    do
    {
        local_2 = 0;

        if (D_5893_map_id == 0)
        {
            MAINOUT_0d22_Entry();
            local_2 = 1;
            local_8 = 0;
        }
        
        // 00d1
        if (D_5893_map_id != 0)
        {
            // 00db
            if (D_5893_map_id < 0x21)
            {
                TOWN_11f0_Entry(local_2 != 0 || local_8 != 0);
                TOWN_141e_MainLoop();
                local_8 = 0;
            }
            else
            {
                // 0104
                ULTIMA_251e_SwitchDisks(2);
                DUNGEON_0e2e_MainLoop(local_2);
                local_8 = 1;
            }

            // 0116
            local_2 = 0;
            ULTIMA_251e_SwitchDisks(1);

            // 0122
            while (!ULTIMA_1674_TestOpenFile(/*0x1393*/ "BRIT.DAT")) {}
            ULTIMA_256e_ReadFileFromDisk(OUTSUBS_0368_GetWorldSavefile(), D_5c5a, 0x100, 0);

            if (D_5893_map_id == 0 && D_5895_map_level != 0)
            {
                ULTIMA_251e_SwitchDisks(5);

                // 0154
                while (!ULTIMA_1674_TestOpenFile(/*0x139c*/ "UNDER.DAT")) {}
                ULTIMA_25d8_WriteFileToDisk(OUTSUBS_0368_GetWorldSavefile(), D_5c5a, 0x100);
            }
        }
        // 016e
    } while (local_2 == 0);

#if defined(TARGET_SDL)
    SLOTS_SetReloadCallback(NULL);
#endif
    ULTIMA_0878_RestoreVideoMode();
}
