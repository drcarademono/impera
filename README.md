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

Enable `--diagonal-movement` to use eight directions for movement, neighboring
interactions, mouse cursors, and combat movement and attacks. This applies to
keyboard and mouse controls and to AI-controlled combatants, including NPCs,
monsters, and party members. Diagonal steps cannot cut blocked corners.

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
  targets fall back to Look.
- Double-click your own tile to Enter a town, Klimb a ladder, rest in a bed,
  or Board a vehicle when the usual keyboard conditions allow it.

Mouse actions work on the four or eight neighboring tiles, depending on the option. Distant targets do not
trigger actions. Look describes distant objects without
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
Sprites default to opaque when the flag is omitted. Terrain tiles remain opaque;
the original tile data is unchanged.

Enable smooth movement independently with `--smooth-movement`. Each successful
tile step animates a scrolling camera over roughly 120 ms, keeping the player
icon centered and the interface fixed. NPCs and monsters also animate their
visible tile steps, including combat actors. Their motion stays coordinated
with camera scrolling; the combat camera stays fixed. This works with keyboard
or mouse movement; collisions and actions still use the original tile and turn
rules. Teleports, spawns, map changes, and dungeon perspective views snap to
their new state without interpolation.
Both options default to off. For example:

```sh
bash scripts/run-linux.sh --fullscreen --mouse --smooth-movement --diagonal-movement
```

For libraries installed under a custom prefix, add its `lib/pkgconfig` (or
`lib64/pkgconfig`) directory to `PKG_CONFIG_PATH` before configuring.

The launcher copies the original data from `Ultima 5` to `build/runtime`
on first run and keeps saves in `build/runtime/SAVEGAME`. Existing runtime
files and saves are preserved on later runs. Set `U5D_DATA_DIR` to an absolute
path to use another data directory, or `U5D_BUILD_DIR` to an absolute path
if you configured a different build directory. Game filenames and directory
components are matched without regard to ASCII letter case, including fonts,
maps, NPCs, conversations, saves, Ultima IV imports, and optional audio.
Existing saves retain their filename spelling when written. Exact spelling
wins; if a lookup has multiple matches differing only by case, it reports an
error rather than choosing one. The launcher rejects conflicting source
spellings and preserves existing runtime files and saves.
New character creation requires the original `INIT.GAM` file (at least 4192
bytes), which supplies the starting party and world state. Add it to the data
directory if it is missing; `INIT.OOL` is a different file and cannot replace
it. Choose **Create New Character** before **Journey Onward** when there is
no active saved character.

Characters previously created without `INIT.GAM` have invalid starting state.
Back up `build/runtime/SAVEGAME`, restore the original `INIT.GAM`, and create
a new character. Rebuilding alone cannot repair an already damaged save.

Optional modern music and effects belong in `BGM` and `SFX` inside the data
directory; they are not included in the original DOS assets. Do not set
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
