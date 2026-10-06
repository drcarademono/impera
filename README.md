# Impera — Ultima 5 Engine Port

**Impera** is an enhanced engine port of **Ultima 5: Warriors of Destiny**,
in the tradition of [Exult](https://exult.sourceforge.io/),
[Nuvie](https://nuvie.sourceforge.net/), and [Pentagram](https://pentagram.sourceforge.net/).
It runs the original game data on modern systems while adding optional graphics,
input, audio, and save-management improvements.

Impera uses **[u5d — Ultima V Decompilation Project](https://github.com/wonst719/u5d)**
by **wonst719** as its base. Credit for the original decompilation and reconstructed
game engine belongs to that project.

You must supply your own Ultima 5 game files. Game data and optional music are
external and are not bundled into the engine. The build still produces an
executable named `ultima5`, and environment variables retain the `U5D_` prefix.

## Enhanced features

- Fullscreen overhead maps that show more of the world, with crisp integer-scaled pixels.
- Mouse movement, contextual double-click actions, single-click direction selection,
  combat aiming, and mouse selection in supported menus.
- Optional smooth camera and actor movement, with independent movement and sprite-animation speeds.
- Optional diagonal movement and interactions, including combat and corner sliding.
- Transparent character and selected static-object sprites, inferred ground beneath
  objects, and foreground occlusion by southern blocking scenery.
- In-game Engine Options with persistent settings and the original font and blue pixel-art frame.
- Unlimited named save slots, gameplay thumbnails, play time, location labels,
  scrollable save/load browsers, and confirmed deletion.
- External music playback and optional WAV effects, with synthesized original DOS
  PC-speaker effects when recordings are absent.
- First-launch game/music directory selection, separate writable saves, and
  case-insensitive game-file lookup.

## Keyboard commands

The original game commands remain available. These are the port's main additions
and the keys used by its new screens:

| Key | Context | Action |
| --- | --- | --- |
| `Ctrl+O` | Gameplay | Open **Engine Options**; return to the suspended game afterward. |
| `Ctrl+W` | World, town, or dungeon command prompt | Open **Save Game**. |
| `Ctrl+L` | World, town, or dungeon command prompt | Open **Load Game**. |
| `Q`, then `Y` | Normal save command | Open **Save Game** instead of writing one fixed slot. |
| `O` | Main menu | Open **Engine Options**. |
| Arrow keys | Engine Options and save/load browsers | Move between entries; Left/Right adjusts speed settings. |
| `Enter` | New menus | Select an entry, toggle an option, or confirm a save name. |
| `Page Up` / `Page Down` | Save/load browsers | Scroll through slots. |
| `Delete` | Named save selected in a save/load browser | Ask for confirmation, then delete the slot with `Y`; `N` or Escape cancels. |
| `Escape` | Engine Options or save/load browser | Return to the menu or game. |
| `Escape` / `Space` | Action direction prompt | Cancel the action. |
| `Home`, `Page Up`, `End`, `Page Down` | Gameplay, with Diagonal Movement enabled | Northwest, northeast, southwest, southeast respectively. |
| `Ctrl+E` | Gameplay | Preserve the original **Exit to DOS?** confirmation. |
| `Ctrl+E` | Main menu, Engine Options, save/load browsers, or cutscenes | Exit immediately. |

`Ctrl+W` and `Ctrl+L` are gameplay shortcuts and do not open another browser from
inside menus or other prompts. Normal keyboard action selection still works when
mouse controls are enabled.

## Getting started

### Build and launch on Linux

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

For libraries installed under a custom prefix, add its `lib/pkgconfig` (or
`lib64/pkgconfig`) directory to `PKG_CONFIG_PATH` before configuring.

### Select your game files

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

## Engine Options

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

### Available settings

| Setting | Effect |
| --- | --- |
| Fullscreen | Expand the overhead map to the display with uniform pixel scaling. |
| Mouse Control | Enable map movement and contextual actions; options menus remain clickable when off. |
| Smooth Movement | Animate camera scrolling and visible actor steps. |
| Diagonal Movement | Enable eight-way movement, neighboring actions, and combat. |
| Dithered Darkness | Fade visibility edges with an Ultima 6-style ordered pixel pattern. |
| Transparent Sprites | Reveal ground around character and supported object sprites. |
| Music | Enable or mute external music playback. |
| Sound Effects | Enable or mute WAV overrides and synthesized effects. |
| Movement Speed | Adjust held movement and smooth tile transitions. |
| Animation Speed | Adjust all animated sprites independently of movement and turns. |

Selected rows use a white highlight with black text; checkbox colors remain
visible to show their state. Mouse hover selects rows, including **Return to
Menu** / **Return to Game**; left-click activates them. Sliders accept click
and drag. The menus use a pointer cursor rather than map direction arrows.

### Command-line-only options

`--legacy-save` enables **Legacy Save** in the load menu when valid original save
files exist. It is disabled by default and has no Engine Options setting.
`--help` lists the executable's launch options.

## Saving, loading, and starting a new game

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

## Mouse controls

Enable **Mouse Control** in Engine Options. The supplied PNG cursors show the four or eight
movement directions over the overhead map and a pointer over menus, cutscenes,
and the status/text column. Cursor pixels scale with the displayed game pixels
using nearest-neighbor scaling, including when the window size changes. Hover
over an item in the main menu, party selector, equipment/scroll selector,
reagent list, shop inventory, or inn guest register to highlight it; left-click
to select it. Keyboard selection remains available.

The launcher copies `textures/cursors` into the
runtime directory; copy that folder into the working directory when running the
binary directly.

Overhead map controls work in windowed and fullscreen mode:

- Hold the right button over the map to walk toward the cursor in eight
  directions when **Diagonal Movement** is enabled, or four otherwise. Release it to stop. Diagonal steps cannot cut through blocked
  corners; sailing retains the game's four-way headings.
- Single-left-click a tile at any distance to Look. Double-left-click an adjacent
  or (with **Diagonal Movement**) diagonal NPC to Talk, a door or chest to Open, or a loose object to Get. Other nearby
  targets fall back to Look. NPCs can also be double-clicked across one of the
  tables, desks, doors, or other tiles supported by keyboard Talk. Food on table
  settings can be taken from the same directions supported by keyboard Get.
- Double-click your own tile to Enter a town, Klimb a ladder, rest in a bed,
  or Board a vehicle when the usual keyboard conditions allow it.

After typing an action such as Talk, Search, Open, Get, Push, or Klimb, select
its direction with a single left-click on an eligible neighboring tile. Talk
also accepts NPCs across supported furniture. Keyboard directions remain available;
Escape or Space cancels the direction prompt.

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

Directional cursors target with their visible arrow tip, including after window
resizing. On combat maps, both cursor orientation and clicked action directions
are relative to the **active party character**, rather than the battlefield center.

## Movement and graphics

### Fullscreen maps

To fill your display, enable **Fullscreen** in Engine Options.
This detects the desktop resolution, adds overhead map rows and columns, and
moves the status and command column to the right edge. Tiles and text use the
same integer scale in both directions, with crisp pixels. Daylight reveals the
expanded view; darkness and obstacles still restrict visibility. Combat retains
its original 11x11 battlefield. Title screens and dungeon perspective views keep
their original layout, centered without stretching. A few unused edge pixels
may remain when the display dimensions are not divisible by the pixel scale.
Without the option, the game uses the existing window size settings.
`--help` lists launch options.

### Diagonal movement

Enable **Diagonal Movement** in Engine Options to use eight directions for movement, neighboring
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
**Diagonal Movement** is enabled.

### Smooth movement and speed

Enable **Smooth Movement** in Engine Options. Each successful
tile step animates a scrolling camera over roughly 120 ms, keeping the player
icon centered and the interface fixed. NPCs and monsters also animate their
visible tile steps, including combat actors. Their motion stays coordinated
with camera scrolling; the combat camera stays fixed. This works with keyboard
or mouse movement; collisions and actions still use the original tile and turn
rules. Teleports, spawns, map changes, and dungeon perspective views snap to
their new state without interpolation.
Use **Movement Speed** to set the held mouse and keyboard movement speed,
and **Animation Speed** to set animated sprite playback independently,
including NPC poses, fountains, flames, and water effects.
Both default to `1`; the menu sliders offer `0.5`, `0.75`, and `1`.
A multiplier of `0.5` halves the speed. Sprite animation speed works with or without
**Smooth Movement** and does not change camera transition timing or game turns.
Movement speed also scales smooth tile transitions so held input continues
without waiting at tile boundaries.
Smooth movement uses timed keyboard repeats so held input continues across tile
boundaries; otherwise keyboard repeat retains the operating system's behavior.

### Dithered darkness

Enable **Dithered Darkness** in Engine Options for an Ultima 6-style pixel-pattern
fade along night-time and line-of-sight boundaries. It is off by default. The
player's tile and visible masonry (including doorways, closed doors, windows and wall torches) stay
clear, fully unseen terrain stays black, and gameplay
visibility rules are unchanged. The pattern uses original game pixels and stays
attached to the map during smooth scrolling. It works in windowed and expanded fullscreen
overhead views; combat and dungeon perspective views keep their usual rendering.

The matching launch flag is `--dithered-darkness`. Like other Engine Options,
the setting is saved in `ENGINE.CFG`; the flag enables it for startup and overrides
the saved value.

### Transparent sprites

Enable **Transparent Sprites** to draw player, NPC, and monster sprites over their ground
instead of replacing the entire tile. Black sprite pixels are transparent,
except for a one-pixel black outline around colored pixels (including diagonal
neighbors). The outline can extend one pixel into adjacent map tiles and is
clipped at the map frame. Blocking scenery to the south is drawn in front
of overlapping sprite outlines. This also applies to expanded fullscreen maps and
smooth movement. Fountains (all animation frames), wells, braziers, cannonballs, cannons,
telescopes, stacks of logs, stocks (empty or occupied), guillotines,
torture racks, pendulums (all animation frames), metal grates, portcullises,
carpets, ladders, and bellows also use this setting. Their map cells
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
**Transparent Sprites**.
Sprites default to opaque unless Transparent Sprites is enabled. Terrain tiles remain opaque;
the original tile data is unchanged.

## Music and sound effects

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
No audio assets are needed for synthesized effects. Do not set SDL's dummy video/audio drivers when playing on your desktop.

## File locations and compatibility

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

## Troubleshooting

Runtime logging is **always enabled**, starting before the first-launch directory
picker. No environment flag is needed. With the Linux launcher, logs live in
`build/runtime/LOG.TXT` and `build/runtime/LOG.PREV.TXT`; direct launches write
them in the working directory. Errors also go to the terminal. If the log cannot
be created, error messages still go to the terminal.

Logs include timestamps and sequence numbers, build/SDL/platform information,
selected data/audio directories, renderer and audio setup, settings changes,
map/position/active-combatant changes, gameplay commands, file transfers, and
save/load/delete outcomes. Records are flushed immediately. Each log segment is
limited to approximately 4 MiB; the previous segment or previous launch is kept
as `LOG.PREV.TXT`. Copy **both files before restarting repeatedly**, and include
the steps to reproduce, operating system, and affected save when reporting a bug.
Logs can contain local directory paths and gameplay state.

Linux/macOS fatal signals append a crash marker and retain normal signal/core-dump
termination. Windows retains its existing crash dump handler. These diagnostics
preserve the last recorded context; they do not guarantee a stack trace or explain
every crash. A normal exit records **Clean application shutdown**. `U5D_DEBUG=1`
is accepted as an environment variable but is no longer needed to enable tracing.

Missing required map or NPC data now exits with an error naming the file
instead of retrying indefinitely.

## Development

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

## Upstream project

The upstream [u5d project](https://github.com/wonst719/u5d) reconstructs Ultima V
v1.16 for MS-DOS, aiming for functional equivalence and assembly-level semantic
matching. Its original linker has not been found, so perfect binary matching is
not currently possible. FM-TOWNS disassembly is also used as a reference where
needed. See upstream for current decompilation progress and its original
project documentation.

## Credits

- **[carademono](https://github.com/drcarademono)** — Impera port and enhancements.
- **[wonst719](https://github.com/wonst719)** — [u5d — Ultima V Decompilation Project](https://github.com/wonst719/u5d), the base engine and original decompilation.
