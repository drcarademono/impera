# u5d - Ultima V Decompilation Project

## Introduction

This is an ongoing decompilation project of Ultima V: Warriors of Destiny.

Inspired by [u4-decompiled](https://github.com/ergonomy-joe/u4-decompiled), I started this project as a personal challenge to better understand the original game and its codebase.

## Goals

- Create a functionally equivalent version of the original game that can run on modern platforms.

- Match as much of the original code (v1.16 for MS-DOS) as possible.

Since the original linker (an unknown version of PLINK86) has not been found, perfect binary matching is currently impossible. Instead, the project focuses on assembly-level semantic matching.

When necessary, disassemblies from other platforms such as FM-TOWNS are also used as references.

## Current Status

### Matching Progress

- `ULTIMA.EXE`: approximately 95% matched, excluding assembly functions
- `*.OVL`: approximately 95% matched

### Playability

The game can be played through to the ending using the original data files. However, major and minor bugs may still remain throughout the game.

## Building

### Linux Target

Install a C compiler, CMake 3.16 or newer, Python 3, pkg-config, SDL3, and SDL3_mixer
development packages. This target uses **SDL3_mixer 3.2 or newer**, not
SDL2_mixer. SDL3 must include your desktop's X11 or Wayland video driver.
If your distribution does not package these libraries, build them from the
official [SDL](https://github.com/libsdl-org/SDL) and
[SDL_mixer](https://github.com/libsdl-org/SDL_mixer) releases. The cloud build
was tested with SDL 3.4.18 and SDL_mixer 3.2.4.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
bash scripts/run-linux.sh
```

On first launch, a pixel-art setup screen asks for the directory containing your
own Ultima 5 game files and a **Music (Optional)** directory. Select a row with
Up/Down and Enter, or click it, to open the system folder picker. **Start Game**
validates the game files and remembers both locations in `build/runtime/DATA.CFG`.
The setup screen returns if a selected directory or required game asset goes
missing. Delete `DATA.CFG` to choose different locations. **Clear Music** removes
the optional selection. Escape cancels startup.

Before the game files are available, setup uses the engine's public-domain 8×8
font. After choosing a valid game directory it uses the original `IBM.CH` font.
Game assets and music stay external; the engine does not embed or copy them.
Writable saves stay in `build/runtime/SAVEGAME`, leaving the selected installation
unchanged. `U5D_DATA_DIR=/absolute/path/to/game` also supports an explicit game
location for unattended launches. The separate music folder takes precedence
over a `Music` folder alongside the game files.

To fill your display, launch with `bash scripts/run-linux.sh --fullscreen`.
This detects the desktop resolution, adds overhead map rows and columns, and
moves the status and command column to the right edge. Tiles and text use the
same integer scale in both directions, with crisp pixels. Daylight reveals the
expanded view; darkness and obstacles still restrict visibility. Combat retains
its original 11x11 battlefield. Title screens and dungeon perspective views keep
their original layout, centered without stretching. A few unused edge pixels
may remain when the display dimensions are not divisible by the pixel scale.
Without the option, the game uses the existing window size settings.
`--help` lists launch options.

Choose **Engine Options** below **Return to the View** in the main menu
(or press `O`). Press `Ctrl+O` during gameplay to open Engine Options
and return to your game with `Esc`. Fullscreen, Mouse Control, Smooth Movement, Diagonal Movement,
Transparent Sprites, Music, and Sound Effects can be switched on or off.
Changes apply immediately. Movement Speed and Animation Speed have sliders
with ticks at 0.5, 0.75, and 1; use Left/Right or click and drag the slider.
Up/Down selects a setting, Enter toggles it, and Escape returns to the menu or your game.
The screen uses the original game font and integer-scaled pixels.

Settings are saved in `ENGINE.CFG` in the runtime directory and loaded on the
next launch. Explicit command line flags override saved settings. Sliders also
display command line speed values outside their tick range without changing
them until adjusted. Mouse input remains available on the settings screen
even when Mouse Control is off, so it can be turned back on.

Press `Ctrl+W`, or press `Q`, then answer **Y** to **Save game?** to open the **Save Game** browser.
Choose **New Save**, type a name (up to 26 characters in the game font), and
press Enter. Existing slots can be selected and overwritten after confirmation.
Names may repeat: each slot has its own identifier. Slots have no fixed limit;
the list shows four at a time with Up/Down, Page Up/Down, mouse wheel scrolling,
and a draggable scrollbar. Press Delete on a named slot, then Y to confirm deletion
or N/Escape to cancel. Escape or **Return to Game** cancels.

Completing **Create New Character** automatically creates a normal named slot
using the Avatar's name, fresh world state, and zero play time. The title-screen
flow stays the same: return to the title, then choose **Journey Onward** to find
the new game at the top of **Load Game**. Imported Ultima IV characters also get
a normal slot. Existing named saves are preserved.

Each slot includes the complete party save, both world-object lists, cumulative
play time, location metadata (map, level and coordinates), and a small screenshot
thumbnail for manual saves. Save and load rows show the location using the
existing map-name data. Older slots without location metadata remain loadable
and show **Unknown Location**. **Journey Onward** opens the matching
**Load Game** browser. `Ctrl+L` opens it during gameplay and resumes the selected
save immediately. Both `Ctrl+W` and `Ctrl+L` work at world, town, and dungeon
command prompts; they are inactive inside other menus. The command-line-only `--legacy-save` flag enables **Legacy Save** when valid original save files exist; it is hidden by default. This preserves access to the original
single-slot save. Slot data lives under `SAVEGAME/slots` in the runtime directory;
back up that directory along with `SAVEGAME` when moving your saves.

Play time measures elapsed time in loaded game sessions and persists with each
slot. Creating or importing a character starts a new clock. Existing DOS saves
have no historical play-time data, so their clock starts at zero. Menus count
as time within a loaded session; time while the application is closed does not.

Enable `--diagonal-movement` to use eight directions for movement, neighboring
interactions, mouse cursors, and combat movement and attacks. This applies to
keyboard and mouse controls and to AI-controlled combatants, including NPCs,
monsters, and party members. When one side of a diagonal is blocked, movement
slides along the open side for that turn. Continued input keeps trying the
original diagonal. If both sides are blocked, movement stops; diagonal steps
still cannot cut blocked corners.

Without this option, movement, actions, and combat attacks use cardinal
directions. The map has four mouse cursor zones (north, south, east, west).
Distant Look descriptions remain available in either mode, but Look actions
require a cardinal neighbor by default or any of the eight neighbors when
`--diagonal-movement` is enabled.

Enable mouse controls with `--mouse`. The supplied PNG cursors show the four or eight
movement directions over the overhead map and a pointer over menus, cutscenes,
and the status/text column. Cursor pixels scale with the displayed game pixels
using nearest-neighbor scaling, including when the window size changes. Hover
over an item in the main menu, party selector, equipment/scroll selector,
reagent list, shop inventory, or inn guest register to highlight it; left-click
to select it. Keyboard selection remains available.

The launcher copies `textures/cursors` into the
runtime directory; copy that folder alongside the game data when running the
binary directly.

Enable mouse controls with `--mouse`. They work in the overhead town and outdoor
views, in both windowed and fullscreen mode:

- Hold the right button over the map to walk toward the cursor in eight
  directions when `--diagonal-movement` is enabled, or four otherwise. Release it to stop. Diagonal steps cannot cut through blocked
  corners; sailing retains the game's four-way headings.
- Single-left-click a tile at any distance to Look. Double-left-click an adjacent
  or (with `--diagonal-movement`) diagonal NPC to Talk, a door or chest to Open, or a loose object to Get. Other nearby
  targets fall back to Look. NPCs can also be double-clicked across one of the
  tables, desks, doors, or other tiles supported by keyboard Talk. Food on table
  settings can be taken from the same directions supported by keyboard Get.
- Double-click your own tile to Enter a town, Klimb a ladder, rest in a bed,
  or Board a vehicle when the usual keyboard conditions allow it.

Mouse actions work on the four or eight neighboring tiles, depending on the option.
Talk can reach an NPC two tiles away across supported furniture; other distant
targets do not trigger contextual actions. Look describes distant objects without
offering actions such as drinking or dropping coins; those require adjacency,
including a diagonal neighbor. Single clicks wait 300 ms to distinguish double
clicks. Right-button movement also works during combat command entry, relative to the
active fighter, including diagonals when enabled. Combat uses its usual turn and collision
rules. In combat, double-left-click an enemy within weapon range to start Attack
and position the Aim cursor on it. A separate single-left-click confirms the
attack during Aim; keyboard aiming and confirmation remain available. Attacks respect
the cardinal/diagonal option and the normal weapon, obstruction, and turn rules.
Map commands are disabled during menus, dialogues, dungeon
perspective views, and other input prompts. Menu lists support mouse selection;
use the keyboard for other prompts. Clicking the status column does not issue map commands.

Enable `--transparent-sprites` to draw player, NPC, and monster sprites over their ground
instead of replacing the entire tile. Black sprite pixels are transparent,
except for a one-pixel black outline around colored pixels (including diagonal
neighbors). The outline can extend one pixel into adjacent map tiles and is
clipped at the map frame. Blocking scenery to the south is drawn in front
of overlapping sprite outlines. This also applies to expanded fullscreen maps and
smooth movement. Fountains (all animation frames), wells, braziers, cannonballs, cannons,
telescopes, stacks of logs, stocks (empty or occupied), guillotines,
torture racks, pendulums (all animation frames), metal grates, portcullises,
carpets, and ladders also use this flag. Their map cells
have no separate ground layer, so their background uses the most common
immediately adjacent brick floor, grass, stone floor, or wooden floor tile
(ties prefer that order). With no adjacent recognized ground, they stay opaque. This
inference affects rendering only and leaves map data and interactions unchanged.
The static-object audit checked all 256 map tiles against the tile art, object
names, and town/castle map usage. It includes only freestanding objects with
black backgrounds around their artwork. Furniture such as barrels, beds, drawers, chairs,
tables, and street lamps already includes colored floor or grass pixels;
these keep their original artwork. Solid walls, doors, stairs, fences, other wall fixtures,
terrain, and effects also retain their original rendering.
Chairs, tables (including food and candelabrum variants), and all seated NPC
poses stay opaque with no added outline.
The sleeping-in-bed NPC, empty manacles, and all four occupied-manacles NPC
frames retain their complete opaque artwork with no added outline, even with
`--transparent-sprites`.
Sprites default to opaque unless enabled by a flag or saved settings. Terrain tiles remain opaque;
the original tile data is unchanged.

Enable smooth movement independently with `--smooth-movement`. Each successful
tile step animates a scrolling camera over roughly 120 ms, keeping the player
icon centered and the interface fixed. NPCs and monsters also animate their
visible tile steps, including combat actors. Their motion stays coordinated
with camera scrolling; the combat camera stays fixed. This works with keyboard
or mouse movement; collisions and actions still use the original tile and turn
rules. Teleports, spawns, map changes, and dungeon perspective views snap to
their new state without interpolation.
Use `--movement-speed N` to set the held mouse and keyboard movement speed,
and `--animation-speed N` to set animated sprite playback independently,
including NPC poses, fountains, flames, and water effects.
Both accept multipliers from `0.1` to `10` and default to `1`: `2` doubles the
speed and `0.5` halves it. Sprite animation speed works with or without
`--smooth-movement` and does not change camera transition timing or game turns.
Movement speed also scales smooth tile transitions so held input continues
without waiting at tile boundaries.
For example, double both speeds with
`--smooth-movement --movement-speed 2 --animation-speed 2`.
Smooth movement or explicit movement speed uses timed keyboard repeats;
otherwise keyboard repeat retains the operating system's behavior.
Both options initially default to off. For example:

```sh
bash scripts/run-linux.sh --fullscreen --mouse --smooth-movement --diagonal-movement
```

For libraries installed under a custom prefix, add its `lib/pkgconfig` (or
`lib64/pkgconfig`) directory to `PKG_CONFIG_PATH` before configuring.

The launcher keeps writable saves in `build/runtime/SAVEGAME` and reads game
assets from the directory selected in setup. Existing saves are preserved. Set `U5D_DATA_DIR` to an absolute
path to use another data directory, or `U5D_BUILD_DIR` to an absolute path
if you configured a different build directory. Game filenames and directory
components are matched without regard to ASCII letter case, including fonts,
maps, NPCs, conversations, saves, Ultima IV imports, and optional audio.
Existing saves retain their filename spelling when written. Exact spelling
wins; if a lookup has multiple matches differing only by case, it reports an
error rather than choosing one.
New character creation requires the original `INIT.GAM` file (at least 4192
bytes), which supplies the starting party and world state. Add it to the data
directory if it is missing; `INIT.OOL` is a different file and cannot replace
it. Choose **Create New Character** before **Journey Onward** when there is
no active saved character.

Characters previously created without `INIT.GAM` have invalid starting state.
Back up `build/runtime/SAVEGAME`, restore the original `INIT.GAM`, and create
a new character. Rebuilding alone cannot repair an already damaged save.

Player-supplied music belongs in `Music` inside the data directory, or beside
the executable (for example `build/Music` when running the Linux build).
The same locations work for `Sound` overrides. Music files are
selected by their two-digit track prefix, so names such as
`02 - Britannic Lands.mp3` work without renaming. MP3, Ogg, WAV, and FLAC are
supported. Slots 01–15 use the game's existing music assignments; 16 (Amiga
Theme) is accepted but has no automatic gameplay assignment. Music stays
external and is not bundled into the executable. Legacy `BGM/01.ogg` through
`BGM/15.ogg` remain supported as fallbacks.

Optional WAV sound overrides belong in `Sound`, using the filenames listed in
`src/audio/sfx_map.h` (for example `step0.wav`, `step1.wav`, and `fountain.wav`).
Each missing or unreadable effect falls back to the original PC-speaker tone,
noise, pulse, or sweep algorithm. Frequencies are Hz and become integer PIT
divisors via `1193182 / frequency`. Noise uses the original 16-bit PRNG;
pulses gate the divisor-60 carrier using wrapping 16-bit accumulators and
thresholds. Sweeps retain DOS signed 16-bit arithmetic and integer division.
`sfx_map.h` remains the authoritative effect parameter table. Title effects
without parameter tuples require their WAV recordings.

SDL converts each calibrated delay unit to **50 microseconds**, consistently
across all effects: tone duration is `dur` units, noise holds each frequency
for `rate` units until elapsed reaches `dur`, PWM performs `dur` iterations
of `delay` units, and sweeps hold each step for `tickStep` units. This is an
explicit timing conversion, not an emulation of DOS CPU calibration via
`D_5356`/`D_5426`; it does not alter the sound algorithms. PCM is sampled at
48 kHz without frequency/duty clamps or click envelopes. Legacy `SFX` files
also remain supported.
Audio is read directly from the optional selected music folder or the usual
external audio directories; players can also place it in `build/runtime`.
No audio assets are needed for synthesized effects. Do not set
SDL's dummy video/audio drivers when playing on your desktop. Headless cloud
validation covered rendering, menu input, and the character-name prompt;
desktop graphics, audible output, and a complete playthrough are unverified.

Errors are written to the terminal and `build/runtime/LOG.TXT`. The log is
replaced on each launch, so copy it before restarting when reporting a bug.
For detailed tracing, run `U5D_DEBUG=1 bash scripts/run-linux.sh`; verbose logs
can grow quickly. If the log cannot be created, errors still go to the terminal.
Missing required map or NPC data now exits with an error naming the file
instead of retrying indefinitely.

To run the native settings and savegame regression checks:

```sh
cmake -S . -B build -DU5D_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

### Windows Target

Open `u5win/u5win.slnx` with Visual Studio 2026 and build the solution.

### MS-DOS Target (DJGPP)

Run `make` on a machine with DJGPP installed.

Depending on the environment, you may need to adjust the `CROSS_CC_PREFIX` value in the `Makefile`.

### MS-DOS Target (Original Toolchain)

Run `src\build.bat` on a machine or virtual machine with Microsoft C 5.1 installed.

The compiler installation path is `C:\MSC51`.

Note: executable linking is currently not possible. This target is used only for disassembly matching.
